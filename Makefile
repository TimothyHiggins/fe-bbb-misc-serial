ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabi-

ifneq ($(KERNELRELEASE),)
obj-m := feserial.o
else
KDIR := $(HOME)/nunchukdrv/linux
all:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$$PWD
endif
.PHONY: clean
clean:
	-@rm *.ko *.o *.symvers *.cmd *.order *.mod.c 2>/dev/null || true
