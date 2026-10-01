boot/kernel.o: boot/kernel.c ../include/config.h ../include/acpi.h \
  ../include/types.h ../include/limine.h ../include/pmm.h \
  ../include/bitmap.h ../include/printf.h ../include/serial.h \
  ../include/system.h ../include/spinlock.h ../include/uacpi/acpi.h \
  ../include/uacpi/platform/compiler.h ../include/uacpi/helpers.h \
  ../include/uacpi/types.h ../include/uacpi/status.h \
  ../include/uacpi/internal/compiler.h ../include/uacpi/platform/types.h \
  ../include/uacpi/platform/arch_helpers.h \
  ../include/uacpi/platform/atomic.h ../include/uacpi/platform/config.h \
  ../include/uacpi/log.h ../include/uacpi/resources.h \
  ../include/uacpi/tables.h ../include/uacpi/uacpi.h \
  ../include/uacpi/kernel_api.h ../include/uacpi/namespace.h \
  ../include/uacpi/utilities.h ../include/apic.h \
  ../include/linked_list.h ../include/util.h ../include/pci.h \
  ../include/isr.h ../include/bootloader.h ../include/console.h \
  ../include/disk.h ../include/elf.h ../include/task.h \
  ../include/avl_tree.h ../include/vfs.h ../include/linux.h \
  ../include/linux_event_codes.h ../include/fakefs.h \
  ../include/fastSyscall.h ../include/fb.h ../include/gdt.h \
  ../include/idt.h ../include/kb.h ../include/dev.h \
  ../include/circular.h ../include/kernel_helper.h ../include/linuxkpi.h \
  ../include/malloc.h ../include/md5.h ../include/mouse.h \
  ../include/nic_controller.h ../include/arp.h ../include/eth.h \
  ../include/udp.h ../include/ipv4.h ../include/netports.h \
  ../services/net/lwip/include/lwip/netif.h \
  ../services/net/lwip/include/lwip/opt.h \
  ../services/net/lwip/include/lwipopts.h \
  ../services/net/lwip/include/arch/cc.h ../include/string.h \
  ../services/net/lwip/include/lwip/debug.h \
  ../services/net/lwip/include/lwip/arch.h \
  ../services/net/lwip/include/lwip/err.h \
  ../services/net/lwip/include/lwip/ip_addr.h \
  ../services/net/lwip/include/lwip/def.h \
  ../services/net/lwip/include/lwip/ip4_addr.h \
  ../services/net/lwip/include/lwip/ip6_addr.h \
  ../services/net/lwip/include/lwip/pbuf.h \
  ../services/net/lwip/include/lwip/stats.h \
  ../services/net/lwip/include/lwip/mem.h \
  ../services/net/lwip/include/lwip/memp.h \
  ../services/net/lwip/include/lwip/priv/memp_std.h \
  ../services/net/lwip/include/lwip/priv/memp_priv.h \
  ../services/net/lwip/include/lwip/priv/mem_priv.h \
  ../include/module_net.h ../include/paging.h ../include/psf.h \
  ../include/rtc.h ../include/shell.h ../include/sys.h \
  ../include/syscalls.h ../include/testing.h ../include/./types.h \
  ../include/timer.h ../include/vga.h ../include/vmm.h
../include/config.h:
../include/acpi.h:
../include/types.h:
../include/limine.h:
../include/pmm.h:
../include/bitmap.h:
../include/printf.h:
../include/serial.h:
../include/system.h:
../include/spinlock.h:
../include/uacpi/acpi.h:
../include/uacpi/platform/compiler.h:
../include/uacpi/helpers.h:
../include/uacpi/types.h:
../include/uacpi/status.h:
../include/uacpi/internal/compiler.h:
../include/uacpi/platform/types.h:
../include/uacpi/platform/arch_helpers.h:
../include/uacpi/platform/atomic.h:
../include/uacpi/platform/config.h:
../include/uacpi/log.h:
../include/uacpi/resources.h:
../include/uacpi/tables.h:
../include/uacpi/uacpi.h:
../include/uacpi/kernel_api.h:
../include/uacpi/namespace.h:
../include/uacpi/utilities.h:
../include/apic.h:
../include/linked_list.h:
../include/util.h:
../include/pci.h:
../include/isr.h:
../include/bootloader.h:
../include/console.h:
../include/disk.h:
../include/elf.h:
../include/task.h:
../include/avl_tree.h:
../include/vfs.h:
../include/linux.h:
../include/linux_event_codes.h:
../include/fakefs.h:
../include/fastSyscall.h:
../include/fb.h:
../include/gdt.h:
../include/idt.h:
../include/kb.h:
../include/dev.h:
../include/circular.h:
../include/kernel_helper.h:
../include/linuxkpi.h:
../include/malloc.h:
../include/md5.h:
../include/mouse.h:
../include/nic_controller.h:
../include/arp.h:
../include/eth.h:
../include/udp.h:
../include/ipv4.h:
../include/netports.h:
../services/net/lwip/include/lwip/netif.h:
../services/net/lwip/include/lwip/opt.h:
../services/net/lwip/include/lwipopts.h:
../services/net/lwip/include/arch/cc.h:
../include/string.h:
../services/net/lwip/include/lwip/debug.h:
../services/net/lwip/include/lwip/arch.h:
../services/net/lwip/include/lwip/err.h:
../services/net/lwip/include/lwip/ip_addr.h:
../services/net/lwip/include/lwip/def.h:
../services/net/lwip/include/lwip/ip4_addr.h:
../services/net/lwip/include/lwip/ip6_addr.h:
../services/net/lwip/include/lwip/pbuf.h:
../services/net/lwip/include/lwip/stats.h:
../services/net/lwip/include/lwip/mem.h:
../services/net/lwip/include/lwip/memp.h:
../services/net/lwip/include/lwip/priv/memp_std.h:
../services/net/lwip/include/lwip/priv/memp_priv.h:
../services/net/lwip/include/lwip/priv/mem_priv.h:
../include/module_net.h:
../include/paging.h:
../include/psf.h:
../include/rtc.h:
../include/shell.h:
../include/sys.h:
../include/syscalls.h:
../include/testing.h:
../include/./types.h:
../include/timer.h:
../include/vga.h:
../include/vmm.h:
