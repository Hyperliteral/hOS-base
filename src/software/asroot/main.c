#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: asroot <command> [args...]\n");
        return 1;
    }

    if (setuid(0) != 0) {
        perror("asroot: setuid failed");
        return 1;
    }
    if (setgid(0) != 0) {
        perror("asroot: setgid failed");
        return 1;
    }

    execvp(argv[1], &argv[1]);

    perror("asroot failed");
    return 1;
}