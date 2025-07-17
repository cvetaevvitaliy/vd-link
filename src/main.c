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
#include "src/drm_display.h"
#include "msp-osd.h"
#include "wfb_status_link.h"

static volatile int running = 1;

static void signal_handler(int sig)
{
    printf("\n[ MAIN ] Caught signal %d, exit ...\n", sig);
    running = 0;
    msp_osd_stop();
    rtp_receiver_stop();
    drm_close();
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

    msp_osd_init(&config);

    rtp_receiver_start(&config);

    while (running) {
        usleep(100000); // Sleep for 100 ms
    }

    return 0;
}
