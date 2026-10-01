#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL"); // bogus license. not actually gpl
MODULE_AUTHOR("hyperliteral");
MODULE_DESCRIPTION("Sample linuxkpi module - demonstrates the KPI shim");
MODULE_VERSION("0.1");

static struct pci_device_id linuxhello_pci_ids[] = {
        PCI_DEVICE(0x15AD, 0x0405), // VMware SVGA II (same as vmware_svga2.c)
        { 0 }
};
MODULE_DEVICE_TABLE(pci, linuxhello_pci_ids);

static void *drv_buf;

static int linuxhello_probe(struct pci_dev *pdev,
                                                        const struct pci_device_id *id) {
  pr_info("linuxhello: probing %04x:%04x via KPI!\n", pdev->vendor,
                  pdev->device);

  drv_buf = kzalloc(4096, GFP_KERNEL);
  if (!drv_buf)
        return -ENOMEM;

  int err = pci_enable_device(pdev);
  if (err < 0) {
        kfree(drv_buf);
        drv_buf = NULL;
        return err;
  }

  pci_set_master(pdev);
  pci_set_drvdata(pdev, drv_buf);
  return 0;
}

static void linuxhello_remove(struct pci_dev *pdev) {
  pr_info("linuxhello: removing %04x:%04x\n", pdev->vendor, pdev->device);
  kfree(pci_get_drvdata(pdev));
  pci_set_drvdata(pdev, NULL);
}

static struct pci_driver linuxhello_driver = {
        .name = "linuxhello",
        .id_table = linuxhello_pci_ids,
        .probe = linuxhello_probe,
        .remove = linuxhello_remove,
};

static int __init linuxhello_init(void) {
  int err = pci_register_driver(&linuxhello_driver);
  if (err)
        return err;
  pr_info("linuxhello: registered with the Linux KPI!\n");
  return 0;
}

static void __exit linuxhello_exit(void) { pci_unregister_driver(&linuxhello_driver); }

module_init(linuxhello_init);
module_exit(linuxhello_exit);