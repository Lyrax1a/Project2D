#include "usurper_asset_codec.h"

static int length(const volatile uint8_t *input, unsigned size, unsigned *cursor,
                  unsigned base, unsigned capacity, unsigned *value) {
    *value = base;
    if (base != 15u) return 0;
    unsigned extra;
    do {
        if (*cursor >= size) return -1;
        extra = input[(*cursor)++];
        if (*value > capacity || extra > capacity - *value) return -1;
        *value += extra;
    } while (extra == 255u);
    return 0;
}
int us_lz4_decode(const volatile uint8_t *input, unsigned input_size,
                  volatile uint8_t *output, unsigned capacity, unsigned *produced) {
    unsigned cursor = 0, written = 0; *produced = 0;
    while (cursor < input_size) {
        unsigned token = input[cursor++], literals, match;
        if (length(input, input_size, &cursor, token >> 4, capacity, &literals) ||
            literals > input_size - cursor || literals > capacity - written) return -1;
        for (unsigned i = 0; i < literals; ++i) output[written++] = input[cursor++];
        if (cursor == input_size) { *produced = written; return 0; }
        if (input_size - cursor < 2u) return -1;
        unsigned offset = input[cursor] | (unsigned)input[cursor + 1u] << 8; cursor += 2u;
        if (!offset || offset > written ||
            length(input, input_size, &cursor, token & 15u, capacity, &match) ||
            match > capacity - written || capacity - written - match < 4u) return -1;
        match += 4u;
        for (unsigned i = 0; i < match; ++i) { output[written] = output[written - offset]; ++written; }
    }
    return -1;
}
