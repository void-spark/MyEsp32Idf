#include <string.h>

#include "esp_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "sys/param.h"

#include "i2s_sink.h"

#include <mad.h>
//#include <stream.h>
#include <frame.h>
#include <synth.h>

#include "mp3player.h"

// The theoretical maximum frame size is 2881 bytes,
// MPEG 2.5 Layer II, 8000 Hz @ 160 kbps, with a padding slot plus 8 byte MAD_BUFFER_GUARD.
#define MAX_FRAME_SIZE (2889)

// The theoretical minimum frame size of 24 plus 8 byte MAD_BUFFER_GUARD.
#define MIN_FRAME_SIZE (32)

struct mp3player {
    RingbufHandle_t ringBuf;
    QueueSetHandle_t queueSet;
    SemaphoreHandle_t stopSemaphore;
    SemaphoreHandle_t stoppedSemaphore;

    struct mad_stream *stream;
    struct mad_frame *frame;
    struct mad_synth *synth;

    unsigned char writeBuf[MAX_FRAME_SIZE];
    unsigned char* writeBufEnd;
    TaskHandle_t madHandle;
};

static void tskmad(void *pvParameters);

mp3player_handle_t mp3player_create(int prio, RingbufHandle_t ringBuf) {

    mp3player_handle_t player = (mp3player_handle_t)calloc(1, sizeof(struct mp3player));

    player->queueSet = xQueueCreateSet( 1 + 1 );


    player->stopSemaphore = xSemaphoreCreateBinary();
    player->stoppedSemaphore = xSemaphoreCreateBinary();

    player->ringBuf = ringBuf;

    xRingbufferAddToQueueSetRead(player->ringBuf, player->queueSet);
    xQueueAddToSet(player->stopSemaphore, player->queueSet);


    player->stream = (struct mad_stream *)malloc(sizeof(struct mad_stream));
    player->frame = (struct mad_frame *)malloc(sizeof(struct mad_frame));
    player->synth = (struct mad_synth *)malloc(sizeof(struct mad_synth));

    mad_stream_init(player->stream);
    mad_frame_init(player->frame);
    mad_synth_init(player->synth);

    player->writeBufEnd = player->writeBuf;

    printf("Create MAD task\n");

    if (xTaskCreatePinnedToCore(tskmad, "tskmad", 16100, player, prio, &player->madHandle, 1) != pdPASS) {
        printf("ERROR creating MAD task! Out of memory?\n");
    };
    printf("Player: %p\n", player);
    printf("Task: %p\n", player->madHandle);

    return player;
}

void mp3player_destroy(mp3player_handle_t player) {

    printf("Destroying MP3 player: %p\n", player);

    // Let the task know it's time to stop
    xSemaphoreGive(player->stopSemaphore);

    printf("Waiting for task to end\n");

    // Wait for the task to end
    xSemaphoreTake(player->stoppedSemaphore, portMAX_DELAY);


    xQueueRemoveFromSet(player->stopSemaphore, player->queueSet);
    xRingbufferRemoveFromQueueSetRead(player->ringBuf, player->queueSet);

    vQueueDelete(player->stoppedSemaphore);
    vQueueDelete(player->stopSemaphore);

    vQueueDelete(player->queueSet);

    printf("Delete MAD data structures\n");
    mad_synth_finish(player->synth);
    mad_frame_finish(player->frame);
    mad_stream_finish(player->stream);

    free(player->synth);
    free(player->frame);
    free(player->stream);

    printf("Delete MP3 player structure\n");
    free(player);
}

static unsigned long decode(mp3player_handle_t player, const unsigned char * buffer, unsigned long length) {
    mad_stream_buffer(player->stream, buffer, length);

    // Decode frames until MAD complains
    while(true) {
        // returns 0 or -1
        if (mad_frame_decode(player->frame, player->stream) == -1) {
            if (!MAD_RECOVERABLE(player->stream->error)) {
                //We're most likely out of buffer and need to call input() again
                //printf("breaking: dec err 0x%04x (%s)\n", stream->error, mad_stream_errorstr(stream));
                break;
            }

            // Skip the invalid 'frame'
            //printf("dec err 0x%04x (%s)\n", stream->error, mad_stream_errorstr(stream));
            continue;
        }
        mad_synth_frame(player->synth, player->frame);
    }

    // next_frame is the position MAD is interested in resuming from
    return player->stream->next_frame - buffer;
}

// handler for mp3 data
static void tskmad(void *pvParameters) {
    //TODO: led on i2s events(buffer empty) ?
    // full        digitalWrite(LED3_EXT, LOW);
    // empty         digitalWrite(LED3_EXT, HIGH);
    //   Also, clear on empty: 
    //        // Would prefer to only write till the buffer is full, so we don't lose the last good frames.
    //        i2s_zero_dma_buffer(I2S_NUM_0);

    mp3player_handle_t player = (mp3player_handle_t)pvParameters;

    printf("MP3 player reporting: %p\n", player);

    while(true) {
        QueueSetMemberHandle_t activatedMember = xQueueSelectFromSet(player->queueSet, portMAX_DELAY);
        if(activatedMember == player->stopSemaphore) {           
            printf("This is the end, my one and only friend.\n");
            xSemaphoreTake(player->stopSemaphore, 0);
            printf("Drop the noise.\n");
            i2s_zero_dma_buffer(I2S_NUM_0);
            printf("So long, and thanks for all the fish.\n");
            // Notify of our demise
            xSemaphoreGive(player->stoppedSemaphore);
            printf("From hells heart I stab at thee.\n");
            vTaskDelete(NULL);
            return;
        } else {
            while(true) {
                // ring buffer read mutex
                size_t spaceLeft = sizeof(player->writeBuf) - (player->writeBufEnd - player->writeBuf);

                size_t bytesReceived = 0;
                void * received = xRingbufferReceiveUpTo(player->ringBuf, &bytesReceived, 0, spaceLeft);
                if(received == NULL) {
                    // We need to wait again
                    break;
                }
                memcpy(player->writeBufEnd, received, bytesReceived);
                player->writeBufEnd += bytesReceived;
                vRingbufferReturnItem(player->ringBuf, received);

                // Okay, let MAD decode the buffer.
                unsigned long used = decode(player, player->writeBuf, player->writeBufEnd - player->writeBuf);

                unsigned char * from = player->writeBuf + used;
                unsigned char * to = player->writeBuf;
                while(from < player->writeBufEnd) {
                    *to++ = *from++;
                }
                player->writeBufEnd = to;
            }
        }
    }
}

// i2s: queue gets a pointer when interrupt handler indicates dma queue empty
// writing is protected with semapore, gets a ptr of queue to write to.
//   and pointery stuff..

/* Called by the NXP modifications of libmad. Sets the needed output sample rate. */
void set_dac_sample_rate(int rate) {
}

/* render callback for the libmad synth */
void render_sample_block(short *sample_buff_ch0, short *sample_buff_ch1, int num_samples, unsigned int num_channels) {

    renderSamples16(sample_buff_ch0, sample_buff_ch1, num_samples);
}
