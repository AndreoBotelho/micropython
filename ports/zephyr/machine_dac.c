/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
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


#include <stdio.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/sys/printk.h>
#include "zephyr_device.h"

#if MICROPY_PY_MACHINE_DAC

typedef struct _mdac_obj_t {
    mp_obj_base_t base;
    const struct device *dev;
    struct dac_channel_cfg config;
} mdac_obj_t;

static void mdac_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    const mdac_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "DAC(%s, channel=%u, resolution=%u buffered=%u)",
        self->dev->name,
        self->config.channel_id,
        self->config.resolution,
        self->config.buffered);
}

static mp_obj_t mdac_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_channel, ARG_resolution, ARG_buffer,};
    
    static const mp_arg_t allowed_args[] = {
    	{ MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_channel, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },        
        { MP_QSTR_resolution, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },  
        { MP_QSTR_buffer, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },             
    };
    
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    
    const struct device *dev = zephyr_device_find(args[ARG_id].u_obj);

    mdac_obj_t *self = mp_obj_malloc(mdac_obj_t, &machine_dac_type);
    
    self->dev = dev;
	self->config.channel_id  = args[ARG_channel].u_int;
	self->config.resolution  = args[ARG_resolution].u_int;
	self->config.buffered = args[ARG_buffer].u_bool;

	if (dac_channel_setup(self->dev, &self->config) == 0) {
		return MP_OBJ_FROM_PTR(self);
	}
	mp_raise_ValueError(MP_ERROR_TEXT("Setting up of DAC failed"));
	
}

static mp_obj_t mdac_write(mp_obj_t self_in, mp_obj_t value_in) {
    mdac_obj_t *self = self_in;
    int value = mp_obj_get_int(value_in);
    if (value < 0 || value > (1 << self->config.resolution)-1) {
        mp_raise_ValueError(MP_ERROR_TEXT("value out of range"));
    }
	if (dac_write_value(self->dev, self->config.channel_id, value) == 0) {
		return mp_const_none;
	}
	mp_raise_ValueError(MP_ERROR_TEXT("DAC write failed"));
}

MP_DEFINE_CONST_FUN_OBJ_2(mdac_write_obj, mdac_write);

static const mp_rom_map_elem_t mdac_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mdac_write_obj) },
};

static MP_DEFINE_CONST_DICT(mdac_locals_dict, mdac_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_dac_type,
    MP_QSTR_DAC,
    MP_TYPE_FLAG_NONE,
    make_new, mdac_make_new,
    print, mdac_print,
    locals_dict, &mdac_locals_dict
    );

#endif // MICROPY_PY_MACHINE_DAC
