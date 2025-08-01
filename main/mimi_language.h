#ifndef MIMI_LANGUAGE_H
#define MIMI_LANGUAGE_H

#include <stdbool.h>

#define MAX_MNEMONIC_LENGTH 80
#define MAX_ARGUMENT_LENGTH 80

unsigned int extractLexeme(
    unsigned int startPos,
    unsigned int bufferLength, const char* buffer,
    char* lexeme, bool* isString);

#endif //MIMI_LANGUAGE_H
