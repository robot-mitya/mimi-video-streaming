#include "mimi_camera.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "mimi_common.h"
#include "esp_log.h"
#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include "sccb.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"

#define CAM_GC2145_ADDR 0x3C
#define CAM_REGISTER_0x17 0x17
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

static jpeg_frame_t jpeg_pool[JPEG_FRAME_POOL_SIZE];
static int jpeg_pool_index = 0;

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

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_YUV422, //YUV422,GRAYSCALE,RGB565,JPEG
    // .frame_size = FRAMESIZE_HVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    .frame_size = FRAMESIZE_320X320,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    // .frame_size = FRAMESIZE_QVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    // .frame_size = FRAMESIZE_QQVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

esp_err_t init_camera(void) {
    const esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MIMI, "Camera init failed with error 0x%x", err);
        return err;
    }

    // Rotate 180:
    uint8_t reg = SCCB_Read(CAM_GC2145_ADDR, CAM_REGISTER_0x17);
    reg |= 0x03;
    SCCB_Write(CAM_GC2145_ADDR, CAM_REGISTER_0x17, reg);

    return ESP_OK;
}

void camera_task(void *)
{
    jpeg_enc_config_t enc_cfg = {
        // // HVGA:
        // .width = 480,
        // .height = 320,
        // 320x320:
        .width = 320,
        .height = 320,
        // // QVGA:
        // .width = 320,
        // .height = 240,
        // // QQVGA:
        // .width = 160,
        // .height = 120,
        .src_type = JPEG_PIXEL_FORMAT_YCbYCr,
        .subsampling = JPEG_SUBSAMPLE_422,
        // .subsampling = JPEG_SUBSAMPLE_GRAY,
        .quality = 10,
        .rotate = JPEG_ROTATE_0D,
        .task_enable = true,
        .hfm_task_priority = ENCODING_TASK_PRIORITY,
        .hfm_task_core = ENCODING_TASK_CORE_ID
    };

    // Initialize JPEG frame pool
    for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
        jpeg_pool[i].in_buf = jpeg_calloc_align(MAX_JPEG_SIZE, 16);
        jpeg_pool[i].fb.buf = heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
        jpeg_pool[i].fb.len = 0;
        jpeg_pool[i].fb.width = 0;
        jpeg_pool[i].fb.height = 0;
        jpeg_pool[i].fb.format = PIXFORMAT_JPEG;
    }

    jpeg_enc_handle_t jpeg_enc = NULL;
    if (jpeg_enc_open(&enc_cfg, &jpeg_enc) != JPEG_ERR_OK) {
        ESP_LOGE(TAG_MIMI, "jpeg_enc_open() failed");
        vTaskDelete(NULL);
        return;
    }

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG_MIMI, "Camera capture failed");
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
            ESP_LOGE(TAG_MIMI, "JPEG encoding failed (%d)", jret);
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        jpeg_frame->fb.width = fb->width;
        jpeg_frame->fb.height = fb->height;
        jpeg_frame->fb.len = jpeg_len;

        if (xQueueSend(frame_queue, &jpeg_frame, 0) != pdTRUE) {
            ESP_LOGD(TAG_MIMI, "Frame queue full, dropping frame. Frame size: %d bytes.", jpeg_len);
        }
    }
}
