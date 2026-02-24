/* string_stress.c — String manipulation benchmark (50K iterations).
   Equivalent to examples/il/benchmarks/string_stress.il */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    int64_t sum = 0;
    for (int64_t i = 0; i < 50000; ++i) {
        char buf[64];
        /* Build "Hello World!" by concatenation */
        strcpy(buf, "Hello");
        strcat(buf, " ");
        strcat(buf, "World");
        strcat(buf, "!");
        sum += (int64_t)strlen(buf);
    }
    return (int)(sum & 0xFF);
}
