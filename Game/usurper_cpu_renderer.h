#ifndef USURPER_CPU_RENDERER_H
#define USURPER_CPU_RENDERER_H

#include "../Engine/Software/preboard_core/sw/include/ts2d_preboard.h"

/* Explicitly register initialized surfaces in the reserved exhibition-source
 * arena. Neither telemetry, decoder workspace nor upload memory is accepted. */
int usurper_cpu_register_source(uint32_t base, unsigned width, unsigned height,
                               unsigned stride_bytes);
void usurper_cpu_clear_sources(void);
/* Separately registered immutable Warden pixel subsection, after CRC/format
 * validation. Texture headers and relocated geometry-cache memory are excluded.
 * Clearing exhibition sources leaves this attachment intact. */
int usurper_cpu_register_packed_source(uint32_t base,uint32_t bytes);
void usurper_cpu_clear_packed_source(void);

/* Resolve and validate one command without reading or writing pixel memory.
 * NOP / END resolve to empty surfaces. Validation uses the actually accessed
 * rectangle after destination clipping, including both flip flags. */
int usurper_cpu_resolve(uint32_t framebuffer, unsigned atlas_height,
                        const ts2d_command_t *command,
                        ts2d_surface_t *destination, ts2d_surface_t *source);

/* Every command resolves its own physical source. Each batch is prevalidated
 * in full before any writes; END terminates the batch as in the fixed ISA. */
int usurper_cpu_execute(uint32_t framebuffer, unsigned atlas_height,
                        const ts2d_command_t *commands, size_t count);

#endif
