#include "link_callbacks.h"
#include <string.h>
#include "cpuinfo.h"
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>

static volatile bool running;
static pthread_t telemetry_thread;


void link_cmd_rx_callback(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size)
{
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
            } else if (cmd_id == LINK_CMD_GET) {
                // Respond with current WFB key
                char wfb_key[64] = "my_wfb_key"; // Replace with actual key retrieval logic
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_WFB_KEY, wfb_key, strlen(wfb_key));
            }
            break;
        // Handle other commands...
        default:
            printf("Unknown command ID: %d", cmd_id);
            link_send_cmd(LINK_CMD_NACK, sub_cmd_id, NULL, 0);
            break;
    }
}

void send_telemetry_update_thread_fn(void)
{
    cpu_info_t cpu_info;

    while (running) {
        cpu_info = get_cpu_info();
        link_send_sys_telemetry(cpu_info.temperature_celsius, cpu_info.usage_percent);
        sleep(5); // Send telemetry every 5 seconds
        printf("Telemetry sent: CPU Temp=%.2fC, CPU Usage=%.2f%%\n", cpu_info.temperature_celsius, cpu_info.usage_percent);
    }
    cpu_info = get_cpu_info();
    link_send_sys_telemetry(cpu_info.temperature_celsius, cpu_info.usage_percent);
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