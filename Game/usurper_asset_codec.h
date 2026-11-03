#ifndef USURPER_ASSET_CODEC_H
#define USURPER_ASSET_CODEC_H
#include <stdint.h>
int us_lz4_decode(const volatile uint8_t *input, unsigned input_size,
                  volatile uint8_t *output, unsigned capacity, unsigned *produced);
#endif
