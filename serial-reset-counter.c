// arm-linux-gnueabi-gcc -static -o serial-reset-counter serial-reset-counter.c
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define SERIAL_RESET_COUNTER 0
#define SERIAL_GET_COUNTER 1

int main(void)
{
    int fd, ret;

    fd = open("/dev/feserial-48024000", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Unable to open /dev/serial\n");
        exit (1);
    }

    ret = ioctl(fd, SERIAL_RESET_COUNTER);
    if (ret < 0) {
        fprintf(stderr, "Unable to reset counter\n");
        exit (1);
    }

    printf("Counter reset\n");
    return 0;
}
