#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include "elf.h"
#include "macho.h"
#include "task.h"
#include "types.h"

bool executable_is_header(const void *image, size_t size);
Task *executableExecute(char *filepath, uint32_t argc, char **argv,
                                                uint32_t envc, char **envv, bool startup);

#endif
