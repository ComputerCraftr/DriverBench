#include <stddef.h>
#include <stdlib.h>

static void write_out_of_bounds(unsigned char *allocation,
                                volatile size_t index) {
    allocation[index] = 1U;
}

int main(void) {
    unsigned char *const allocation = malloc(8U);
    if (allocation == NULL) {
        return 2;
    }
    volatile size_t invalid_index = 8U;
    write_out_of_bounds(allocation, invalid_index);
    free(allocation);
    return 0;
}
