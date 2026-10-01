#include <console.h>
#include <dev.h>
#include <kernel_helper.h>
#include <nic_controller.h>
#include <paging.h>
#include <poll.h>
#include <ports.h>
#include <system.h>
#include <task.h>
#include <types.h>
#include <util.h>
#include <vmm.h>

// Kernel helper thread entry...
// It will help with cleaning up processes, managing networking, etc while
// adhering to spinlocks (in contrast with interrupts)
// Copyright (C) 2024 Panagiotis

Task *netHelperTask = 0;

// port-driven service ports (created at init, named port 1..n per service)
MachPort *portNetd = 0;
MachPort *portReaper = 0;

void helperNet() {
  while (true) {
    if (netQueueRead == netQueueWrite) {
      // empty :p
      return;
    }

    handlePacket(netQueue[netQueueRead].nic, netQueue[netQueueRead].buff,
                 netQueue[netQueueRead].packetLength);
    netQueueRead = (netQueueRead + 1) % QUEUE_MAX;
  }
}

void helperReaper() {
  spinlockAcquire(&LOCK_REAPER);
  if (reaperTask) {
    if (reaperTask->state == TASK_STATE_SIGKILLED)
      taskKill(reaperTask->id, 128 + reaperTask->tmpRecV);
    if (reaperTask->state != TASK_STATE_DEAD)
      goto end;

    // we have a task to kill!
    taskFreeChildren(reaperTask); // free the children

    // free stacks
    size_t stackSize = USER_STACK_PAGES * BLOCK_SIZE;
    void  *tssRsp = (void *)(reaperTask->whileTssRsp - stackSize);
    void  *syscallRsp = (void *)(reaperTask->whileSyscallRsp - stackSize);
    VirtualFree(tssRsp, USER_STACK_PAGES);
    VirtualFree(syscallRsp, USER_STACK_PAGES);

    // free info splits
    taskInfoFsDiscard(reaperTask->infoFs);

    // delayed port cleanup (receive-right destruction has to happen when
    // nobody can race the task's namespace anymore)
    portsTaskCleanup(reaperTask);

    // interrupted syscalls
    TaskSysInterrupted *intrBrowse =
        (TaskSysInterrupted *)reaperTask->dsSysIntr.firstObject;
    while (intrBrowse) {
      TaskSysInterrupted *next = (TaskSysInterrupted *)intrBrowse->_ll.next;
      free(next); // quick n dirty
      intrBrowse = next;
    }

    // free the task now that it's safe
    taskListDestroy(reaperTask);

    // todo: free() the task in a safe manner. also implement some system for
    // editing the global LL without race conditions
    // (*) depends: task blocking, watchlists, task adding, forking, etc.

    // will need a good system for that so maybe delay it to when multithreading
    // is implemented!

    // done! continue listening
    reaperTask = 0;
  }

end:
  spinlockRelease(&LOCK_REAPER);
}

// todo: find cleaner alternatives to this and to the kernel helper as a whole
void helperVolatilePoll() {
  if (!consoleDisabled) // kernel io
    pollInstanceRing(69, EPOLLIN);
  for (int i = 0; i < lastInputEvent; i++) {
    if (atomicRead64(&devInputEvents[i].deviceEvents.readPtr) !=
        atomicRead64(&devInputEvents[i].deviceEvents.writePtr))
      pollInstanceRing((size_t)&devInputEvents[i], EPOLLIN);
  }
}

// ---- port-driven kernel services (true kernel mode tasks) ----
// Each service task owns its port (receive right) and blocks on it via the
// plain syscallPortReceive(); a producer pings with portPingKernel(). The
// task that runs a service is always currentTask itself, so no task-guessing
// is needed.

// networkd: drains the ISR packet ring whenever a NIC posts "packet ready"
void serviceNetd() {
  while (!portNetd)
    handControl(); // if we got scheduled before initiateKernelThreads set up

  while (true) {
    char   msgBuf[16];
    size_t size = sizeof(msgBuf);
    size_t descCount = 0;
    size_t ret = syscallPortReceive(portNetd->id, msgBuf, &size, 0, &descCount,
                                    0);
    if (ret != 0)
      continue; // spurious wake or dead port; go back to blocking

    helperNet();
  }
}

// reapd: blocks on its port until taskKill's reaper handoff pings it
void serviceReaper() {
  while (!portReaper)
    handControl();

  while (true) {
    char   msgBuf[16];
    size_t size = sizeof(msgBuf);
    size_t descCount = 0;
    size_t ret = syscallPortReceive(portReaper->id, msgBuf, &size, 0,
                                    &descCount, 0);
    if (ret != 0)
      continue;

    helperReaper();
  }
}

// inputd: legacy poll behavior, separate task so netd/reaper aren't affected
void serviceInputPoll() {
  while (true) {
    helperVolatilePoll();
    handControl();
  }
}

void initiateKernelThreads() {
  // create each service and hand it its port before moving on, so preemption
  // can never catch a service without a valid port
  netHelperTask = taskCreateKernel((size_t)serviceNetd, 0);
  taskNameKernel(netHelperTask, "netd", 5);
  portNetd = portAllocateKernel(netHelperTask);

  Task *reaperTaskSrv = taskCreateKernel((size_t)serviceReaper, 0);
  taskNameKernel(reaperTaskSrv, "reaperd", 8);
  portReaper = portAllocateKernel(reaperTaskSrv);

  Task *inputdTask = taskCreateKernel((size_t)serviceInputPoll, 0);
  taskNameKernel(inputdTask, "inputd", 7);
}
