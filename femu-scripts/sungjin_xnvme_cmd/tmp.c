#include <stdio.h>
#include <string.h>

int main() {
    int pid = 32;
    char buf[256];

    sprintf(buf, "%d", pid);

    printf("buf %s, len %lu\n", buf, strlen(buf));

    // expected result: buf 32, len 2
    return 0;
}
