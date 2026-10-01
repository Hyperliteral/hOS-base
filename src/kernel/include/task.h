#ifndef TASK_H
#define TASK_H

#include "avl_tree.h"
#include "isr.h"
#include "linked_list.h"
#include "system.h"
#include "types.h"
#include "vfs.h"

// "Descriptor Privilege Level"
#define DPL_USER 3

#define KERNEL_TASK_ID 1

#define schedIdleCmdline ("schedIdle")

#define entryCmdline ("kernel")
#define lwipCmdline ("lwip")

typedef struct {
  uint64_t edi;
  uint64_t esi;
  uint64_t ebx;
  uint64_t ebp;
  uint64_t return_eip;
} TaskReturnContext;

typedef enum TASK_STATE {
  TASK_STATE_DEAD = 0,
  TASK_STATE_READY = 1,
  TASK_STATE_IDLE = 2,
  TASK_STATE_WAITING_INPUT = 3,
  TASK_STATE_CREATED = 4, // just made by taskCreate()
  TASK_STATE_WAITING_CHILD = 5,
  TASK_STATE_WAITING_CHILD_SPECIFIC = 6, // task->waitingForPid
  TASK_STATE_WAITING_VFORK = 7,
  TASK_STATE_BLOCKED = 8,
  TASK_STATE_SIGKILLED = 9,
  TASK_STATE_FUTEX = 10,
  TASK_STATE_DUMMY = 69,
} TASK_STATE;

typedef struct KilledInfo {
  LLheader _ll;

  uint64_t pid;
  uint16_t ret;
} KilledInfo;

typedef struct Task Task;

// task_info.c
typedef struct TaskInfoFs {
  Spinlock LOCK_FS;
  int          utilizedBy;

  char        *cwd;
  uint32_t umask;
} TaskInfoFs;

TaskInfoFs *taskInfoFsAllocate();
TaskInfoFs *taskInfoFsClone(TaskInfoFs *old);
void                taskInfoFsDiscard(TaskInfoFs *target);

typedef struct UserspaceMapping {
  void  *virt; // it is the key aswell
  size_t pages;
  bool   onDemand;
} UserspaceMapping;

typedef struct TaskInfoPagedir {
  Spinlock LOCK_PD;
  int          utilizedBy;

  uint64_t heap_start;
  uint64_t heap_end;

  uint64_t mmap_start;
  uint64_t mmap_end;

  AVLheader *mappings; // UserspaceMapping*

  uint64_t *pagedir;
} TaskInfoPagedir;

TaskInfoPagedir *taskInfoPdAllocate(bool pagedir);
TaskInfoPagedir *taskInfoPdClone(TaskInfoPagedir *old);
void                         taskInfoPdDiscard(TaskInfoPagedir *target);

typedef struct IntTimerInternal {
  uint64_t at;        // checked against timerTicks (ms)
  uint64_t reset; // reset value (ms)
} IntTimerInternal;

typedef struct TaskInfoSignal {
  Spinlock LOCK_SIGNAL;
  int          utilizedBy;

  IntTimerInternal itimerReal; // ITIMER_REAL
  struct sigaction signals[_NSIG + 1];
} TaskInfoSignal;

TaskInfoSignal *taskInfoSignalAllocate();
TaskInfoSignal *taskInfoSignalClone(TaskInfoSignal *old);
void                        taskInfoSignalDiscard(TaskInfoSignal *target);

typedef struct TaskInfoFiles {
  SpinlockCnt WLOCK_FILES;
  int                 utilizedBy;

  size_t rlimitFdsSoft;
  size_t rlimitFdsHard;

  uint8_t *fdBitmap;

  AVLheader *firstFile; // value of OpenFile*
} TaskInfoFiles;

TaskInfoFiles *taskInfoFilesAllocate();
void taskInfoFilesDiscard(TaskInfoFiles *target, void *task);

typedef struct TaskSysInterrupted {
  LLheader _ll;

  uint64_t number; // rax
} TaskSysInterrupted;

#define EXTRAS_DISABLE_FUTEX (1 << 0)
#define EXTRAS_INVOLUTARY_WAKEUP (1 << 1)

struct Task {
  uint64_t id;
  int          pgid;
  int          tgid;
  int          sid;
  int          priority; // lower numerically = higher priority (0 > *)
  bool         kernel_task;
  uint8_t  state;

  size_t   uid, gid;
  size_t   euid, egid;

  uint64_t waitingForPid; // wait4()

  AsmPassedInterrupt registers;
  uint64_t whileTssRsp;
  uint64_t whileSyscallRsp;

  // needed for syscalls
  uint64_t *pagedirOverride;

  bool systemCallInProgress;
  bool schedPageFault;

  AsmPassedInterrupt *syscallRegs;
  uint64_t                        syscallRsp;

  LLcontrol dsSysIntr; // struct TaskSysInterrupted

  // Useful to switch, for when TLS is available
  uint64_t fsbase;
  uint64_t gsbase;

  // PLEASE assert() it is zero after use and zero it manually if state changes
  size_t forcefulWakeupTimeUnsafe;

  termios  term;
  uint32_t tmpRecV;
  int          kernelErrno;
  int          ctrlPty;
  void        *spinlockQueueEntry; // check on kill!

  Semaphore lwipSem;

  char  *cmdline;
  size_t cmdlineLen;
  char  *execname; // elf executable name

  // Remember, this is per THREAD! It just gets cloned
  sigset_t sigBlockList;
  sigset_t sigPendingList;

  TaskInfoFs          *infoFs;
  TaskInfoPagedir *infoPd;
  TaskInfoFiles   *infoFiles;
  TaskInfoSignal  *infoSignals;

  __attribute__((aligned(16))) uint8_t fpuenv[512];
  uint32_t                                                         mxcsr;

  bool noInformParent;

  Spinlock  LOCK_CHILD_TERM;
  LLcontrol dsChildTerminated; // struct KilledInfo
  int           childrenTerminatedAmnt;
  int          *tidptr;

  uint64_t extras; // extra flags

#if CONFIG_MH_PORTS
  LLcontrol machPorts; // MachPortDesc (port rights)
#endif

  Task *parent;
  Task *next;
};

extern SpinlockCnt TASK_LL_MODIFY;

extern Task *firstTask;
extern Task *currentTask;

extern Task *dummyTask;

extern bool tasksInitiated;

// needed for libraries that still depend on some sort of errno
#define errno (currentTask->kernelErrno)

void taskSpinlockExit(Task *task, Spinlock *lock);

void initiateTasks();

Task *taskCreate(uint32_t id, uint64_t rip, bool kernel_task, uint64_t *pagedir,
                                 uint32_t argc, char **argv);
Task *taskCreateKernel(uint64_t rip, uint64_t rdi);
void  taskNameKernel(Task *target, const char *str, int len);
void  taskCreateFinish(Task *task);

void taskAdjustHeap(Task *task, size_t new_heap_end, size_t *start,
                                        size_t *end);

Task *taskGet(uint32_t id);
void  taskKill(uint32_t id, uint16_t ret);
void  taskFreeChildren(Task *task);

size_t taskChangeCwd(char *newdir);
Task  *taskFork(AsmPassedInterrupt *cpu, uint64_t rsp, int cloneFlags,
                                bool spinup);
void   taskFilesCopy(Task *original, Task *target, bool respectCOE);

Task *taskListAllocate();
void  taskListDestroy(Task *target);

uint64_t taskGenerateId();
void         taskCallReaper(Task *target);

#endif