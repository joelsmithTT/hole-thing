# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: GPL-2.0-only

obj-m += tenstorrent-vfio-pci.o
tenstorrent-vfio-pci-y := tenstorrent_vfio_pci.o

KDIR := /lib/modules/$(shell uname -r)/build

.PHONY: all modules modules_install clean

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
