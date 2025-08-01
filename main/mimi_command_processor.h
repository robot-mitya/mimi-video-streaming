#ifndef MIMI_COMMAND_PROCESSOR_H
#define MIMI_COMMAND_PROCESSOR_H

/**
 * Params: command line, start position after the mnemonic in the command line.
 * Returns: status.
 */
typedef int (*CommandFunc)(char*, unsigned int);

CommandFunc getCommandHandler(const char* mnemonic);

#endif //MIMI_COMMAND_PROCESSOR_H
