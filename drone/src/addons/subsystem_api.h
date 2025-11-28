/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Public runtime interface for dynamically loaded VD-Link subsystems.
 * Every plugin must export vdlink_get_subsystem_descriptor() returning a
 * fully populated descriptor struct.
 */

#ifndef VDLINK_SUBSYSTEM_API_H
#define VDLINK_SUBSYSTEM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VDLINK_SUBSYSTEM_API_VERSION 1u

#define VDLINK_SUBSYSTEM_DESCRIPTOR_FN "vdlink_get_subsystem_descriptor"

typedef enum {
	SUBSYS_LOG_ERROR = 0,
	SUBSYS_LOG_WARN,
	SUBSYS_LOG_INFO,
	SUBSYS_LOG_DEBUG
} subsystem_log_severity_t;

typedef enum {
	SUBSYS_OVERLAY_COLOR_WHITE = 0,
	SUBSYS_OVERLAY_COLOR_BLACK,
	SUBSYS_OVERLAY_COLOR_RED,
	SUBSYS_OVERLAY_COLOR_GREEN,
	SUBSYS_OVERLAY_COLOR_BLUE,
	SUBSYS_OVERLAY_COLOR_YELLOW,
	SUBSYS_OVERLAY_COLOR_CYAN,
	SUBSYS_OVERLAY_COLOR_MAGENTA
} subsystem_overlay_color_e;

typedef struct {
	float x;
	float y;
} subsystem_overlay_point_norm_t;

typedef struct {
	struct {
		float roll;
		float pitch;
		float yaw;
	} attitude;
	float altitude_m;
	uint16_t rc_channels[16];
} subsystem_fc_properties_t;


typedef void (*subsystem_log_fn)(subsystem_log_severity_t severity,
	const char *component,
	const char *message,
	void *user_data);

/* Callback prototype for flight controller property updates;
	@param properties Pointer to subsystem_fc_properties_t structure with updated values
	@param timestamp_ms Pointer to variable to receive timestamp in milliseconds
*/
typedef void (*fc_property_update_callback_t)(const subsystem_fc_properties_t *properties, uint64_t* timestamp_ms);
	
typedef int (*subsystem_enable_rc_override_fn)(const uint8_t *channels,
												 size_t channel_count);

/* Send RC override using buffer of channel values
    @param channels Pointer to array of channel values (uint16_t)
	@param channel_count Number of channels in the array
*/
typedef int (*subsystem_send_rc_buf_override_fn)(const uint16_t *channels,
												 size_t channel_count);

/* Send RC override using individual channel values;
	Channell map will be negotiated with flight controller; 
	@param throttle Throttle channel value
	@param yaw Yaw channel value
	@param pitch Pitch channel value
	@param roll Roll channel value
	@param aux1 Auxiliary channel 1 value
	@param aux2 Auxiliary channel 2 value
	@param aux3 Auxiliary channel 3 value
	@param aux4 Auxiliary channel 4 value
*/
typedef int (*subsystem_send_rc_override_fn)(uint16_t throttle,
											 uint16_t yaw,
											 uint16_t pitch,
											 uint16_t roll,
											 uint16_t aux1,
											 uint16_t aux2,
											 uint16_t aux3,
											 uint16_t aux4);

/* Register callback for flight controller property updates
	@param callback Function pointer to be invoked on property updates
	@param frequency_hz Frequency in Hz for callback invocation
*/
typedef int (*subsystem_register_fc_property_update_callback_fn)(fc_property_update_callback_t callback,
																   uint32_t frequency_hz);												 

/* Initialize overlay subsystem
*/
typedef int (*subsystem_overlay_init_fn)(void);

/* Draw text on overlay
	@param point Normalized point (0.0 - 1.0) for text position
	@param text Null-terminated string to draw
	@param color Color enum value
	@param alpha Alpha value (0-255)
	@param size Font size in points
*/
typedef int (*subsystem_overlay_draw_text_fn)(subsystem_overlay_point_norm_t point,
											 const char *text,
											 subsystem_overlay_color_e color,
											 uint8_t alpha,
											 int size);

/* Draw rectangle on overlay
	@param left_top Normalized point (0.0 - 1.0) for top-left corner
	@param right_bottom Normalized point (0.0 - 1.0) for bottom-right corner
	@param color Color enum value
	@param alpha Alpha value (0-255)
	@param thickness Line thickness in pixels
*/
typedef int (*subsystem_overlay_draw_rectangle_fn)(subsystem_overlay_point_norm_t left_top,
													subsystem_overlay_point_norm_t right_bottom,
													subsystem_overlay_color_e color,
													uint8_t alpha,
													int thickness);

/* Draw crosshair on overlay
	@param center Normalized point (0.0 - 1.0) for crosshair center
	@param size Size of the crosshair (0.0 - 1.0)
	@param color Color enum value
	@param alpha Alpha value (0-255)
	@param thickness Line thickness in pixels
*/
typedef int (*subsystem_overlay_draw_crosshair_fn)(subsystem_overlay_point_norm_t center,
													float size,
													subsystem_overlay_color_e color,
													uint8_t alpha,
													int thickness);

/*
   Draw bitmap on overlay
	@param x X coordinate in pixels for top-left corner
	@param y Y coordinate in pixels for top-left corner
	@param bitmap_data Pointer to bitmap data in ARGB8888 format
	@param bitmap_width Width of the bitmap in pixels
	@param bitmap_height Height of the bitmap in pixels
*/
typedef int (*subsystem_overlay_draw_bitmap_fn)(int x, int y, const uint8_t *bitmap_data,
											   int bitmap_width, int bitmap_height, int bpp);

/* Draw screen on overlay; 
	Should be called after all drawing commands are issued
*/
typedef int (*subsystem_overlay_draw_screen_fn)(void);

/* Clear overlay drawings;
*/
typedef int (*subsystem_overlay_clear_fn)(void);

/* Start receiving video stream;
	@param width Desired stream width
	@param height Desired stream height
*/
typedef int (*subsystem_video_start_receiving_stream_fn)(uint32_t width, uint32_t height);

/* Stop receiving video stream;
*/
typedef int (*subsystem_video_stop_receiving_stream_fn)(void);

/* Get video stream frame;
	@param frame_data Pointer to buffer to receive frame data
	@param frame_size Pointer to variable to receive frame size; on input, should contain size of provided buffer
	@param timestamp_ms Pointer to variable to receive frame timestamp in milliseconds
*/
typedef int (*subsystem_video_get_stream_frame_fn)(uint8_t* frame_data, size_t *frame_size, uint64_t *timestamp_ms);


typedef struct subsystem_host_api_s {
	struct {
	subsystem_enable_rc_override_fn enable_rc_override;
	subsystem_send_rc_buf_override_fn send_rc_buf_override;
    subsystem_send_rc_override_fn send_rc_override;
    subsystem_register_fc_property_update_callback_fn register_fc_property_update_callback;
	} fc;
	struct {
		subsystem_overlay_init_fn init;
		subsystem_overlay_draw_text_fn draw_text;
		subsystem_overlay_draw_rectangle_fn draw_rectangle;
		subsystem_overlay_draw_crosshair_fn draw_crosshair;
		subsystem_overlay_draw_bitmap_fn draw_bitmap;
		subsystem_overlay_draw_screen_fn draw_screen;
		subsystem_overlay_clear_fn clear;
	} overlay;
	struct {
		subsystem_video_start_receiving_stream_fn start_receiving_stream;
		subsystem_video_stop_receiving_stream_fn stop_receiving_stream;
		subsystem_video_get_stream_frame_fn get_stream_frame;
	} video;
} subsystem_host_api_t;

typedef struct subsystem_context_s {
	bool is_debug_build;
	const char *conf_file_path;
	subsystem_log_fn logger;
	void *logger_user_data;
	const subsystem_host_api_t *host_api;
} subsystem_context_t;

static inline void subsystem_log(const subsystem_context_t *ctx,
								 subsystem_log_severity_t severity,
								 const char *component,
								 const char *message)
{
	if (ctx && ctx->logger) {
		ctx->logger(severity, component, message, ctx->logger_user_data);
	}
}

typedef int (*subsystem_init_fn)(const subsystem_context_t *ctx);
typedef void (*subsystem_shutdown_fn)(void);

typedef struct {
	uint32_t api_version;
	const char *name;
	const char *version;
	subsystem_init_fn init;
	subsystem_shutdown_fn shutdown;
} subsystem_descriptor_t;

#endif // VDLINK_SUBSYSTEM_API_H
