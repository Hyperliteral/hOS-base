#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// initd-sys: manage the /etc/modules boot-module list, in the spirit of
// BSD's sysrc/rcctl. `initd-sys enable drm` adds `drm` to the list (a
// plain newline-separated file); initd then loads /boot/drm.ko at boot.

static const char *MODULES_FILE = "/etc/modules";

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
    return s;
}

static int isBlank(const char *s) {
    return s[0] == '\0' || s[0] == '#';
}

static int countLines(void) {
    FILE *f = fopen(MODULES_FILE, "r");
    if (!f)
        return 0;
    int   n = 0;
    char  buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        char *line = trim(buf);
        if (!isBlank(line))
            n++;
    }
    fclose(f);
    return n;
}

static char **readLines(int *count) {
    int n = countLines();
    char **lines = calloc((size_t)n + 1, sizeof(char *));
    if (!lines)
        return NULL;

    FILE *f = fopen(MODULES_FILE, "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            char *line = trim(buf);
            if (isBlank(line))
                continue;
            char *copy = strdup(line);
            if (copy)
                lines[(*count)++] = copy;
        }
        fclose(f);
    }
    return lines;
}

static void writeLines(char **lines, int count) {
    FILE *f = fopen(MODULES_FILE, "w");
    if (!f) {
        fprintf(stderr, "initd-sys: cannot open %s for writing\n",
                MODULES_FILE);
        return;
    }
    for (int i = 0; i < count; i++)
        fprintf(f, "%s\n", lines[i]);
    fclose(f);
}

static int cmdEnable(const char *module) {
    int     count = 0;
    char  **lines = readLines(&count);

    for (int i = 0; i < count; i++) {
        if (strcmp(lines[i], module) == 0) {
            printf("%s is already enabled\n", module);
            for (int i = 0; i < count; i++)
                free(lines[i]);
            free(lines);
            return 0;
        }
    }

    char **newLines = realloc(lines, ((size_t)count + 1) * sizeof(char *));
    if (!newLines) {
        for (int i = 0; i < count; i++)
            free(lines[i]);
        free(lines);
        return 1;
    }
    newLines[count] = strdup(module);
    writeLines(newLines, count + 1);
    printf("%s enabled (load at boot: /boot/%s.ko)\n", module, module);

    for (int i = 0; i < count + 1; i++)
        free(newLines[i]);
    free(newLines);
    return 0;
}

static int cmdDisable(const char *module) {
    int    count = 0;
    char **lines = readLines(&count);
    int    removed = 0;

    for (int i = 0; i < count; i++) {
        if (strcmp(lines[i], module) == 0) {
            free(lines[i]);
            lines[i] = NULL;
            removed = 1;
        }
    }

    if (!removed) {
        printf("%s is not enabled\n", module);
        for (int i = 0; i < count; i++)
            free(lines[i]);
        free(lines);
        return 0;
    }

    int n = 0;
    for (int i = 0; i < count; i++) {
        if (lines[i])
            lines[n++] = lines[i];
    }
    writeLines(lines, n);
    printf("%s disabled\n", module);

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
    return 0;
}

static int cmdList(void) {
    int    count = 0;
    char **lines = readLines(&count);
    if (!count) {
        printf("No modules enabled\n");
        free(lines);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        printf("%s\n", lines[i]);
        free(lines[i]);
    }
    free(lines);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s enable|disable <module> | list\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "enable") == 0 && argc == 3)
        return cmdEnable(argv[2]);
    if (strcmp(argv[1], "disable") == 0 && argc == 3)
        return cmdDisable(argv[2]);
    if (strcmp(argv[1], "list") == 0 && argc == 2)
        return cmdList();

    usage(argv[0]);
    return 1;
}