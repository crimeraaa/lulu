#include <stdlib.h>
#include <stdio.h>

#define TESTSIZE    8

// #define SCANLINE    "AAAABBCCCCCDDD"
#define SCANLINE    "WWWWWWWWWWWWBWWWWWWWWWWWWBBBWWWWWWWWWWWWWWWWWWWWWWWWBWWWWWWWWWWWWWW"
#define SCANSIZE    (sizeof(SCANLINE) / sizeof(SCANLINE[0]))

typedef struct {
    char value;
    int count;
} RLE_Run;

typedef struct {
    RLE_Run sequence[16];
    int count;
} RLE_Sequence;

/* https://en.wikipedia.org/wiki/Run-length_encoding */
int main() {
    RLE_Sequence rle = {0};
    rle.count = -1; // -1 so we can increment on the first match
    // Start with 0 so first char always starts the RLE correctly
    char c = 0;
    for (int i = 0; i < (int)SCANSIZE; i++) {
        // Start a new run
        if (c != SCANLINE[i]) {
            c = SCANLINE[i];
            rle.count++;
            rle.sequence[rle.count].value = c;
            rle.sequence[rle.count].count = 1;
        } else {
            rle.sequence[rle.count].count++;
        }
    }
    for (int i = 0; i < rle.count; i++) {
        printf("'%c' = %i\n", rle.sequence[i].value, rle.sequence[i].count);
    }
    return 0;
}
