#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>

#define PCI_VENDOR_ID_TENSTORRENT	0x1E52
#define PCI_DEVICE_ID_GRAYSKULL		0xFACA
#define PCI_DEVICE_ID_WORMHOLE		0x401E
#define PCI_DEVICE_ID_BLACKHOLE		0xB140

static int tenstorrent_simple_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	pci_ignore_hotplug(pdev);
	dev_info(&pdev->dev, "hotplug disabled for [%04x:%04x] (rev %02x)\n", pdev->vendor, pdev->device, pdev->revision);

	/* Don't actually bind -- leave the device free for vfio-pci. */
	return -ENODEV;
}

static const struct pci_device_id tenstorrent_simple_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_GRAYSKULL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_WORMHOLE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_BLACKHOLE) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, tenstorrent_simple_ids);

static struct pci_driver tenstorrent_simple_driver = {
	.name		= "tenstorrent-simple",
	.id_table	= tenstorrent_simple_ids,
	.probe		= tenstorrent_simple_probe,
};

static int __init tenstorrent_simple_init(void)
{
	return pci_register_driver(&tenstorrent_simple_driver);
}

static void __exit tenstorrent_simple_exit(void)
{
	pci_unregister_driver(&tenstorrent_simple_driver);
}

module_init(tenstorrent_simple_init);
module_exit(tenstorrent_simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tenstorrent Inc.");
MODULE_DESCRIPTION("Minimal PCI stub for Tenstorrent devices (hotplug disable)");
