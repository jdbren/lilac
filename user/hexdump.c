#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// basic hexdump utility for debugging purposes
void hexdump(const void *data, size_t size)
{
    const unsigned char *byte_data = (const unsigned char*)data;
    for (size_t i = 0; i < size; i += 16) {
        printf("%08zx  ", i);
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            printf("%02x ", byte_data[i + j]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("fopen");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    if (size == -1) {
        perror("ftell");
        fclose(file);
        return 1;
    }
    fseek(file, 0, SEEK_SET);
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        perror("malloc");
        fclose(file);
        return 1;
    }

    if (fread(buffer, 1, size, file) != size) {
        perror("fread");
        free(buffer);
        fclose(file);
        return 1;
    }

    fclose(file);

    hexdump(buffer, size);
    free(buffer);

    return 0;
}