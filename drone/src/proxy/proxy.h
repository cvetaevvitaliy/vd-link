/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef PROXY_H
#define PROXY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize proxy module
 * @return 0 on success, -1 on error
 */
int proxy_init(void);

/**
 * @brief Setup proxy tunnels to remote server
 * @param remote_ip Remote server IP address
 * @param stream_port Remote stream port (usually 5602)
 * @param cmd_downlink_port Remote command downlink port (usually 5610)
 * @param cmd_uplink_port Remote command uplink port (unused, kept for compatibility)
 * @param rc_port Remote RC port
 * @return 0 on success, -1 on error
 * 
 * This function sets up:
 * - socat tunnel for stream: 5602 -> remote:stream_port
 * - socat tunnel for command downlink: 5610 -> remote:cmd_downlink_port
 * - drone_nat_proxy program with arguments: remote_ip cmd_downlink_port rc_port
 */
int proxy_setup_tunnels(const char* remote_ip, int stream_port, int cmd_downlink_port, int cmd_uplink_port, int rc_port);

/**
 * @brief Stop all proxy tunnels and drone_nat_proxy
 * @return 0 on success, -1 on error
 */
int proxy_stop_tunnels(void);

/**
 * @brief Check if proxy is active
 * @return true if proxy is running, false otherwise
 */
bool proxy_is_active(void);

/**
 * @brief Cleanup proxy module
 */
void proxy_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* PROXY_H */
