#include "arch/sys_arch.h"
#include "lwipopts.h"

#include "lwip/arch.h"
#include "lwip/opt.h"
#include <lwip/debug.h>
#include <lwip/stats.h>
#include <lwip/sys.h>
#include <timer.h>
#include <linked_list.h>


void sys_init(void) {
  // maybe-todo? if needed.
}

void sys_mutex_lock(Spinlock *spinlock) { spinlockAcquire(spinlock); }
void sys_mutex_unlock(Spinlock *spinlock) { spinlockRelease(spinlock); }

err_t sys_mutex_new(Spinlock *spinlock) {
  memset(spinlock, 0, sizeof(Spinlock));
  return ERR_OK;
}

err_t sys_sem_new(sys_sem_t *sem, uint8_t cnt) {
  if (cnt != 0) {
    debugf("[lwip::glue::sem::new] cnt{%d}\n", cnt);
  }
  sem->invalid = false;
  sem->cnt = cnt;
  sys_mutex_new(&sem->LOCK);
  return ERR_OK;
}

err_t LWIP_NETCONN_THREAD_SEM_ALLOC() {
  sys_sem_new(&currentTask->lwipSem, 0);
  return ERR_OK;
}

err_t LWIP_NETCONN_THREAD_SEM_FREE() {
  sys_sem_new(&currentTask->lwipSem, 0);
  return ERR_OK;
}

sys_sem_t *LWIP_NETCONN_THREAD_SEM_GET() { return &currentTask->lwipSem; }

void sys_sem_signal(sys_sem_t *sem) { semaphorePost(sem); }

uint32_t sys_arch_sem_wait(sys_sem_t *sem, uint32_t timeout) {
  bool ret;
  if (timeout == 0) {
    ret = semaphoreWait(sem, 0); // Block indefinitely
  } else {
    // If your semaphoreWait takes a timeout in ms:
    ret = semaphoreWait(sem, timeout);
  }
  return ret ? 0 : SYS_ARCH_TIMEOUT;
}

void sys_sem_free(sys_sem_t *sem) { sys_sem_new(sem, 0); }

void sys_sem_set_invalid(sys_sem_t *sem) {
  if (sem->invalid) {
    debugf("[lwip::glue] Already invalid!\n");
  }
  sem->invalid = true;
}

int sys_sem_valid(sys_sem_t *sem) { return !sem->invalid; }

uint32_t sys_now(void) { return timerBootUnix * 1000 + timerTicks; }

sys_thread_t sys_thread_new(const char *pcName,
                            void (*pxThread)(void *pvParameters), void *pvArg,
                            int iStackSize, int iPriority) {
  Task *task = taskCreateKernel((uint64_t)pxThread, (uint64_t)pvArg);
  taskNameKernel(task, lwipCmdline, sizeof(lwipCmdline));
  return task->id;
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
  if (!size) {
    size = TCPIP_MBOX_SIZE;
  }
  memset(mbox, 0, sizeof(sys_mbox_t));
  mbox->invalid = false;
  mbox->size = size;
  mbox->msges = malloc(sizeof(mbox->msges[0]) * (size + 1));
  LinkedListInit(&mbox->firstBlock, sizeof(mboxBlock));
  sys_mutex_new(&mbox->LOCK);
  return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox) {
  spinlockAcquire(&mbox->LOCK);
  assert(mbox->ptrWrite == mbox->ptrRead);
  free(mbox->msges);
  spinlockRelease(&mbox->LOCK);
}

void sys_mbox_set_invalid(sys_mbox_t *mbox) {
  spinlockAcquire(&mbox->LOCK);
  mbox->invalid = true;
  spinlockRelease(&mbox->LOCK);
}

int sys_mbox_valid(sys_mbox_t *mbox) {
  spinlockAcquire(&mbox->LOCK);
  int ret = !mbox->invalid;
  spinlockRelease(&mbox->LOCK);
  return ret;
}

// Helper: Safely inserts message and wakes waiting tasks (Assumes mbox->LOCK is held)
static void sys_mbox_post_internal(sys_mbox_t *q, void *msg) {
  q->msges[q->ptrWrite] = msg;
  q->ptrWrite = (q->ptrWrite + 1) % q->size;

  mboxBlock *browse = (mboxBlock *)q->firstBlock.firstObject;
  while (browse) {
    mboxBlock *next = (mboxBlock *)(browse->_ll.next);
    if (browse->write == false) {
      browse->task->forcefulWakeupTimeUnsafe = 0;
      browse->task->state = TASK_STATE_READY;
      LinkedListRemove(&q->firstBlock, sizeof(mboxBlock), browse);
    }
    browse = next;
  }
}

void sys_mbox_post(sys_mbox_t *q, void *msg) {
  while (true) {
    spinlockAcquire(&q->LOCK);
    if ((q->ptrWrite + 1) % q->size != q->ptrRead) {
      break; // Space available
    }
    spinlockRelease(&q->LOCK);
    handControl();
  }

  sys_mbox_post_internal(q, msg);
  spinlockRelease(&q->LOCK);
}

err_t sys_mbox_trypost(sys_mbox_t *q, void *msg) {
  spinlockAcquire(&q->LOCK);
  if ((q->ptrWrite + 1) % q->size == q->ptrRead) {
    spinlockRelease(&q->LOCK);
    return ERR_MEM;
  }

  sys_mbox_post_internal(q, msg);
  spinlockRelease(&q->LOCK);
  return ERR_OK;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *q, void *msg) {
  // ISR context shouldn't acquire traditional spinlocks this way if they can sleep,
  // but if your kernel supports it, keep it. Otherwise, use lock-free queues.
  return sys_mbox_trypost(q, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *q, void **msg, u32_t timeout) {
  uint32_t timeStart = sys_now();

  while (true) {
    spinlockAcquire(&q->LOCK);
    
    // 1. Check if message is ready
    if (q->ptrRead != q->ptrWrite) {
      *msg = q->msges[q->ptrRead];
      q->ptrRead = (q->ptrRead + 1) % q->size;
      spinlockRelease(&q->LOCK);
      return 0;
    }

    // 2. Check Timeout using unified sys_now()
    if (timeout) {
      uint32_t elapsed = sys_now() - timeStart;
      if (elapsed >= timeout) {
        spinlockRelease(&q->LOCK);
        return SYS_ARCH_TIMEOUT;
      }
      currentTask->forcefulWakeupTimeUnsafe = timeStart + timeout;
    }

    // 3. Block current task
    mboxBlock *block = LinkedListAllocate(&q->firstBlock, sizeof(mboxBlock));
    block->task = currentTask;
    block->write = false;

    taskSpinlockExit(currentTask, &q->LOCK);
    currentTask->state = TASK_STATE_BLOCKED;
    handControl();

    assert(!currentTask->forcefulWakeupTimeUnsafe);
  }
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *q, void **msg) {
  spinlockAcquire(&q->LOCK);
  if (q->ptrRead == q->ptrWrite) {
    spinlockRelease(&q->LOCK);
    return SYS_MBOX_EMPTY;
  }

  *msg = q->msges[q->ptrRead];
  q->ptrRead = (q->ptrRead + 1) % q->size;
  spinlockRelease(&q->LOCK);

  return ERR_OK;
}

// Serial I/O stubs
void *sio_open(u8_t devnum) { return 0; }
u32_t sio_write(void *fd, const u8_t *data, u32_t len) { return 0; }
void sio_send(u8_t c, void *fd) {}
u8_t sio_recv(void *fd) { return 0; }
u32_t sio_read(void *fd, u8_t *data, u32_t len) { return 0; }
u32_t sio_tryread(void *fd, u8_t *data, u32_t len) { return 0; }