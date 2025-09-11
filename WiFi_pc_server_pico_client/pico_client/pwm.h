#ifndef PWM_H
#define PWM_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "stdio.h"

#define PWM_PIN 11

extern uint16_t wrap_value;
extern uint16_t level_cw_per;
extern uint16_t level_cw_per_ver2;
extern uint16_t level_stop_per;
extern uint16_t level_ccw_per;
extern uint slice_num;

void initialisePwm();
void pwm_cycle_right();
void pwm_cycle_left();
void pwm_cycle_stop();

void pwm_cycle_right_debug();
void pwm_cycle_left_debug();


typedef enum {
    RIGHT,
    LEFT
} type_right_or_left;

void pwm_cycle_by_angle(float angle, type_right_or_left right_or_left);

#endif
