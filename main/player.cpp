#include "player.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs.h"

#include "i2sstuff.h"
#include "sound.h"
#include "mp3player.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <mad.h>
#include <stream.h>
#include <frame.h>
#include <synth.h>


#define PIN_NUM_MISO GPIO_NUM_19
#define PIN_NUM_MOSI GPIO_NUM_5
#define PIN_NUM_CLK  GPIO_NUM_18
#define PIN_NUM_CS GPIO_NUM_4


#define BCK 23
#define DIN 22
#define LCK 21

#define I2S_NUM         (I2S_NUM_0)

#define LED3_EXT GPIO_NUM_25

static RingbufHandle_t ringBuf = NULL;

#define SDSPI_HOST_DEFAULT_FIXED() {\
    .flags = SDMMC_HOST_FLAG_SPI, \
    .slot = HSPI_HOST, \
    .max_freq_khz = SDMMC_FREQ_DEFAULT, \
    .io_voltage = 3.3f, \
    .init = &sdspi_host_init, \
    .set_bus_width = NULL, \
    .get_bus_width = NULL, \
    .set_card_clk = &sdspi_host_set_card_clk, \
    .do_transaction = &sdspi_host_do_transaction, \
    .deinit = &sdspi_host_deinit, \
    .command_timeout_ms = 0, \
}

void playerSetup() {
    //for 36Khz sample rates, we create 100Hz sine wave, every cycle need 36000/100 = 360 samples (4-bytes or 8-bytes each sample)
    //depend on bits_per_sample
    //using 6 buffers, we need 60-samples per buffer
    //if 2-channels, 16-bit each channel, total buffer is 360*4 = 1440 bytes

    // i2s_setup(I2S_NUM, BCK, LCK, DIN, 8, 64); // old pcm/mp3?
    // i2s_setup(I2S_NUM, BCK, LCK, DIN, 6, 60, I2S_BITS_PER_SAMPLE_16BIT, 44100); //sine

    i2s_setup(I2S_NUM, BCK, LCK, DIN, 8, 256, I2S_BITS_PER_SAMPLE_16BIT, 44100);

    gpio_pad_select_gpio(LED3_EXT);
    gpio_set_direction(LED3_EXT, GPIO_MODE_OUTPUT);
    gpio_set_level(LED3_EXT, 0);
}

#define PRIO_MAD configMAX_PRIORITIES - 2
#define PRIO_NET configMAX_PRIORITIES - 3

static void tsknet(void *pvParameters);

static void tskpcm(void *pvParameters);

void playerStart() {
    // setup_triangle_sine_waves(I2S_NUM, I2S_BITS_PER_SAMPLE_16BIT, 44100);

    // if (xTaskCreatePinnedToCore(tsk_triangle_sine_waves, "tsk_triangle_sine_waves", 2000, NULL, PRIO_NET, NULL, 1)!=pdPASS) {
    //     printf("ERROR creating note task! Out of memory?\n");
    // };
    



    printf("Create ringbuf\n");
    ringBuf = xRingbufferCreate(12*1024, RINGBUF_TYPE_BYTEBUF); // was 16*

    printf("Create NET task\n");
    if (xTaskCreatePinnedToCore(tsknet, "tsknet", 1024*3, NULL, PRIO_NET, NULL, 1)!=pdPASS) {
        printf("ERROR creating NET task! Out of memory?\n");
    };
    printf("Player started\n");
}

static mp3player_handle_t mp3Player = NULL;

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


    sdmmc_host_t host = SDSPI_HOST_DEFAULT_FIXED();
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
        vTaskDelete(NULL);
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    DIR* dir = opendir("/sdcard/MUSIC");
    if(dir == NULL) {
        printf("Failed to open dir: %s\n", strerror(errno));
        vTaskDelete(NULL);
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
        vTaskDelete(NULL);
    }        

    printf("Opening mp3 file\n");
    FILE* f = fopen("/sdcard/MUSIC/ENGLIS~1.MP3", "rb");
    if (f == NULL) {
        printf("Failed to open file for reading: %s\n", strerror(errno));
        vTaskDelete(NULL);
    }

    printf("Creating mp3 player\n");
    mp3Player = mp3player_create(PRIO_MAD, ringBuf);
    const size_t bufsize2 = 256;
    uint8_t readBuf2[bufsize2] = {};

    printf("Reading file\n");
    size_t filesize = 0;
    while(true) {
        size_t bytesRead2 = fread(readBuf2, 1, bufsize2, f);
        if(bytesRead2 == 0) {
            if(ferror(f)) {
                printf("Failed to read file: %s\n", strerror(errno));
                vTaskDelete(NULL);
            }
            break;
        }
        filesize += bytesRead2;
        xRingbufferSend(ringBuf, readBuf2, bytesRead2, portMAX_DELAY);
    }
    printf("Closing mp3 file, read: %d\n", filesize);
    fclose(f);

    printf("Destroying mp3 player\n");
    mp3player_destroy(mp3Player);
    

    esp_err_t ret2 = esp_vfs_fat_sdmmc_unmount();
    if (ret2 != ESP_OK) {
        printf("Failed to unmount the card (%s).\n", esp_err_to_name(ret2));
        vTaskDelete(NULL);
    }

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
        mp3Player = mp3player_create(PRIO_MAD, ringBuf);

        const size_t bufsize = 256;
        uint8_t readBuf[bufsize] = {};

        while(true) {

            int bytesRead = read(client_sock, readBuf, bufsize);
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
                    close(client_sock);

                    printf("Disconnected\n");
        // TODO: Clear/delete ring buffer? Mayby in the mp3 player? method for that.. :)
        //                receiveBuffer.skipData(receiveBuffer.bufferedBytes());

                    mp3player_destroy(mp3Player);
                    break;
                }

                // Try again??
                continue;

            }
            xRingbufferSend(ringBuf, readBuf, bytesRead, portMAX_DELAY);
        }
    }

}

// Handler for pcm data
static void tskpcm(void *pvParameters) {
    uint8_t writeBuf[256] = {};
    unsigned int sampleBuf[sizeof(writeBuf)/4] = {};

    // Bytes left over, 0-3,
    int left = 0;


    while(true) {
        size_t receivedBytes = 0;
        void * rbData = xRingbufferReceiveUpTo(ringBuf, &receivedBytes, portMAX_DELAY, sizeof(writeBuf) - left);
        memcpy(writeBuf + left, rbData, receivedBytes);
        vRingbufferReturnItem(ringBuf, rbData);

        const size_t buffered = left + receivedBytes;
        const size_t usableSamples = buffered / 4;
        const size_t usableBytes = usableSamples * 4;

        for(size_t pos = 0 ; pos < usableSamples; pos++) {
            sampleBuf[pos] = 0;
            sampleBuf[pos] |= writeBuf[0 + 4*pos];
            sampleBuf[pos] <<= 8;
            sampleBuf[pos] |= writeBuf[1 + 4*pos];
            sampleBuf[pos] <<= 8;
            sampleBuf[pos] |= writeBuf[2 + 4*pos];
            sampleBuf[pos] <<= 8;
            sampleBuf[pos] |= writeBuf[3 + 4*pos];
        }

        // Using portMAX_DELAY means this blocks till all bytes are written
        i2s_write_bytes(I2S_NUM_0, (const char *)sampleBuf, usableBytes, portMAX_DELAY);

        for(int pos = usableBytes; pos < buffered ; pos++) {
            writeBuf[pos - usableBytes]  = writeBuf[pos];
        }
        left = buffered - usableBytes;
    }
}

