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
#include "rtp_streamer/rtp_streamer.h"
#include "screensaver/screensaver.h"
#include "link_callbacks/link_callbacks.h"

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

static void parse_args(int argc, char* argv[], common_config_t* config)
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
    int ret = 0;
    common_config_t config = {0}; // common configuration
    screensaver_nv12_t screensaver; // screensaver frame (if camera not available)

    // set defaults configs
    config_init_defaults(&config);

    // Try to load config from file
    if (config_load(PATH_TO_CONFIG_FILE, &config) != 0) {
        printf("Can't load '%s' config file\n", PATH_TO_CONFIG_FILE);
    }

    print_banner();

    // override config with command line args
    parse_args(argc, argv, &config);
    setup_signals();

    printf("Configuration:\n");
    printf("RTP Streamer:\n");
    printf(" ip: %s\n", config.rtp_streamer_config.ip);
    printf(" port: %d\n", config.rtp_streamer_config.port);
    printf("Encoder:\n");
    printf(" codec: %s\n", config.encoder_config.codec == CODEC_H264 ? "H.264" : "H.265");
    printf(" resolution: %dx%d\n", config.encoder_config.width, config.encoder_config.height);
    printf(" bitrate: %d\n", config.encoder_config.bitrate);
    printf(" fps: %d\n", config.encoder_config.fps);
    printf(" gop: %d\n", config.encoder_config.gop);
    printf(" osd: %dx%d @ (%d,%d)\n", config.encoder_config.osd_config.width,
           config.encoder_config.osd_config.height,
           config.encoder_config.osd_config.pos_x,
           config.encoder_config.osd_config.pos_y);
    printf(" focus mode: %s\n", config.encoder_config.encoder_focus_mode.focus_quality >= 0 ? "ON" : "OFF");
    printf(" focus quality: %d\n", config.encoder_config.encoder_focus_mode.focus_quality);
    printf(" focus frame size: %d%%\n", config.encoder_config.encoder_focus_mode.frame_size);
    printf("Camera ID: %d\n", config.camera_csi_config.cam_id);
    printf(" Auto White Balance: %s\n", config.camera_csi_config.auto_white_balance ? "ON" : "OFF");
    printf(" Brightness: %d\n", config.camera_csi_config.brightness);
    printf(" Contrast: %d\n", config.camera_csi_config.contrast);
    printf(" Saturation: %d\n", config.camera_csi_config.saturation);
    printf(" Sharpness: %d\n", config.camera_csi_config.sharpness);
    printf(" Flip: %s\n", config.camera_csi_config.flip ? "ON" : "OFF");
    printf(" Mirror: %s\n", config.camera_csi_config.mirror ? "ON" : "OFF");
    printf("\n");

    config.encoder_config.callback = rtp_streamer_push_frame; // register callback for encoded frames

    ret = rtp_streamer_init(&config);
    if (ret != 0) {
        printf("Failed to initialize RTP streamer\n");
        config_cleanup(&config);
        return -1;
    }

    ret = encoder_init(&config.encoder_config);
    if (ret != 0) {
        printf("Failed to initialize encoder\n");
        rtp_streamer_deinit();
        config_cleanup(&config);
        return -1;
    }

    ret = link_init(LINK_DRONE);
    if (ret != 0) {
        printf("Failed to initialize link\n");
        encoder_clean();
        rtp_streamer_deinit();
        config_cleanup(&config);
        return -1;
    }
    link_register_cmd_rx_cb(link_cmd_rx_callback);

    if (screensaver_create_nv12_solid(config.stream_width, config.stream_height,
                                  0x10 /*Y black*/, 0x80 /*U*/, 0x80 /*V*/,
                                  &screensaver) != 0) {
        printf("Failed to create screensaver frame\n");
        encoder_clean();
        rtp_streamer_deinit();
        config_cleanup(&config);
        return -1;
    }

    running = true;
    while (running) {
        encoder_manual_push_frame(&config.encoder_config, screensaver.data, (int)screensaver.size_bytes);
        usleep(16 * 1000);
    }

    encoder_clean();
    rtp_streamer_deinit();
    config_cleanup(&config);

    return 0;
}