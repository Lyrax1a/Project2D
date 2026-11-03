#ifndef USURPER_PROFILE_CONSOLE_H
#define USURPER_PROFILE_CONSOLE_H
#include "usurper_profile.h"

#define US_PROFILE_REQUEST_MAGIC 0x31435055u /* UPC1 */
#define US_PROFILE_RESPONSE_MAGIC 0x31535055u /* UPS1 */
#define US_PROFILE_PACKET_BYTES 32u
enum us_profile_operation {US_PROFILE_EXIT,US_PROFILE_GET,US_PROFILE_PUT,US_PROFILE_SWITCH,US_PROFILE_RESET,US_PROFILE_INFO};

typedef struct {
    us_profile_store_t store;
    us_profile_rules_t rules;
    uint32_t initialized,active_slot;
} us_profile_console_state_t;

void usurper_profile_console(void);
unsigned usurper_profile_active_slot(void);

/* Testable finite command core. Uses the actual scene export/import API.
 * init snapshots current scene into normal only on first successful entry.
 * handle never performs UART or Flash I/O. */
int us_profile_console_enter(us_profile_console_state_t *state);
int us_profile_console_handle(us_profile_console_state_t *state,
                              const uint8_t request[US_PROFILE_PACKET_BYTES],
                              const void *payload,uint32_t payload_bytes,
                              uint8_t response[US_PROFILE_PACKET_BYTES],
                              uint8_t output[US_PROFILE_BYTES],uint32_t *output_bytes);
#endif
