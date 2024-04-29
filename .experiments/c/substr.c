#include <stdio.h>

int main() {
    // This compiles but it feels like it shouldn't...
    printf("Hi %s!\n", &"  mom"[2]);
    return 0;
}
