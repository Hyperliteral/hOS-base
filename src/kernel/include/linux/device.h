#ifndef _LINUX_DEVICE_H
#define _LINUX_DEVICE_H

#include <linux/printk.h>
#include <linux/types.h>

// Just enough of the driver model for probe/remove callbacks to compile and
// print through. No sysfs / no power management yet.
struct device {
  struct device *parent;
  const char        *init_name;
  void                  *driver_data;
  void                  *bus;
  unsigned int   id;
};

static inline const char *dev_name(const struct device *dev) {
  return (dev && dev->init_name) ? dev->init_name : "cavos-device";
}

#define dev_emerg(dev, fmt, ...) printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...) printk(KERN_ALERT fmt, ##__VA_ARGS__)
#define dev_crit(dev, fmt, ...) printk(KERN_CRIT fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...) printk(KERN_ERR fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) printk(KERN_INFO fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...) printk(KERN_DEBUG fmt, ##__VA_ARGS__)

#endif
