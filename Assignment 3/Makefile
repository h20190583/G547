obj-m := usb_final.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	rm -rf *.o *.mod.c *.symvers *.order *.ko *.mk *.cmd *.o.cmd *.swo *.swp
