#ifndef MIMI_COMMON_H
#define MIMI_COMMON_H

// ReSharper disable once CppUnusedIncludeDirective
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define CAMERA_TASK_CORE_ID 0
#define CAMERA_TASK_PRIORITY 10
#define ENCODING_TASK_CORE_ID 1
#define ENCODING_TASK_PRIORITY 5
#define STREAMING_TASK_CORE_ID 1
#define STREAMING_TASK_PRIORITY 5

extern const char *TAG_MIMI;

extern QueueHandle_t frame_queue;

#endif //MIMI_COMMON_H
