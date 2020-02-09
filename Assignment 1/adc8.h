#ifndef ADC8_H
#define ADC8_H

#include <linux/ioctl.h>

#define MAJOR_NUM 111

#define IOCTL_SET_ALIGNMENT _IOW(MAJOR_NUM, 0, char *)

#define IOCTL_SELECT_CHANNEL _IOW(MAJOR_NUM, 1, char *)

#define DEVICE_FILE_NAME "/dev/adc8"

#endif
