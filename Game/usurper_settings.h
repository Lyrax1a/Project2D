#ifndef USURPER_SETTINGS_H
#define USURPER_SETTINGS_H
#include <stdint.h>
/* Original GameState defaults: master .8, music .5, effects .8. The board
 * exposes music/effect levels; master remains the original fixed .8. */
typedef struct { uint32_t music_volume,sfx_volume,flags,control_mode; } usurper_settings_t;
#define US_SETTINGS_MUSIC 1u
#define US_SETTINGS_SFX 2u
#define US_SETTINGS_SHAKE 4u
#define US_SETTINGS_DEFAULT {50u,80u,7u,0u}
static inline int us_settings_valid(const usurper_settings_t *v) {
    return v&&v->music_volume<=100u&&v->sfx_volume<=100u&&v->flags<=7u&&v->control_mode<=2u;
}
#endif
