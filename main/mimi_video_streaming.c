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

static camera_fb_t *latest_fb = NULL;
static SemaphoreHandle_t fb_mutex = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
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
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    char part_buf[64];
    static const char *boundary = "--frame";
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    // httpd_resp_send_chunk(req, "\r\n", 2);

    esp_err_t ret;

    while (1) {
        if (xSemaphoreTake(fb_mutex, portMAX_DELAY)) {
            camera_fb_t *fb = latest_fb;
            if (fb) {
                // ESP_LOGI(TAG, "JPEG frame size: %d bytes, [%dx%d]", fb->len, fb->width, fb->height);
                ESP_LOGI(TAG, "Raw frame format: %d, size: %dx%d, len: %d", fb->format, fb->width, fb->height, fb->len);
                snprintf(part_buf, sizeof(part_buf),
                         "\r\n%s\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n",
                         boundary, fb->len);
                ret = httpd_resp_send_chunk(req, part_buf, strlen(part_buf));
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "httpd_resp_send_chunk(%d) failed with code %d", strlen(part_buf), ret);
                    break;
                }
                ret = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "httpd_resp_send_chunk(%d) failed with code %d", fb->len, ret);
                    break;
                }
            }
            xSemaphoreGive(fb_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "Client disconnected from /stream");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        const httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
    }
    return server;
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

    .pixel_format = PIXFORMAT_RGB565, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_QVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static esp_err_t init_camera(void) {
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
    }
    return err;
}

static void camera_task(void *arg)
{
    // Codec configuration:
    jpeg_enc_config_t enc_cfg = {
        .quality = 50,
        .width = 320,  // (Size values are temporary. I'll change them on the first frame.)
        .height = 240
    };

    // Create JPEG codec:
    jpeg_enc_handle_t jpeg_enc = NULL;
    if (jpeg_enc_open(&enc_cfg, &jpeg_enc) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_enc_open() failed");
        vTaskDelete(NULL);
        return;
    }

    // Allocate buffer once:
    const int out_len = MAX_JPEG_SIZE;
    uint8_t *out_buf = heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM);
    if (!out_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer");
        jpeg_enc_close(jpeg_enc);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Update width and height for the first time:
        if (enc_cfg.width != fb->width || enc_cfg.height != fb->height) {
            enc_cfg.width = fb->width;
            enc_cfg.height = fb->height;
            jpeg_enc_close(jpeg_enc);
            if (jpeg_enc_open(&enc_cfg, &jpeg_enc) != JPEG_ERR_OK) {
                ESP_LOGE(TAG, "jpeg_enc_open() failed on reinit");
                esp_camera_fb_return(fb);
                break;
            }
        }

        int jpeg_len = 0;

        // As suggested for the jpeg_enc_process(), I use jpeg_calloc_align() for the 16-bytes aligned buffer.
        uint8_t *in_buf = jpeg_calloc_align(fb->len, 16);
        if (!in_buf) {
            ESP_LOGE(TAG, "Failed to alloc aligned input buffer");
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        memcpy(in_buf, fb->buf, fb->len);

        const jpeg_error_t jret = jpeg_enc_process(jpeg_enc, in_buf, fb->len, out_buf, out_len, &jpeg_len);
        free(in_buf);

        if (jret != JPEG_ERR_OK || jpeg_len <= 0) {
            ESP_LOGE(TAG, "JPEG encoding failed (%d)", jret);
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        camera_fb_t *jpeg_fb = malloc(sizeof(camera_fb_t));
        if (!jpeg_fb) {
            ESP_LOGE(TAG, "Failed to alloc camera_fb_t");
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        *jpeg_fb = (camera_fb_t){
            .buf = malloc(jpeg_len),
            .len = jpeg_len,
            .width = fb->width,
            .height = fb->height,
            .format = PIXFORMAT_JPEG
        };
        if (!jpeg_fb->buf) {
            ESP_LOGE(TAG, "Failed to alloc output buffer");
            free(jpeg_fb);
            esp_camera_fb_return(fb);
            continue;
        }

        memcpy(jpeg_fb->buf, out_buf, jpeg_len);

        if (xSemaphoreTake(fb_mutex, portMAX_DELAY)) {
            if (latest_fb) {
                free(latest_fb->buf);
                free(latest_fb);
            }
            latest_fb = jpeg_fb;
            // ESP_LOGI(TAG, "JPEG frame size: %d bytes, [%dx%d]", latest_fb->len, latest_fb->width, latest_fb->height);
            xSemaphoreGive(fb_mutex);
        } else {
            free(jpeg_fb->buf);
            free(jpeg_fb);
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
    ESP_LOGI(TAG, "Startup...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing WiFi connection...");
    init_wifi();

    fb_mutex = xSemaphoreCreateMutex();
    assert(fb_mutex);

    ESP_LOGI(TAG, "Initializing camera...");
    ESP_ERROR_CHECK(init_camera());

    xTaskCreatePinnedToCore(camera_task, "camera_task", 8192, NULL, 5, NULL, 0);
    start_webserver();

    // TODO: add minglish UART command handler
}
