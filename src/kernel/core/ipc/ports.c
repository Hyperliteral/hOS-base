// Mach-style port rights implementation
// Copyright (C) 2026

#if CONFIG_MH_PORTS

#include <linked_list.h>
#include <linux.h>
#include <malloc.h>
#include <poll.h>
#include <ports.h>
#include <schedule.h>
#include <string.h>
#include <syscalls.h>
#include <task.h>
#include <timer.h>
#include <util.h>

LLcontrol dsMachPorts;
uint64_t  machPortIdCurr = 1;

static void portRef(MachPort *port) { port->refs++; }

static void portDeref(MachPort *port) {
  if (--port->refs > 0)
    return;

  MachMessage *msg = (MachMessage *)port->queue.firstObject;
  while (msg) {
    MachMessage *next = (MachMessage *)msg->_ll.next;
    if (msg->data)
      free(msg->data);
    free(msg);
    msg = next;
  }
  free(port);
}

void portsTaskInit(Task *task) {
  if (!task->machPorts.firstObject && task->machPorts.structSize == 0)
    LinkedListInit(&task->machPorts, sizeof(MachPortDesc));
}

void portsTaskCleanup(Task *task) {
  MachPortDesc *desc = (MachPortDesc *)task->machPorts.firstObject;
  while (desc) {
    MachPortDesc *next = (MachPortDesc *)desc->_ll.next;
    MachPort     *port = desc->port;

    spinlockAcquire(&port->LOCK_QUEUE);
    if (port->owner == task) {
      port->owner = 0;
      port->dead = true;
    } else
      portDeref(port);
    spinlockRelease(&port->LOCK_QUEUE);

    free(desc);
    desc = next;
  }
  task->machPorts.firstObject = 0;
}

static MachPortDesc *portDescFind(Task *task, mach_port_name_t name) {
  MachPortDesc *desc = (MachPortDesc *)task->machPorts.firstObject;
  while (desc) {
    if (desc->port->id == name)
      return desc;
    desc = (MachPortDesc *)desc->_ll.next;
  }
  return 0;
}

bool portRightCheck(Task *task, mach_port_name_t name, uint32_t right) {
  MachPortDesc *desc = portDescFind(task, name);
  if (!desc)
    return false;
  return (desc->rights & right) && !desc->port->dead;
}

// returns port with LOCK_QUEUE held, ref'd (0 on failure)
static MachPort *portLockByRight(Task *task, mach_port_name_t name,
                                 uint32_t right) {
  MachPortDesc *desc = portDescFind(task, name);
  if (!desc || !(desc->rights & right) || desc->port->dead)
    return 0;
  MachPort *port = desc->port;
  portRef(port);
  spinlockAcquire(&port->LOCK_QUEUE);
  if (port->dead) {
    spinlockRelease(&port->LOCK_QUEUE);
    portDeref(port);
    return 0;
  }
  return port;
}

static uint64_t portIdGenerate() {
  static Spinlock LOCK_PORT_ID = {0};
  spinlockAcquire(&LOCK_PORT_ID);
  uint64_t id = machPortIdCurr++;
  spinlockRelease(&LOCK_PORT_ID);
  return id;
}

static MachPort *portAllocateInternal(Task *task) {
  MachPort *port = calloc(sizeof(MachPort), 1);
  port->id = portIdGenerate();
  port->refs = 1;
  port->owner = task;
  LinkedListInit(&port->queue, sizeof(MachMessage));

  MachPortDesc *desc =
      LinkedListAllocate(&task->machPorts, sizeof(MachPortDesc));
  desc->port = port;
  desc->rights = MACH_PORT_RIGHT_RECEIVE;
  return port;
}

static MachMessage *messageAllocate(void *msg, size_t size) {
  MachMessage *m = calloc(sizeof(MachMessage), 1);
  m->size = size;
  m->data = malloc(size);
  memcpy(m->data, msg, size);
  LinkedListInit(&m->ports, sizeof(MachMsgPortDesc));
  return m;
}

size_t syscallPortAllocate() {
  portsTaskInit(currentTask);
  MachPort *port = portAllocateInternal(currentTask);
  spinlockRelease(&port->LOCK_QUEUE);
  return port->id;
}

// ---- in-kernel API: services call these directly ----

MachPort *portAllocateKernel(Task *task) {
  portsTaskInit(task);
  MachPort *port = portAllocateInternal(task);
  spinlockRelease(&port->LOCK_QUEUE);
  return port;
}

// wakes the receive-right owner if it's blocked on this port
static void portWakeReceiver(MachPort *port) {
  Task *receiver = port->owner;
  if (receiver && receiver->state == TASK_STATE_BLOCKED)
    receiver->state = TASK_STATE_READY;
}

// ISR-safe fire-and-forget ping: kernel is trusted, no rights needed
void portPingKernel(MachPort *port) {
  if (!port)
    return;

  spinlockAcquire(&port->LOCK_QUEUE);
  if (port->dead) {
    spinlockRelease(&port->LOCK_QUEUE);
    return;
  }

  MachMessage *m = messageAllocate(&(char){1}, 1);
  portWakeReceiver(port);
  LinkedListPushFrontUnsafe(&port->queue, m);

  spinlockRelease(&port->LOCK_QUEUE);
  pollInstanceRing((size_t)port, EPOLLIN);
}

// consuming dispositions remove the local right, non-consuming don't
static void messageStealDescriptors(MachMessage *m, void *descs,
                                    size_t descCount) {
  for (size_t i = 0; i < descCount; i++) {
    PortDesc     *d = &((PortDesc *)descs)[i];
    MachPortDesc *srcDesc = portDescFind(currentTask, d->name);
    if (!srcDesc)
      continue;

    MachMsgPortDesc *md =
        LinkedListAllocate(&m->ports, sizeof(MachMsgPortDesc));
    md->port = srcDesc->port;
    md->disposition = d->disposition;
    portRef(srcDesc->port);

    if (d->disposition == MACH_MSG_TYPE_MOVE_SEND ||
        d->disposition == MACH_MSG_TYPE_MOVE_RECEIVE)
      LinkedListRemove(&currentTask->machPorts, sizeof(MachPortDesc), srcDesc);
  }
}

size_t syscallPortSend(mach_port_name_t name, void *msg, size_t size,
                       void *descs, size_t descCount) {
  if (size > PORT_MSG_MAX)
    return ERR(E2BIG);
  if (!portRightCheck(currentTask, name, MACH_PORT_RIGHT_SEND) &&
      !portRightCheck(currentTask, name, MACH_PORT_RIGHT_SEND_ONCE))
    return ERR(EPERM);

  MachPort *port = portLockByRight(currentTask, name, MACH_PORT_RIGHT_SEND);
  if (!port)
    port = portLockByRight(currentTask, name, MACH_PORT_RIGHT_SEND_ONCE);
  if (!port)
    return ERR(EINVAL);

  MachMessage *m = messageAllocate(msg, size);
  if (descs)
    messageStealDescriptors(m, descs, descCount);

  // send-once rights are consumed after a single delivery
  MachPortDesc *desc = portDescFind(currentTask, name);
  if (desc && (desc->rights & MACH_PORT_RIGHT_SEND_ONCE) &&
      !(desc->rights & MACH_PORT_RIGHT_SEND))
    LinkedListRemove(&currentTask->machPorts, sizeof(MachPortDesc), desc);

  // wake a blocked receiver, if any
  portWakeReceiver(port);

  LinkedListPushFrontUnsafe(&port->queue, m);
  spinlockRelease(&port->LOCK_QUEUE);
  pollInstanceRing((size_t)port, EPOLLIN);
  return 0;
}

size_t syscallPortReceive(mach_port_name_t name, void *msg, size_t *size,
                          void *descs, size_t *descCount, size_t timeout) {
  portsTaskInit(currentTask);
  MachPortDesc *desc = portDescFind(currentTask, name);
  if (!desc || !(desc->rights & MACH_PORT_RIGHT_RECEIVE))
    return ERR(EPERM);

  MachPort *port = desc->port;
  spinlockAcquire(&port->LOCK_QUEUE);
  if (port->dead) {
    spinlockRelease(&port->LOCK_QUEUE);
    return ERR(EPERM);
  }

  while (!port->queue.firstObject) {
    if (timeout && timerTicks >= timeout) {
      spinlockRelease(&port->LOCK_QUEUE);
      return ERR(EAGAIN);
    }
    if (signalsPendingQuick(currentTask)) {
      spinlockRelease(&port->LOCK_QUEUE);
      return ERR(EINTR);
    }
    taskSpinlockExit(currentTask, &port->LOCK_QUEUE);
    currentTask->state = TASK_STATE_BLOCKED;
    handControl();

    spinlockAcquire(&port->LOCK_QUEUE);
  }

  MachMessage *m = (MachMessage *)port->queue.firstObject;
  LinkedListUnregister(&port->queue, sizeof(MachMessage), m);
  spinlockRelease(&port->LOCK_QUEUE);

  // copy out
  if (*size < m->size) {
    free(m->data);
    free(m);
    return ERR(EMSGSIZE);
  }
  *size = m->size;
  memcpy(msg, m->data, m->size);

  // deliver port rights to the receiver
  size_t           count = 0;
  MachMsgPortDesc *md = (MachMsgPortDesc *)m->ports.firstObject;
  while (md && count < PORT_MAX_DESCS) {
    MachMsgPortDesc *next = (MachMsgPortDesc *)md->_ll.next;

    MachPortDesc *rdesc =
        LinkedListAllocate(&currentTask->machPorts, sizeof(MachPortDesc));
    rdesc->port = md->port;

    switch (md->disposition) {
    case MACH_MSG_TYPE_MOVE_RECEIVE:
      rdesc->rights = MACH_PORT_RIGHT_RECEIVE;
      break;
    case MACH_MSG_TYPE_COPY_SEND:
    case MACH_MSG_TYPE_MAKE_SEND:
      rdesc->rights = MACH_PORT_RIGHT_SEND;
      break;
    case MACH_MSG_TYPE_MAKE_SEND_ONCE:
      rdesc->rights = MACH_PORT_RIGHT_SEND_ONCE;
      break;
    default:
      rdesc->rights = MACH_PORT_RIGHT_SEND;
      break;
    }

    PortDesc *out = &((PortDesc *)descs)[count];
    out->name = md->port->id;
    out->disposition = md->disposition;
    count++;

    free(md);
    md = next;
  }
  *descCount = count;

  free(m->data);
  free(m);
  return 0;
}

// XNU-style handoff: copies a right directly into another task's port namespace
size_t machPortHandoff(uint32_t targetPid, mach_port_name_t targetName,
                       mach_port_name_t name, uint32_t disposition) {
  Task *target = taskGet(targetPid);
  if (!target || target->state == TASK_STATE_DEAD)
    return ERR(ESRCH);

  MachPortDesc *srcDesc = portDescFind(currentTask, name);
  if (!srcDesc)
    return ERR(EPERM);

  portsTaskInit(target);
  MachPortDesc *dstDesc = portDescFind(target, targetName);

  spinlockAcquire(&srcDesc->port->LOCK_QUEUE);
  bool dead = srcDesc->port->dead;
  spinlockRelease(&srcDesc->port->LOCK_QUEUE);
  if (dead)
    return ERR(EPERM);

  if (!dstDesc) {
    // fresh slot in the target
    dstDesc = LinkedListAllocate(&target->machPorts, sizeof(MachPortDesc));
    dstDesc->port = srcDesc->port;
    dstDesc->rights = 0;
    portRef(srcDesc->port);
  }

  switch (disposition) {
  case MACH_MSG_TYPE_COPY_SEND:
    dstDesc->rights |= MACH_PORT_RIGHT_SEND;
    break;
  case MACH_MSG_TYPE_MAKE_SEND_ONCE:
    dstDesc->rights |= MACH_PORT_RIGHT_SEND_ONCE;
    break;
  case MACH_MSG_TYPE_MOVE_RECEIVE:
    dstDesc->rights |= MACH_PORT_RIGHT_RECEIVE;
    if (srcDesc->rights & MACH_PORT_RIGHT_RECEIVE) {
      spinlockAcquire(&srcDesc->port->LOCK_QUEUE);
      srcDesc->port->owner = target;
      spinlockRelease(&srcDesc->port->LOCK_QUEUE);
      LinkedListRemove(&currentTask->machPorts, sizeof(MachPortDesc), srcDesc);
    }
    break;
  default:
    dstDesc->rights |= MACH_PORT_RIGHT_SEND;
    break;
  }

  if (disposition == MACH_MSG_TYPE_MOVE_SEND) {
    dstDesc->rights |= MACH_PORT_RIGHT_SEND;
    LinkedListRemove(&currentTask->machPorts, sizeof(MachPortDesc), srcDesc);
  }

  return 0;
}

size_t syscallPortInsertRight(Task *targetTask, mach_port_name_t targetName,
                              mach_port_name_t name, uint32_t disposition) {
  if (targetTask != currentTask)
    return ERR(EPERM);
  return machPortHandoff(targetTask->id, targetName, name, disposition);
}

void syscallsRegPorts() {
  LinkedListInit(&dsMachPorts, sizeof(MachPort));
  registerSyscall(SYSCALL_PORT_ALLOCATE, syscallPortAllocate);
  registerSyscall(SYSCALL_PORT_SEND, syscallPortSend);
  registerSyscall(SYSCALL_PORT_RECEIVE, syscallPortReceive);
  registerSyscall(SYSCALL_PORT_INSERT_RIGHT, syscallPortInsertRight);
}

#endif /* CONFIG_MH_PORTS */
