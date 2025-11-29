/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
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
#include "msp-osd.h"
#ifdef WFB_STATUS_LINK
#include "wfb_status_link.h"
#endif
#include "ui/ui.h"

#ifdef PLATFORM_DESKTOP
#include "sdl2_display.h"
#endif

#ifdef PLATFORM_ROCKCHIP
#include "src/drm_display.h"
#endif

static volatile int running = 1;

static void signal_handler(int sig)
{
    printf("\n[ MAIN ] Caught signal %d, exit ...\n", sig);
    running = 0;
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
            "Version: %s\nBuild from: %s\nGit hash: %s\n\n", GIT_TAG, GIT_BRANCH, GIT_HASH
    );
}

static void print_usage(const char* prog)
{
    printf("\n");
    printf("Usage: %s [--ip <address>] [--port <number>] [--help]\n", prog);
    printf("Options:\n");
    printf("  --ip <address>   Set the IP address to listen on (default: 0.0.0.0)\n");
    printf("  --port <number>  Set the port to listen for RTP stream (default: 5602)\n");
#ifdef WFB_STATUS_LINK
    printf("  --wfb            Set the port to listen for wfb-server link status (default: 8003)\n");
#endif
    printf("Defaults: --ip 0.0.0.0 --port 5602 --wfb 8003\n");
}

static void parse_args(int argc, char* argv[], struct config_t* config)
{
    static struct option long_options[] = {
            {"ip", required_argument, 0, 'i'},
            {"port", required_argument, 0, 'p'},
#ifdef WFB_STATUS_LINK
            {"wfb", required_argument, 0, 'w'},
#endif
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
#ifdef WFB_STATUS_LINK
        case 'w': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535 || config->port == port) {
                fprintf(stderr, "Invalid WFB port number: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            config->wfb_port = port;
        } break;
#endif
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

#ifdef PLATFORM_ROCKCHIP
    drm_init("/dev/dri/card0", &config);
#endif
#ifdef PLATFORM_DESKTOP
    if (sdl2_display_init(&config) < 0) {
        fprintf(stderr, "SDL2 display initialization failed\n");
        return -1;
    }
#endif

    rtp_receiver_start(&config);

    msp_osd_init(&config);

    ui_init();

    while (running) {
#ifdef PLATFORM_DESKTOP
        if (sdl2_display_poll() < 0) {
            signal_handler(SIGINT); // sdl2 should quit
            break;
        }
        usleep(1000); // Sleep for 1 ms prevent 100% CPU */
#endif

#ifdef PLATFORM_ROCKCHIP
        usleep(100000); // Sleep for 100 ms
#endif
    }

    msp_osd_stop();
    ui_deinit();
    rtp_receiver_stop();
#ifdef PLATFORM_ROCKCHIP
    drm_close();
#endif
#ifdef PLATFORM_DESKTOP
    sdl2_display_deinit();
#endif

    return 0;
}
