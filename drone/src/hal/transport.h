#pragma once


typedef enum {
    TRANSPORT_METHOD_UNKNOWN = 0,
    TRANSPORT_METHOD_ETHERNET,
    TRANSPORT_METHOD_WIFI,
    TRANSPORT_METHOD_CELLULAR
} transport_method_t;


transport_method_t detect_current_transport_method();
transport_method_t get_current_transport_method();