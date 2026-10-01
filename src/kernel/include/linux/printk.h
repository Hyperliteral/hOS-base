#ifndef _LINUX_PRINTK_H
#define _LINUX_PRINTK_H

#include <linux/types.h>

// via linuxkpi.c, backs onto the kernel's own formatter
int printk(const char *fmt, ...);

#define KERN_EMERG "<0>"
#define KERN_ALERT "<1>"
#define KERN_CRIT "<2>"
#define KERN_ERR "<3>"
#define KERN_WARNING "<4>"
#define KERN_NOTICE "<5>"
#define KERN_INFO "<6>"
#define KERN_DEBUG "<7>"

#define pr_emerg(fmt, ...) printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...) printk(KERN_ALERT fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...) printk(KERN_CRIT fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) printk(KERN_ERR fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...) printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(KERN_INFO fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) printk(KERN_DEBUG fmt, ##__VA_ARGS__)

#endif
