#include "player.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs.h"

#include "i2s_sink.h"
#include "sound.h"
#include "mp3player.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#define PIN_NUM_MISO GPIO_NUM_19
#define PIN_NUM_MOSI GPIO_NUM_5
#define PIN_NUM_CLK  GPIO_NUM_18
#define PIN_NUM_CS GPIO_NUM_4

static QueueHandle_t soundData = NULL;

void playerSetup() {
    i2sSetup();
}

#define PRIO_MAD configMAX_PRIORITIES - 2
#define PRIO_NET configMAX_PRIORITIES - 3

static void tsknet(void *pvParameters);

static void tskpcm(void *pvParameters);

//#define SOUND_DATA_QUEUE_LEN 8
#define SOUND_DATA_QUEUE_LEN 64

void playerStart() {
    // setup_triangle_sine_waves(I2S_BITS_PER_SAMPLE_16BIT, 44100);

    // if (xTaskCreatePinnedToCore(tsk_triangle_sine_waves, "tsk_triangle_sine_waves", 2000, NULL, PRIO_NET, NULL, 1)!=pdPASS) {
    //     printf("ERROR creating note task! Out of memory?\n");
    // };
    

    printf("Create sound data queue\n");
    soundData = xQueueCreate(SOUND_DATA_QUEUE_LEN, sizeof(dataBlock));

    printf("Create NET task\n");
    if (xTaskCreatePinnedToCore(tsknet, "tsknet", 1024*3, NULL, PRIO_NET, NULL, 1)!=pdPASS) {
        printf("ERROR creating NET task! Out of memory?\n");
    };
    printf("Player started\n");
}

static mp3player_handle_t mp3Player = NULL;

static bool mountCard() {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = PIN_NUM_MISO;
    slot_config.gpio_mosi = PIN_NUM_MOSI;
    slot_config.gpio_sck  = PIN_NUM_CLK;
    slot_config.gpio_cs = PIN_NUM_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card = 0;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            printf("Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.\n");
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
    esp_err_t ret2 = esp_vfs_fat_sdmmc_unmount();
    if (ret2 != ESP_OK)
    {
        printf("Failed to unmount the card (%s).\n", esp_err_to_name(ret2));
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

static void playFile() {
    if (!mountCard()) {
        vTaskDelete(NULL);
    }

    if(!printDir("/sdcard/MUSIC")) {
        vTaskDelete(NULL);
    }

    printf("Opening mp3 file\n");
    FILE* f = fopen("/sdcard/MUSIC/ENGLIS~1.MP3", "rb");
    if (f == NULL) {
        printf("Failed to open file for reading: %s\n", strerror(errno));
        vTaskDelete(NULL);
    }

    printf("Reading file\n");
    struct dataBlock data = {};
    size_t filesize = 0;
    while(true) {
        data.used = fread(data.data, 1, DATA_BLOCK_SIZE, f);
        if(data.used == 0) {
            if(ferror(f)) {
                printf("Failed to read file: %s\n", strerror(errno));
                vTaskDelete(NULL);
            }
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

    if (!unmountCard()) {
        vTaskDelete(NULL);
    }    
}

static bool readFully(struct dataBlock* block, int client_sock) {
    // TODO: Leftovers from close
    block->used = 0;
    while(block->used < DATA_BLOCK_SIZE) {
        int bytesRead = read(client_sock, block->data +  block->used, DATA_BLOCK_SIZE -  block->used);
        if(bytesRead < 0) {
            bool connected;
            switch (errno) {
                case ENOTCONN:
                case EPIPE:
                case ECONNRESET:
                case ECONNREFUSED:
                case ECONNABORTED:
                    connected = false;
                    break;
                default:
                    // Still connected ???
                    connected = true;
                    break;
            }

            if(!connected) {
                return false;
            }

            // Try again??
            continue;
        }
        block->used += bytesRead;
    }
    return true;
}

    // Bigger buffer, 32k? (dma buffer could be smaller? 32x64 for mp3 thing?)
    // separate reading tcp/ writing to dma
    // no wait > dma (0)

    // Why does RC receiver no work anymore??? Logic analyzer influences!! everything is a bit big now.. :)
    // POwer???? It's fine on the USB plug, or ground loop... 3 dev's
     //I2S_EVENT_TX_DONE
     //!!!! keep a volatile counter based on that :) (dun worklike that...)
                // ouch!
                // Second speaker, see if long leads work.
                // Wifi, router mayby? sending side?? nodelay?? buffer size on sending size.. test speed with nothing else?
                // Blinky light for buffer empty/buffer full!
                // Move to cpp file, then commit
                // Zero when buffer empty ? better then ugly noise?
                // Good to measure voltage my old tomato :)

static void tsknet(void *pvParameters) {

    printf("Creating MP3 player...\n");
    mp3Player = mp3player_create(PRIO_MAD, SOUND_DATA_QUEUE_LEN, soundData);
    printf("MP3 player created\n");

    //playFile();

    int sockfd = socket(AF_INET , SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("Could not create server socket\n");
        return;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8088);
    if(bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Could not bind server socket\n");
        return;
    }

    // TODO 4 = lwip backlog, could be 1 or zero??
    if(listen(sockfd , 4) < 0) {
        printf("Could not start listening on server socket\n");
        return;
    }
    //fcntl(sockfd, F_SETFL, O_NONBLOCK);


  
    printf("Started server\n");
  

    while(true) {

        bool _noDelay = false;
        int client_sock;
        struct sockaddr_in _client;
        int cs = sizeof(struct sockaddr_in);
        client_sock = lwip_accept_r(sockfd, (struct sockaddr *)&_client, (socklen_t*)&cs);
        if(client_sock < 0){
            printf("Accept failed\n");
            return;
        }

        int val = 1;
        if(setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&val, sizeof(int)) != ESP_OK) {
            printf("Set SO_KEEPALIVE failed\n");
            return;
        }

        val = _noDelay;
        if(setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(int)) != ESP_OK) {
            printf("Set TCP_NODELAY failed\n");
            return;
        }

        printf("Connected\n");
        // TODO: Far less mem for pcm
        // if (xTaskCreatePinnedToCore(tskpcm, "tskpcm", 16100, NULL, PRIO_MAD, NULL, 1) != pdPASS) {
        //     printf("ERROR creating PCM task! Out of memory?\n");
        // };

        struct dataBlock data = {};

        while(true) {
            if(!readFully(&data, client_sock)) {
                close(client_sock);

                printf("Disconnected\n");

                data.reset = true;
                xQueueSendToBack(soundData, &data, portMAX_DELAY);

                break;
            }
            data.reset = false;
            xQueueSendToBack(soundData, &data, portMAX_DELAY);
        }
    }


    printf("Destroying MP3 player...\n");
    mp3player_destroy(mp3Player);
    printf("MP3 player destroyed\n");

    vTaskDelete(NULL);
}

// Handler for pcm data
static void tskpcm(void *pvParameters) {
    struct dataBlock data = {};
    while(true) {
        //TODO: This helps performance, but.. only partial, think context switching
        // revert prio (same prob, lots of switching for single bytes)? fill up buffer until full (or nothing left to fill with), then handle?


/// YO! pre allocated buffers, x the size of the queue, stick pointers on queue. No.. doh
// Idea: can't put new one on, till there is space in the queue, which means one is free, but that doesn't work in many ways.
        xQueueReceive(soundData, &data, portMAX_DELAY);
        // TODO: What if data is not a multiple of 4? ..
        renderSamples32(data.data, data.used / 4);
    }
}

