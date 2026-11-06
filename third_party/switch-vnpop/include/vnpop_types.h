#ifndef VNPOP_TYPES_H
#define VNPOP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VNPOP_SCREEN_WIDTH        1280
#define VNPOP_SCREEN_HEIGHT        720

/* Default VN dialogue region on 1280x720 Nintendo Switch screen */
#define VNPOP_DEFAULT_DIALOGUE_X    90
#define VNPOP_DEFAULT_DIALOGUE_Y   480
#define VNPOP_DEFAULT_DIALOGUE_W  1100
#define VNPOP_DEFAULT_DIALOGUE_H   210

#define VNPOP_MAX_CHARS_PER_LINE   128
#define VNPOP_MAX_LINES_PER_BOX      8

typedef struct VnpopRect {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} VnpopRect;

static inline VnpopRect vnpop_rect_make(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    VnpopRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static inline VnpopRect vnpop_default_dialogue_rect(void) {
    return vnpop_rect_make(
        VNPOP_DEFAULT_DIALOGUE_X,
        VNPOP_DEFAULT_DIALOGUE_Y,
        VNPOP_DEFAULT_DIALOGUE_W,
        VNPOP_DEFAULT_DIALOGUE_H
    );
}

typedef struct VnpopCharBox {
    uint32_t codepoint;   /* UTF-32 / Unicode codepoint */
    VnpopRect screen_box; /* Screen coordinates (0..1280, 0..720) */
    float confidence;     /* Recognition confidence [0.0f .. 1.0f] */
} VnpopCharBox;

typedef struct VnpopTextLine {
    VnpopCharBox chars[VNPOP_MAX_CHARS_PER_LINE];
    uint16_t char_count;
    uint64_t dhash;
} VnpopTextLine;

typedef struct VnpopOcrResult {
    VnpopTextLine lines[VNPOP_MAX_LINES_PER_BOX];
    uint16_t line_count;
    uint64_t dhash;
    bool cached;          /* True if reused from 0ms perceptual hash cache */
} VnpopOcrResult;

#ifdef __cplusplus
}
#endif

#endif /* VNPOP_TYPES_H */
