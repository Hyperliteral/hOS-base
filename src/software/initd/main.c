#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/wait.h>

// /etc/modules is a plain newline-separated list of kernel modules to load at
// boot, e.g.:
//   hello
//   drm
// initd loads each of those from /boot/<name>.ko through the cavOS module-load
// syscall (see core/syscall/linux/syscalls_cavos.c).
#define SYS_CAVOS_MODULE_LOAD 410

static const char *MODULES_FILE = "/etc/modules";
static const char *BOOT_DIR = "/boot";

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
    return s;
}

static void loadModules(void) {
    FILE *f = fopen(MODULES_FILE, "r");
    if (!f) {
        printf("[initd] %s not found, no modules to load\n", MODULES_FILE);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *name = trim(line);
        if (name[0] == '\0' || name[0] == '#')
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s.ko", BOOT_DIR, name);

        long ret = syscall(SYS_CAVOS_MODULE_LOAD, path);
        if (ret == 0)
            printf("[initd] Loaded module %s\n", name);
        else
            printf("[initd] Failed to load %s (%s)\n", path, strerror(-(int)ret));
    }

    fclose(f);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("[initd] Starting init daemon (PID 1)...\n");

    // Load the modules requested in /etc/modules (enable with `initd-sys enable <mod>`)
    loadModules();

    // Spawn the initial shell
    pid_t shell_pid = fork();
    if (shell_pid < 0) {
        printf("[initd] Error: Initial fork failed!\n");
        _exit(1);
    }
    else if (shell_pid == 0) {
        char *exec_argv[] = { "/bin/bash", NULL };
        char *exec_envp[] = { "PATH=/bin", "TERM=linux", NULL };
        execve("/bin/bash", exec_argv, exec_envp);
        perror("[initd] Failed to execute /bin/bash");
        _exit(127);
    }

    while (1) {
        int status = 0;

        // Wait for ANY child process (-1) to exit (reaps orphans and bash alike)
        pid_t terminated = waitpid(-1, &status, 0);

        if (terminated < 0) {
            continue; // Interrupted or no children left
        }

        if (terminated == shell_pid) {
            printf("[initd] Bash process (PID %d) exited. Restarting...\n", (int)terminated);

            // Respawn bash
            shell_pid = fork();
            if (shell_pid < 0) {
                printf("[initd] Error: Failed to respawn bash!\n");
                continue;
            }
            else if (shell_pid == 0) {
                char *exec_argv[] = { "/bin/bash", NULL };
                char *exec_envp[] = { "PATH=/bin", "TERM=linux", NULL };
                execve("/bin/bash", exec_argv, exec_envp);
                perror("[initd] Failed to execute /bin/bash");
                _exit(127);
            }
        } else {
            printf("[initd] Reaped orphan process PID %d\n", (int)terminated);
        }
    }

    return 0;
}