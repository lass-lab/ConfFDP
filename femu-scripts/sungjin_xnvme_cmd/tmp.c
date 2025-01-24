#include <stdio.h>
#include <string.h>
#include <fcntl.h>
// #include <ioctl.h>
#include <asm-generic/ioctl.h>
#define F2FS_IOC_SUNGJIN		_IO(0xf5, 24)
int main() {
    int pid = 32;
    char buf[256];

    sprintf(buf, "%d", pid);

    printf("buf %s, len %lu %d\n", buf, strlen(buf),F2FS_IOC_SUNGJIN);

    // expected result: buf 32, len 2
    return 0;
}
