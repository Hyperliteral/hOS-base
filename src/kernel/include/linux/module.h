#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>

// Our module loader (core/util/elf.c) looks up and calls init_module(), and
// optionally cleanup_module() on unload. These macros let unmodified Linux
// driver sources register their init/exit entry points.
#define module_init(fn)                                                                                                                \
  int init_module(void) { return fn(); }
#define module_exit(fn)                                                                                                                \
  void cleanup_module(void) { fn(); }

#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define MODULE_ALIAS(x)
#define MODULE_DEVICE_TABLE(type, name)

// Module-side export requests are ignored for the time being; modules can not
// (yet) re-export symbols to one another through us.
#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)

struct module {
  unsigned long state;
  const char   *name;
};

#define THIS_MODULE ((struct module *)0)

#endif
