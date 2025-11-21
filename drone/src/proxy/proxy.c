/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "log.h"

static const char* module_name_str = "PROXY";

static bool proxy_active = false;
static char current_remote_ip[256] = {0};
static int current_stream_port = 0;
static int current_cmd_downlink_port = 0;
static int current_cmd_uplink_port = 0;
static int current_rc_port = 0;

int proxy_init(void)
{
    INFO("Proxy module initialized");
    return 0;
}

static int kill_proxy_processes(void)
{
    int ret;
    
    INFO("Killing all proxy processes (socat and drone_nat_proxy)");
    
    // Kill all socat processes
    ret = system("pkill -f socat");
    if (ret != 0) {
        DEBUG("pkill socat returned %d (may be normal if no socat processes were running)", ret);
    }
    
    // Kill drone_nat_proxy processes
    ret = system("pkill -f drone_nat_proxy");
    if (ret != 0) {
        DEBUG("pkill drone_nat_proxy returned %d (may be normal if no drone_nat_proxy processes were running)", ret);
    }
    
    // Give processes time to terminate
    usleep(100000); // 100ms
    
    // Force kill any remaining processes
    ret = system("pkill -9 -f socat");
    if (ret != 0) {
        DEBUG("pkill -9 socat returned %d (may be normal if no socat processes were running)", ret);
    }
    
    ret = system("pkill -9 -f drone_nat_proxy");
    if (ret != 0) {
        DEBUG("pkill -9 drone_nat_proxy returned %d (may be normal if no drone_nat_proxy processes were running)", ret);
    }
    
    // Wait a bit more to ensure cleanup
    usleep(100000); // 100ms
    
    return 0;
}

static int start_socat_tunnel(const char* local_port, const char* remote_ip, const char* remote_port)
{
    char command[512];
    int ret;
    
    // Build socat command
    snprintf(command, sizeof(command), 
            "socat -u UDP4-LISTEN:%s,bind=127.0.0.1,fork UDP:%s:%s &",
            local_port, remote_ip, remote_port);
    
    INFO("Starting socat tunnel: %s", command);
    
    ret = system(command);
    if (ret != 0) {
        ERROR("Failed to start socat tunnel: %s", command);
        return -1;
    }
    
    return 0;
}

int proxy_setup_tunnels(const char* remote_ip, int stream_port, int cmd_downlink_port, int cmd_uplink_port, int rc_port)
{
    if (remote_ip == NULL || strlen(remote_ip) == 0) {
        ERROR("Invalid remote IP address");
        return -1;
    }
    
    if (stream_port <= 0 || stream_port > 65535) {
        ERROR("Invalid stream port: %d", stream_port);
        return -1;
    }
    
    if (cmd_downlink_port <= 0 || cmd_downlink_port > 65535) {
        ERROR("Invalid cmd_downlink port: %d", cmd_downlink_port);
        return -1;
    }
    
    if (cmd_uplink_port <= 0 || cmd_uplink_port > 65535) {
        ERROR("Invalid cmd_uplink port: %d", cmd_uplink_port);
        return -1;
    }
    
    if (rc_port <= 0 || rc_port > 65535) {
        ERROR("Invalid rc port: %d", rc_port);
        return -1;
    }
    
    INFO("Setting up proxy tunnels to %s (stream:%d, cmd_downlink:%d, cmd_uplink:%d, rc:%d)", 
         remote_ip, stream_port, cmd_downlink_port, cmd_uplink_port, rc_port);
    
    // Stop any existing tunnels first
    proxy_stop_tunnels();
    
    // Start new tunnels
    char stream_port_str[16];
    char cmd_downlink_port_str[16];
    char cmd_uplink_port_str[16];
    char rc_port_str[16];
    char nat_proxy_command[512];
    
    snprintf(stream_port_str, sizeof(stream_port_str), "%d", stream_port);
    snprintf(cmd_downlink_port_str, sizeof(cmd_downlink_port_str), "%d", cmd_downlink_port);
    snprintf(cmd_uplink_port_str, sizeof(cmd_uplink_port_str), "%d", cmd_uplink_port);
    snprintf(rc_port_str, sizeof(rc_port_str), "%d", rc_port);
    
    // Start stream tunnel (5602 -> remote:stream_port)
    if (start_socat_tunnel("5602", remote_ip, stream_port_str) != 0) {
        ERROR("Failed to start stream tunnel");
        return -1;
    }
    
    // Start command downlink tunnel (5610 -> remote:cmd_downlink_port)
    if (start_socat_tunnel("5610", remote_ip, cmd_downlink_port_str) != 0) {
        ERROR("Failed to start command downlink tunnel");
        proxy_stop_tunnels(); // Cleanup on failure
        return -1;
    }
    
    // Start drone_nat_proxy program
    snprintf(nat_proxy_command, sizeof(nat_proxy_command), 
            "/usr/bin/drone_nat_proxy %s %d %d %d &",
            remote_ip, cmd_uplink_port, rc_port);
    
    INFO("Starting drone_nat_proxy: %s", nat_proxy_command);
    
    int ret = system(nat_proxy_command);
    if (ret != 0) {
        ERROR("Failed to start drone_nat_proxy: %s", nat_proxy_command);
        proxy_stop_tunnels(); // Cleanup on failure
        return -1;
    }
    
    // Store current configuration
    strncpy(current_remote_ip, remote_ip, sizeof(current_remote_ip) - 1);
    current_remote_ip[sizeof(current_remote_ip) - 1] = '\0';
    current_stream_port = stream_port;
    current_cmd_downlink_port = cmd_downlink_port;
    current_cmd_uplink_port = cmd_uplink_port;
    current_rc_port = rc_port;
    proxy_active = true;
    
    INFO("Proxy tunnels and drone_nat_proxy started successfully");
    return 0;
}

int proxy_stop_tunnels(void)
{
    if (!proxy_active) {
        DEBUG("Proxy tunnels are not active");
        return 0;
    }
    
    INFO("Stopping proxy tunnels and drone_nat_proxy");
    
    if (kill_proxy_processes() != 0) {
        ERROR("Failed to stop proxy processes");
        return -1;
    }
    
    // Clear current configuration
    memset(current_remote_ip, 0, sizeof(current_remote_ip));
    current_stream_port = 0;
    current_cmd_downlink_port = 0;
    current_cmd_uplink_port = 0;
    current_rc_port = 0;
    proxy_active = false;
    
    INFO("Proxy tunnels stopped");
    return 0;
}

bool proxy_is_active(void)
{
    return proxy_active;
}

void proxy_cleanup(void)
{
    INFO("Cleaning up proxy module");
    
    if (proxy_active) {
        proxy_stop_tunnels();
    }
    
    INFO("Proxy module cleaned up");
}
