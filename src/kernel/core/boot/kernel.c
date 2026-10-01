#include <acpi.h>
#include <apic.h>
#include <bootloader.h>
#include <console.h>
#include <disk.h>
#include <elf.h>
#include <fakefs.h>
#include <fastSyscall.h>
#include <fb.h>
#include <gdt.h>
#include <idt.h>
#include <isr.h>
#include <kb.h>
#include <kernel_helper.h>
#include <limine.h>
#include <linuxkpi.h>
#include <malloc.h>
#include <md5.h>
#include <mouse.h>
#include <nic_controller.h>
#include <paging.h>
#include <pci.h>
#include <pmm.h>
#include <psf.h>
#include <rtc.h>
#include <serial.h>
#include <shell.h>
#include <string.h>
#include <sys.h>
#include <syscalls.h>
#include <system.h>
#include <task.h>
#include <testing.h>
#include <timer.h>
#include <util.h>
#include <vga.h>
#include <vmm.h>

// static volatile LIMINE_BASE_REVISION(2);
void _start(void) {
  systemDiskInit = false;

  /*
  if (LIMINE_BASE_REVISION_SUPPORTED == false)
  panic();
  */

  initiateGDT();
  initiateISR();

  initiateSerial();
  initialiseBootloaderParser();

  // Framebuffer doesn't depend on paging, limine prepares it anyways
  initiateVGA();
  initiateConsole();
  clearScreen();

  // None of the two depend on paging
  initiatePMM();
  initiateVMM();

  LinkedListInit(&dsIrqHandler, sizeof(irqHandler));
  initiateACPI(); // needed for APIC setup
  initiatePaging();
  initiateAPIC();
  asm volatile("sti" ::: "memory");

  debugf("\n====== REACHED SYSTEM ======\n");
  initiateApicTimer(); // mouse needs a timer
  LinkedListInit(&dsMountPoint, sizeof(MountPoint));
  fsMount("/dev/", CONNECTOR_DEV, 0, 0); // mouse & kb need it
  initiateKb();
  initiateMouse();
  // any filesystem operations depend on currentTask
  initiateTasks();
  initiateKernelThreads();
  initiateNetworking();
  initiatePCI();
  initiateLinuxKPI();
  fsMount("/", CONNECTOR_AHCI, 0, 1);
  fsMount("/boot/", CONNECTOR_AHCI, 0, 0);
  fsMount("/sys/", CONNECTOR_SYS, 0, 0);
  fsMount("/proc/", CONNECTOR_PROC, 0, 0);

  // just in case there's another font preference
  psfLoadFromFile(DEFAULT_FONT_PATH);

  initiateSyscallInst();
  initiateSyscalls();

  initiateSSE();

  testingInit();

  if (!systemDiskInit)
    printf("[warning] System disk has not been detected!\n");

  fsMount("/", CONNECTOR_AHCI, 0, 1);
  fsMount("/boot/", CONNECTOR_AHCI, 0, 0);

  debugf("[kernel] Spawning init system (/usr/bin/initd)...\n");
  run("/usr/bin/initd", true, 0, 0);

  // if init exits, panic because PID 1 may not die
  panic();
}