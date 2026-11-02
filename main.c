#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "commands.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    // 1. illus --pull {Url}
    if (strcmp(argv[1], "--pull") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[Dreams] Error: --pull requires a repository URL.\n");
            fprintf(stderr, "Usage: illus --pull {Url}\n");
            return 1;
        }
        cmd_pull(argc, argv);
    }
    // 2. illus --cast [--store] [--fresh]
    else if (strcmp(argv[1], "--cast") == 0) {
        int store = 0;
        int fresh = 0;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--store") == 0) {
                store = 1;
            } else if (strcmp(argv[i], "--fresh") == 0) {
                fresh = 1;
            } else {
                fprintf(stderr, "[Dreams] Warning: Unknown flag for --cast: %s\n", argv[i]);
            }
        }

        cmd_cast(store, fresh);
    }
    else if (strcmp(argv[1], "--update") == 0) {
    cmd_update();
    }
    else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        cmd_version();
    }
    // 3. illus --shed [Package]
    else if (strcmp(argv[1], "--shed") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[Dreams] Error: --shed requires a package name.\n");
            fprintf(stderr, "Usage: illus --shed [Package]\n");
            return 1;
        }
        cmd_shed(argv[2]);
    }
    // 4. illus --list
    else if (strcmp(argv[1], "--list") == 0) {
        cmd_list();
    }
    else {
        fprintf(stderr, "[Dreams] Error: Unknown command '%s'\n", argv[1]);
        print_help(argv[0]);
        return 1;
    }

    return 0;
}