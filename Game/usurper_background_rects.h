/* Exact complement of an opaque copy rectangle within a pixel viewport. */
#ifndef USURPER_BACKGROUND_RECTS_H
#define USURPER_BACKGROUND_RECTS_H
#include <stdint.h>

typedef struct { unsigned x, y, width, height; } us_background_rect_t;

static inline unsigned us_background_uncovered(unsigned vw, unsigned vh,
    unsigned x, unsigned y, unsigned width, unsigned height,
    us_background_rect_t output[4]) {
    if (!vw || !vh) return 0;
    if (!width || !height || x >= vw || y >= vh) {
        output[0] = (us_background_rect_t){0, 0, vw, vh};
        return 1;
    }
    /* Subtract before adding, so arbitrary unsigned dimensions cannot wrap. */
    unsigned right = x + (width < vw - x ? width : vw - x);
    unsigned bottom = y + (height < vh - y ? height : vh - y);
    unsigned n = 0;
    if (y) output[n++] = (us_background_rect_t){0, 0, vw, y};
    if (bottom < vh) output[n++] = (us_background_rect_t){0, bottom, vw, vh - bottom};
    if (x) output[n++] = (us_background_rect_t){0, y, x, bottom - y};
    if (right < vw) output[n++] = (us_background_rect_t){right, y, vw - right, bottom - y};
    return n;
}
#endif
