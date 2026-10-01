COMPILER = ~/opt/cross/bin/x86_64-cavos-gcc
SYSROOT = $(CURDIR)/../../../target
CFLAGS = -std=c17 -Wall -Wextra -nostdlib -fno-builtin -fno-pie -fno-pic \
                 -mcmodel=kernel -mno-red-zone -fno-stack-protector -ffreestanding \
                 -I../../kernel/include -c --sysroot=$(SYSROOT)
OUTPUT = module.ko
TARGET = ../../../target/boot/

all: clean compile install

compile:
	$(COMPILER) main.c -o $(OUTPUT) $(CFLAGS)

install:
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)linuxhello.ko
	sudo chown root:root $(TARGET)linuxhello.ko

clean:
	rm -f $(TARGET)$(OUTPUT)