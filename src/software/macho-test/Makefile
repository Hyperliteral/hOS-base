ROOT = $(abspath $(CURDIR)/../../..)
COMPILER = $(ROOT)/tools/userland/macho-clang
RUNTIME = $(ROOT)/src/libs/macho
OUTPUT = macho-test
TARGET = $(ROOT)/target/usr/bin/
CFLAGS = -Wall -Wextra -Werror

.PHONY: all runtime compile install clean

all: clean install

runtime:
	$(MAKE) -C $(RUNTIME)

compile: runtime
	$(COMPILER) $(CFLAGS) main.c -o $(OUTPUT)

install: compile
	mkdir -p $(TARGET)
	sudo cp $(OUTPUT) $(TARGET)

clean:
	rm -f $(OUTPUT)
