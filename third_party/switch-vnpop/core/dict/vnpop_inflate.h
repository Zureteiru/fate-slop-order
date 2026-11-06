#ifndef VNPOP_INFLATE_H
#define VNPOP_INFLATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decompresses raw DEFLATE or zlib stream into out_buf. Returns bytes written, or 0 on error. */
size_t vnpop_inflate(void* out_buf, size_t out_max, const void* in_buf, size_t in_size);

#ifdef __cplusplus
}
#endif

#endif /* VNPOP_INFLATE_H */
