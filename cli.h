#ifndef CLI_H
#define CLI_H

#include "commands.h"

#define CLI_MAX_POSITIONALS 64

typedef enum {
    CLI_CMD_NONE = 0,
    CLI_CMD_HELP,
    CLI_CMD_VERSION,
    CLI_CMD_INSTALL,
    CLI_CMD_REMOVE,
    CLI_CMD_LIST,
    CLI_CMD_INFO,
    CLI_CMD_SEARCH,
    CLI_CMD_UPDATE,
    CLI_CMD_PULL,
    CLI_CMD_CAST,
    CLI_CMD_DOCTOR
} cli_command;

typedef struct {
    cli_command command;
    illus_options opts;
    const char *args[CLI_MAX_POSITIONALS];
    int nargs;
    int want_help;
} cli_parsed;

int cli_main(int argc, char *argv[]);
void cli_print_help(const char *program, cli_command command);
const char *cli_command_name(cli_command command);

#endif
