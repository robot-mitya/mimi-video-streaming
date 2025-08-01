#include "mimi_uart.h"

#include <string.h>

#include "mimi_common.h"
#include "esp_err.h"
#include "mimi_language.h"
#include "mimi_command_processor.h"

#include "driver/uart.h"

void init_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0));
}

void uart_task(void *) {
    char data[UART_BUF_SIZE];
    int len = 0;
    char line[UART_BUF_SIZE];
    int line_pos = 0;
    char mnemonic[MAX_MNEMONIC_LENGTH];

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        len = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; ++i) {
                const char c = data[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line[line_pos] = 0;
                        unsigned int startPos = 0;
                        bool isString;
                        startPos = extractLexeme(startPos, UART_BUF_SIZE, line, mnemonic, &isString);
                        const CommandFunc commandHandler = getCommandHandler(mnemonic);
                        if (commandHandler != NULL) {
                            commandHandler(line, startPos);
                        }
                        // uart_write_bytes(UART_PORT, mnemonic, strlen(mnemonic));
                        // uart_write_bytes(UART_PORT, "\r\n", 2);
                        line_pos = 0;
                    }
                } else if (line_pos < UART_BUF_SIZE - 1) {
                    line[line_pos++] = c;
                }
            }
        }
    }
}
