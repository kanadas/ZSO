#Kernel sources dir -- change
#KDIR ?= /lib/modules/`uname -r`/build
KDIR ?= ../linux/

default:
	$(MAKE) -C $(KDIR) M=$$PWD

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
