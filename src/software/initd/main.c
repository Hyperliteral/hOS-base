#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>

// /etc/modules is a plain newline-separated list of kernel modules to load at
// boot, e.g.:
//   hello
//   drm
// initd loads each of those from /boot/<name>.ko through the cavOS module-load
// syscall (see core/syscall/linux/syscalls_cavos.c).
#define SYS_CAVOS_MODULE_LOAD 410

static const char *MODULES_FILE = "/etc/modules";
static const char *BOOT_DIR = "/boot";

// The interactive shell, and the environment it starts from.
//
// "-l" matters: a login shell is what makes bash read /etc/profile, and on cavOS
// /etc/profile is the single place that carries both the real PATH and the
// coloured PS1. Without it neither /etc/profile nor /etc/profile.d is ever read,
// so the prompt comes out bare and the shell keeps whatever PATH it was handed.
// Starting it non-login with a bare "PATH=/bin" is what made everything outside
// /bin unreachable: clear and Xorg live in /usr/bin, adduser in /usr/sbin and apk
// in /sbin, so all four reported as "command not found" even though they were
// installed and sitting on the filesystem.
//
// The environment below is the floor, not the whole story - /etc/profile will
// overwrite PATH with the same value - but a program that initd execs later, or
// one that never sources /etc/profile, still needs a PATH that can actually
// reach the filesystem. HOME/USER/SHELL matter because tools consult them
// instead of calling getpwuid(), which is not something to make them do first.
//
// argv[0] is "bash" and not the path it is opened by. The kernel copies argv
// verbatim into /proc/<pid>/cmdline, and `ps` prints that as its COMMAND column,
// so the full path here is exactly why the shell was listed as "/bin/bash -l".
// A shell resolving a name off PATH behaves the same way: it passes along the
// word that was typed, not the path it turned up in. Keeping the path in argv[0]
// is also the option busybox handles worst, since it picks its applet by argv[0]
// and would go looking for one named "/usr/bin/adduser".
static char *shell_argv[] = { "bash", "-l", NULL };
static char *shell_envp[] = {
    "PATH=/bin:/usr/bin:/usr/local/bin:/usr/local/sbin:/usr/sbin:/sbin:/bin",
    "HOME=/root",
    "USER=root",
    "LOGNAME=root",
    "SHELL=/bin/bash",
    "TERM=linux",
    NULL,
};

// Runs in the child of fork(); execve only returns on failure.
static void execShell(void) {
    execve("/bin/bash", shell_argv, shell_envp);
    perror("[initd] Failed to execute /bin/bash");
    _exit(127);
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
    return s;
}

static void mountVirtualFilesystems(void) {
    // Mount pseudo-filesystems that were shifted out of the kernel
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) == 0) {
        printf("[initd] Mounted /proc successfully\n");
    } else {
        perror("[initd] Failed to mount /proc");
    }

    if (mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) == 0) {
        printf("[initd] Mounted /sys successfully\n");
    } else {
        perror("[initd] Failed to mount /sys");
    }
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

    printf("[initd] starting...\n");

    // Mount core virtual filesystems early in user-space
    mountVirtualFilesystems();

    // Load the modules requested in /etc/modules (enable with `initd-sys enable <mod>`)
    loadModules();

    // Spawn the initial shell
    pid_t shell_pid = fork();
    if (shell_pid < 0) {
        printf("[initd] Error: Initial fork failed!\n");
        _exit(1);
    }
    else if (shell_pid == 0) {
        execShell();
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
                execShell();
            }
        } else {
            printf("[initd] Reaped orphan process PID %d\n", (int)terminated);
        }
    }

    return 0;
}