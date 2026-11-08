#include <stdio.h>

#include "cli.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    return cli_main(argc, argv);
}
