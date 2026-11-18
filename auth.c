#include <stdio.h>
#include <string.h>

static int is_admin(const char* name) {
    int allowed;
    if (strcmp(name, "admin") == 0) {
        allowed = 1;
    }
    return allowed;
}

int main(void) {
    puts(is_admin("guest") ? "ACCESS GRANTED" : "access denied");
    return 0;
}