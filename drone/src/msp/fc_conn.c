#include "fc_conn.h"
#include "msp.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <time.h>
#include <pthread.h>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/time.h>


#define OSD_DEFAULT_CHAR_X    53//93
#define OSD_DEFAULT_CHAR_Y    20//70
#define FC_POLL_PERIOD_MSEC 2000
#define FC_INACTIVE_TIMEOUT_MSEC 10000
#define MSP_AGGREGATION_TIMEOUT_MSEC 500

#define MSEC_PER_SEC 1000
#define NSEC_PER_MSEC 1000000
#define SERIAL_POLL_RATE_HZ 5


static uint64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static msp_state_t *rx_msp_state;
static msp_displayport_cb_t displayport_cb = NULL;
static int serial_fd = -1;
static _Atomic int quit = 0;
static uint64_t last_fc_response = 0;
static uint64_t last_msp_send = 0;
static pthread_t fc_poll_thread;

static uint8_t *aggregation_buffer = NULL;
static uint16_t aggregation_mtu = 4048;
static uint16_t aggregation_timeout = MSP_AGGREGATION_TIMEOUT_MSEC;
static uint16_t aggregate_bytes = 0;
static uint64_t last_aggregation_send = 0;


void register_msp_displayport_cb(msp_displayport_cb_t cb)
{
    displayport_cb = cb;
}

static void send_aggregated_buffer()
{
    if (aggregation_buffer && aggregate_bytes > 0) {
        ssize_t sent = displayport_cb(aggregation_buffer, aggregate_bytes);
        if (sent < 0) {
            printf("Error send data\n");
        }
        aggregate_bytes = 0;
    }
    last_aggregation_send = get_time_ms();
}

static void send_display_size(uint8_t canvas_size_x, uint8_t canvas_size_y) {
    uint8_t buffer[8];
    uint8_t payload[2] = {canvas_size_x, canvas_size_y};
    construct_msp_command(buffer, MSP_CMD_SET_OSD_CANVAS, payload, 2, MSP_OUTBOUND);
    write(serial_fd, &buffer, sizeof(buffer));
}

static void send_variant_request() {
    uint8_t buffer[6];
    construct_msp_command(buffer, MSP_CMD_FC_VARIANT, NULL, 0, MSP_OUTBOUND);
    write(serial_fd, &buffer, sizeof(buffer));
}

static void send_version_request() {
    uint8_t buffer[6];
    construct_msp_command(buffer, MSP_CMD_API_VERSION, NULL, 0, MSP_OUTBOUND);
    write(serial_fd, &buffer, sizeof(buffer));
}


static void rx_msp_callback(msp_msg_t *msp_message)
{
    static uint8_t message_buffer[256]; // only needs to be the maximum size of an MSP packet, we only care to fwd MSP
    // printf("MSP CMD: %d, Size: %d, Direction: %d\n", msp_message->cmd, msp_message->size, msp_message->direction);
    if (!(msp_message->cmd == MSP_CMD_DISPLAYPORT ||
        msp_message->cmd == MSP_CMD_FC_VARIANT ||
        msp_message->cmd == MSP_CMD_API_VERSION)) {
            return; // Ignore other commands
    }

    uint16_t size = msp_data_from_msg(message_buffer, msp_message);

    if (!aggregation_mtu) {
        // No aggregation, send directly
        if (displayport_cb(message_buffer, size) < 0) {
            printf("Error sending data\n");
        }
    } else {
        if (msp_message->cmd == MSP_CMD_DISPLAYPORT &&
            msp_message->payload[0] == MSP_DISPLAYPORT_DRAW_SCREEN && size < 8) {

            memcpy(aggregation_buffer + aggregate_bytes, message_buffer, size);
            aggregate_bytes += size;

            send_aggregated_buffer();
        } else {
            if (aggregate_bytes + size >= aggregation_mtu) {
                send_aggregated_buffer();
            }

            memcpy(aggregation_buffer + aggregate_bytes, message_buffer, size);
            aggregate_bytes += size;
        }
    }

    last_fc_response = get_time_ms();
//    uint16_t size = msp_data_from_msg(message_buffer, msp_message);
//    copy_to_msp_frame_buffer(message_buffer, size);
//    if(msp_message->payload[0] == MSP_DISPLAYPORT_DRAW_SCREEN) {
//        // Once we have a whole frame of data, send it to the goggles.
//        write(socket_fd, frame_buffer, fb_cursor);
//        DEBUG_PRINT("DRAW! wrote %d bytes\n", fb_cursor);
//        fb_cursor = 0;
//    }
}

static void* fc_polling_thread(void *arg)
{
    struct pollfd poll_fds[2];
    uint64_t last_fc_poll_time = get_time_ms();
    uint8_t osd_size_x = OSD_DEFAULT_CHAR_X;
    uint8_t osd_size_y = OSD_DEFAULT_CHAR_Y;
    ssize_t serial_data_size;
    uint8_t serial_data[1024] = {0};
    aggregation_buffer = malloc(aggregation_mtu);

    send_display_size(osd_size_x, osd_size_y);
    printf("FC inactive, sending display size %d x %d\n", osd_size_x, osd_size_y);

    while (!quit) {
        poll_fds[0].fd = serial_fd;
        poll_fds[0].events = POLLIN;
        poll(poll_fds, 2, ((MSEC_PER_SEC / SERIAL_POLL_RATE_HZ) / 2));
        uint64_t now = get_time_ms();
        // printf("POLL now %llu  last %llu \n", now, last_fc_poll_time);

        if (now - last_fc_poll_time >= FC_POLL_PERIOD_MSEC) {
            send_version_request();
            send_variant_request();
            // printf("FC poll request sent\n");

            if (now - last_fc_response >= FC_INACTIVE_TIMEOUT_MSEC) {
                send_display_size(osd_size_x, osd_size_y);
                printf("FC inactive, sending display size %d x %d\n", osd_size_x, osd_size_y);
            }

            last_fc_poll_time = now;
        }

        if (aggregate_bytes > 0 && (now - last_aggregation_send >= aggregation_timeout)) {
            send_aggregated_buffer();
        }

        // We got inbound serial data, process it as MSP data.
        serial_data_size = read(serial_fd, serial_data, sizeof(serial_data));
        if (serial_data_size > 0) {
            // printf("RECEIVED data! length %d\n", serial_data_size);
            rx_msp_state->cb = &rx_msp_callback;
            for (ssize_t i = 0; i < serial_data_size; i++) {
                msp_process_data(rx_msp_state, serial_data[i]);
            }
        }
    }
    return 0;
}

int connect_to_fc(const char *device, int baudrate)
{
    serial_fd = open_serial_port(device, baudrate);
    if (serial_fd == 0) {
        printf("Failed to open serial port %s\n", device);
        return -1;
    }
    printf("Serial port %s opened successfully\n", device);
    rx_msp_state = calloc(1, sizeof(msp_state_t));
    rx_msp_state->cb = &rx_msp_callback;

    if (pthread_create(&fc_poll_thread, NULL, fc_polling_thread, NULL) != 0) {
        printf("Failed to create flight controller polling thread\n");
        return -1;
    }

}

void disconnect_from_fc()
{
    if (rx_msp_state) {
        free(rx_msp_state);
        rx_msp_state = NULL;
    }
    
    if (serial_fd >= 0) {
        close(serial_fd);
        serial_fd = -1;
    }
    
    printf("Disconnected from flight controller\n");
}