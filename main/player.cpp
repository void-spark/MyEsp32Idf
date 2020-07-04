#include "player.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "esp_system.h"

#include "i2s_sink.h"
#include "sound.h"
#include "mp3player.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

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
                // Wifi, router mayby? sending side?? nodelay?? buffer size on sending size.. test speed with nothing else?
                // Blinky light for buffer empty/buffer full!
                // Move to cpp file, then commit
                // Good to measure voltage my old tomato :)

static void tsknet(void *pvParameters) {

    printf("Creating MP3 player...\n");
    mp3Player = mp3player_create(PRIO_MAD, SOUND_DATA_QUEUE_LEN, soundData);
    printf("MP3 player created\n");

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
        client_sock = accept(sockfd, (struct sockaddr *)&_client, (socklen_t*)&cs);
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

