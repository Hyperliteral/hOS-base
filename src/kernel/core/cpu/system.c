#include <bootloader.h>
#include <console.h>
#include <fb.h>
#include <system.h>

// Source code for handling ports via assembly references
// Copyright (C) 2024 Panagiotis

// External reference to your global framebuffer struct defined in fb.c
extern Framebuffer fb;

void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
  __asm__ __volatile__("cpuid \n"
               : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
               : "a"(*eax), "c"(*ecx)
               : "memory");
}

uint8_t inportb(uint16_t _port) {
  uint8_t rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
}

void outportb(uint16_t _port, uint8_t _data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
}

uint16_t inportw(uint16_t port) {
  uint16_t result;
  __asm__("in %%dx, %%ax" : "=a"(result) : "d"(port));
  return result;
}

void outportw(unsigned short port, unsigned short data) {
  __asm__("out %%ax, %%dx" : : "a"(data), "d"(port));
}

uint32_t inportl(uint16_t portid) {
  uint32_t ret;
  __asm__ __volatile__("inl %%dx, %%eax" : "=a"(ret) : "d"(portid));
  return ret;
}

void outportl(uint16_t portid, uint32_t value) {
  __asm__ __volatile__("outl %%eax, %%dx" : : "d"(portid), "a"(value));
}

uint64_t rdmsr(uint32_t msrid) {
  uint32_t low;
  uint32_t high;
  __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msrid));
  return (uint64_t)low << 0 | (uint64_t)high << 32;
}

uint64_t wrmsr(uint32_t msrid, uint64_t value) {
  uint32_t low = value >> 0 & 0xFFFFFFFF;
  uint32_t high = value >> 32 & 0xFFFFFFFF;
  __asm__ __volatile__("wrmsr" : : "a"(low), "d"(high), "c"(msrid) : "memory");
  return value;
}

bool checkSSE() {
  uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;
  cpuid(&eax, &ebx, &ecx, &edx);
  return (edx >> 25) & 1;
}

bool checkFXSR() {
  uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;
  cpuid(&eax, &ebx, &ecx, &edx);
  return (edx >> 24) & 1;
}

extern void asm_enable_SSE();
void        initiateSSE() {
  if (!checkSSE()) {
    debugf("[sse] FATAL! No support for Streaming Simd Extensions found!\n");
    panic();
  }

  if (!checkFXSR()) {
    debugf("[sse] FATAL! No support for FXSR found!\n");
    panic();
  }

  // asm_enable_SSE();
  // enable SSE
  __asm__ __volatile__("mov %%cr0, %%rax;"
                      "and $0xFFFB, %%ax;"
                      "or  $2, %%eax;"
                      "mov %%rax, %%cr0;"
                      "mov %%cr4, %%rax;"
                      "or  $0b11000000000, %%rax;"
                      "mov %%rax, %%cr4;"
               :
               :
               : "rax");

  // set NE in cr0 and reset x87 fpu
  __asm__ __volatile__("fninit;"
                      "mov %%cr0, %%rax;"
                      "or $0b100000, %%rax;"
                      "mov %%rax, %%cr0;"
               :
               :
               : "rax");

  // Do AVX stuff under certain conditions (qemu and prolly other stuff fake
  // not having support but all AVX instructions succeed)
  uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
  cpuid(&eax, &ebx, &ecx, &edx);

  if (ecx & (1 << 26)) {
    debugf("[cpu] The xsave instruction is available. Enabling..\n");
    uint64_t cr4;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (uint32_t)1 << 18;
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));

    if (ecx & (1 << 28)) {
      debugf("[cpu] The AVX extensions are available. Enabling..\n");
      uint32_t xcr0_lo = 0x7; // x87 (bit 0), SSE (bit 1), AVX (bit 2)
      uint32_t xcr0_hi = 0;
      __asm__ __volatile__("xsetbv" ::"c"(0), "a"(xcr0_lo), "d"(xcr0_hi));
    }
  }

  debugf("[cpu] Extra CPU features have all been enabled without issue\n");
}

void panic() {
  debugf("[kernel] Kernel panic triggered!\n");
  __asm__ __volatile__("cli");

  // If the framebuffer is initialized, override Xorg with a Red Screen of Death
  if (fb.virt != 0 && fb.width > 0 && fb.height > 0) {
    // 1. Paint screen dark red (BGRA: B=0, G=0, R=136) utilizing pitch safety
    for (int y = 0; y < (int)fb.height; y++) {
      unsigned int row_offset = y * fb.pitch;
      for (int x = 0; x < (int)fb.width; x++) {
        unsigned int offset = row_offset + (x * 4);
        fb.virt[offset] = 0;       // Blue
        fb.virt[offset + 1] = 0;   // Green
        fb.virt[offset + 2] = 136; // Red
        fb.virt[offset + 3] = 0;   // Alpha
      }
    }

    // 2. Draw a retro TUI error box in the center of the screen
    int box_w = 520;
    int box_h = 200;
    if ((int)fb.width > box_w && (int)fb.height > box_h) {
      int box_x = (fb.width - box_w) / 2;
      int box_y = (fb.height - box_h) / 2;

      for (int y = box_y; y < box_y + box_h; y++) {
        unsigned int row_offset = y * fb.pitch;
        for (int x = box_x; x < box_x + box_w; x++) {
          unsigned int offset = row_offset + (x * 4);

          bool is_border = (x == box_x || x == box_x + box_w - 1 ||
                            y == box_y || y == box_y + box_h - 1);

          if (is_border) {
            fb.virt[offset] = 255; // White border
            fb.virt[offset + 1] = 255;
            fb.virt[offset + 2] = 255;
            fb.virt[offset + 3] = 0;
          } else {
            fb.virt[offset] =
                60; // Dark blue-grey box background (B=60, G=20, R=20)
            fb.virt[offset + 1] = 20;
            fb.virt[offset + 2] = 20;
            fb.virt[offset + 3] = 0;
          }
        }
      }

      // 3. Configure Console for the TUI Box using raw pixel coordinates
      consoleDisabled = false;
      changeTextColor(255, 255, 255); // Bright white text
      changeBg(20, 20, 60);           // Match box background

      int padding_x = box_x + 24;
      int padding_y = box_y + 24;

      // Print Line 1: Title
      setConsoleX(padding_x);
      setConsoleY(padding_y);
      const char *msg1 = "KERNEL PANIC";
      for (int i = 0; msg1[i] != '\0'; i++)
        putchar_(msg1[i]);

      // Print Line 2: Details (Added a leading space to prevent first-char
      // clipping)
      setConsoleX(padding_x);
      setConsoleY(padding_y + (TTY_CHARACTER_HEIGHT * 2));
      const char *msg2 = "The OS has run into an issue.";
      for (int i = 0; msg2[i] != '\0'; i++)
        putchar_(msg2[i]);

      // Print Line 3: Action
      setConsoleX(padding_x);
      setConsoleY(padding_y + (TTY_CHARACTER_HEIGHT * 4));
      const char *msg3 = "It is halted safely, please reboot.";
      for (int i = 0; msg3[i] != '\0'; i++)
        putchar_(msg3[i]);
    }
  }

  while (true)
    __asm__ __volatile__("hlt");
}

void _assert(bool expression, char *file, int line) {
  if (!expression) {
    debugf("[assert] Assertation failed! file{%s:%d}\n", file, line);
    panic();
  }
}

bool checkInterrupts(void) {
  uint64_t flags;
  __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
  return (flags & (1ULL << 9)) != 0;
}

bool isProtectedMemory(uint64_t virt) {
  if (virt > bootloader.hhdmOffset &&
      virt < (bootloader.hhdmOffset + bootloader.mmTotal))
    return true;
  else if (virt > bootloader.kernelVirtBase &&
           virt < bootloader.kernelVirtBase + 268435456) {
    return true;
  }

  return false;
}

#include <paging.h>
#include <task.h>
void handControl() {
  if (!tasksInitiated)
    return;

  // handControl() MUST only ever be called with interrupts enabled, since it
  // synchronously yields to the scheduler (int $0x81). If IF happens to be
  // cleared, print enough context to identify the caller before asserting.
  assert(checkInterrupts()); // no volatile contexts
  if (!checkInterrupts()) {
    debugf("[handControl] FATAL: yielded with interrupts disabled!\n");
    debugf("[handControl] rflags-interrupt-bit: {%ld}\n", checkInterrupts());
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wframe-address"
    debugf("[handControl] calling site: %p, callers: %p %p %p %p\n",
           __builtin_return_address(0), __builtin_return_address(1),
           __builtin_return_address(2), __builtin_return_address(3),
           __builtin_return_address(4));
#pragma clang diagnostic pop

    if (currentTask) {
      debugf("[handControl] current task id{%d} name{\"%.*s\"} state{%d}\n",
             currentTask->id, currentTask->cmdlineLen, currentTask->cmdline,
             currentTask->state);
    }
  }
  __asm__ __volatile__("int $0x81" ::: "memory");
}

// Endianness
uint16_t switch_endian_16(uint16_t val) { return (val << 8) | (val >> 8); }

uint32_t switch_endian_32(uint32_t val) {
  return (val << 24) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) |
         (val >> 24);
}