// mimi_video_streaming - ESP32-S3 MJPEG streaming with minglish control

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "mimi_camera.h"
#include "nvs_flash.h"
#include "mimi_common.h"

static int retry_num = 0;
static EventGroupHandle_t wifi_event_group;

#define WIFI_SSID           "Link_D65F_2.4GHz"
#define WIFI_PASS           "27224069"
#define WIFI_MAX_RETRY      5
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static void event_handler(void*,
    // ReSharper disable once CppParameterMayBeConst
    esp_event_base_t event_base,
    // ReSharper disable once CppParameterMayBeConst
    int32_t event_id,
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG_MIMI, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_MIMI,"Connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t* event = event_data;
        ESP_LOGI(TAG_MIMI, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t http_stream_handler(httpd_req_t *req)
{
    static const char *boundary = "\r\n--123456789000000000000987654321\r\n";
    static const char *content_type = "image/jpeg";

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=123456789000000000000987654321");

    while (1) {
        jpeg_frame_t *jpeg_frame = NULL;

        if (xQueueReceive(frame_queue, &jpeg_frame, pdMS_TO_TICKS(10)) == pdTRUE) {
            char header_buf[128];
            const int header_len = snprintf(header_buf, sizeof(header_buf),
                                      "%sContent-Type: %s\r\nContent-Length: %u\r\n\r\n",
                                      boundary, content_type, jpeg_frame->fb.len);

            if (httpd_resp_send_chunk(req, header_buf, header_len) != ESP_OK ||
                httpd_resp_send_chunk(req, (const char *)jpeg_frame->fb.buf, (ssize_t)jpeg_frame->fb.len) != ESP_OK) {
                ESP_LOGW(TAG_MIMI, "Client disconnected");
                break;
                }
        }
    }

    httpd_resp_send_chunk(req, NULL, 0); // Закрыть поток
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    const httpd_config_t config = {
        .task_priority      = STREAMING_TASK_PRIORITY,
        .stack_size         = 4096,
        .core_id            = STREAMING_TASK_CORE_ID,
        .task_caps          = (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .server_port        = 80,
        .ctrl_port          = ESP_HTTPD_DEF_CTRL_PORT,
        .max_open_sockets   = 7,
        .max_uri_handlers   = 8,
        .max_resp_headers   = 8,
        .backlog_conn       = 5,
        .lru_purge_enable   = false,
        .recv_wait_timeout  = 5,
        .send_wait_timeout  = 5,
        .global_user_ctx = NULL,
        .global_user_ctx_free_fn = NULL,
        .global_transport_ctx = NULL,
        .global_transport_ctx_free_fn = NULL,
        .enable_so_linger = false,
        .linger_timeout = 0,
        .keep_alive_enable = false,
        .keep_alive_idle = 0,
        .keep_alive_interval = 0,
        .keep_alive_count = 0,
        .open_fn = NULL,
        .close_fn = NULL,
        .uri_match_fn = NULL
    };
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        const httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = http_stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
    }
    return server;
}

static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG_MIMI, "Wi-Fi init finished");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_MIMI, "Connected to AP: %s", WIFI_SSID);
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        ESP_LOGI(TAG_MIMI, "RSSI: %d dBm\n", ap_info.rssi);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_MIMI, "Failed to connect to SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG_MIMI, "Unexpected event");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(wifi_event_group);
}

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

    frame_queue = xQueueCreate(JPEG_FRAME_POOL_SIZE, sizeof(jpeg_frame_t *));
    xTaskCreatePinnedToCore(camera_task, "camera_task", 4096, NULL, CAMERA_TASK_PRIORITY, NULL, CAMERA_TASK_CORE_ID);
    start_webserver();

    // TODO: add minglish UART command handler

    ESP_LOGI(TAG_MIMI, "Free heap: %lu", esp_get_free_heap_size());
    ESP_LOGI(TAG_MIMI, "Free PSRAM: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
