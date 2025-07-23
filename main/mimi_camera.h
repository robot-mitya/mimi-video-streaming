#ifndef MIMI_CAMERA_H
#define MIMI_CAMERA_H

#include "esp_camera.h"

#define JPEG_FRAME_POOL_SIZE 3

typedef struct {
    uint8_t *in_buf;      // Aligned input buffer (for encoder)
    camera_fb_t fb;       // Output JPEG frame struct (for streaming)
} jpeg_frame_t;

esp_err_t init_camera(void);
void camera_task(void *);

#endif //MIMI_CAMERA_H
