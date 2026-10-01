#include <stdint.h>
#include "system.h"

void syscallTest(char *msg) {
  __asm__ volatile("int $0x80" ::"a"(SYSCALL_TEST), "b"(msg));
}

void syscallExitTask(int return_code) {
  __asm__ volatile("int $0x80" ::"a"(SYSCALL_EXIT_TASK), "b"(return_code));
}

uint32_t syscallFork(void) {
  uint32_t ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_FORK));
  return ret;
}

void syscallRead(int file, char *str, uint32_t count) {
  __asm__ volatile("int $0x80" ::"a"(SYSCALL_READ), "b"(file), "c"(str),
               "d"(count));
}

void syscallWrite(int file, char *str, uint32_t count) {
  __asm__ volatile("int $0x80" ::"a"(SYSCALL_WRITE), "b"(file), "c"(str),
               "d"(count));
}

uint32_t syscallGetPid(void) {
  uint32_t ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETPID));
  return ret;
}

int syscallGetArgc(void) {
  int ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETARGC));
  return ret;
}

char *syscallGetArgv(int curr) {
  char *ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GETARGV), "b"(curr));
  return ret;
}

uint32_t syscallGetHeapStart(void) {
  uint32_t ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GET_HEAP_START));
  return ret;
}

uint32_t syscallGetHeapEnd(void) {
  uint32_t ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_GET_HEAP_END));
  return ret;
}

void syscallAdjustHeapEnd(uint32_t heap_end) {
  __asm__ volatile("int $0x80" ::"a"(SYSCALL_ADJUST_HEAP_END), "b"(heap_end));
}