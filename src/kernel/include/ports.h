#ifndef PORTS_H
#define PORTS_H

// Mach-style port rights over the existing fd infrastructure
// Copyright (C) 2026

// General Mach kernel feature: when this is off the whole port/IPC subsystem
// (implementation, syscalls and the port-driven kernel services) is left out.
#if CONFIG_MH_PORTS

#include "avl_tree.h"
#include "isr.h"
#include "linked_list.h"
#include "spinlock.h"
#include "task.h"
#include "types.h"

#define MACH_PORT_RIGHT_RECEIVE (1 << 0)
#define MACH_PORT_RIGHT_SEND (1 << 1)
#define MACH_PORT_RIGHT_SEND_ONCE (1 << 2)

#define MACH_MSG_TYPE_COPY_SEND 19
#define MACH_MSG_TYPE_MAKE_SEND 20
#define MACH_MSG_TYPE_MOVE_SEND 17
#define MACH_MSG_TYPE_MOVE_RECEIVE 16
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21
#define MACH_MSG_TYPE_PORT_NAME 15

typedef uint32_t mach_port_name_t;

#define PORT_MSG_MAX 4096
#define PORT_MAX_DESCS 8

typedef struct PortDesc {
  mach_port_name_t name;
  uint32_t                 disposition;
} PortDesc;

typedef struct MachPort {
  LLheader _ll;

  uint64_t id;
  int          refs;

  bool dead;

  LLcontrol queue; // MachMessage
  Spinlock  LOCK_QUEUE;

  Task *owner; // receive right owner
} MachPort;

extern LLcontrol dsMachPorts;
extern uint64_t  machPortIdCurr;

typedef struct MachPortDesc {
  LLheader _ll;

  MachPort *port;
  uint32_t  rights; // MACH_PORT_RIGHT_*
} MachPortDesc;

typedef struct MachMsgPortDesc {
  LLheader _ll;

  MachPort *port;
  uint32_t  disposition; // MACH_MSG_TYPE_*
} MachMsgPortDesc;

typedef struct MachMessage {
  LLheader _ll;

  size_t size;
  char  *data;

  LLcontrol ports; // MachMsgPortDesc
} MachMessage;

bool portRightCheck(Task *task, mach_port_name_t name, uint32_t right);
void portsTaskInit(Task *task);
void portsTaskCleanup(Task *task);

// in-kernel API (services use these directly, not the syscall layer).
// NOTE: never swap currentTask around the syscall functions; a service must
// block with ITSELF running (currentTask == itself), so it calls
// syscallPortReceive() on a port it owns directly.
MachPort *portAllocateKernel(Task *task);
void          portPingKernel(MachPort *port);

size_t syscallPortAllocate();
size_t syscallPortSend(mach_port_name_t name, void *msg, size_t size,
                                           void *descs, size_t descCount);
size_t syscallPortReceive(mach_port_name_t name, void *msg, size_t *size,
                                                  void *descs, size_t *descCount, size_t timeout);
size_t syscallPortInsertRight(Task *targetTask, mach_port_name_t targetName,
                                                          mach_port_name_t name, uint32_t disposition);
size_t machPortHandoff(uint32_t targetPid, mach_port_name_t targetName,
                                           mach_port_name_t name, uint32_t disposition);

#define SYSCALL_PORT_ALLOCATE 400
#define SYSCALL_PORT_SEND 401
#define SYSCALL_PORT_RECEIVE 402
#define SYSCALL_PORT_INSERT_RIGHT 403

void syscallsRegPorts();

#endif /* CONFIG_MH_PORTS */

#endif
