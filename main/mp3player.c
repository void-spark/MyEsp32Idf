#include <string.h>

#include "esp_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#include "sys/param.h"

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

    struct mad_stream *stream;
    struct mad_frame *frame;
    struct mad_synth *synth;

    unsigned char writeBuf[MAX_FRAME_SIZE];
    unsigned char* writeBufEnd;
    TaskHandle_t madHandle;
};

static void tskmad(void *pvParameters);

mp3player_handle_t mp3player_create(int prio, RingbufHandle_t ringBuf) {

    mp3player_handle_t player = calloc(1, sizeof(struct mp3player));

    player->ringBuf = ringBuf;

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
    printf("Player %p\n", player);
    printf("madHandle %p\n", player->madHandle);

    return player;
}

void mp3player_destroy(mp3player_handle_t player) {
    printf("Delete MAD task\n");
    printf("Player %p\n", player);
    printf("madHandle %p\n", player->madHandle);

    vTaskDelete( player->madHandle );

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

// handler for mp3 data
static void tskmad(void *pvParameters) {
    //TODO: led on i2s events(buffer empty) ?
    // full        digitalWrite(LED3_EXT, LOW);
    // empty         digitalWrite(LED3_EXT, HIGH);
    //   Also, clear on empty: 
    //        // Would prefer to only write till the buffer is full, so we don't lose the last good frames.
    //        i2s_zero_dma_buffer(I2S_NUM_0);

    mp3player_handle_t player = (mp3player_handle_t)pvParameters;

    while(true) {
        // next_frame is the position MAD is interested in resuming from
        unsigned char  *from = (unsigned char  *)player->stream->next_frame;
        // Move the next frame bytes to the start of writeBuf
        unsigned char *to = player->writeBuf;
        for(; from < player->stream->bufend ; from++, to++) {
            *to = *from;
        }
        player->writeBufEnd = to;

        size_t sampleBytes = sizeof(player->writeBuf) - (player->writeBufEnd - player->writeBuf);
        size_t got = 0;
        void * rbData =xRingbufferReceiveUpTo(player->ringBuf, &got, portMAX_DELAY, sampleBytes);
        memcpy(player->writeBufEnd, rbData, got);
        player->writeBufEnd += got;
        vRingbufferReturnItem(player->ringBuf, rbData);

        // Okay, let MAD decode the buffer.
        mad_stream_buffer(player->stream, (unsigned char*) player->writeBuf, player->writeBufEnd - player->writeBuf);


        // decode frames until MAD complains
        while(1) {

            // returns 0 or -1
            int ret = mad_frame_decode(player->frame, player->stream);
            if (ret == -1) {
                if (!MAD_RECOVERABLE(player->stream->error)) {
                    //We're most likely out of buffer and need to call input() again
                    //printf("breaking: dec err 0x%04x (%s)\n", stream->error, mad_stream_errorstr(stream));
                    break;
                }

                //printf("dec err 0x%04x (%s)\n", stream->error, mad_stream_errorstr(stream));
                continue;
            }
            mad_synth_frame(player->synth, player->frame);
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

    // Based on synth.c, at most 32 samples at a time.
    static char sampleBuf[32 * 2 * 2] = {};

    for(int pos = 0 ; pos < MIN(32, num_samples); pos++) {
        short ch0Sample = sample_buff_ch0[pos] / 2;
        short ch1Sample = sample_buff_ch1[pos] / 2;

        sampleBuf[pos * 4 ] = ch0Sample & 0xff;
        sampleBuf[pos * 4 + 1] = (ch0Sample >> 8) & 0xff;

        sampleBuf[pos * 4 + 2] = ch1Sample  & 0xff;
        sampleBuf[pos * 4 + 3] = (ch1Sample >> 8) & 0xff;
    }
    int sampleBytes = num_samples * 4;
    unsigned int bufPos = 0;
    while(sampleBytes - bufPos > 0) {
        int written = i2s_write_bytes(I2S_NUM_0, ((const char *)sampleBuf) + bufPos, sampleBytes - bufPos, portMAX_DELAY);
        bufPos += written;
    }

    return;
}
