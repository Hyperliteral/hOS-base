#include <elf.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <malloc.h>
#include <pci.h>
#include <printf.h>
#include <serial.h>
#include <string.h>
#include <types.h>

// cavOS Linux KPI: a Linux-kernel-programming-interface shim, in the spirit of
// FreeBSD's linuxkpi. It (a) provides linux/* headers that unmodified Linux
// driver source can build against, and (b) implements + exports the backing
// symbols (kmalloc, printk, pci_register_driver, ...) for the module loader.
// Copyright (C) 2024 Panagiotis

#define KPI_EXPORT(sym) kernelSymbolRegister(#sym, (void *)&sym)

// ---- memory (slab) ----

void *kmalloc(size_t size, gfp_t flags) {
  (void)flags;
  return malloc(size);
}

void *kzalloc(size_t size, gfp_t flags) {
  void *p = kmalloc(size, flags);
  if (p)
    memset(p, 0, size);
  return p;
}

void *kcalloc(size_t n, size_t size, gfp_t flags) {
  if (size && n > ((size_t)-1) / size)
    return 0;
  return kzalloc(n * size, flags);
}

void kfree(const void *ptr) {
  if (ptr)
    free((void *)ptr);
}

// ---- printing ----

int printk(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = vprintf_(fmt, args);
  va_end(args);
  return ret;
}

// ---- PCI ----

typedef struct KpiPciBinding {
  struct KpiPciBinding *next;
  struct pci_driver    *drv;
  struct pci_dev       *pdev;
  void                 *sysdata; // underlying PCI *
} KpiPciBinding;

static KpiPciBinding *bindings;

static void fillPciDev(struct pci_dev *pdev, PCI *pci) {
  memset(pdev, 0, sizeof(*pdev));
  pdev->vendor = pci->vendor_id;
  pdev->device = pci->device_id;
  pdev->bus = pci->bus;
  pdev->devfn = (pci->slot << 3) | pci->function;

  // the PCI bookkeeping struct doesn't cache class/revision; read config space
  uint16_t cfg08 = ConfigReadWord(pci->bus, pci->slot, pci->function, 0x08);
  uint16_t cfg0a = ConfigReadWord(pci->bus, pci->slot, pci->function, 0x0a);
  pdev->revision = cfg08 & 0xFF;
  pdev->class = ((uint32_t)(cfg0a >> 8) << 16) |
                ((uint32_t)(cfg0a & 0xFF) << 8) | (cfg08 >> 8);

  pdev->sysdata = pci;
  pdev->dev.init_name = pci->name ? pci->name : "pci-device";
}

static bool pciIdMatch(const struct pci_device_id *id,
                       const struct pci_dev       *dev) {
  if (id->vendor != PCI_ANY_ID && id->vendor != dev->vendor)
    return false;
  if (id->device != PCI_ANY_ID && id->device != dev->device)
    return false;
  if (id->subvendor != PCI_ANY_ID && id->subvendor != dev->subsystem_vendor)
    return false;
  if (id->subdevice != PCI_ANY_ID && id->subdevice != dev->subsystem_device)
    return false;
  if (id->class_mask && ((id->class ^ dev->class) & id->class_mask))
    return false;
  return true;
}

static const struct pci_device_id *pciFindId(const struct pci_device_id *table,
                                             const struct pci_dev       *dev) {
  for (const struct pci_device_id *id = table;
       id->vendor || id->device || id->class || id->class_mask; id++) {
    if (pciIdMatch(id, dev))
      return id;
  }
  return 0;
}

int pci_register_driver(struct pci_driver *drv) {
  printk(KERN_INFO "linuxkpi: registering pci_driver {%s}\n",
         drv->name ? drv->name : "?");
  if (!drv->id_table)
    return -EINVAL;

  int matched = 0;
  for (PCI *pci = (PCI *)dsPCI.firstObject; pci; pci = (PCI *)pci->_ll.next) {
    if (!drv->probe)
      continue;

    struct pci_dev *pdev = (struct pci_dev *)kzalloc(sizeof(*pdev), GFP_KERNEL);
    fillPciDev(pdev, pci);

    const struct pci_device_id *id = pciFindId(drv->id_table, pdev);
    if (!id) {
      kfree(pdev);
      continue;
    }

    if (drv->probe(pdev, id) == 0) {
      KpiPciBinding *b =
          (KpiPciBinding *)kmalloc(sizeof(KpiPciBinding), GFP_KERNEL);
      b->drv = drv;
      b->pdev = pdev;
      b->sysdata = pci;
      b->next = bindings;
      bindings = b;
      matched++;
    } else {
      kfree(pdev);
    }
  }

  if (!matched)
    printk(KERN_INFO "linuxkpi: pci_driver {%s} matched nothing\n",
           drv->name ? drv->name : "?");
  return 0;
}

void pci_unregister_driver(struct pci_driver *drv) {
  KpiPciBinding **pp = &bindings;
  while (*pp) {
    KpiPciBinding *b = *pp;
    if (b->drv == drv) {
      if (drv->remove)
        drv->remove(b->pdev);
      kfree(b->pdev);
      *pp = b->next;
      kfree(b);
    } else {
      pp = &b->next;
    }
  }
}

int pci_enable_device(struct pci_dev *dev) {
  __u16 cmd;
  pci_read_config_word(dev, PCI_COMMAND, &cmd);
  cmd |= 0x0003; // memory + io space
  pci_write_config_word(dev, PCI_COMMAND, cmd);
  return 0;
}

void pci_disable_device(struct pci_dev *dev) {
  __u16 cmd;
  pci_read_config_word(dev, PCI_COMMAND, &cmd);
  cmd &= ~0x0003;
  pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void pci_set_master(struct pci_dev *dev) {
  __u16 cmd;
  pci_read_config_word(dev, PCI_COMMAND, &cmd);
  cmd |= 0x0004; // bus master
  pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void pci_clear_master(struct pci_dev *dev) {
  __u16 cmd;
  pci_read_config_word(dev, PCI_COMMAND, &cmd);
  cmd &= ~0x0004;
  pci_write_config_word(dev, PCI_COMMAND, cmd);
}

static void pciCfgAddr(const struct pci_dev *dev, uint8_t *bus, uint8_t *slot,
                       uint8_t *func) {
  if (dev->bus < 8) {
    *bus = dev->bus;
    *slot = dev->devfn >> 3;
    *func = dev->devfn & 7;
  } else {
    // fall back to the underlying sysdata we stashed
    PCI *pci = (PCI *)dev->sysdata;
    *bus = pci->bus;
    *slot = pci->slot;
    *func = pci->function;
  }
}

int pci_read_config_byte(struct pci_dev *dev, int where, __u8 *val) {
  uint8_t bus, slot, func;
  pciCfgAddr(dev, &bus, &slot, &func);
  uint16_t word = ConfigReadWord(bus, slot, func, where & ~1u);
  *val = (where & 1) ? (word >> 8) : word;
  return 0;
}

int pci_read_config_word(struct pci_dev *dev, int where, __u16 *val) {
  uint8_t bus, slot, func;
  pciCfgAddr(dev, &bus, &slot, &func);
  *val = ConfigReadWord(bus, slot, func, where);
  return 0;
}

int pci_read_config_dword(struct pci_dev *dev, int where, __u32 *val) {
  uint8_t bus, slot, func;
  pciCfgAddr(dev, &bus, &slot, &func);
  __u16 lo = ConfigReadWord(bus, slot, func, where);
  __u16 hi = ConfigReadWord(bus, slot, func, where + 2);
  *val = ((__u32)hi << 16) | lo;
  return 0;
}

int pci_write_config_word(struct pci_dev *dev, int where, __u16 val) {
  uint8_t bus, slot, func;
  pciCfgAddr(dev, &bus, &slot, &func);
  __u32 old = ConfigReadWord(bus, slot, func, where) |
              ((__u32)ConfigReadWord(bus, slot, func, where + 2) << 16);
  __u32 mask = (where & 1) ? 0xFFFF0000u : 0x0000FFFFu;
  old &= ~mask;
  old |= (__u32)val << (8 * (where & 1));
  ConfigWriteDword(bus, slot, func, where & ~3u, old);
  return 0;
}

int pci_write_config_byte(struct pci_dev *dev, int where, __u8 val) {
  __u16 word;
  pci_read_config_word(dev, where & ~1u, &word);
  if (where & 1)
    word = (word & 0x00FF) | ((__u16)val << 8);
  else
    word = (word & 0xFF00) | val;
  pci_write_config_word(dev, where & ~1u, word);
  return 0;
}

int pci_write_config_dword(struct pci_dev *dev, int where, __u32 val) {
  uint8_t bus, slot, func;
  pciCfgAddr(dev, &bus, &slot, &func);
  ConfigWriteDword(bus, slot, func, where, val);
  return 0;
}

void initiateLinuxKPI(void) {
  // publish the backing implementations to the module loader
  KPI_EXPORT(kmalloc);
  KPI_EXPORT(kzalloc);
  KPI_EXPORT(kcalloc);
  KPI_EXPORT(kfree);
  KPI_EXPORT(printk);
  KPI_EXPORT(pci_register_driver);
  KPI_EXPORT(pci_unregister_driver);
  KPI_EXPORT(pci_enable_device);
  KPI_EXPORT(pci_disable_device);
  KPI_EXPORT(pci_set_master);
  KPI_EXPORT(pci_clear_master);
  KPI_EXPORT(pci_read_config_byte);
  KPI_EXPORT(pci_read_config_word);
  KPI_EXPORT(pci_read_config_dword);
  KPI_EXPORT(pci_write_config_byte);
  KPI_EXPORT(pci_write_config_word);
  KPI_EXPORT(pci_write_config_dword);

  debugf("[linuxkpi] Linux KPI is ready\n");
}