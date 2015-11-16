ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabi-

ifneq ($(KERNELRELEASE),)
obj-m := feserial.o
else
KDIR := $(HOME)/nunchukdrv/linux
all:
	$(MAKE) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -C $(KDIR) M=$$PWD
	arm-linux-gnueabi-gcc -static -o serial-get-counter serial-get-counter.c
	arm-linux-gnueabi-gcc -static -o serial-reset-counter serial-reset-counter.c
endif
.PHONY: clean
clean:
	-@rm *.ko *.o *.symvers *.cmd *.order *.mod.c 2>/dev/null || true
	-@rm serial-get-counter serial-reset-counter 2>/dev/null || true
