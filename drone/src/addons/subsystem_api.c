#include "subsystem_api.h"
#include <errno.h>
#include "log.h"

#include "fc_conn.h"

static const char* module_name_str = "subsystem_api";

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

static int host_overlay_draw_text_stub(subsystem_overlay_point_norm_t point, const char *text, subsystem_overlay_color_e color, int size)
{
    (void)point.x;
    (void)point.y;
    (void)text;
    (void)color;
    (void)size;
    INFO("overlay_draw_text() is not wired yet");
    return -ENOTSUP;
}

static int host_overlay_draw_rectangle_stub(subsystem_overlay_point_norm_t left_top, subsystem_overlay_point_norm_t right_bottom, subsystem_overlay_color_e color, int thickness)
{
    (void)left_top.x;
    (void)left_top.y;
    (void)right_bottom.x;
    (void)right_bottom.y;
    (void)color;
    (void)thickness;
    INFO("overlay_draw_rectangle() is not wired yet");
    return -ENOTSUP;
}

static int host_overlay_draw_crosshair_stub(subsystem_overlay_point_norm_t center, float size, subsystem_overlay_color_e color, int thickness)
{
    (void)center.x;
    (void)center.y;
    (void)size;
    (void)color;
    (void)thickness;
    INFO("overlay_draw_crosshair() is not wired yet");
    return -ENOTSUP;
}

static int host_overlay_draw_screen_stub(void)
{
    INFO("overlay_draw_screen() is not wired yet");
    return -ENOTSUP;
}

static int host_overlay_clear_stub(void)
{
    INFO("overlay_clear() is not wired yet");
    return -ENOTSUP;
}

static int host_video_start_receiving_stream_stub(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    INFO("video_start_receiving_stream() is not wired yet");
    return -ENOTSUP;
}

static int host_video_stop_receiving_stream_stub(void)
{
    INFO("video_stop_receiving_stream() is not wired yet");
    return -ENOTSUP;
}

static int host_video_get_stream_frame_stub(uint8_t* frame_data, size_t* frame_size, uint64_t* timestamp_ms)
{
    (void)frame_data;
    (void)frame_size;
    (void)timestamp_ms;
    INFO("video_get_stream_frame() is not wired yet");
    return -ENOTSUP;
}


const subsystem_host_api_t g_host_api = {
	.fc = {
		.enable_rc_override = host_enable_rc_override_stub,
		.send_rc_buf_override = host_send_rc_buf_override_stub,
		.send_rc_override = host_send_rc_override_stub,
		.register_fc_property_update_callback = host_register_fc_property_update_callback_stub,
	},
	.overlay = {
		.draw_text = host_overlay_draw_text_stub,
		.draw_rectangle = host_overlay_draw_rectangle_stub,
		.draw_crosshair = host_overlay_draw_crosshair_stub,
		.draw_screen = host_overlay_draw_screen_stub,
		.clear = host_overlay_clear_stub,
	},
    .video = {
        .start_receiving_stream = host_video_start_receiving_stream_stub,
        .stop_receiving_stream = host_video_stop_receiving_stream_stub,
        .get_stream_frame = host_video_get_stream_frame_stub,
    }
};