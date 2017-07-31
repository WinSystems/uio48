# Makefile for uio48

ifneq ($(KERNELRELEASE),) # called by kbuild
	obj-m := uio48.o
else # called from command line
	KERNEL_VERSION = `uname -r`
	KERNELDIR := /lib/modules/$(KERNEL_VERSION)/build
	PWD  := $(shell pwd)
	MODULE_INSTALLDIR = /lib/modules/$(KERNEL_VERSION)/kernel/drivers/gpio/

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

uio48io.o: uio48io.c uio48.h Makefile
	gcc -c $(EXTRA_CFLAGS) uio48io.c

all:    default install poll flash

install:
	mkdir -p $(MODULE_INSTALLDIR)
	rm -f $(MODULE_INSTALLDIR)uio48.ko
	install -c -m 0644 uio48.ko $(MODULE_INSTALLDIR)
	/sbin/depmod -a

uninstall:
	rm -f $(MODULE_INSTALLDIR)uio48.ko
	/sbin/depmod -a

flash: flash.c uio48.h uio48io.o Makefile
	gcc -static flash.c uio48io.o -o flash
	chmod a+x flash

poll:  poll.c uio48.h uio48io.o Makefile
	gcc -D_REENTRANT -static poll.c uio48io.o -o poll -lpthread
	chmod a+x poll

endif
 
clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions /dev/uio48?

spotless:
	rm -rf ioctl poll flash Module.* *.o *~ core .depend .*.cmd *.ko *.mod.c *.order .tmp_versions /dev/uio48?
