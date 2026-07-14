#include <limits.h>

int main(void) {
    volatile int maximum = INT_MAX;
    volatile int increment = 1;
    volatile int overflow = maximum + increment;
    return overflow == 0;
}
