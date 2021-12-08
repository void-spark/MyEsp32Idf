#include "sd_doorbell.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"

#include "esp_system.h"

// VFS with SD over SPI
#include "driver/sdspi_host.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

// I2S and MP3
#include "i2s_sink.h"
#include "mp3player.h"

#define LED3_EXT GPIO_NUM_25

#define PIN_NUM_MISO GPIO_NUM_19
#define PIN_NUM_MOSI GPIO_NUM_5
#define PIN_NUM_CLK  GPIO_NUM_18
#define PIN_NUM_CS GPIO_NUM_4

#define SPI_DMA_CHAN    2

#define SPI_SD_HOST VSPI_HOST

#define PRIO_MP3 configMAX_PRIORITIES - 2
#define PRIO_DRB configMAX_PRIORITIES - 3

#define SOUND_DATA_QUEUE_LEN 64

static void tskDoorbell(void *pvParameters);

static QueueHandle_t soundData = NULL;

static EventGroupHandle_t doorbellEventGroup;

const int START_BIT = BIT0;
const int STOP_BIT = BIT1;

static mp3player_handle_t mp3Player = NULL;

void sdDoorbellSetup() {
    gpio_pad_select_gpio(LED3_EXT);
    gpio_set_direction(LED3_EXT, GPIO_MODE_OUTPUT);
    gpio_set_level(LED3_EXT, 0);

    i2sSetup();
}

void sdDoorbellStart() {

    printf("Create sound data queue\n");
    soundData = xQueueCreate(SOUND_DATA_QUEUE_LEN, sizeof(dataBlock));
    doorbellEventGroup = xEventGroupCreate();

    printf("Create NET task\n");
    if (xTaskCreatePinnedToCore(tskDoorbell, "tskDoorbell", 1024*3, NULL, PRIO_DRB, NULL, 1)!=pdPASS) {
        printf("ERROR creating doorbell task! Out of memory?\n");
    };
    printf("Player started\n");
}

void sdDoorbellRing() {
    xEventGroupClearBits(doorbellEventGroup, STOP_BIT);
    xEventGroupSetBits(doorbellEventGroup, START_BIT);
}

void sdDoorbellQuiet() {
    xEventGroupSetBits(doorbellEventGroup, STOP_BIT);
}

static bool mountCard() {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_SD_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags =0,
        .intr_flags = 0,
    };

    esp_err_t ret = spi_bus_initialize(SPI_SD_HOST, &bus_cfg, SPI_DMA_CHAN);
    if (ret != ESP_OK) {
        printf("Failed to initialize bus.\n");
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI_SD_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card = 0;
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            printf("Failed to mount filesystem.\n");
        } else {
            printf("Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.\n", esp_err_to_name(ret));
        }
        return false;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return true;
}

static bool unmountCard() {
    esp_err_t ret = esp_vfs_fat_sdmmc_unmount();
    if (ret != ESP_OK) {
        printf("Failed to unmount the card (%s).\n", esp_err_to_name(ret));
        return false;
    }
    ret = spi_bus_free(SPI_SD_HOST);
    if (ret != ESP_OK) {
        printf("Failed to free the SPI bus (%s).\n", esp_err_to_name(ret));
        return false;
    }

    return true;
}

static bool printDir(const char* name) {
    DIR* dir = opendir(name);
    if(dir == NULL) {
        printf("Failed to open dir: %s\n", strerror(errno));
        return false;
    }
    while (true) {
        struct dirent* de = readdir(dir);
        if (!de) {
            break;
        }
        printf("File: %s\n", de->d_name);
    }
    if(closedir(dir) < 0) {
        printf("Failed to close dir: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool playFile(const char* name) {
    printf("Opening mp3 file\n");
    FILE* f = fopen(name, "rb");
    if (f == NULL) {
        printf("Failed to open file for reading: %s\n", strerror(errno));
        return false;
    }

    printf("Reading file\n");
    struct dataBlock data = {};
    size_t filesize = 0;
    while(true) {
        data.used = fread(data.data, 1, DATA_BLOCK_SIZE, f);
        if(data.used == 0) {
            if(ferror(f)) {
                printf("Failed to read file: %s\n", strerror(errno));
                return false;
            }
            break;
        }
        EventBits_t bits = xEventGroupWaitBits(doorbellEventGroup, STOP_BIT, true, false, 0);
        if((bits & STOP_BIT) != 0) {
            break;
        }

        filesize += data.used;
        data.reset = false;
        xQueueSendToBack(soundData, &data, portMAX_DELAY);
    }
    printf("Closing mp3 file, read: %d\n", filesize);
    fclose(f);

    data.reset = true;
    xQueueSendToBack(soundData, &data, portMAX_DELAY);

    return true;
}

static void playSong() {
    if (!mountCard()) {
        vTaskDelete(NULL);
    }

    DIR* dir = opendir("/sdcard/songs");
    if(dir == NULL) {
        printf("Failed to open dir: %s\n", strerror(errno));
        vTaskDelete(NULL);
    }
    uint32_t songs = 0;
    while (true) {
        struct dirent* de = readdir(dir);
        if (!de) {
            break;
        }
        songs++;
    }
    rewinddir(dir);
    uint32_t selected = esp_random() % songs;
    uint32_t pos = 0;
    struct dirent* deSelected;
    while (true) {
        deSelected = readdir(dir);
        if(pos == selected) {
            break;
        }
        pos++;
    }    

    printf("Selected: %s\n", deSelected->d_name);

    char fullPath[strlen(deSelected->d_name) + 20];
    snprintf(fullPath, sizeof(fullPath), "/sdcard/songs/%s", deSelected->d_name);

    if(closedir(dir) < 0) {
        printf("Failed to close dir: %s\n", strerror(errno));
        vTaskDelete(NULL);
    }


    if (!playFile(fullPath)) {
        vTaskDelete(NULL);
    }


    if (!unmountCard()) {
        vTaskDelete(NULL);
    }    
}

static void playFile() {
    if (!mountCard()) {
        vTaskDelete(NULL);
    }

    if(!printDir("/sdcard/MUSIC")) {
        vTaskDelete(NULL);
    }

    if (!playFile("/sdcard/MUSIC/ENGLIS~1.MP3")) {
        vTaskDelete(NULL);
    }

    if (!unmountCard()) {
        vTaskDelete(NULL);
    }    
}

static void tskDoorbell(void *pvParameters) {

    printf("Creating MP3 player...\n");
    mp3Player = mp3player_create(PRIO_MP3, SOUND_DATA_QUEUE_LEN, soundData);
    printf("MP3 player created\n");

// Wait for sem doorbell, sem random song, connection(?), or conn data?
    while(true) {
        if (!mountCard()) {
            gpio_set_level(LED3_EXT, 1);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        if(!printDir("/sdcard/MUSIC")) {
            gpio_set_level(LED3_EXT, 1);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!unmountCard()) {
            gpio_set_level(LED3_EXT, 1);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        gpio_set_level(LED3_EXT, 0);

        //playSong();
        EventBits_t bits = xEventGroupWaitBits(doorbellEventGroup, START_BIT, true, false, pdMS_TO_TICKS(15000));
        if((bits & START_BIT) != 0) {
            playFile();
        }
    }
}