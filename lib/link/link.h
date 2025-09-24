#ifndef VD_LINK_H
#define VD_LINK_H

#include <stdint.h>
#include <stddef.h>
#include "simple.pb.h"
#include "sub/unlucky.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Simple Message functions
int vdlink_encode_simple_message(const vdlink_SimpleMessage *message, uint8_t *buffer, size_t buffer_size, size_t *bytes_written);
int vdlink_decode_simple_message(vdlink_SimpleMessage *message, const uint8_t *buffer, size_t buffer_size);
vdlink_SimpleMessage vdlink_create_simple_message(int32_t id, const char *name, float value);

// Config Request/Response functions
int vdlink_encode_config_request(const vdlink_ConfigRequest *request, uint8_t *buffer, size_t buffer_size, size_t *bytes_written);
int vdlink_decode_config_response(vdlink_ConfigResponse *response, const uint8_t *buffer, size_t buffer_size);
#ifdef __cplusplus
}
#endif

#endif // VD_LINK_H