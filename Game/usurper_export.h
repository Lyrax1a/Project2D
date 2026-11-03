/* SPDX-License-Identifier: MIT */
#ifndef USURPER_EXPORT_H
#define USURPER_EXPORT_H
#include <stdint.h>
#define US_EXPORT_BASE_BAUD 115200u
#define US_EXPORT_BAUD 460800u
#define US_EXPORT_TIMEOUT_MS 5000u
#define US_EXPORT_SETTLE_MS 50u
enum {
    US_EXPORT_OK=0, US_EXPORT_ARGUMENT=-1, US_EXPORT_TOKEN_TIMEOUT=-2,
    US_EXPORT_TOKEN_INVALID=-3, US_EXPORT_SYNC_TIMEOUT=-4,
    US_EXPORT_SYNC_INVALID=-5, US_EXPORT_ACK_TIMEOUT=-6,
    US_EXPORT_ACK_INVALID=-7, US_EXPORT_HOST_REJECTED=-8,
    US_EXPORT_STATE=-9
};
/* IO injection is for isolated host verification; firmware uses the wrappers.
 * ticks is monotonic at hz; flush drains the complete software TX queue into
 * hardware, service makes progress, set_baud changes the actual UART divisor.
 * Hardware TX FIFO capacity must be <=255 bytes (8-bit availability register).
 */
typedef struct {
    void *context;
    uint64_t (*ticks)(void *context);
    int (*receive)(void *context);
    void (*transmit)(void *context,uint8_t value);
    void (*flush)(void *context);
    void (*service)(void *context);
    void (*set_baud)(void *context,uint32_t baud);
    uint32_t hz;
} us_export_io_t;
typedef struct {
    const us_export_io_t *io;
    uint32_t baud;
    unsigned active;
    int error;
} us_export_t;
int us_export_begin(us_export_t *session,const us_export_io_t *io);
int us_export_finish(us_export_t *session);
/* Caller advertises export_baud=460800 and ends/flushed its ASCII header first.
 * On begin failure, do not send binary. On success send unchanged binary+footer,
 * then finish. No measurements, command streams or CRCs are changed here.
 * These wrappers are blocking and must only run after measurement has ended.
 */
int usurper_export_begin(void);
int usurper_export_finish(void);
#endif
