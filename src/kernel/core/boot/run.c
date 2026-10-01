#include <executable.h>
#include <shell.h>
#include <system.h>
#include <task.h>
#include <types.h>
#include <util.h>
#include <vfs.h>

// Kernel process launcher: bootstraps the first userspace program (initd)
// Copyright (C) 2024 Panagiotis

bool run(char *binary, bool wait, int _argc, char **_argv) {
  char *argv[] = {binary};
  Task *task = 0;
  if (_argc > 0)
    task = executableExecute(binary, _argc, _argv, 0, 0, false);
  else
    task = executableExecute(binary, 1, argv, 0, 0, false);
  if (!task)
    return false;

  task->parent = currentTask;
  int stdin = fsUserOpen(task, "/dev/stdin", O_RDWR | O_APPEND, 0);
  int stdout = fsUserOpen(task, "/dev/stdout", O_RDWR | O_APPEND, 0);
  int stderr = fsUserOpen(task, "/dev/stderr", O_RDWR | O_APPEND, 0);

  if (stdin < 0 || stdout < 0 || stderr < 0) {
    debugf("[elf] Couldn't establish basic IO!\n");
    panic();
  }

  OpenFile *fdStdin = fsUserGetNode(task, stdin);
  OpenFile *fdStdout = fsUserGetNode(task, stdout);
  OpenFile *fdStderr = fsUserGetNode(task, stderr);

  fdStdin->id = 0;
  fdStdout->id = 1;
  fdStderr->id = 2;

  taskCreateFinish(task);

  if (task && wait) {
    currentTask->waitingForPid = task->id;
    currentTask->state = TASK_STATE_WAITING_CHILD_SPECIFIC;
    handControl();
  }

  return true;
}