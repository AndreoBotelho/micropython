/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
 * Copyright (c) 2021 Jonathan Hogg
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
// extmod/machine_adc.c via MICROPY_PY_MACHINE_ADC_INCLUDEFILE.

#include "py/mphal.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include "zephyr_device.h"


#define SEQUENCE_RESOLUTION 8 
#define SEQUENCE_SAMPLES 1
#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    const struct device *dev;
    struct adc_channel_cfg config;
    uint32_t channel_en;    
    uint8_t attenuation;    
    uint8_t sample_ns;    
    uint8_t resolution;
} machine_adc_obj_t;

static void mp_machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    const machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ADC(%s, channel=%u, resolution=%u)",
        self->dev->name,
        self->config.channel_id,
        self->resolution);
}

static void mp_machine_adc_init_helper(machine_adc_obj_t *self, size_t n_pos_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_atten, ARG_sample_ns, ARG_channel, ARG_resolution,};
    
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_atten, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },                      
        { MP_QSTR_sample_ns, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },              
        { MP_QSTR_channel, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },        
        { MP_QSTR_resolution, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },        
    };
    
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_pos_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    

    mp_int_t atten = args[ARG_atten].u_int;
    if (atten != -1) {
        self->attenuation = atten;
    }    
    
    mp_int_t ns = args[ARG_sample_ns].u_int;
    if (ns != -1) {
        self->sample_ns = ns;
    } 
    
    self->resolution = args[ARG_resolution].u_int;
    
    int channel = args[ARG_channel].u_int;
    self->channel_en = 1 << channel;
    

	self->config.gain = ADC_GAIN_1;
	self->config.reference = ADC_REF_INTERNAL;
	self->config.acquisition_time = ADC_ACQ_TIME_DEFAULT;
	self->config.channel_id = channel;
    
	if (adc_channel_setup(self->dev, &self->config) < 0) {
		mp_raise_ValueError(MP_ERROR_TEXT("Could not setup channel"));
	}
}

static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_pos_args, size_t n_kw_args, const mp_obj_t *args) {
    mp_arg_check_num(n_pos_args, n_kw_args, 1, MP_OBJ_FUN_ARGS_MAX, true);
    
    const struct device *dev = zephyr_device_find(args[0]);
    
    machine_adc_obj_t *self = mp_obj_malloc(machine_adc_obj_t, &machine_adc_type);
    self->dev = dev;
    self->attenuation = 0;
    self->sample_ns = 0;
    self->channel_en = 1;
    self->resolution = 12;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw_args, args + n_pos_args);
    mp_machine_adc_init_helper(self, n_pos_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t mp_machine_adc_block(machine_adc_obj_t *self) {
    return mp_const_none;
}

static mp_int_t mp_machine_adc_read(machine_adc_obj_t *self) {
	int16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
		.calibrate = true,
		.channels = self->channel_en,
		.resolution = self->resolution,
	};
	if (adc_read(self->dev, &sequence) < 0) {
		mp_raise_ValueError(MP_ERROR_TEXT("Could not read ADC"));
	}
    return buf;
}

static mp_int_t mp_machine_adc_read_u16(machine_adc_obj_t *self) {
    mp_uint_t raw = mp_machine_adc_read(self);
    //Scale raw reading to 16 bit value using a Taylor expansion (for 8 <= bits <= 16)
    mp_uint_t u16 = (16 - self->resolution) | raw >> (2 * self->resolution - 16);
    return u16;
}

static mp_int_t mp_machine_adc_read_uv(machine_adc_obj_t *self) {
	mp_uint_t raw = mp_machine_adc_read(self);
	if (adc_raw_to_millivolts(3300,ADC_GAIN_1,self->resolution, &raw) < 0) {
		mp_raise_ValueError(MP_ERROR_TEXT("(value in mV not available)"));
	} 
    return raw*1000;
}
