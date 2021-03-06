#ifndef MP3PLAYER_H_
#define MP3PLAYER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp3player* mp3player_handle_t;

mp3player_handle_t mp3player_create(int prio, int inputQueueLen, QueueHandle_t inputQueue);
void mp3player_destroy(mp3player_handle_t player);

#ifdef __cplusplus
}
#endif

#endif /* MP3PLAYER_H_ */
