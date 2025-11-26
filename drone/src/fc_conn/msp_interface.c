/**
 * @file msp_interface.c this is part of project 'msp'
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

#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>
#include "msp_interface.h"

#define DEFAULT_POOL_TIMEOUT (500) //ms
msp_port_t msp_port;

static int open_serial_port(msp_interface_t* dev)
{
    if (!dev || !dev->uart_name) {
        return MSP_INTERFACE_ERROR_UART;
    }
    printf("Opening serial port %s at %u baud...\n", dev->uart_name, dev->baud_rate);

    dev->uart_fd = open(dev->uart_name, O_RDWR | O_NOCTTY | O_FSYNC);

    if (dev->uart_fd == -1) {
        perror(dev->uart_name);
        return MSP_INTERFACE_ERROR_UART;
    }

    // Flush away any bytes previously read or written.
    int result = tcflush(dev->uart_fd, TCIOFLUSH);
    if (result) {
        perror("tcflush failed"); // just a warning, not a fatal error
    }

    // Get the current configuration of the serial port.
    struct termios options;
    result = tcgetattr(dev->uart_fd, &options);
    if (result) {
        perror("tcgetattr failed");
        close(dev->uart_fd);
        return MSP_INTERFACE_ERROR_UART;
    }

    // Turn off any options that might interfere with our ability to send and
    // receive raw binary bytes.
    options.c_iflag &= ~(INLCR | IGNCR | ICRNL | IXON | IXOFF);
    options.c_oflag &= ~(ONLCR | OCRNL);
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    // Set up timeouts: Calls to read() will return as soon as there is
    // at least one byte available or when 100 ms has passed.
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 0;

    // This code only supports certain standard baud rates. Supporting
    // non-standard baud rates should be possible but takes more work.
    switch (dev->baud_rate) {
    case 4800:
        cfsetospeed(&options, B4800);
        break;
    case 9600:
        cfsetospeed(&options, B9600);
        break;
    case 19200:
        cfsetospeed(&options, B19200);
        break;
    case 38400:
        cfsetospeed(&options, B38400);
        break;
    case 115200:
        cfsetospeed(&options, B115200);
        break;
    case 576000:
        cfsetospeed(&options, B576000);
        break;
    case 1000000:
        cfsetospeed(&options, B1000000);
        break;
    default:
        printf("warning: baud rate %u is not supported, using 112500.\n", dev->baud_rate);
        cfsetospeed(&options, B115200);
        break;
    }
    cfsetispeed(&options, cfgetospeed(&options));

    result = tcsetattr(dev->uart_fd, TCSANOW, &options);
    if (result) {
        perror("tcsetattr failed");
        close(dev->uart_fd);
        return MSP_INTERFACE_ERROR_UART;
    }

    return MSP_INTERFACE_OK;
}

int msp_interface_init(msp_interface_t* dev)
{
    if (open_serial_port(dev) != MSP_INTERFACE_OK) {
        return MSP_INTERFACE_ERROR_UART;
    }
    msp_interface_write(dev, "\0", 1); // just to open the port

    msp_port.callback = dev->cb;

    return MSP_INTERFACE_OK;
}

int msp_interface_write(msp_interface_t* dev, uint8_t* buff, int len)
{
    //printf("[MSP] Writing %d bytes to FC (%s)\n", len, buff);
    ssize_t result = write(dev->uart_fd, buff, len);
    if (result != (ssize_t)len) {
        perror("failed to write to port");
        return MSP_INTERFACE_ERROR_TX;
    }

    dev->poll_fd.fd = dev->uart_fd;
    dev->poll_fd.events = POLLIN;

    return MSP_INTERFACE_OK;
}

int msp_interface_read(msp_interface_t* dev, const volatile bool* catch)
{
    int n = poll(&dev->poll_fd, 1, DEFAULT_POOL_TIMEOUT);

    if (n > 0) {
        while (1) {
            uint8_t c = 0;
            if (*catch == false) {
                return MSP_INTERFACE_OK;
            }
            ssize_t r = read(dev->uart_fd, &c, 1);
            if (r < 0) {
                perror("failed to read from port");
                return MSP_INTERFACE_ERROR_RX;
            }
            if (r == 0) {
                break;
            }
            msp_process_received_data(&msp_port, c);
        }
    } else {
        printf("[MSP] Timeout: Flight Controller not response\n");
        return MSP_INTERFACE_RX_TIME_OUT; // TIMEOUT pool
    }

    return MSP_INTERFACE_OK;
}

void msp_interface_deinit(msp_interface_t* dev)
{
    if (dev) {
        if (dev->uart_fd > 0) {
            close(dev->uart_fd);
        }
    }
}
