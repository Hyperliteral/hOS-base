COMPILER = clang
SYSROOT = $(.CURDIR)/../../../target
CFLAGS = -std=c17 -pedantic-errors -Wall -Wextra -static \
         --target=x86_64-unknown-elf \
         --sysroot=$(SYSROOT)
OUTPUT = initd
TARGET = ../../../target/usr/bin/

all: clean compile install

compile:
	$(COMPILER) main.c -o $(OUTPUT) $(CFLAGS)

install:
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)

clean:
	rm -f $(OUTPUT)