#include "player.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "i2sstuff.h"
#include "sound.h"
#include "mp3player.h"

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <mad.h>
#include <stream.h>
#include <frame.h>
#include <synth.h>


#define BCK 23
#define DIN 22
#define LCK 21

#define I2S_NUM         (I2S_NUM_0)

#define LED3_EXT GPIO_NUM_25

static RingbufHandle_t ringBuf = NULL;

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
    if (xTaskCreatePinnedToCore(tsknet, "tsknet", 2000, NULL, PRIO_NET, NULL, 1)!=pdPASS) {
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

