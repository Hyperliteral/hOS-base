LINKER = ld.lld

LDFLAGS = \
	-m elf_x86_64 \
	-static \
	-e _start \
	--image-base=0xffffffff80000000 \
	--no-undefined

TARGET = ../../target
MOUNTPOINT = /cavosmnt
TOOLS = ../../tools

TARGET_IMG = ../../disk.img
TARGET_VMWARE = ../../disk.vmdk
TARGET_ISO = ../../cavOS.iso

OUTPUT = $(TARGET)/boot/kernel.elf

CORE = core/core.a
SERVICES = services/services.a

CONFIG_MK = include/config.mk

# Automatically generate defaults if config.mk doesn't exist yet, so it never crashes
ifeq ($(wildcard $(CONFIG_MK)),)
$(shell python3 ../../tools/config/configure.py --defaults)
endif

include $(CONFIG_MK)
export CONFIG_CFLAGS

all: $(CORE) $(SERVICES)
	mkdir $(TARGET)/ -p
	mkdir $(TARGET)/boot/ -p
	# Link directly to the final ELF kernel file
	$(LINKER) $(LDFLAGS) -o $(OUTPUT) $(CORE) $(SERVICES)

config:
	python3 $(TOOLS)/config/configure.py

$(CORE):
	$(MAKE) -C core

$(SERVICES):
	$(MAKE) -C services

disk: all
	chmod +x $(TOOLS)/kernel/make_disk.sh
	$(TOOLS)/kernel/make_disk.sh $(TARGET) $(MOUNTPOINT) $(TARGET_IMG) || (chmod +x $(TOOLS)/kernel/cleanup.sh && $(TOOLS)/kernel/cleanup.sh $(MOUNTPOINT))

disk_dirty: all
	chmod +x $(TOOLS)/kernel/make_disk.sh
	$(TOOLS)/kernel/make_disk.sh $(TARGET) $(MOUNTPOINT) $(TARGET_IMG) yes || (chmod +x $(TOOLS)/kernel/cleanup.sh && $(TOOLS)/kernel/cleanup.sh $(MOUNTPOINT))

vmware:
	qemu-img convert $(TARGET_IMG) -O vmdk $(TARGET_VMWARE)

tools:
	chmod +x $(TOOLS)/toolchain/get_tools.sh
	$(TOOLS)/toolchain/get_tools.sh

clean:
	$(MAKE) -C core clean
	$(MAKE) -C services clean
	rm -f $(OUTPUT) $(CORE) $(SERVICES)

qemu:
	qemu-system-x86_64 -d guest_errors -serial stdio -drive file=$(TARGET_IMG),format=raw,id=disk,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 4g -netdev user,id=mynet0 -net nic,model=e1000,netdev=mynet0 -vga vmware -display sdl -enable-kvm

qemu_dbg:
	qemu-system-x86_64 -d guest_errors -no-shutdown -no-reboot -serial stdio -drive file=$(TARGET_IMG),format=raw,id=disk,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 8g -netdev user,id=mynet0,hostfwd=udp::5555-:69,hostfwd=tcp::5555-:69 -net nic,model=e1000,netdev=mynet0 -object filter-dump,id=id,netdev=mynet0,file=../../netdmp.pcapng -s -S -smp 4 -vga vmware -display sdl -enable-kvm