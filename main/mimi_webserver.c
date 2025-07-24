#include "mimi_webserver.h"

#include "esp_log.h"
#include "mimi_camera.h"
#include "mimi_common.h"

static esp_err_t http_stream_handler(httpd_req_t *req) {
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

httpd_handle_t start_webserver() {
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
