#ifndef SPV_LOADER_H
#define SPV_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Load a .spv file into a newly allocated uint32_t buffer. Caller frees. */
bool spv_load_file(const char *path, uint32_t **out_words, size_t *out_count);

#endif
