#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "simple.pb.h"
#include "sub/unlucky.pb.h"
#include "link.h"

// Encode a simple message to buffer
int vdlink_encode_simple_message(const vdlink_SimpleMessage *message, uint8_t *buffer, size_t buffer_size, size_t *bytes_written) {
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    
    bool status = pb_encode(&stream, vdlink_SimpleMessage_fields, message);
    if (!status) {
        return -1; // Encoding failed
    }
    
    if (bytes_written) {
        *bytes_written = stream.bytes_written;
    }
    
    return 0; // Success
}

// Decode a simple message from buffer
int vdlink_decode_simple_message(vdlink_SimpleMessage *message, const uint8_t *buffer, size_t buffer_size) {
    pb_istream_t stream = pb_istream_from_buffer(buffer, buffer_size);
    
    // Initialize message to zero
    *message = vdlink_SimpleMessage_init_zero;
    
    bool status = pb_decode(&stream, vdlink_SimpleMessage_fields, message);
    if (!status) {
        return -1; // Decoding failed
    }
    
    return 0; // Success
}

// Encode a config request to buffer
int vdlink_encode_config_request(const vdlink_ConfigRequest *request, uint8_t *buffer, size_t buffer_size, size_t *bytes_written) {
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    
    bool status = pb_encode(&stream, vdlink_ConfigRequest_fields, request);
    if (!status) {
        return -1;
    }
    
    if (bytes_written) {
        *bytes_written = stream.bytes_written;
    }
    
    return 0;
}

// Decode a config response from buffer
int vdlink_decode_config_response(vdlink_ConfigResponse *response, const uint8_t *buffer, size_t buffer_size) {
    pb_istream_t stream = pb_istream_from_buffer(buffer, buffer_size);
    
    *response = vdlink_ConfigResponse_init_zero;
    
    bool status = pb_decode(&stream, vdlink_ConfigResponse_fields, response);
    if (!status) {
        return -1;
    }
    
    return 0;
}

// Helper function to create a simple message
vdlink_SimpleMessage vdlink_create_simple_message(int32_t id, const char *name, float value) {
    vdlink_SimpleMessage msg = vdlink_SimpleMessage_init_zero;
    
    msg.has_id = true;
    msg.id = id;
    
    if (name) {
        msg.has_name = true;
        strncpy(msg.name, name, sizeof(msg.name) - 1);
        msg.name[sizeof(msg.name) - 1] = '\0';
    }
    
    msg.has_value = true;
    msg.value = value;
    
    return msg;
}