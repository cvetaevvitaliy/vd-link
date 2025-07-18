/**
 * @file main.c this is part of project 'vd-link'
 * Copyright © vitalii.nimych@gmail.com 2025
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
 * Created vitalii.nimych@gmail.com 30-06-2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include "src/rtp_receiver.h"
#include "src/common.h"
#include "src/drm_display.h"
#include "src/ui_interface/interface_lvgl.h"
#include "msp-osd.h"
#include "wfb_status_link.h"

static volatile int running = 1;
static volatile int cleanup_done = 0;

static void signal_handler(int sig)
{
    printf("\n[ MAIN ] Caught signal %d, exit ...\n", sig);
    running = 0;
    
    // Prevent multiple cleanup calls
    if (cleanup_done) {
        printf("[ MAIN ] Force exit\n");
        exit(1);
    }
    cleanup_done = 1;
    
    msp_osd_stop();
    rtp_receiver_stop();
    ui_interface_deinit();
    drm_close();
    
    printf("[ MAIN ] Cleanup completed, exiting\n");
    exit(0);
}

static void setup_signals(void)
{
    struct sigaction sa = { .sa_handler = signal_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);
}

static void print_banner(void)
{
    printf(
            "\n"
            " ██╗   ██╗██████╗       ██╗     ██╗███╗   ██╗██╗  ██╗\n"
            " ██║   ██║██╔══██╗      ██║     ██║████╗  ██║██║ ██╔╝\n"
            " ██║   ██║██║  ██║█████╗██║     ██║██╔██╗ ██║█████╔╝ \n"
            " ╚██╗ ██╔╝██║  ██║╚════╝██║     ██║██║╚██╗██║██╔═██╗ \n"
            "  ╚████╔╝ ██████╔╝      ███████╗██║██║ ╚████║██║  ██╗\n"
            "   ╚═══╝  ╚═════╝       ╚══════╝╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝\n"
            "\n"
    );
}

static void print_usage(const char* prog)
{
    printf("\n");
    printf("Usage: %s [--ip <address>] [--port <number>] [--help]\n", prog);
    printf("Options:\n");
    printf("  --ip <address>   Set the IP address to listen on (default: 0.0.0.0)\n");
    printf("  --port <number>  Set the port to listen for RTP stream (default: 5602)\n");
    printf("  --wfb            Set the port to listen for wfb-server link status (default: 8003)\n");
    printf("Defaults: --ip 0.0.0.0 --port 5602 --wfb 8003\n");
}

static void parse_args(int argc, char* argv[], struct config_t* config)
{
    static struct option long_options[] = {
            {"ip", required_argument, 0, 'i'},
            {"port", required_argument, 0, 'p'},
            {"wfb", required_argument, 0, 'w'},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:v:w:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'i':
            config->ip = optarg;
            break;
        case 'p': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535 || config->wfb_port == port) {
                fprintf(stderr, "Invalid port number: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            config->port = port;
        } break;
        case 'w': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535 || config->port == port) {
                fprintf(stderr, "Invalid WFB port number: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            config->wfb_port = port;
        } break;
        case 'h':
        default:
            print_usage(argv[0]);
            exit(0);
        }
    }
}

int main(int argc, char* argv[])
{
    struct config_t config = {
        .ip = "0.0.0.0",
        .port = 5602,
        .wfb_port = 8003,
        .pt = 0,
        .codec = CODEC_UNKNOWN,
    };

    print_banner();
    parse_args(argc, argv, &config);

    setup_signals();

    drm_init("/dev/dri/card0", &config);
    
    // Initialize LVGL UI system
    ui_interface_init();
    
    // Create test UI to verify overlay
    lvgl_create_test_ui();

    msp_osd_init(&config);

    rtp_receiver_start(&config);

    while (running) {
        // Update LVGL UI and compositor
        ui_interface_update();

        // Sleep for optimal performance with compositor
        usleep(16000); // ~60 FPS
    }

    return 0;
}
