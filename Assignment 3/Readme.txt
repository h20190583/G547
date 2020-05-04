USB DRIVER:

TO build the module:
1. - Find VID(vendor ID), PID(product ID) and number of sectors of your USB device
   - Update VID PID and number of sectors in the code (usb_final.c)
	(update number of sectors in NR_OF_SECTORS in the constant section)
2. $make all
3. $sudo modprobe -rf uas usb_storage  (to remove preloaded usb drivers)
4. $sudo insmod usb_final.ko  (insert the USB driver module)
5. $dmesg -wH (check for any errors in the kernel log)

INSERT PENDRIVE: check for block driver inserted
1. $sudo fdisk -l 
    - block driver with name PENDRIVE will appear with capcaity and other info
    - partition PENDRIVE1 will appear with type of filesystem
2. $sudo lsblk (lists all the block device registered)
3. $sudo ls -l /dev (lists all the modules loaded)
    - check for PENDRIVE and PENDRIVE1

MOUNT:
1. $sudo mkdir /media/pd
2. $sudo mount -t vfat /dev/PENDRIVE1 /media/pd

READ and WRITE:
1. Goto the directory where the pendrive is mounted 
   - $cd /media/pd
2. $ls (list all the files)
3. $sudo vim filename.txt to create a new file or open an existing file
4. Follow vim commands to make changes (:i to insert, Esc :wq to write and exit and so on)
   - $sudo apt-get install vim (if vim not installed)

UNMOUNT and RMMOD:
1. Come out of the directory
2. $sudo umount /dev/PENDRIVE1   (unmount the pendrive)
3. Remove pendrive
4. $sudo rmmod usb_final.ko      (to remove the loaded module)

END
