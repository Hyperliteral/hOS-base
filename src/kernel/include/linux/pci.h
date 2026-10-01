#ifndef _LINUX_PCI_H
#define _LINUX_PCI_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>

struct pci_device_id {
  __u32 vendor;
  __u32 device;
  __u32 subvendor;
  __u32 subdevice;
  __u32 class;
  __u32                 class_mask;
  unsigned long driver_data;
};

#define PCI_ANY_ID (~0u)
#define PCI_DEVICE(vend, dev)                                                                                                  \
  {.vendor = (vend),                                                                                                                   \
   .device = (dev),                                                                                                                        \
   .subvendor = PCI_ANY_ID,                                                                                                        \
   .subdevice = PCI_ANY_ID,                                                                                                        \
   .class = 0,                                                                                                                                 \
   .class_mask = 0,                                                                                                                        \
   .driver_data = 0}
#define PCI_DEVICE_CLASS(cls, msk)                                                                                         \
  {.vendor = PCI_ANY_ID,                                                                                                           \
   .device = PCI_ANY_ID,                                                                                                           \
   .subvendor = PCI_ANY_ID,                                                                                                        \
   .subdevice = PCI_ANY_ID,                                                                                                        \
   .class = (cls),                                                                                                                         \
   .class_mask = (msk),                                                                                                                \
   .driver_data = 0}

struct pci_dev {
  struct device dev;

  unsigned short vendor;
  unsigned short device;
  unsigned short subsystem_vendor;
  unsigned short subsystem_device;

  unsigned int class;
  unsigned char revision;

  unsigned char bus;
  unsigned int  devfn;
  unsigned int  irq;

  void *sysdata;
  void *driver_data;
};

struct pci_driver {
  const char                                 *name;
  const struct pci_device_id *id_table;
  int (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
  void (*remove)(struct pci_dev *pdev);
  int (*suspend)(struct pci_dev *pdev, unsigned int state);
  int (*resume)(struct pci_dev *pdev);
  void (*shutdown)(struct pci_dev *pdev);
  void *driver;
  void *err_handler;
};

int  pci_register_driver(struct pci_driver *driver);
void pci_unregister_driver(struct pci_driver *driver);

int  pci_enable_device(struct pci_dev *dev);
void pci_disable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);
void pci_clear_master(struct pci_dev *dev);

int pci_read_config_byte(struct pci_dev *dev, int where, __u8 *val);
int pci_read_config_word(struct pci_dev *dev, int where, __u16 *val);
int pci_read_config_dword(struct pci_dev *dev, int where, __u32 *val);
int pci_write_config_byte(struct pci_dev *dev, int where, __u8 val);
int pci_write_config_word(struct pci_dev *dev, int where, __u16 val);
int pci_write_config_dword(struct pci_dev *dev, int where, __u32 val);

static inline void *pci_get_drvdata(struct pci_dev *pdev) {
  return pdev->dev.driver_data;
}
static inline void pci_set_drvdata(struct pci_dev *pdev, void *data) {
  pdev->dev.driver_data = data;
}
static inline const char *pci_name(const struct pci_dev *pdev) {
  return dev_name(&pdev->dev);
}

#endif
