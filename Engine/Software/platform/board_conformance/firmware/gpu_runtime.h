#ifndef TS2D_GPU_RUNTIME_H
#define TS2D_GPU_RUNTIME_H
#include <stdint.h>
void gpu_interrupt_init(void);
int gpu_wait_complete(void);
void gpu_wait_policy(unsigned polling);
void gpu_submission_begin(void);
unsigned gpu_submission_pending(void);
void gpu_set_background_work(void (*work)(void));
extern volatile uint32_t gpu_irq_count,gpu_timer_wakes;
extern uint64_t gpu_wait_cycles;
#endif
