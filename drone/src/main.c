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
#include "encoder/encoder.h"
#include "config/config_parser.h"

#define PATH_TO_CONFIG_FILE "/etc/vd-link.config"

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

static void parse_args(int argc, char* argv[], struct common_config_t* config)
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
            config->rtp_streamer_config.ip = optarg;
            break;
        case 'p': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "Invalid port number: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            config->rtp_streamer_config.port = port;
        } break;
        case 'c': {
            if (strcmp(optarg, "h264") == 0 || strcmp(optarg, "H264") == 0) {
                config->encoder_config.codec = CODEC_H264;
            } else if (strcmp(optarg, "h265") == 0 || strcmp(optarg, "H265") == 0) {
                config->encoder_config.codec = CODEC_H265;
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

int main(int argc, char *argv[])
{
    struct common_config_t config = {
        .rtp_streamer_config.ip = "0.0.0.0",
        .rtp_streamer_config.port = 5602,
        .encoder_config.codec = CODEC_H265,
    };

    if (ini_parse(PATH_TO_CONFIG_FILE, config_parser_handler, &config) < 0) {
        printf("Can't load '%s'\n" PATH_TO_CONFIG_FILE);
    }

    print_banner();
    parse_args(argc, argv, &config);
    setup_signals();

    printf("Configuration:\n");
    printf(" ip: %s\n", config.rtp_streamer_config.ip);
    printf(" port: %d\n", config.rtp_streamer_config.port);
    printf(" codec: %s\n", config.encoder_config.codec == CODEC_H264 ? "H.264" : "H.265");

    running = true;

    while (running) {
        usleep(250 * 1000);
    }

    return 0;
}