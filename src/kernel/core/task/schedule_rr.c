#if CONFIG_SCHED_ROUNDROBIN

#include <bootloader.h>
#include <gdt.h>
#include <isr.h>
#include <malloc.h>
#include <paging.h>
#include <schedule.h>
#include <syscalls.h>
#include <system.h>
#include <task.h>
#include <timer.h>
#include <util.h>
#include <vmm.h>
#include <limits.h> // For INT_MAX

// Round Robin (picks the highest priority level, then rotates within it)

#define SCHEDULE_DEBUG 0

extern TSSPtr *tssPtr;

void schedule(uint64_t rsp) {
  if (!tasksInitiated || !firstTask)
    return;

  AsmPassedInterrupt *cpu = (AsmPassedInterrupt *)rsp;

  Task *curr = firstTask;
  Task *start = curr;
  int highestPriority = INT_MAX;
  int foundReady = 0;
  
  do {
    if (curr->state != TASK_STATE_READY) {
      if (signalsRevivableState(curr->state) && signalsPendingQuick(curr)) {
        assert(curr->registers.cs & GDT_KERNEL_CODE);
        curr->extras |= EXTRAS_INVOLUTARY_WAKEUP;
        curr->forcefulWakeupTimeUnsafe = 0;
        curr->state = TASK_STATE_READY;
      } else if (curr->forcefulWakeupTimeUnsafe &&
                 curr->forcefulWakeupTimeUnsafe <= timerTicks) {
        curr->state = TASK_STATE_READY;
        curr->extras |= EXTRAS_INVOLUTARY_WAKEUP;
        curr->forcefulWakeupTimeUnsafe = 0;
      }
    }

    if (curr->state == TASK_STATE_READY) {
      if (!foundReady || curr->priority < highestPriority) {
        highestPriority = curr->priority;
        foundReady = 1;
      }
    }

    curr = curr->next;
    if (!curr)
      curr = firstTask;
  } while (curr != start);

  Task *bestTask = 0;
  if (foundReady) {
    Task *nextCandidate = currentTask ? currentTask->next : firstTask;
    if (!nextCandidate)
      nextCandidate = firstTask;
      
    Task *startCandidate = nextCandidate;
    
    do {
      if (nextCandidate->state == TASK_STATE_READY && nextCandidate->priority == highestPriority) {
        bestTask = nextCandidate;
        break;
      }
      nextCandidate = nextCandidate->next;
      if (!nextCandidate)
        nextCandidate = firstTask;
    } while (nextCandidate != startCandidate);

    if (!bestTask) {
      curr = firstTask;
      do {
        if (curr->state == TASK_STATE_READY && curr->priority == highestPriority) {
          bestTask = curr;
          break;
        }
        curr = curr->next;
        if (!curr) curr = firstTask;
      } while (curr != firstTask);
    }
  }

  //falback
  Task *next = bestTask ? bestTask : dummyTask;
  Task *old = currentTask;
  currentTask = next;

  if (old->state != TASK_STATE_READY && old->spinlockQueueEntry) {
    spinlockRelease(old->spinlockQueueEntry);
    old->spinlockQueueEntry = 0;
  }

  if (!next->kernel_task) {
    uint64_t rtAt = atomicRead64(&next->infoSignals->itimerReal.at);
    uint64_t rtReset = atomicRead64(&next->infoSignals->itimerReal.reset);
    if (rtAt && rtAt <= timerTicks) {
      atomicBitmapSet(&next->sigPendingList, SIGALRM);
      if (!rtReset)
        atomicWrite64(&next->infoSignals->itimerReal.at, 0);
      else
        atomicWrite64(&next->infoSignals->itimerReal.at, timerTicks + rtReset);
    }
  }

#if SCHEDULE_DEBUG
  debugf("[scheduler] Switching context: id{%d} (prio: %d) -> id{%d} (prio: %d)\n", 
         old->id, old->priority, next->id, next->priority);
  debugf("cpu->usermode_rsp{%lx} rip{%lx} fsbase{%lx} gsbase{%lx}\n",
         next->registers.usermode_rsp, next->registers.rip, old->fsbase,
         old->gsbase);
#endif

  while (!next->kernel_task && !(next->registers.cs & GDT_KERNEL_CODE)) {
    signalsPendingHandleSched(next);
    if (next->state == TASK_STATE_SIGKILLED) {
      // fallback
      currentTask = old;
      next = next->next;
      if (!next)
        next = firstTask;
      
      if (next == old || next->state == TASK_STATE_READY)
        break;
    } else {
      break;
    }
  }

  tssPtr->rsp0 = next->whileTssRsp;
  threadInfo.syscall_stack = next->whileSyscallRsp;

  wrmsr(MSRID_FSBASE, next->fsbase);
  wrmsr(MSRID_GSBASE, next->gsbase);
  wrmsr(MSRID_KERNEL_GSBASE, (size_t)&threadInfo);

  memcpy(&old->registers, cpu, sizeof(AsmPassedInterrupt));

  if (!old->kernel_task) {
    asm volatile(" fxsave %0 " ::"m"(old->fpuenv));
    asm("stmxcsr (%%rax)" : : "a"(&old->mxcsr));
  }

  if (!next->kernel_task) {
    asm volatile(" fxrstor %0 " ::"m"(next->fpuenv));
    asm("ldmxcsr (%%rax)" : : "a"(&next->mxcsr));
  }

  AsmPassedInterrupt *iretqRsp =
      (AsmPassedInterrupt *)(next->whileTssRsp - sizeof(AsmPassedInterrupt));
  memcpy(iretqRsp, &next->registers, sizeof(AsmPassedInterrupt));

  uint64_t *pagedir =
      next->pagedirOverride ? next->pagedirOverride : next->infoPd->pagedir;
  ChangePageDirectoryFake(pagedir);
  
  asm_finalize((size_t)iretqRsp, VirtualToPhysical((size_t)pagedir));
}

#endif