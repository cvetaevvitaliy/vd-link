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
#include "ui_interface.h"
#include "msp-osd.h"
#include "wfb_status_link.h"
#include "log.h"
#include "link.h"

static const char *module_name_str = "MAIN";
#define MAIN_DEBUG 0

static volatile int running = 1;
static volatile int cleanup_done = 0;

static void signal_handler(int sig)
{
    INFO("Caught signal %d, exit ...", sig);

    running = 0;
    
    // Prevent multiple cleanup calls
    if (cleanup_done) {
        ERROR("Force exit");
        exit(1);
    }
    cleanup_done = 1;
    
    msp_osd_stop();
    rtp_receiver_stop();
    ui_interface_deinit();
    drm_close();

    INFO("Cleanup completed, exiting");
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
                ERROR("Invalid port number: %s", optarg);
                exit(EXIT_FAILURE);
            }
            config->port = port;
        } break;
        case 'w': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535 || config->port == port) {
                ERROR("Invalid WFB port number: %s", optarg);
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

void wfb_status_link_callback(const wfb_rx_status *st)
{
    ui_update_wfb_ng_telemetry(st);
    osd_wfb_status_link_callback(st);
}

void link_sys_telemetry_cb(float cpu_temp, float cpu_usage)
{
    INFO_M("CPU Temp: %.2f C, CPU Usage: %.2f%%", module_name_str, cpu_temp, cpu_usage);
    // msp_osd_update_sys_telemetry(cpu_temp, cpu_usage);
}

void msp_osd_detection_rx_callback(const link_detection_box_t* data, size_t count)
{
    if (data == NULL || count == 0) {
        INFO_M("No detection results received", module_name_str);
        return;
    }

    INFO_M("Received %d detection results", module_name_str, count);
    // msp_osd_update_detection_results(data);
}

void msp_osd_displayport_rx_callback(const char* data, size_t size)
{
    if (data == NULL || size == 0) {
        INFO_M("No displayport data received", module_name_str);
        return;
    }

    INFO_M("Received displayport data of size %zu %s", module_name_str, size, data);
    // msp_osd_update_displayport_data(data, size);
}

void msp_osd_cmd_rx_callback(const link_command_pkt_t* cmd)
{
    if (cmd == NULL) {
        INFO_M("No command received", module_name_str);
        return;
    }

    INFO_M("Received command with ID %u", module_name_str, cmd->cmd_id);
    // msp_osd_handle_command(cmd);
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
    lvgl_create_ui();

    msp_osd_init(&config);

    wfb_status_link_start(config.ip, config.wfb_port, wfb_status_link_callback);

    rtp_receiver_start(&config);

    if (link_init(LINK_PORT+1, LINK_PORT) != 0) {
        ERROR("Failed to initialize link module");
    } else {
        link_register_sys_telemetry_rx_cb(link_sys_telemetry_cb);
        link_register_detection_rx_cb(msp_osd_detection_rx_callback);
        link_register_displayport_rx_cb(msp_osd_displayport_rx_callback);
        link_register_cmd_rx_cb(msp_osd_cmd_rx_callback);
        INFO("Link module initialized successfully");
    }

    while (running) {
        // Update LVGL UI and compositor
        ui_interface_update();

        // Sleep for optimal performance with compositor
        usleep(16000); // ~60 FPS
    }

     wfb_status_link_stop();

    return 0;
}
