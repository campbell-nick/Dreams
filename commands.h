#ifndef COMMANDS_H
#define COMMANDS_H

void cmd_pull(int argc, char *argv[]);
void cmd_cast(int store, int fresh);
void cmd_shed(const char *package_name);
void cmd_update(void);
void cmd_list();
void cmd_version();
void print_help(const char *program_name);

#endif