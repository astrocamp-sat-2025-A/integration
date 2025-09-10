#include "pwm.h"

// 変数の定義
uint16_t wrap_value = 19999;
uint16_t level_cw_per = 4;  // NOTE: 目標duty比4%
uint16_t level_cw_per_ver2 = 7; // NOTE: 目標duty比10%
uint16_t level_ccw_per_ver2 = 8; // NOTE: 目標duty比10%
uint16_t level_ccw_per = 11; // NOTE: 目標duty比10%
uint16_t level_stop = 1500;
uint slice_num = 0;

//NOTE: PWMの初期化
void initialisePwm() {
    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(PWM_PIN);
    pwm_set_clkdiv(slice_num, 125.0f);
    pwm_set_wrap(slice_num, wrap_value);
}

//NOTE: 右回転
void pwm_cycle_right() {
    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_ccw_ver2 = (wrap_value + 1) * level_ccw_per_ver2 / 100;
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;


    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_cw_ver2);
    sleep_ms(1500);

    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_ccw);
    sleep_ms(1000);

    pwm_set_enabled(slice_num, false);
}

//NOTE: 左回転
void pwm_cycle_left() {

    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_ccw_ver2 = (wrap_value + 1) * level_ccw_per_ver2 / 100;
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;


    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_ccw_ver2);
    sleep_ms(2000);
    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_cw);
    sleep_ms(1000);

    pwm_set_enabled(slice_num, false);
}

void pwm_cycle_right_debug() {
    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_ccw_ver2 = (wrap_value + 1) * level_ccw_per_ver2 / 100;
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;

    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_ccw);
    sleep_ms(2000);

    pwm_set_enabled(slice_num, false);
}

void pwm_cycle_left_debug() {
    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_ccw_ver2 = (wrap_value + 1) * level_ccw_per_ver2 / 100;
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;

    pwm_set_enabled(slice_num, true);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, level_cw);
    sleep_ms(2000);

    pwm_set_enabled(slice_num, false);
}


void pwm_right_cycle_asiAngle(float angle) {
    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_ccw_ver2 = (wrap_value + 1) * level_ccw_per_ver2 / 100;
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;

    if(angle > 0) {
        //NOTE: 右回転, 1回の回転を15°とする
        for(int i = 0; (float)i < (angle / 15); i++) {
            pwm_cycle_right();
        }
    }else{
        for(int i = 0; (float)i < (angle / 15); i++) {
            pwm_cycle_left();
        }
    }
}
