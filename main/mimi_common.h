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

#define UART_TASK_CORE_ID 0
#define UART_TASK_PRIORITY 6
#define UART_PORT UART_NUM_0
// #define UART_TX_PIN 43
// #define UART_RX_PIN 44
#define UART_BUF_SIZE 80

extern const char *TAG_MIMI;

extern QueueHandle_t frame_queue;

#endif //MIMI_COMMON_H
