#include "callbacks_rtp.h"
#include "log.h"
#include "link.h"

static const char *module_name_str = "CALLBACKS_RTP";

const char* bitrate_values_str = "400 Kbps\n800 Kbps\n1.2 Mbps\n1.6 Mbps\n2.0 Mbps\n4.0 Mbps\n";
const uint32_t bitrate_values[] = {400, 800, 1200, 1600, 2000, 4000};

const char* codec_values_str = "H.264\nH.265";
const uint32_t codec_values[] = {264, 265};

static uint16_t get_idx_from_bitrate(uint16_t bitrate)
{
    for (size_t i = 0; i < sizeof(bitrate_values)/sizeof(bitrate_values[0]); i++) {
        if (bitrate_values[i] == bitrate) {
            return i;
        }
    }
    return 0; // Default to first index if not found
}

static uint16_t get_idx_from_codec(uint32_t codec)
{
    for (size_t i = 0; i < sizeof(codec_values)/sizeof(codec_values[0]); i++) {
        if (codec_values[i] == codec) {
            return i;
        }
    }
    return 0; // Default to first index if not found
}

uint16_t wfb_ng_get_bitrate(void)
{
    size_t  resp_size = 0;
    uint32_t resp = 0;

    link_send_cmd_sync(LINK_CMD_GET, LINK_SUBCMD_BITRATE, NULL, 0, &resp, &resp_size, 500);
    if (resp_size == sizeof(resp)) {
        return get_idx_from_bitrate(resp);
    } else {
        ERROR("Failed to get bitrate, response size mismatch");
        return 0; // Indicate error
    }
}

void wfb_ng_set_bitrate(uint16_t bitrate_idx)
{
    uint32_t bitrate = bitrate_values[bitrate_idx];
    uint32_t resp = 0;
    size_t resp_size = 0;
    link_send_cmd_sync(LINK_CMD_SET, LINK_SUBCMD_BITRATE, &bitrate, sizeof(bitrate), &resp, &resp_size, 500);
    if (resp_size != sizeof(resp) || resp != bitrate) {
        ERROR("Failed to set bitrate to %u Kbps", bitrate);
    } else {
        INFO("Bitrate set to %u Kbps", bitrate);
    }
}

uint16_t wfb_ng_get_codec(void)
{
    size_t resp_size = 0;
    uint8_t resp = 0;

    link_send_cmd_sync(LINK_CMD_GET, LINK_SUBCMD_CODEC, NULL, 0, &resp, &resp_size, 500);
    if (resp_size == sizeof(resp)) {
        return get_idx_from_codec(resp);
    } else {
        ERROR("Failed to get codec, response size mismatch");
        return 0; // Default to H.264 on error
    }
}

void wfb_ng_set_codec(uint16_t codec_idx)
{
    uint8_t codec = codec_values[codec_idx];
    uint32_t resp = 0;
    size_t resp_size = 0;
    link_send_cmd_sync(LINK_CMD_SET, LINK_SUBCMD_CODEC, &codec, sizeof(codec), &resp, &resp_size, 500);
    if (resp_size != sizeof(resp) || resp != codec) {
        ERROR("Failed to set codec to %s", codec == 264 ? "H.264" : "H.265");
    } else {
        INFO("Codec set to %s", codec == 264 ? "H.264" : "H.265");
    }
}

int32_t wfb_ng_get_gop(void)
{
    size_t resp_size = 0;
    uint32_t resp = 0;

    link_send_cmd_sync(LINK_CMD_GET, LINK_SUBCMD_GOP, NULL, 0, &resp, &resp_size, 500);
    if (resp_size == sizeof(resp)) {
        return resp;
    } else {
        ERROR("Failed to get GOP, response size mismatch");
        return 0; // Indicate error
    }
}

void wfb_ng_set_gop(int32_t gop)
{
    uint32_t resp = 0;
    size_t resp_size = 0;

    link_send_cmd_sync(LINK_CMD_SET, LINK_SUBCMD_GOP, &gop, sizeof(gop), &resp, &resp_size, 500);
    if (resp_size != sizeof(resp) || resp != gop) {
        ERROR("Failed to set GOP to %u", gop);
    } else {
        INFO("GOP set to %u", gop);
    }
}