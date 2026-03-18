// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vfio_pci_core.h>

#define PCI_VENDOR_ID_TENSTORRENT	0x1E52
#define PCI_DEVICE_ID_GRAYSKULL		0xFACA
#define PCI_DEVICE_ID_WORMHOLE		0x401E
#define PCI_DEVICE_ID_BLACKHOLE		0xB140

static int tenstorrent_vfio_pci_open_device(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	vfio_pci_core_finish_enable(vdev);
	return 0;
}

static const struct vfio_device_ops tenstorrent_vfio_pci_ops = {
	.name		= "tenstorrent-vfio-pci",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device	= tenstorrent_vfio_pci_open_device,
	.close_device	= vfio_pci_core_close_device,
	.ioctl		= vfio_pci_core_ioctl,
	.device_feature	= vfio_pci_core_ioctl_feature,
	.read		= vfio_pci_core_read,
	.write		= vfio_pci_core_write,
	.mmap		= vfio_pci_core_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static int tenstorrent_vfio_pci_probe(struct pci_dev *pdev,
				      const struct pci_device_id *id)
{
	struct vfio_pci_core_device *vdev;
	int ret;

	vdev = vfio_alloc_device(vfio_pci_core_device, vdev,
				 &pdev->dev, &tenstorrent_vfio_pci_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	dev_set_drvdata(&pdev->dev, vdev);

	ret = vfio_pci_core_register_device(vdev);
	if (ret)
		goto out_put;

	dev_info(&pdev->dev, "probed [%04x:%04x] (rev %02x)\n", pdev->vendor, pdev->device, pdev->revision);
		
	pci_ignore_hotplug(pdev);
	return 0;

out_put:
	vfio_put_device(&vdev->vdev);
	return ret;
}

static void tenstorrent_vfio_pci_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "removing [%04x:%04x]\n", pdev->vendor, pdev->device);
	vfio_pci_core_unregister_device(vdev);
	vfio_put_device(&vdev->vdev);
}

static const struct pci_device_id tenstorrent_vfio_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_GRAYSKULL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_WORMHOLE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_TENSTORRENT, PCI_DEVICE_ID_BLACKHOLE) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, tenstorrent_vfio_pci_ids);

static struct pci_driver tenstorrent_vfio_pci_driver = {
	.name			= "tenstorrent-vfio-pci",
	.id_table		= tenstorrent_vfio_pci_ids,
	.probe			= tenstorrent_vfio_pci_probe,
	.remove			= tenstorrent_vfio_pci_remove,
	.err_handler		= &vfio_pci_core_err_handlers,
	.driver_managed_dma	= true,
};

module_pci_driver(tenstorrent_vfio_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tenstorrent Inc.");
MODULE_DESCRIPTION("VFIO PCI driver for Tenstorrent AI accelerators");
MODULE_IMPORT_NS(VFIO);
