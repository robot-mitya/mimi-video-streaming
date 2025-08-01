#include "mimi_language.h"

#include <ctype.h>

/**
 * Extracts the next whitespace-delimited word from buffer starting at startPos.
 * Skips leading whitespace, copies the word into `word`, and returns the position
 * immediately after the word. If no word is found, returns bufferLength.
 *
 * @param startPos      Index to start scanning from.
 * @param bufferLength  Total length of the input buffer.
 * @param buffer        The input string buffer.
 * @param lexeme        Output buffer to store the extracted lexeme: mnemonic or argument (must be pre-allocated).
 * @param isString      The lexeme is inside the quotation marks in the input buffer.
 * @return              Index of the next character after the extracted word, or bufferLength if
 * done.
 */
unsigned int extractLexeme(
    const unsigned int startPos,
    const unsigned int bufferLength, const char* buffer,
    char* lexeme, bool* isString) {

    unsigned int pos = startPos;

    // Skip leading whitespace
    while (pos < bufferLength && isspace((unsigned char)buffer[pos])) {
        pos++;
    }

    if (pos >= bufferLength) {
        lexeme[0] = '\0';
        return bufferLength;
    }

    // Collect lexeme characters
    bool hasLeadingQuotationMark = false;
    bool hasEndingQuotationMark = false;
    int lexemeLength = 0;
    if (buffer[pos] == '"') { // Words in quotes
        hasLeadingQuotationMark = true;
        pos++; // Skip opening quotation mark
        while (pos < bufferLength) {
            if (buffer[pos] == '\\' && pos + 1 < bufferLength && buffer[pos + 1] == '"') {
                lexeme[lexemeLength++] = '"';
                pos += 2;
            } else if (buffer[pos] == '"') {
                hasEndingQuotationMark = true;
                pos++; // Closing quotation mark
                break;
            } else {
                lexeme[lexemeLength++] = buffer[pos++];
            }
        }
    } else { // Regular word
        while (pos < bufferLength && !isspace((unsigned char)buffer[pos])) {
            lexeme[lexemeLength++] = buffer[pos++];
        }
    }

    // Null-terminate output
    lexeme[lexemeLength] = '\0';

    *isString = hasLeadingQuotationMark && hasEndingQuotationMark;

    return pos;
}
