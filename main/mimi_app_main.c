// mimi_video_streaming - ESP32-S3 MJPEG streaming with minglish control

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "mimi_camera.h"
#include "mimi_common.h"
#include "mimi_webserver.h"
#include "mimi_wifi.h"
#include "mimi_uart.h"

void app_main(void) {
    ESP_LOGI(TAG_MIMI, "Startup...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_MIMI, "Initializing WiFi connection...");
    init_wifi();

    ESP_LOGI(TAG_MIMI, "Initializing camera...");
    ESP_ERROR_CHECK(init_camera());
    ESP_LOGI(TAG_MIMI, "Initializing camera...done");

    frame_queue = xQueueCreate(JPEG_FRAME_POOL_SIZE, sizeof(jpeg_frame_t *));
    xTaskCreatePinnedToCore(camera_task, "camera_task", 4096, NULL, CAMERA_TASK_PRIORITY, NULL, CAMERA_TASK_CORE_ID);
    start_webserver();

    init_uart();
    xTaskCreatePinnedToCore(uart_task, "uart_task", 2048, NULL, UART_TASK_PRIORITY, NULL, UART_TASK_CORE_ID);

    ESP_LOGI(TAG_MIMI, "Free heap: %lu", esp_get_free_heap_size());
    ESP_LOGI(TAG_MIMI, "Free PSRAM: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
