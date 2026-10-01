COMPILER = ~/opt/cross/bin/x86_64-cavos-gcc
SYSROOT = $(CURDIR)/../../../target
CFLAGS = -std=c17 -pedantic-errors -Wall -Wextra -static --sysroot=$(SYSROOT)
OUTPUT = testing
TARGET = ../../../target/usr/bin/

all: clean compile install

compile:
	$(COMPILER) main.c utils.c -o $(OUTPUT) $(CFLAGS)

install:
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)

clean:
	rm -f $(OUTPUT)
