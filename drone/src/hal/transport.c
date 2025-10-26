#include "transport.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static transport_method_t current_transport_method = TRANSPORT_METHOD_UNKNOWN;

transport_method_t detect_current_transport_method()
{
    FILE *fp;
    char buffer[256];
    bool has_wlan0 = false;
    bool has_wwan0 = false;
    bool has_usb0 = false;

    fp = popen("ip link show", "r");
    if (fp == NULL) {
        return TRANSPORT_METHOD_UNKNOWN;
    }

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (strstr(buffer, "wlan0:") != NULL && strstr(buffer, "UP") != NULL) {
            has_wlan0 = true;
        } else if (strstr(buffer, "wwan0:") != NULL && strstr(buffer, "UP") != NULL) {
            has_wwan0 = true;
        } else if (strstr(buffer, "usb0:") != NULL && strstr(buffer, "UP") != NULL) {
            has_usb0 = true;
        }
    }

    pclose(fp);

    if (has_wlan0) {
        return TRANSPORT_METHOD_WIFI;
    } else if (has_wwan0) {
        return TRANSPORT_METHOD_CELLULAR;
    } else if (has_usb0) {
        return TRANSPORT_METHOD_ETHERNET;
    }
    return TRANSPORT_METHOD_UNKNOWN;
}

transport_method_t get_current_transport_method()
{
    if (current_transport_method == TRANSPORT_METHOD_UNKNOWN) {
        current_transport_method = detect_current_transport_method();
    }
    return current_transport_method;
}