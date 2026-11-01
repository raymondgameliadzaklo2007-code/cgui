#include "spv_loader.h"

#include <stdio.h>
#include <stdlib.h>

bool spv_load_file(const char *path, uint32_t **out_words, size_t *out_count)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open SPIR-V file: %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (bytes < 4 || (bytes % 4) != 0) {
        fprintf(stderr, "Invalid SPIR-V size: %s (%ld bytes)\n", path, bytes);
        fclose(f);
        return false;
    }

    uint32_t *words = malloc((size_t)bytes);
    if (!words) {
        fclose(f);
        return false;
    }

    if (fread(words, 1, (size_t)bytes, f) != (size_t)bytes) {
        fprintf(stderr, "Failed to read SPIR-V: %s\n", path);
        free(words);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Magic number check */
    if (words[0] != 0x07230203u) {
        fprintf(stderr, "Not a SPIR-V module: %s\n", path);
        free(words);
        return false;
    }

    *out_words = words;
    *out_count = (size_t)bytes / 4;
    return true;
}
