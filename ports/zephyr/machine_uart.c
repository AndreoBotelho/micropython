/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 * Copyright (c) 2020 Yonatan Schachter
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
// extmod/machine_uart.c via MICROPY_PY_MACHINE_UART_INCLUDEFILE.

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <stdlib.h>

#include "py/mperrno.h"
#include "zephyr_device.h"

// The UART class doesn't have any constants for this port.
#define MICROPY_PY_MACHINE_UART_CLASS_CONSTANTS

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    const struct device *dev;
    struct ring_buf rx_ring_buf;
    struct ring_buf tx_ring_buf;
    uint16_t rxbuf;          
    uint16_t txbuf;      
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t timeout_char;  // timeout waiting between chars (in ms)
} machine_uart_obj_t;


static void uart_cb(const struct device *dev, void *ctx)
{
	machine_uart_obj_t *self = (machine_uart_obj_t * )ctx;
	uint8_t *buf;
	uint32_t len;
	uint8_t c;
	
	if (!uart_irq_update(dev)) {
		return;
	}
	
	if (uart_irq_rx_ready(dev)) {
		len = ring_buf_put_claim(&self->rx_ring_buf, &buf, self->rxbuf);
		if (len != 0) {
			len = uart_fifo_read(dev, buf, len);
			ring_buf_put_finish(&self->rx_ring_buf, len);
		}else{
			uart_irq_rx_disable(dev);
			while (uart_fifo_read(dev, &c, 1) == 1) {} //discard received bytes
		}
	}
	
	if (uart_irq_tx_ready(dev)) {
		len = ring_buf_get_claim(&self->tx_ring_buf, &buf, self->txbuf);
		if (len != 0) {
			len = uart_fifo_fill(dev, buf, len);
			if (len > 0) {
				ring_buf_get_finish(&self->tx_ring_buf, len);
			}
		}else{
			uart_irq_tx_disable(dev);
		}
		
	}
}

static const char *_parity_name[] = {"None", "Odd", "Even", "Mark", "Space"};
static const char *_stop_bits_name[] = {"0.5", "1", "1.5", "2"};
static const char *_data_bits_name[] = {"5", "6", "7", "8", "9"};
static const char *_flow_control_name[] = {"None", "RTS/CTS", "DTR/DSR"};

static void mp_machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    struct uart_config config;
    uart_config_get(self->dev, &config);
    mp_printf(print, "UART(\"%s\", baudrate=%u, data_bits=%s, parity=%s, stop=%s, flow_control=%s, tx_buf=%u, rx_buf=%u, timeout=%u, timeout_char=%u)",
        self->dev->name, config.baudrate, _data_bits_name[config.data_bits],
        _parity_name[config.parity], _stop_bits_name[config.stop_bits], _flow_control_name[config.flow_ctrl],
        self->txbuf, self->rxbuf, self->timeout, self->timeout_char);
}

static void mp_machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_txbuf, ARG_rxbuf, ARG_timeout, ARG_timeout_char };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 115200} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_txbuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 128} },        
        { MP_QSTR_rxbuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 128} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    struct uart_config uart_cfg = {
		.baudrate = args[ARG_baudrate].u_int,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
		.data_bits = UART_CFG_DATA_BITS_8
	};
	   // set data bits
    switch (args[ARG_bits].u_int) {
        case 0:
            break;
        case 5:
            uart_cfg.data_bits = UART_CFG_DATA_BITS_5;
            break;
        case 6:
            uart_cfg.data_bits = UART_CFG_DATA_BITS_6;
            break;
        case 7:
            uart_cfg.data_bits = UART_CFG_DATA_BITS_7;
            break;
        case 8:
            uart_cfg.data_bits = UART_CFG_DATA_BITS_8;
            break;        
        case 9:
            uart_cfg.data_bits = UART_CFG_DATA_BITS_9;
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid data bits"));
            break;
    }
    
    // set stop bits
    switch (args[ARG_stop].u_int) {
        case 0:
            break;
        case 1:
            uart_cfg.stop_bits = UART_CFG_STOP_BITS_1;
            break;
        case 2:
            uart_cfg.stop_bits = UART_CFG_STOP_BITS_2;
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("invalid stop bits"));
            break;
    }
	
	 // set parity
    if (args[ARG_parity].u_obj != MP_OBJ_NULL) {
        if (args[ARG_parity].u_obj == mp_const_none) {
            uart_cfg.parity = UART_CFG_PARITY_NONE;
        } else {
            mp_int_t parity = mp_obj_get_int(args[ARG_parity].u_obj);
            if (parity & 1) {
                uart_cfg.parity = UART_CFG_PARITY_ODD;
            } else {
                uart_cfg.parity = UART_CFG_PARITY_EVEN;
            }
        }
    }
    
	if (uart_configure(self->dev, &uart_cfg) != 0) {
		mp_raise_ValueError(MP_ERROR_TEXT("Could not configure device"));
	}
	
    self->timeout = args[ARG_timeout].u_int;
    self->timeout_char = args[ARG_timeout_char].u_int;
    
    self->txbuf = args[ARG_txbuf].u_int;
	uint8_t * tx_buf = (uint8_t *)malloc(self->txbuf);
	
	if (tx_buf != NULL){
		ring_buf_init(&self->tx_ring_buf, self->txbuf, tx_buf);
	}else{
		mp_raise_ValueError(MP_ERROR_TEXT("Could not alloc tx buffer"));
	}
	    
	self->rxbuf = args[ARG_rxbuf].u_int;
	uint8_t * rx_buf = (uint8_t *)malloc(self->rxbuf);
	
	if (rx_buf != NULL){
		ring_buf_init(&self->rx_ring_buf, self->rxbuf, rx_buf);
	}else{
		mp_raise_ValueError(MP_ERROR_TEXT("Could not alloc rx buffer"));
	}
    
	uart_irq_callback_user_data_set(self->dev, uart_cb, (void *)self);
	
	uart_irq_rx_enable(self->dev);
}

static mp_obj_t mp_machine_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    machine_uart_obj_t *self = mp_obj_malloc(machine_uart_obj_t, &machine_uart_type);
    self->dev = zephyr_device_find(args[0]);

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_uart_deinit(machine_uart_obj_t *self) {
    uart_irq_rx_disable(self->dev);      
    uart_irq_tx_disable(self->dev);
    (void)self;
}

static mp_int_t mp_machine_uart_any(machine_uart_obj_t *self) {
    return ring_buf_size_get(&self->rx_ring_buf);
}

static bool mp_machine_uart_txdone(machine_uart_obj_t *self) {
    return uart_irq_tx_complete(self->dev);
}

static mp_uint_t mp_machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    //check valid data
    int rdata = ring_buf_size_get(&self->rx_ring_buf);
    // make sure we want at least 1 char
    if( rdata == 0 || size == 0 ) {
    	return 0;
    }
    if (size > rdata){
    	size = rdata;
    }
    mp_uint_t bytes_read = 0;
    uart_irq_rx_disable(self->dev);
	bytes_read = ring_buf_get(&self->rx_ring_buf, (uint8_t *)buf_in, size);
    uart_irq_rx_enable(self->dev);
    return bytes_read;
}

static mp_uint_t mp_machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uart_irq_tx_disable(self->dev);
    size = ring_buf_put(&self->tx_ring_buf, (uint8_t *)buf_in, size);
    uart_irq_tx_enable(self->dev);
    return size;
}

static mp_uint_t mp_machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    mp_uint_t ret;

    if (request == MP_STREAM_POLL) {
        ret = 0;
        // read is always blocking

        if (arg & MP_STREAM_POLL_WR) {
            ret |= MP_STREAM_POLL_WR;
        }
        return ret;
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}
