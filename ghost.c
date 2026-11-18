#include <stdio.h>

__attribute__((noinline)) void a(void) {
    volatile int secret = 0x5EC12E7;
    (void)secret;
}

__attribute__((noinline)) int b(void) {
    volatile int leftover;
    return leftover;
}

int main(void) {
    a();
    printf("b() sees: 0x%X\n", b());
    return 0;
}
