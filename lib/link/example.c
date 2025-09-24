#include <stdio.h>
#include <string.h>
#include "link.h"

int main() {
    printf("VD-Link protobuf example\n");
    
    // Create a simple message using helper function
    vdlink_SimpleMessage message = vdlink_create_simple_message(42, "test_message", 3.14f);
    
    // Create buffer for encoding
    uint8_t buffer[128];
    size_t bytes_written;
    
    // Encode the message using library function
    int result = vdlink_encode_simple_message(&message, buffer, sizeof(buffer), &bytes_written);
    
    if (result != 0) {
        printf("Encoding failed\n");
        return 1;
    }
    
    printf("Encoded message size: %zu bytes\n", bytes_written);
    
    // Decode the message back using library function
    vdlink_SimpleMessage decoded_message;
    result = vdlink_decode_simple_message(&decoded_message, buffer, bytes_written);
    
    if (result != 0) {
        printf("Decoding failed\n");
        return 1;
    }
    
    printf("Decoded message: id=%d, name=%s, value=%.2f\n", 
           decoded_message.id, decoded_message.name, decoded_message.value);
    
    // Test config request/response
    vdlink_ConfigRequest request = vdlink_ConfigRequest_init_zero;
    request.has_session_id = true;
    request.session_id = 123;
    request.has_command = true;
    strncpy(request.command, "get_status", sizeof(request.command) - 1);
    
    uint8_t config_buffer[256];
    size_t config_bytes_written;
    
    result = vdlink_encode_config_request(&request, config_buffer, sizeof(config_buffer), &config_bytes_written);
    if (result == 0) {
        printf("Config request encoded: %zu bytes\n", config_bytes_written);
    }
    
    return 0;
}