#include "cpuinfo.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>


cpu_info_t get_cpu_info(void)
{
    static cpu_info_t cached = {-1.0f, -1.0f};
    static uint64_t last_total = 0, last_idle = 0;
    static struct timespec last_time = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long delta_ms = (now.tv_sec - last_time.tv_sec) * 1000 +
                    (now.tv_nsec - last_time.tv_nsec) / 1000000;
    if (delta_ms < 500) {
        return cached;
    }

    // CPU Usage
    FILE *fp_stat = fopen("/proc/stat", "r");
    if (fp_stat) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp_stat)) {
            uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
                uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
                uint64_t total_diff = total - last_total;
                uint64_t idle_diff = idle - last_idle;

                last_total = total;
                last_idle = idle;

                if (total_diff > 0) {
                    cached.usage_percent = 100.0f * (1.0f - (float)idle_diff / total_diff);
                }
            }
        }
        fclose(fp_stat);
    }

    // CPU Temperature
    FILE *fp_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r"); // zone 0 is usually CPU temp, 1 is NPU
    if (fp_temp) {
        int temp_millic;
        if (fscanf(fp_temp, "%d", &temp_millic) == 1) {
            cached.temperature_celsius = temp_millic / 1000.0f;
        }
        fclose(fp_temp);
    }

    last_time = now;
    return cached;
}