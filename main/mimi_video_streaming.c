// mimi_video_streaming - ESP32-S3 MJPEG streaming with minglish control

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_jpeg_enc.h"

static const char *TAG = "mimi_video";

static int retry_num = 0;
static EventGroupHandle_t wifi_event_group;

#define WIFI_SSID           "Link_D65F_2.4GHz"
#define WIFI_PASS           "27224069"
#define WIFI_MAX_RETRY      5
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define CAM_PIN_PWDN 38
#define CAM_PIN_RESET (-1)   //software reset will be performed
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D0 11
#define CAM_PIN_D1 9
#define CAM_PIN_D2 8
#define CAM_PIN_D3 10
#define CAM_PIN_D4 12
#define CAM_PIN_D5 18
#define CAM_PIN_D6 17
#define CAM_PIN_D7 16

#define MAX_JPEG_SIZE (200 * 1024)

#define JPEG_FRAME_POOL_SIZE 3

typedef struct {
    uint8_t *in_buf;      // Aligned input buffer (for encoder)
    camera_fb_t fb;       // Output JPEG frame struct (for streaming)
} jpeg_frame_t;

static jpeg_frame_t jpeg_pool[JPEG_FRAME_POOL_SIZE];
static int jpeg_pool_index = 0;


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
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t* event = event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_YUV422, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_QVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static esp_err_t init_camera(void) {
    const esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
    }
    return err;
}

QueueHandle_t frame_queue;

jpeg_pixel_format_t s;
static void camera_task(void *)
{
    jpeg_enc_config_t enc_cfg = {
        // QVGA:
        .width = 320,
        .height = 240,
        // // QQVGA:
        // .width = 160,
        // .height = 120,
        .src_type = JPEG_PIXEL_FORMAT_YCbYCr,
        .subsampling = JPEG_SUBSAMPLE_422,
        // .subsampling = JPEG_SUBSAMPLE_GRAY,
        .quality = 10,
        .rotate = JPEG_ROTATE_180D,
        .task_enable = true,
        .hfm_task_priority = 5,
        .hfm_task_core = 1
    };

    jpeg_enc_handle_t jpeg_enc = NULL;
    if (jpeg_enc_open(&enc_cfg, &jpeg_enc) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_enc_open() failed");
        vTaskDelete(NULL);
        return;
    }

    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        jpeg_frame_t *jpeg_frame = &jpeg_pool[jpeg_pool_index];
        jpeg_pool_index = (jpeg_pool_index + 1) % JPEG_FRAME_POOL_SIZE;

        memcpy(jpeg_frame->in_buf, fb->buf, fb->len);
        int jpeg_len = 0;
        const jpeg_error_t jret = jpeg_enc_process(
            jpeg_enc,
            jpeg_frame->in_buf, (int)fb->len,
            jpeg_frame->fb.buf, MAX_JPEG_SIZE,
            &jpeg_len
        );

        esp_camera_fb_return(fb);

        if (jret != JPEG_ERR_OK || jpeg_len <= 0) {
            ESP_LOGE(TAG, "JPEG encoding failed (%d)", jret);
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        jpeg_frame->fb.width = fb->width;
        jpeg_frame->fb.height = fb->height;
        jpeg_frame->fb.len = jpeg_len;

        if (xQueueSend(frame_queue, &jpeg_frame, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Frame queue full, dropping frame. Frame size: %d bytes.", jpeg_len);
        }
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
                ESP_LOGW(TAG, "Client disconnected");
                break;
                }
        }
    }

    httpd_resp_send_chunk(req, NULL, 0); // Закрыть поток
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    const httpd_config_t config = HTTPD_DEFAULT_CONFIG();
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
    ESP_LOGI(TAG, "Wi-Fi init finished");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP: %s", WIFI_SSID);
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        ESP_LOGI(TAG, "RSSI: %d dBm\n", ap_info.rssi);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(wifi_event_group);
}

void app_main(void) {

    // Initialize JPEG frame pool
    for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
        jpeg_pool[i].in_buf = jpeg_calloc_align(MAX_JPEG_SIZE, 16);
        jpeg_pool[i].fb.buf = heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
        jpeg_pool[i].fb.len = 0;
        jpeg_pool[i].fb.width = 0;
        jpeg_pool[i].fb.height = 0;
        jpeg_pool[i].fb.format = PIXFORMAT_JPEG;
    }

    ESP_LOGI(TAG, "Startup...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing WiFi connection...");
    init_wifi();

    ESP_LOGI(TAG, "Initializing camera...");
    ESP_ERROR_CHECK(init_camera());

    frame_queue = xQueueCreate(JPEG_FRAME_POOL_SIZE, sizeof(jpeg_frame_t *));
    xTaskCreatePinnedToCore(camera_task, "camera_task", 4096, NULL, 10, NULL, 0);
    start_webserver();

    // TODO: add minglish UART command handler
}
