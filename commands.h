#ifndef COMMANDS_H
#define COMMANDS_H

int cmd_pull(int argc, char *argv[]);
int cmd_cast(int store, int fresh);
int cmd_update(void);
int cmd_version(void);
int cmd_shed(const char *package_name);
int cmd_list(void);
void print_help(const char *program_name);

#endif
