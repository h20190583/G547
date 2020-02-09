#include "adc8.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>              /* open */
#include <unistd.h>             /* exit */
#include <sys/ioctl.h>          /* ioctl */


/*
 * Functions for the ioctl calls
 */
uint16_t buffer;

int ioctl_set_alignment(int file_desc, char *message)
{
    int ret_val;
    //IOCTL CALL
    ret_val = ioctl(file_desc, IOCTL_SET_ALIGNMENT, message);

    if (ret_val < 0) {
        printf("ioctl_set_msg failed:%d\n", ret_val);
        exit(-1);
    }
    printf("alignment set to : %c\n", *message);
    return 0;
}

int ioctl_set_channel(int file_desc, char *message)
{
    int ret_val;
    //IOCTL CALL 
    ret_val = ioctl(file_desc, IOCTL_SELECT_CHANNEL, message);

    if (ret_val < 0) {
        printf("ioctl_get_msg failed:%d\n", ret_val);
        exit(-1);
    }

    printf("channel set :%c\n", *message);
    return 0;
}


/*
 * Main - Call the ioctl functions
 */
int main()
{
    int count;
    int file_desc, ret_val;
    char *msg = "Message passed by ioctl\n";
    
    //OPEN DEVICE FILE
    file_desc = open(DEVICE_FILE_NAME, 0);
    if (file_desc < 0) {
        printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
        exit(-1);
    }
    /* 1 : right aligned 2 : left aligned Default : left aligned */
    /* channel range from 1 to 8, if out of range , deafult channel 1 is selected*/
    //IOCTL AND READ FUNCTIO CALLS DEMO

    //left aligned and channel 5 selected
    ioctl_set_alignment(file_desc, "2");
    ioctl_set_channel(file_desc, "5" );
    count = read(file_desc, &buffer, sizeof(buffer));
    printf("10 bit adc value: \n");
    printf("%u\n", buffer);

    //right aligned and channel 6 selected
    ioctl_set_alignment(file_desc, "1");
    ioctl_set_channel(file_desc, "6" );
    count = read(file_desc, &buffer, sizeof(buffer));
    printf("10 bit adc value: \n");
    printf("%u\n", buffer);
   
    close(file_desc);
    return 0;
}
