#include "link_callbacks.h"
#include <string.h>
#include "cpuinfo.h"
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include "camera/camera_manager.h"
#include "encoder/encoder.h"
#include "common.h"
#include "hal/lte_modem.h"
#include "hal/transport.h"
#include "fc_conn/fc_conn.h"
#include "config/config_parser.h"

static volatile bool running;
static pthread_t telemetry_thread;
extern common_config_t config;
extern camera_manager_t camera_manager;


void link_cmd_rx_callback(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size)
{
    printf("Received command: cmd_id=%d, sub_cmd_id=%d, size=%zu\n", cmd_id, sub_cmd_id, size);
    // Handle received command
    switch (sub_cmd_id) {
        case LINK_SUBCMD_SYS_INFO:
            if (cmd_id == LINK_CMD_GET) {
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_SYS_INFO, NULL, 0);
            }
            break;
        case LINK_SUBCMD_WFB_KEY:
            if (cmd_id == LINK_CMD_SET) {
                char buf[64] = {0};
                memcpy(buf, data, size);
                printf("Received WFB key: %s\n", buf);
                // Process the key...
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_WFB_KEY, NULL, 0);
            }
            else if (cmd_id == LINK_CMD_GET) {
                // Respond with current WFB key
                char wfb_key[64] = "my_wfb_key"; // Replace with actual key retrieval logic
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_WFB_KEY, wfb_key, strlen(wfb_key));
            }
            break;
        case LINK_SUBCMD_CAMERA:
            if (cmd_id == LINK_CMD_GET) {
                int current_camera_index = camera_get_current_camera_index(&camera_manager);
                if (current_camera_index < 0) {
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_CAMERA, NULL, 0);
                    break;
                }
                uint32_t num_cameras = camera_manager.count;
                uint32_t response[2] = {current_camera_index, num_cameras};
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_CAMERA, response, sizeof(response));
            }
            else if (cmd_id == LINK_CMD_SET) {
                if (size != sizeof(uint32_t)) {
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_CAMERA, NULL, 0);
                    break;
                }
                uint32_t camera_id = *(uint32_t*)data;
                printf("Switching to camera ID: %d\n", camera_id);
                bool switch_success = camera_select_camera_by_idx(&camera_manager, &config, camera_id);
                if (switch_success) {
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_CAMERA, &camera_id, sizeof(camera_id));
                } else {
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_CAMERA, NULL, 0);
                }
            }
            break;
        case LINK_SUBCMD_DETECTION:
            // Handle detection command
            if (cmd_id == LINK_CMD_GET) {
                /* Not implemented */
                uint32_t enabled = 0; // Replace with actual detection status
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_DETECTION, &enabled, sizeof(enabled));
            }
            else if (cmd_id == LINK_CMD_SET) {
                if (size != sizeof(uint32_t)) {
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_DETECTION, NULL, 0);
                    break;
                }
                uint32_t enabled = *(uint32_t*)data;
                // detection_enable(enabled);
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_DETECTION, &enabled, sizeof(enabled));
            }
            break;
        case LINK_SUBCMD_FOCUS_MODE:
            // Handle focus mode command
            if (cmd_id == LINK_CMD_GET) {
                uint32_t focus_mode_quality = config.encoder_config.encoder_focus_mode.focus_quality;
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_FOCUS_MODE, &focus_mode_quality, sizeof(focus_mode_quality));
            }
            else if (cmd_id == LINK_CMD_SET) {
                uint32_t focus_mode_quality = *(uint32_t*)data;
                config.encoder_config.encoder_focus_mode.focus_quality = focus_mode_quality;
                encoder_focus_mode(&config.encoder_config);
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_FOCUS_MODE, NULL, 0);
            }
            break;
        case LINK_SUBCMD_FPS:
            if (cmd_id == LINK_CMD_GET) {
                uint32_t fps = config.encoder_config.fps;
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_FPS, &fps, sizeof(fps));
            }
            else if (cmd_id == LINK_CMD_SET) {
                uint32_t fps = *(uint32_t*)data;
                uint32_t old_fps = config.encoder_config.fps;
                if (encoder_set_fps(fps) == 0) {
                    printf("Set FPS to %u successfully\n", fps);
                    config.encoder_config.fps = fps;
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_FPS, &fps, sizeof(fps));
                }
                else {
                    printf("Failed to set FPS to %u\n", fps);
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_FPS, &old_fps, sizeof(old_fps));
                }
            }
            break;
        case LINK_SUBCMD_BITRATE:
            if (cmd_id == LINK_CMD_GET) {
                uint32_t bitrate = config.encoder_config.bitrate / 1024; // convert bps to kbps
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_BITRATE, &bitrate, sizeof(bitrate));
            }
            else if (cmd_id == LINK_CMD_SET) {
                uint32_t bitrate = *(uint32_t*)data;
                uint32_t old_bitrate = config.encoder_config.bitrate * 1024;
                int ret = encoder_set_bitrate(bitrate * 1024 /* convert kbps to bps */);
                if (ret == 0) {
                    config.encoder_config.bitrate = bitrate * 1024;
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_BITRATE, &bitrate, sizeof(bitrate));
                } else {
                    int ret = encoder_set_bitrate(old_bitrate);
                    if (ret != 0) {
                        printf("Critical: Failed to revert bitrate to %u after failed set to %u\n", old_bitrate, bitrate);
                    }
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_BITRATE, &old_bitrate, sizeof(old_bitrate));
                }
            }
            break;
        case LINK_SUBCMD_HDR:
            if (cmd_id == LINK_CMD_GET) {
                bool hdr_enabled = *(bool*)data;
                /* Not implemented */
                link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_HDR, NULL, 0);
            }
            else if (cmd_id == LINK_CMD_SET) {
                /* Not implemented */
                link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_HDR, NULL, 0);
            }
            break;
        case LINK_SUBCMD_GOP:
            if (cmd_id == LINK_CMD_SET) {
                uint32_t gop_size = *(uint32_t*)data;
                uint32_t old_gop = config.encoder_config.gop;
                if (encoder_set_gop(gop_size) == 0) {
                    printf("Set GOP to %u successfully\n", gop_size);
                    config.encoder_config.gop = gop_size;
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_GOP, &gop_size, sizeof(gop_size));
                } else {
                    printf("Failed to set GOP to %u\n", gop_size);
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_GOP, &old_gop, sizeof(old_gop));
                }
            }
            if (cmd_id == LINK_CMD_GET) {
                uint32_t gop = config.encoder_config.gop;
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_GOP, &gop, sizeof(gop));
            }
            break;
        case LINK_SUBCMD_PAYLOAD_SIZE:
            if (cmd_id == LINK_CMD_SET) {
                uint32_t payload_size = *(uint32_t*)data;
                /* Not implemented */
                link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_PAYLOAD_SIZE, NULL, 0);
            }
            if (cmd_id == LINK_CMD_GET) {
                /* Not implemented */
                link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_PAYLOAD_SIZE, NULL, 0);
            }
            break;
        case LINK_SUBCMD_VBR:
            if (cmd_id == LINK_CMD_SET) {
                rate_control_mode_t mode = (data) ? *(rate_control_mode_t*)data : RATE_CONTROL_CBR;
                if (encoder_set_rate_control(mode) == 0) {
                    printf("Switched to %s successfully\n", mode == RATE_CONTROL_VBR ? "VBR" : "CBR");
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_VBR, &mode, sizeof(mode));
                    break;
                } else {
                    printf("Failed to switch to %s\n", mode == RATE_CONTROL_VBR ? "VBR" : "CBR");
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_VBR, &config.encoder_config.rate_mode, sizeof(config.encoder_config.rate_mode));
                }
            }
            if (cmd_id == LINK_CMD_GET) {
                uint32_t vbr_enabled = config.encoder_config.rate_mode == RATE_CONTROL_VBR;
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_VBR, &vbr_enabled, sizeof(vbr_enabled));
            }
            break;
        case LINK_SUBCMD_CODEC:
            if (cmd_id == LINK_CMD_SET) {
                codec_type_t codec = *(codec_type_t*)data;
                if (encoder_set_codec(codec) == 0) {
                    printf("Switched codec to %d successfully\n", codec);
                    link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_CODEC, &codec, sizeof(codec));
                } else {
                    printf("Failed to switch codec to %d\n", codec);
                    link_send_cmd(LINK_CMD_NACK, LINK_SUBCMD_CODEC, &config.encoder_config.codec, sizeof(config.encoder_config.codec));
                }
            }
            else if (cmd_id == LINK_CMD_GET) {
                uint32_t is_codec_h265 = (config.encoder_config.codec == CODEC_H265) ? 1 : 0;
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_CODEC, &is_codec_h265, sizeof(is_codec_h265));
            }
            break;
        case LINK_SUBCMD_SAVE_PERSISTENT:
            if (cmd_id == LINK_CMD_SET) {
                // Save current configuration to persistent storage
                config_save("/etc/vd-link.config", &config);
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_SAVE_PERSISTENT, NULL, 0);
            }
            break;
        // Handle other commands...
        default:
            printf("Unknown command ID: %d", cmd_id);
            link_send_cmd(LINK_CMD_NACK, sub_cmd_id, NULL, 0);
            break;
    }
}

void link_rc_rx_callback(const uint16_t* channel_values, size_t channel_count)
{
    // Handle received RC channel values
    printf("Received RC channel values:\n");
    for (size_t i = 0; i < channel_count; ++i) {
        printf(" Channel %zu: %d ", i, channel_values[i]);
    }
    printf("\n");
}

void send_telemetry_update_thread_fn(void)
{
    cpu_info_t cpu_info;
    struct lte_signal_info lte_info = {0};

    while (running) {
        cpu_info = get_cpu_info();
        transport_method_t current_transport_method = get_current_transport_method();
        link_sys_telemetry_t telemetry = {
            .cpu_temperature = cpu_info.temperature_celsius,
            .cpu_usage_percent = cpu_info.usage_percent,
            .rtt_ms = link_get_last_rtt_ms(),
        };

        if (current_transport_method == TRANSPORT_METHOD_CELLULAR) {
            lte_modem_get_signal_info(&lte_info);
            // char buf[1280];
            // lte_modem_get_signal_str(buf, sizeof(buf));
            // printf("LTE Modem Signal Info: %s\n", buf);
        }

        switch (current_transport_method) {
            case TRANSPORT_METHOD_CELLULAR:
                if (strcmp(lte_info.type, "lte") == 0) {
                    telemetry.phy_type = LINK_PHY_TYPE_LTE;
                    telemetry.lte_signal.rssi = lte_info.rssi;
                    telemetry.lte_signal.rsrq = lte_info.rsrq;
                    telemetry.lte_signal.rsrp = lte_info.rsrp;
                    telemetry.lte_signal.snr = lte_info.snr;
                } else if (strcmp(lte_info.type, "wcdma") == 0) {
                    telemetry.phy_type = LINK_PHY_TYPE_WCDMA;
                    telemetry.wcdma_signal.rssi = lte_info.rssi;
                } else {
                    telemetry.phy_type = LINK_PHY_TYPE_UNKNOWN;
                }
                break;
            case TRANSPORT_METHOD_WIFI:
                telemetry.phy_type = LINK_PHY_TYPE_WIFI;
                // TODO: Populate WiFi signal info
                break;
            case TRANSPORT_METHOD_ETHERNET:
                telemetry.phy_type = LINK_PHY_TYPE_ETHERNET;
                break;
            default:
                telemetry.phy_type = LINK_PHY_TYPE_UNKNOWN;
                break;
        }

        link_send_sys_telemetry(&telemetry);

        msp_send_update_rssi(25);

        sleep(5); // Send telemetry every 5 seconds
#if 0
        printf("Telemetry sent: CPU Temp=%.2fC, CPU Usage=%.2f%% ", cpu_info.temperature_celsius, cpu_info.usage_percent);
        printf("Link Type=%d ", telemetry.phy_type);
        if (telemetry.phy_type == LINK_PHY_TYPE_LTE) {
            printf("LTE RSSI=%lddBm RSRQ=%lddB RSRP=%lddBm SNR=%.1fdB\n",
                   telemetry.lte_signal.rssi,
                   telemetry.lte_signal.rsrq,
                   telemetry.lte_signal.rsrp,
                   telemetry.lte_signal.snr);
        } else if (telemetry.phy_type == LINK_PHY_TYPE_WCDMA) {
            printf("WCDMA RSSI=%lddBm\n", telemetry.wcdma_signal.rssi);
        } else if (telemetry.phy_type == LINK_PHY_TYPE_WIFI) {
            printf("WiFi RSSI=%lddBm\n", telemetry.wifi_signal.rssi);
        } else if (telemetry.phy_type == LINK_PHY_TYPE_ETHERNET) {
            printf("Ethernet connection\n");
        } else {
            printf("Unknown link type\n");
        }
        printf("\n");
#endif
    }
}

int link_start_telemetry_thread()
{
    running = true;
    if (pthread_create(&telemetry_thread, NULL, (void*(*)(void*))send_telemetry_update_thread_fn, NULL) != 0) {
        perror("Failed to create telemetry thread");
        return -1;
    }
    pthread_detach(telemetry_thread);
    return 0;
}

void link_stop_telemetry_thread()
{
    running = false;
    pthread_join(telemetry_thread, NULL);
}