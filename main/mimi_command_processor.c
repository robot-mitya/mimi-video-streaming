#include "mimi_command_processor.h"

#include <string.h>

#include "mimi_common.h"
#include "driver/uart.h"

typedef struct {
    const char *mnemonic;
    CommandFunc handler;
} CommandEntry;

extern CommandEntry command_table[];

void uartOutputMessage(const char* message) {
    uart_write_bytes(UART_PORT, message, strlen(message));
}

int pingCameraCommand(char* commandLine, unsigned int startPosition) {
    uartOutputMessage("pong-camera\r\n");
    return 0;
}

CommandEntry command_table[] = {
    {"ping-camera", pingCameraCommand},
    {NULL, NULL}
};

CommandFunc getCommandHandler(const char* mnemonic) {
    // char buffer[100];
    // sprintf(buffer, "++++ %s\r\n", mnemonic);
    // uartOutputMessage(buffer);
    for (int i = 0; command_table[i].mnemonic != NULL; i++) {
        // sprintf(buffer, "++++ Iteration %d: %s\r\n", i, command_table[i].mnemonic);
        // uartOutputMessage(buffer);
        if (strcmp(command_table[i].mnemonic, mnemonic) == 0) {
            return command_table[i].handler;
        }
    }
    return NULL;
}