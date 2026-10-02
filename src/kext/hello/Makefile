COMPILER = clang
SYSROOT = $(.CURDIR)/../../../target
TRIPLE = x86_64-unknown-elf
CFLAGS = -std=c17 -Wall -Wextra -nostdlib -fno-builtin -fno-pie -fno-pic \
                 -mcmodel=kernel -mno-red-zone -fno-stack-protector -ffreestanding \
                 -I../../kernel/include -c --sysroot=$(SYSROOT) --target=$(TRIPLE) -isystem $(SYSROOT)/usr/include
OUTPUT = module.ko
TARGET = ../../../target/boot/

all: clean compile install

compile:
	$(COMPILER) main.c -o $(OUTPUT) $(CFLAGS)

install:
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)hello.ko
	sudo chown root:root $(TARGET)hello.ko

clean:
	rm -f $(TARGET)$(OUTPUT)