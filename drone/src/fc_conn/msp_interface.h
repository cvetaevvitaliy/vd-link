/**
 * @file msp_interface.h this is part of project 'msp'
 * Copyright © 2020-2023
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Created vitalii.nimych@gmail.com 12-06-2023
 */

#ifndef MSP_MSP_INTERFACE_H
#define MSP_MSP_INTERFACE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <sys/poll.h>
#include "msp.h"
#include <stdbool.h>

enum msp_state {
    MSP_INTERFACE_ERROR_UART = -3,
    MSP_INTERFACE_ERROR_TX = -2,
    MSP_INTERFACE_ERROR_RX = -1,
    MSP_INTERFACE_OK = 0,
    MSP_INTERFACE_RX_TIME_OUT,
};

typedef struct {
    const char* uart_name;
    uint32_t baud_rate;
    int uart_fd;
    struct pollfd poll_fd;
    int telemetry_update;
    msp_msg_callback cb;
    msp_state_t msp_state;

} msp_interface_t;

int msp_interface_init(msp_interface_t* dev);

int msp_interface_write(msp_interface_t* dev, uint8_t* buff, int len);

int msp_interface_read(msp_interface_t* dev, const volatile bool* catch);

void msp_interface_deinit(msp_interface_t* dev);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif //MSP_MSP_INTERFACE_H
