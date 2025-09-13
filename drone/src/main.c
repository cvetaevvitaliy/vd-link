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
#include "common.h"
#include "encoder.h"

static volatile bool running = false;

static void signal_handler(int sig)
{
    printf("\n[ MAIN ] Caught signal %d, exit ...\n", sig);
    running = false;
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

static void print_usage(const char* prog)
{
    printf("\n");
    printf("Usage: %s [--ip <address>] [--port <number>] [--help]\n", prog);
    printf("Options:\n");
    printf("  --ip <address>   Set the IP address to listen on (default: 0.0.0.0)\n");
    printf("  --port <number>  Set the port to listen for RTP stream (default: 5602)\n");
    printf("  --codec <type>   Set the codec type (h264 or h265, default: h265)\n");
    printf("  --help            Show this help and exit\n");
    printf("Defaults: --ip 0.0.0.0 --port 5602\n");
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

static void parse_args(int argc, char* argv[], struct config_t* config)
{
    static struct option long_options[] = {
        {"ip", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"codec", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:c:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'i':
            config->ip = optarg;
            break;
        case 'p': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "Invalid port number: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            config->port = port;
        } break;
        case 'c': {
            if (strcmp(optarg, "h264") == 0) {
                config->codec_type = CODEC_H264;
            } else if (strcmp(optarg, "h265") == 0) {
                config->codec_type = CODEC_H265;
            } else {
                fprintf(stderr, "Unsupported codec type: %s\n", optarg);
                print_usage(argv[0]);
                exit(0);
            }
        } break;
        case 'h':
        default:
            print_usage(argv[0]);
            exit(0);
        }
    }
}

static void update_config_encoder(struct config_t *config, encoder_config_t *enc_cfg)
{
    if (!config || !enc_cfg) {
        return;
    }
    enc_cfg->codec = config->codec_type;
    enc_cfg->rate_mode = RATE_CONTROL_AVBR;
    enc_cfg->width = config->stream_width;
    enc_cfg->height = config->stream_height;
    enc_cfg->bitrate = config->stream_bitrate;
    enc_cfg->fps = 60;
    enc_cfg->gop = 60; // 2 seconds GOP
    enc_cfg->osd_config.width = 200;
    enc_cfg->osd_config.height = 50;
    enc_cfg->osd_config.pos_x = 10;
    enc_cfg->osd_config.pos_y = 10;
    enc_cfg->encoder_focus_mode.focus_quality = -8; // Quality adjustment for focus area
    enc_cfg->encoder_focus_mode.frame_size = 65;    // Focus on center 65% of frame
}

int main(int argc, char *argv[])
{
    encoder_config_t encoder_config = {0};

    struct config_t config = {
        .ip = "0.0.0.0",
        .port = 5602,
        .codec_type = CODEC_H265,
    };

    print_banner();
    parse_args(argc, argv, &config);
    setup_signals();

    update_config_encoder(&config, &encoder_config);

    printf("Configuration:\n");
    printf(" ip: %s\n", config.ip);
    printf(" port: %d\n", config.port);

    running = true;

    while (running) {
        usleep(250 * 1000);
    }

    return 0;
}