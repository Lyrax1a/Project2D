#ifndef USURPER_AUDIO_H
#define USURPER_AUDIO_H
#include <stdint.h>
#include "usurper_settings.h"
enum {US_AUDIO_CLICK=1,US_AUDIO_SHOT,US_AUDIO_HIT,US_AUDIO_KILL,US_AUDIO_HURT,
      US_AUDIO_CAPTURE,US_AUDIO_RELEASE,US_AUDIO_RESEARCH,US_AUDIO_WIN,US_AUDIO_LOSS,
      US_AUDIO_WARNING,US_AUDIO_PICKUP,US_AUDIO_ARMOR,US_AUDIO_BOSS_PHASE,
      US_AUDIO_RAIL,US_AUDIO_MUSIC_FIRST=16};
typedef struct {uint32_t available,requested,queued,suppressed,software_dropped,submitted,hardware_dropped,samples;} usurper_audio_stats_t;
#if defined(USURPER_AUDIO_ENABLED) || defined(USURPER_AUDIO_IMPLEMENTATION)
void usurper_audio_init(void);
void usurper_audio_service(void);
void usurper_audio_configure(const usurper_settings_t *settings);
void usurper_audio_event(unsigned event);
void usurper_audio_tick(unsigned phase);
void usurper_audio_mute(unsigned muted);
const usurper_audio_stats_t *usurper_audio_stats(void);
#else
static inline void usurper_audio_configure(const usurper_settings_t *v){(void)v;}
static inline void usurper_audio_event(unsigned event){(void)event;}
static inline void usurper_audio_tick(unsigned phase){(void)phase;}
#endif
#endif
