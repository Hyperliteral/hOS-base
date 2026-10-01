COMPILER = ~/opt/cross/bin/x86_64-cavos-gcc
SYSROOT = $(CURDIR)/../../../target
CFLAGS = -std=c17 -pedantic-errors -Wall -Wextra -static --sysroot=$(SYSROOT)
OUTPUT = install-cavos
TARGET = ../../../target/usr/bin/

all: clean compile install

compile:
	$(COMPILER) main.c -o $(OUTPUT) $(CFLAGS)

install:
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)
	sudo chown root:root $(TARGET)$(OUTPUT)
	sudo chmod 755 $(TARGET)$(OUTPUT)

clean:
	rm -f $(OUTPUT)
