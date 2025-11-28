#include "subsystem_api.h"
#include <errno.h>
#include "log.h"

#include "fc_conn.h"
#include "encoder/overlay.h"
#include "camera/camera_csi.h"

static const char* module_name_str = "subsystem_api";
static int overlay_width = 0;
static int overlay_height = 0;

static uint32_t color_to_argb(subsystem_overlay_color_e color, uint8_t alpha)
{
    switch (color) {
        case SUBSYS_OVERLAY_COLOR_WHITE:
            return ARGB(alpha, 255, 255, 255);
        case SUBSYS_OVERLAY_COLOR_BLACK:
            return ARGB(alpha, 0, 0, 0);
        case SUBSYS_OVERLAY_COLOR_RED:
            return ARGB(alpha, 255, 0, 0);
        case SUBSYS_OVERLAY_COLOR_GREEN:
            return ARGB(alpha, 0, 255, 0);
        case SUBSYS_OVERLAY_COLOR_BLUE:
            return ARGB(alpha, 0, 0, 255);
        case SUBSYS_OVERLAY_COLOR_YELLOW:
            return ARGB(alpha, 255, 255, 0);
        case SUBSYS_OVERLAY_COLOR_CYAN:
            return ARGB(alpha, 0, 255, 255);
        case SUBSYS_OVERLAY_COLOR_MAGENTA:
            return ARGB(alpha, 255, 0, 255);
        default:
            return ARGB(alpha, 255, 255, 255); // Default to white
    }
}

static int host_enable_rc_override_stub(const uint8_t *channels, size_t channel_count)
{
	(void)channels;
	(void)channel_count;
	enable_rc_override_on_fc(channels, channel_count);
	return 0;
}

static int host_send_rc_buf_override_stub(const uint16_t *channels, size_t channel_count)
{
    send_rc_override_to_fc((uint16_t*)channels, channel_count);
    return 0;
}

static int host_send_rc_override_stub(uint16_t throttle, uint16_t yaw, uint16_t pitch, uint16_t roll, uint16_t aux1, uint16_t aux2, uint16_t aux3, uint16_t aux4)
{
    (void)throttle;
    (void)yaw;
    (void)pitch;
    (void)roll;
    (void)aux1;
    (void)aux2;
    (void)aux3;
    (void)aux4;
    INFO("send_rc_override() is not wired yet");
    return -ENOTSUP;
}

static int host_register_fc_property_update_callback_stub(fc_property_update_callback_t callback, uint32_t frequency_hz)
{
    register_fc_property_update_callback(callback, frequency_hz);
    return 0;
}

static int host_overlay_init_stub(void)
{
    int ret = overlay_init();
    overlay_get_overlay_size(&overlay_width, &overlay_height);
    // printf("[%s] Overlay initialized with ret =%d, size: %dx%d\n", module_name_str, ret, overlay_width, overlay_height);
    return ret;
}

static int host_overlay_draw_text_stub(subsystem_overlay_point_norm_t point, const char *text, subsystem_overlay_color_e color, uint8_t alpha, int size)
{
    int x = (int)(point.x * overlay_width);;
    int y = (int)(point.y * overlay_height);;
    uint32_t color_value = color_to_argb(color, alpha);
    overlay_draw_text(x, y, text, color_value, size);
    return 0;
}

static int host_overlay_draw_rectangle_stub(subsystem_overlay_point_norm_t left_top, subsystem_overlay_point_norm_t right_bottom, subsystem_overlay_color_e color, uint8_t alpha, int thickness)
{
    int x1 = (int)(left_top.x * overlay_width);;
    int y1 = (int)(left_top.y * overlay_height);
    int x2 = (int)(right_bottom.x * overlay_width);;
    int y2 = (int)(right_bottom.y * overlay_height);
    uint32_t color_value = color_to_argb(color, alpha);
    overlay_draw_rect(x1, y1, x2, y2, color_value, thickness);

    return 0;
}

static int host_overlay_draw_crosshair_stub(subsystem_overlay_point_norm_t center, float size, subsystem_overlay_color_e color, uint8_t alpha, int thickness)
{
    int x = (int)(center.x * overlay_width);;
    int y = (int)(center.y * overlay_height);
    int pixel_size = (int)(size * (overlay_width < overlay_height ? overlay_width : overlay_height));;
    uint32_t color_value = color_to_argb(color, alpha);
    overlay_draw_crosshair(x, y, pixel_size, color_value, thickness);
    return 0;
}

static int host_overlay_draw_bitmap_stub(int x, int y, const uint8_t *bitmap_data,
                                        int bitmap_width, int bitmap_height, int bpp)
{
    overlay_draw_bitmap(x, y, bitmap_data, bitmap_width, bitmap_height, bpp);
    return 0;
}

static int host_overlay_draw_screen_stub(void)
{
    return overlay_push_to_encoder();
}

static int host_overlay_clear_stub(void)
{
    overlay_clear();
    return 0;
}

static int host_video_start_receiving_stream_stub(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    INFO("video_start_receiving_stream(): Frame capture is enabled automatically");
    return 0; // Frame capture is already enabled during camera initialization
}

static int host_video_stop_receiving_stream_stub(void)
{
    INFO("video_stop_receiving_stream(): Frame capture terminated");
    return 0; // For now, just return success
}

static int host_video_get_stream_frame_stub(uint8_t* frame_data, size_t* frame_size, uint64_t* timestamp_ms)
{
    if (!frame_data || !frame_size || !timestamp_ms) {
        return -EINVAL;
    }

    uint32_t width, height;
    int ret = camera_csi_get_latest_frame(frame_data, frame_size, &width, &height, timestamp_ms);

    if (ret == -1) {
        return -EAGAIN; // No frame available yet
    } else if (ret == -2) {
        return -E2BIG; // Buffer too small, frame_size contains required size
    }

    return 0; // Success
}


const subsystem_host_api_t g_host_api = {
	.fc = {
		.enable_rc_override = host_enable_rc_override_stub,
		.send_rc_buf_override = host_send_rc_buf_override_stub,
		.send_rc_override = host_send_rc_override_stub,
		.register_fc_property_update_callback = host_register_fc_property_update_callback_stub,
	},
	.overlay = {
        .init = host_overlay_init_stub,
		.draw_text = host_overlay_draw_text_stub,
		.draw_rectangle = host_overlay_draw_rectangle_stub,
		.draw_crosshair = host_overlay_draw_crosshair_stub,
		.draw_screen = host_overlay_draw_screen_stub,
        .draw_bitmap = host_overlay_draw_bitmap_stub,
		.clear = host_overlay_clear_stub,
	},
    .video = {
        .start_receiving_stream = host_video_start_receiving_stream_stub,
        .stop_receiving_stream = host_video_stop_receiving_stream_stub,
        .get_stream_frame = host_video_get_stream_frame_stub,
    }
};