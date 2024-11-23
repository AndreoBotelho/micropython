/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// This file is never compiled standalone, it's included directly from
// extmod/machine_pwm.c via MICROPY_PY_MACHINE_PWM_INCLUDEFILE.

#include "py/mphal.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include "zephyr_device.h"

typedef struct _machine_pwm_obj_t {
    mp_obj_base_t base;
    const struct device *dev;
    uint8_t channel;    
    uint8_t active;
    uint32_t period;
    uint32_t pulse;
} machine_pwm_obj_t;

static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty);

/******************************************************************************/
// MicroPython bindings for PWM

static void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(%u)", self->channel);
}

static void mp_machine_pwm_init_helper(machine_pwm_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_ch, ARG_freq, ARG_duty_u16, ARG_duty_ns };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_ch, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty_u16, MP_ARG_INT, {.u_int = -1} },        
        { MP_QSTR_duty_ns, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    
    self->channel = args[ARG_ch].u_int;
    if (self->channel == 255) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM not supported on channel %d"), self->channel);
    }
    
    if (args[ARG_freq].u_int != -1) {
        self->period = 1000000000 / args[ARG_freq].u_int;
    }
    
    if (args[ARG_duty_u16].u_int != -1) {
        self->pulse = (uint32_t)((args[ARG_duty_u16].u_int / 65536.0f) * self->period);
    }
    
    if (args[ARG_duty_ns].u_int != -1) {
        self->pulse= args[ARG_duty_ns].u_int;
    }
    
    if (pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL) != 0){
    	mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM init error"));
    	return;
    }
    
    self->active = 1;
}

static mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    
    const struct device *dev = zephyr_device_find(args[0]);
    
    // create PWM object from the given device
    machine_pwm_obj_t *self = mp_obj_malloc(machine_pwm_obj_t, &machine_pwm_type);
    self->dev = dev ;   
    self->channel = -1;
    self->active = 0;
    self->period = 1000;
    self->pulse = 500;

    // start the PWM running for this channel
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_pwm_init_helper(self, n_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
    if (self->active) {
    	pwm_set(self->dev, self->channel, self->period, 0, 0);
    	self->active = 0;
    }
}

static mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT((1000000000/self->period));
}

static void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq) {
    float p = (float)self->pulse / (float)self->period;
    self->period = 1000000000 / freq;
    self->pulse = self->period * p;
    if (pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL) != 0){
    	mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM freq error"));
    }
}

static void set_active(machine_pwm_obj_t *self, bool set_pin) {
    if (!self->active) {
        pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL);
        self->active = 1;
    }
}

#if MICROPY_PY_MACHINE_PWM_DUTY

static mp_obj_t mp_machine_pwm_duty_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(self->pulse);
}

static void mp_machine_pwm_duty_set(machine_pwm_obj_t *self, mp_int_t duty) {
    self->pulse = duty;
    if (pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL) != 0){
    	mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM duty error"));
    }
}

#endif
static mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    set_active(self, true);
    return MP_OBJ_NEW_SMALL_INT((uint16_t)((self->pulse * 65536.0f)/self->period));
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty) {
    set_active(self, false);
    self->pulse = (uint32_t)((duty / 65536.0f) * self->period);
    if (pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL) != 0){
    	mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM duty error"));
    }
}

static mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    set_active(self, true);
    return MP_OBJ_NEW_SMALL_INT(self->pulse);
}

static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty) {
    set_active(self, false);
    self->pulse = duty;
    if (pwm_set(self->dev, self->channel, self->period, self->pulse, PWM_POLARITY_NORMAL) != 0){
    	mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("PWM duty error"));
    }
}
