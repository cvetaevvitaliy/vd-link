#include "wifi_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "log.h"

static const char *module_name_str = "WIFI_API";
// Static variables for lazy initialization
static struct nl_sock *nl_sock = NULL;
static int nl_family_id = -1;
static bool initialized = false;

static int interface_exists(const char *ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    int ret = ioctl(sock, SIOCGIFINDEX, &ifr);
    close(sock);
    return ret == 0;
}

static int error_callback(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg) {
    const char *operation = (const char *)arg;
    if (operation) {
        ERROR("Netlink error during %s: %s (errno=%d)", operation, strerror(-err->error), -err->error);
    } else {
        ERROR("Netlink error: %s (errno=%d)", strerror(-err->error), -err->error);
    }
    return NL_STOP;
}

static int invalid_callback(struct nl_msg *msg, void *arg) {
    INFO("Invalid netlink message received");
    return NL_SKIP;
}

static int send_netlink_request(struct nl_msg *msg, int (*valid_cb)(struct nl_msg *, void *), void *cb_arg) {
    struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        ERROR("Failed to allocate netlink callback");
        nlmsg_free(msg);
        return -1;
    }

    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, valid_cb, cb_arg);
    nl_cb_err(cb, NL_CB_CUSTOM, error_callback, "send_netlink_request");

    int err = nl_send_auto_complete(nl_sock, msg);
    if (err < 0) {
        ERROR("Failed to send netlink message: %s (errno=%d)", strerror(-err), -err);
        nl_cb_put(cb);
        nlmsg_free(msg);
        return err;
    }

    INFO("Sent netlink message, receiving response...");
    err = nl_recvmsgs(nl_sock, cb);
    if (err < 0) {
        ERROR("Failed to receive netlink messages: %s (errno=%d)", strerror(-err), -err);
    } else {
        INFO("Successfully received netlink response");
    }

    nl_cb_put(cb);
    nlmsg_free(msg);
    return err;
}

static uint32_t freq_to_channel(uint32_t freq) {
    // 2.4 GHz band: channels 1-14, 2412 + (ch-1)*5
    if (freq >= 2412 && freq <= 2484) {
        return ((freq - 2412) / 5) + 1;
    }
    // 5 GHz band: channels 36-165, 5180 + (ch-36)*5 for some, but varies
    // Simplified: 5 GHz starts around 5180
    if (freq >= 5180 && freq <= 5885) {
        // Approximate
        return ((freq - 5180) / 5) + 36;
    }
    // 6 GHz band: channels 1-233, 5955 + (ch-1)*5
    if (freq >= 5955 && freq <= 7115) {
        return ((freq - 5955) / 5) + 1;
    }
    return 0; // Unknown
}

static int frequency_callback(struct nl_msg *msg, void *arg) {
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *band, *freq;
    int rem1, rem2;
    wifi_frequency_t *frequencies = ((void **)arg)[0];
    uint32_t *count = ((void **)arg)[1];
    size_t max_count = *(size_t *)((void **)arg)[2];

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
              genlmsg_attrlen(gnlh, 0), NULL);

    if (!tb[NL80211_ATTR_WIPHY_BANDS])
        return NL_SKIP;

    nla_for_each_nested(band, tb[NL80211_ATTR_WIPHY_BANDS], rem1) {
        struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];
        
        nla_parse(tb_band, NL80211_BAND_ATTR_MAX, nla_data(band),
                  nla_len(band), NULL);

        if (!tb_band[NL80211_BAND_ATTR_FREQS])
            continue;

        nla_for_each_nested(freq, tb_band[NL80211_BAND_ATTR_FREQS], rem2) {
            struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
            
            nla_parse(tb_freq, NL80211_FREQUENCY_ATTR_MAX,
                      nla_data(freq), nla_len(freq), NULL);

            if (!tb_freq[NL80211_FREQUENCY_ATTR_FREQ])
                continue;
            if (*count < max_count) {
                frequencies[*count].frequency = nla_get_u32(tb_freq[NL80211_FREQUENCY_ATTR_FREQ]);
                frequencies[*count].channel = freq_to_channel(frequencies[*count].frequency);
                frequencies[*count].disabled = tb_freq[NL80211_FREQUENCY_ATTR_DISABLED] != NULL;
                (*count)++;
            }
        }
    }
    return NL_SKIP;
}

static int init_netlink(void) {
    if (initialized) {
        return 0; // Already initialized
    }

    // Allocate netlink socket
    nl_sock = nl_socket_alloc();
    if (!nl_sock) {
        PERROR("Failed to allocate netlink socket");
        return -1;
    }

    // Connect to generic netlink
    if (genl_connect(nl_sock) < 0) {
        PERROR("Failed to connect to generic netlink");
        nl_socket_free(nl_sock);
        nl_sock = NULL;
        return -1;
    }

    // Set buffer size
    nl_socket_set_buffer_size(nl_sock, 8192, 8192);

    // Get nl80211 family ID
    nl_family_id = genl_ctrl_resolve(nl_sock, "nl80211");
    if (nl_family_id < 0) {
        PERROR("Failed to resolve nl80211 family");
        nl_socket_free(nl_sock);
        nl_sock = NULL;
        return -1;
    }

    initialized = true;
    return 0;
}

uint32_t wifi_api_get_frequencies(char *iface, wifi_frequency_t *frequencies, size_t max_count) {
    if (!frequencies || max_count == 0) {
        return 0;
    }

    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return 0;
    }

    struct nl_msg *msg;
    struct nl_cb *cb;
    uint32_t count = 0;
    int err;

    // Allocate netlink message
    msg = nlmsg_alloc();
    if (!msg) {
        return 0;
    }

    // Setup GET_WIPHY command
    genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_flag(msg, NL80211_ATTR_SPLIT_WIPHY_DUMP);
    if (iface && *iface) {
        nla_put_string(msg, NL80211_ATTR_IFNAME, iface);
    }

    // Setup callback
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        nlmsg_free(msg);
        return 0;
    }

    void *cb_args[] = {frequencies, &count, &max_count};

    // Set valid callback to parse frequencies
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, frequency_callback, cb_args);

    // Send message and handle response
    err = nl_send_auto_complete(nl_sock, msg);
    if (err < 0) {
        nl_cb_put(cb);
        nlmsg_free(msg);
        return 0;
    }

    nl_recvmsgs(nl_sock, cb);

    // Cleanup
    nl_cb_put(cb);
    nlmsg_free(msg);

    return count;
}

int wifi_api_get_phy_index(const char *phy_name) {
    if (!phy_name || strncmp(phy_name, "phy", 3) != 0) {
        return -1;
    }
    return atoi(phy_name + 3);
}

static int interface_name_callback(struct nl_msg *msg, void *arg) {
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    char ***interfaces = (char ***)arg;
    size_t *count = (size_t *)((void **)arg)[1];
    size_t max_count = *(size_t *)((void **)arg)[2];

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
              genlmsg_attrlen(gnlh, 0), NULL);

    if (tb[NL80211_ATTR_IFNAME] && *count < max_count) {
        const char *ifname = nla_get_string(tb[NL80211_ATTR_IFNAME]);
        (*interfaces)[*count] = strdup(ifname);
        (*count)++;
        INFO("Found interface: %s", ifname);
    }
    return NL_SKIP;
}

static int interface_callback(struct nl_msg *msg, void *arg) {
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    uint32_t *freq = (uint32_t *)arg;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
              genlmsg_attrlen(gnlh, 0), NULL);

    // Try to get frequency from interface first
    if (tb[NL80211_ATTR_WIPHY_FREQ]) {
        *freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
        INFO("Current frequency: %u MHz", *freq);
        
        // Also check for center frequency
        if (tb[NL80211_ATTR_CENTER_FREQ1]) {
            uint32_t center_freq = nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ1]);
            INFO("Center frequency: %u MHz", center_freq);
        }
    }
    // For monitor mode interfaces, try getting channel info
    else if (tb[NL80211_ATTR_CENTER_FREQ1]) {
        *freq = nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ1]);
        INFO("Center frequency: %u MHz", *freq);
    }
    
    return NL_SKIP;
}

// Now using iw commands instead of netlink for bandwidth operations
#if 0
static int bandwidth_callback(struct nl_msg *msg, void *arg) {
    INFO("bandwidth_callback: CALLED - parsing netlink message");
    
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    uint32_t *bandwidth = (uint32_t *)arg;

    INFO("bandwidth_callback: genl command = %d", gnlh->cmd);
    
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
              genlmsg_attrlen(gnlh, 0), NULL);

    // Debug: print ALL available attributes
    INFO("All available attributes in netlink response:");
    bool found_any = false;
    for (int i = 0; i <= NL80211_ATTR_MAX; i++) {
        if (tb[i]) {
            found_any = true;
            int attr_len = nla_len(tb[i]);
            
            const char *attr_name = "unknown";
            switch (i) {
                case NL80211_ATTR_WIPHY_FREQ: attr_name = "WIPHY_FREQ"; break;
                case NL80211_ATTR_CENTER_FREQ1: attr_name = "CENTER_FREQ1"; break;
                case NL80211_ATTR_CHANNEL_WIDTH: attr_name = "CHANNEL_WIDTH"; break;
                case NL80211_ATTR_IFINDEX: attr_name = "IFINDEX"; break;
                case NL80211_ATTR_IFNAME: attr_name = "IFNAME"; break;
                case NL80211_ATTR_IFTYPE: attr_name = "IFTYPE"; break;
                case NL80211_ATTR_WIPHY: attr_name = "WIPHY"; break;
                case NL80211_ATTR_MAC: attr_name = "MAC"; break;
            }
            
            // Log attribute with value if it's a numeric type
            if (attr_len == 4 && (i == NL80211_ATTR_WIPHY_FREQ || i == NL80211_ATTR_CENTER_FREQ1 || 
                                  i == NL80211_ATTR_CHANNEL_WIDTH || i == NL80211_ATTR_IFINDEX || 
                                  i == NL80211_ATTR_WIPHY || i == NL80211_ATTR_IFTYPE)) {
                uint32_t val = nla_get_u32(tb[i]);
                INFO("  - Attribute %d (%s) = %u (len=%d)", i, attr_name, val, attr_len);
            } else if (i == NL80211_ATTR_IFNAME) {
                const char *ifname = nla_get_string(tb[i]);
                INFO("  - Attribute %d (%s) = '%s' (len=%d)", i, attr_name, ifname, attr_len);
            } else {
                INFO("  - Attribute %d (%s) present (len=%d)", i, attr_name, attr_len);
            }
        }
    }
    
    if (!found_any) {
        ERROR("No attributes found in netlink response at all!");
        return NL_SKIP;
    }

    // Try to get channel width from interface first
    if (tb[NL80211_ATTR_CHANNEL_WIDTH]) {
        uint32_t channel_width = nla_get_u32(tb[NL80211_ATTR_CHANNEL_WIDTH]);
        
        // Convert nl80211 channel width enum to MHz
        switch (channel_width) {
            case NL80211_CHAN_WIDTH_20_NOHT:
            case NL80211_CHAN_WIDTH_20:
                *bandwidth = 20;
                break;
            case NL80211_CHAN_WIDTH_40:
                *bandwidth = 40;
                break;
            case NL80211_CHAN_WIDTH_80:
                *bandwidth = 80;
                break;
            case NL80211_CHAN_WIDTH_160:
                *bandwidth = 160;
                break;
            case NL80211_CHAN_WIDTH_80P80:
                *bandwidth = 160; // 80+80 MHz
                break;
            default:
                *bandwidth = 20; // Default fallback
                break;
        }
        
        INFO("Found channel width enum %u, converted to bandwidth: %u MHz", channel_width, *bandwidth);
        return NL_SKIP;
    }
    
    // For monitor mode, calculate bandwidth from frequency information
    if (tb[NL80211_ATTR_WIPHY_FREQ] && tb[NL80211_ATTR_CENTER_FREQ1]) {
        uint32_t freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
        uint32_t center_freq = nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ1]);
        
        // Calculate bandwidth based on center frequency offset
        uint32_t diff = (center_freq > freq) ? (center_freq - freq) : (freq - center_freq);
        
        if (diff == 0) {
            *bandwidth = 20; // 20MHz channels have no offset
        } else if (diff == 10) {
            *bandwidth = 40; // 40MHz channels have 10MHz offset
        } else if (diff == 30) {
            *bandwidth = 80; // 80MHz channels have 30MHz offset
        } else if (diff == 70) {
            *bandwidth = 160; // 160MHz channels have 70MHz offset
        } else {
            // Try to guess based on reasonable offsets
            if (diff <= 15) {
                *bandwidth = 40;
            } else if (diff <= 40) {
                *bandwidth = 80;
            } else {
                *bandwidth = 160;
            }
        }
        
        INFO("Calculated bandwidth from freq %u MHz and center %u MHz (diff=%u): %u MHz", 
             freq, center_freq, diff, *bandwidth);
        return NL_SKIP;
    }
    
    // If only frequency is available, assume 20MHz
    if (tb[NL80211_ATTR_WIPHY_FREQ]) {
        uint32_t freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
        *bandwidth = 20; // Default for single frequency
        INFO("Only frequency %u MHz available, assuming 20MHz bandwidth", freq);
        return NL_SKIP;
    }
    
    ERROR("No frequency or channel width information found in netlink response - no usable attributes");
    return NL_SKIP;
}
#endif

#if 0
static int set_bandwidth_result_callback(struct nl_msg *msg, void *arg) {
    // This callback is called when SET_WIPHY command completes successfully
    uint32_t expected_bandwidth = *(uint32_t *)arg;
    INFO("SET_WIPHY command completed for bandwidth %u MHz", expected_bandwidth);
    return NL_SKIP;
}
#endif

uint32_t wifi_api_get_current_frequency(char *iface) {
    if (!iface || !*iface) {
        return 0;
    }

    if (!interface_exists(iface)) {
        ERROR("Interface %s does not exist", iface);
        return 0;
    }

    // Use iw command to get interface info and parse frequency
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "iw dev %s info 2>/dev/null", iface);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        ERROR("Failed to execute iw command: %s", strerror(errno));
        return 0;
    }
    
    char line[256];
    uint32_t frequency = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        // Look for line like: "channel 161 (5805 MHz), width: 40 MHz, center1: 5815 MHz"
        if (strstr(line, "channel") && strstr(line, "MHz")) {
            // Look for frequency in parentheses like "(5805 MHz)"
            char *freq_start = strchr(line, '(');
            if (freq_start) {
                int freq_val = 0;
                if (sscanf(freq_start, "(%d MHz)", &freq_val) == 1) {
                    frequency = (uint32_t)freq_val;
                    break;
                }
            }
        }
    }
    
    int status = pclose(fp);
    if (status != 0) {
        ERROR("iw command failed with status: %d", status);
        return 0;
    }
    
    if (frequency == 0) {
        ERROR("Could not parse frequency from iw info output");
    }
    
    return frequency;

#if 0
    // Original netlink code - commented out for now
    
    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return 0;
    }

    uint32_t current_freq = 0;

    // Get interface index first (like iw does)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        
        if (ioctl(sock, SIOCGIFINDEX, &ifr) == 0) {
            // Use interface index instead of name
            struct nl_msg *msg = nlmsg_alloc();
            if (msg) {
                genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_GET_INTERFACE, 0);
                nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
                
                INFO("Getting frequency for %s (ifindex=%d)", iface, ifr.ifr_ifindex);
                send_netlink_request(msg, interface_callback, &current_freq);
            }
        }
        close(sock);
    }

    return current_freq;
#endif
}

int wifi_api_set_current_frequency(char *iface, uint32_t freq) {
    if (!iface || !*iface || freq == 0) {
        return -1;
    }

    if (!interface_exists(iface)) {
        ERROR("Interface %s does not exist", iface);
        return -1;
    }

    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return -1;
    }

    // Get interface index (like iw does)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ERROR("Failed to create socket for ioctl");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    
    if (ioctl(sock, SIOCGIFINDEX, &ifr) != 0) {
        ERROR("Failed to get interface index for %s", iface);
        close(sock);
        return -1;
    }
    close(sock);

    struct nl_msg *msg;
    int err;

    // Allocate netlink message
    msg = nlmsg_alloc();
    if (!msg) {
        return -1;
    }

    // Setup SET_WIPHY command with interface index (like iw does)
    genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_SET_WIPHY, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq);

    INFO("Setting frequency %u MHz on interface %s (ifindex=%d)", freq, iface, ifr.ifr_ifindex);

    // Send message
    err = nl_send_auto_complete(nl_sock, msg);
    nlmsg_free(msg);

    if (err < 0) {
        ERROR("Failed to set frequency: %s", strerror(-err));
        return -1;
    }

    INFO("Set frequency to %u MHz on interface %s", freq, iface);
    return 0;
}

void wifi_api_cleanup(void) {
    if (nl_sock) {
        nl_socket_free(nl_sock);
        nl_sock = NULL;
    }
    nl_family_id = -1;
    initialized = false;
}

size_t wifi_api_get_interfaces_for_phy(uint32_t phy_index, char **interfaces, size_t max_count) {
    if (!interfaces || max_count == 0) {
        return 0;
    }

    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return 0;
    }

    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) {
        return 0;
    }

    genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST | NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
    nla_put_u32(msg, NL80211_ATTR_WIPHY, phy_index);

    size_t count = 0;
    void *cb_args[] = {interfaces, &count, &max_count};
    send_netlink_request(msg, interface_name_callback, cb_args);

    return count;
}

uint32_t wifi_api_get_bandwidth(char *iface) {
    if (!iface || !*iface) {
        return 0;
    }

    if (!interface_exists(iface)) {
        ERROR("Interface %s does not exist", iface);
        return 0;
    }

    // Use iw command to get interface info and parse bandwidth
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "iw dev %s info 2>/dev/null", iface);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        ERROR("Failed to execute iw command: %s", strerror(errno));
        return 0;
    }
    
    char line[256];
    uint32_t bandwidth = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        // Look for line like: "channel 161 (5805 MHz), width: 40 MHz, center1: 5815 MHz"
        if (strstr(line, "width:") && strstr(line, "MHz")) {
            char *width_pos = strstr(line, "width:");
            if (width_pos) {
                // Parse the width value
                int width_val = 0;
                if (sscanf(width_pos, "width: %d MHz", &width_val) == 1) {
                    bandwidth = (uint32_t)width_val;
                    break;
                }
            }
        }
    }
    
    int status = pclose(fp);
    if (status != 0) {
        ERROR("iw command failed with status: %d", status);
        return 0;
    }
    
    if (bandwidth == 0) {
        ERROR("Could not parse bandwidth from iw info output");
    }
    
    return bandwidth;

#if 0
    // Original netlink code - commented out for now
    
    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return 0;
    }

    uint32_t current_bandwidth = 0;

    // Get interface index first (like iw does)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        
        if (ioctl(sock, SIOCGIFINDEX, &ifr) == 0) {
            // Try GET_WIPHY command instead of GET_INTERFACE (like iw info does)
            struct nl_msg *msg = nlmsg_alloc();
            if (msg) {
                // Use GET_WIPHY with interface index to get current operating parameters
                genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_GET_WIPHY, 0);
                nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
                
                INFO("Getting bandwidth for %s (ifindex=%d) using GET_WIPHY", iface, ifr.ifr_ifindex);
                int result = send_netlink_request(msg, bandwidth_callback, &current_bandwidth);
                
                // Log the result for debugging
                if (result < 0) {
                    ERROR("send_netlink_request failed with error: %d", result);
                } else if (current_bandwidth > 0) {
                    INFO("Successfully retrieved bandwidth: %u MHz", current_bandwidth);
                } else {
                    ERROR("bandwidth_callback did not set any bandwidth value");
                    
                    // Fallback: try GET_INTERFACE as well
                    INFO("Trying fallback GET_INTERFACE approach");
                    struct nl_msg *fallback_msg = nlmsg_alloc();
                    if (fallback_msg) {
                        genlmsg_put(fallback_msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_GET_INTERFACE, 0);
                        nla_put_u32(fallback_msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
                        
                        int fallback_result = send_netlink_request(fallback_msg, bandwidth_callback, &current_bandwidth);
                        if (fallback_result >= 0 && current_bandwidth > 0) {
                            INFO("Fallback approach retrieved bandwidth: %u MHz", current_bandwidth);
                        } else {
                            ERROR("Both GET_WIPHY and GET_INTERFACE failed to retrieve bandwidth");
                        }
                    }
                }
            } else {
                ERROR("Failed to allocate netlink message");
            }
        } else {
            ERROR("Failed to get interface index for %s: %s", iface, strerror(errno));
        }
        close(sock);
    } else {
        ERROR("Failed to create socket for ioctl: %s", strerror(errno));
    }

    return current_bandwidth;
#endif
}

int wifi_api_set_bandwidth(char *iface, uint32_t bandwidth) {
    if (!iface || !*iface || bandwidth == 0) {
        return -1;
    }

    if (!interface_exists(iface)) {
        ERROR("Interface %s does not exist", iface);
        return -1;
    }

    // Get current frequency first
    uint32_t current_freq = wifi_api_get_current_frequency(iface);
    if (current_freq == 0) {
        ERROR("Could not get current frequency for %s", iface);
        return -1;
    }

    // Convert bandwidth to iw channel type
    const char *channel_type;
    switch (bandwidth) {
        case 20:
            channel_type = "HT20";
            break;
        case 40:
            channel_type = "HT40+"; // Could be HT40- depending on channel, but + works for most
            break;
        case 80:
            channel_type = "80MHz";
            break;
        case 160:
            channel_type = "160MHz";
            break;
        default:
            ERROR("Unsupported bandwidth: %u MHz", bandwidth);
            return -1;
    }

    // Calculate channel number from frequency
    uint32_t channel = 0;
    if (current_freq >= 2412 && current_freq <= 2484) {
        // 2.4 GHz band
        channel = ((current_freq - 2412) / 5) + 1;
    } else if (current_freq >= 5180 && current_freq <= 5885) {
        // 5 GHz band - simplified calculation
        channel = ((current_freq - 5180) / 5) + 36;
    } else {
        ERROR("Unknown frequency band for %u MHz", current_freq);
        return -1;
    }

    // Use iw command to set channel with bandwidth
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "iw dev %s set channel %u %s", iface, channel, channel_type);
    
    int result = system(cmd);
    if (result != 0) {
        ERROR("Failed to set bandwidth: iw command returned %d", result);
        return -1;
    }

    INFO("Successfully set bandwidth to %u MHz on interface %s", bandwidth, iface);

    // Verify the bandwidth was actually set by reading it back
    uint32_t actual_bandwidth = wifi_api_get_bandwidth(iface);
    if (actual_bandwidth == 0) {
        ERROR("Failed to read back bandwidth after setting");
        return -1;
    }

    if (actual_bandwidth != bandwidth) {
        ERROR("Bandwidth verification failed: requested %u MHz, actual %u MHz", bandwidth, actual_bandwidth);
        return -1;
    }

    return 0;

#if 0
    // Original netlink code - commented out for now
    
    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return -1;
    }

    // Convert MHz to nl80211 channel width enum
    uint32_t channel_width;
    switch (bandwidth) {
        case 20:
            channel_width = NL80211_CHAN_WIDTH_20;
            break;
        case 40:
            channel_width = NL80211_CHAN_WIDTH_40;
            break;
        case 80:
            channel_width = NL80211_CHAN_WIDTH_80;
            break;
        case 160:
            channel_width = NL80211_CHAN_WIDTH_160;
            break;
        default:
            ERROR("Unsupported bandwidth: %u MHz", bandwidth);
            return -1;
    }

    // Get interface index (like iw does)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ERROR("Failed to create socket for ioctl");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    
    if (ioctl(sock, SIOCGIFINDEX, &ifr) != 0) {
        ERROR("Failed to get interface index for %s", iface);
        close(sock);
        return -1;
    }
    close(sock);

    struct nl_msg *msg;
    struct nl_cb *cb;
    int err;

    // Allocate netlink message
    msg = nlmsg_alloc();
    if (!msg) {
        ERROR("Failed to allocate netlink message");
        return -1;
    }

    // Setup SET_WIPHY command with interface index and channel width
    genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_SET_WIPHY, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
    nla_put_u32(msg, NL80211_ATTR_CHANNEL_WIDTH, channel_width);

    INFO("Setting bandwidth %u MHz on interface %s (ifindex=%d)", bandwidth, iface, ifr.ifr_ifindex);

    // Setup callback with proper error handling
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        ERROR("Failed to allocate netlink callback");
        nlmsg_free(msg);
        return -1;
    }

    // Set callbacks
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, set_bandwidth_result_callback, &bandwidth);
    nl_cb_err(cb, NL_CB_CUSTOM, error_callback, NULL);

    // Send message
    err = nl_send_auto_complete(nl_sock, msg);
    if (err < 0) {
        ERROR("Failed to send netlink message: %s", strerror(-err));
        nl_cb_put(cb);
        nlmsg_free(msg);
        return -1;
    }

    // Receive and process response
    err = nl_recvmsgs(nl_sock, cb);
    if (err < 0) {
        ERROR("Failed to receive netlink response: %s", strerror(-err));
        nl_cb_put(cb);
        nlmsg_free(msg);
        return -1;
    }

    // Cleanup
    nl_cb_put(cb);
    nlmsg_free(msg);

    // Verify the bandwidth was actually set by reading it back
    uint32_t actual_bandwidth = wifi_api_get_bandwidth(iface);
    if (actual_bandwidth == 0) {
        ERROR("Failed to read back bandwidth after setting - interface may not support this operation");
        return -1;
    }

    if (actual_bandwidth != bandwidth) {
        ERROR("Bandwidth verification failed: requested %u MHz, actual %u MHz", bandwidth, actual_bandwidth);
        return -1;
    }

    INFO("Successfully set and verified bandwidth to %u MHz on interface %s", bandwidth, iface);
    return 0;
#endif
}
#if 0
// Combined function to set frequency and bandwidth together (like iw channel command)
int wifi_api_set_channel(char *iface, uint32_t freq, uint32_t bandwidth) {
    if (!iface || !*iface || freq == 0 || bandwidth == 0) {
        return -1;
    }

    if (!interface_exists(iface)) {
        ERROR("Interface %s does not exist", iface);
        return -1;
    }

    if (init_netlink() < 0) {
        ERROR("Failed to initialize netlink");
        return -1;
    }

    // Convert MHz to nl80211 channel width enum
    uint32_t channel_width;
    switch (bandwidth) {
        case 20:
            channel_width = NL80211_CHAN_WIDTH_20;
            break;
        case 40:
            channel_width = NL80211_CHAN_WIDTH_40;
            break;
        case 80:
            channel_width = NL80211_CHAN_WIDTH_80;
            break;
        case 160:
            channel_width = NL80211_CHAN_WIDTH_160;
            break;
        default:
            ERROR("Unsupported bandwidth: %u MHz", bandwidth);
            return -1;
    }

    // Calculate center frequency based on bandwidth
    uint32_t center_freq = freq;
    if (bandwidth == 40) {
        // For 40MHz, center frequency might be offset by ±10MHz
        center_freq = freq; // Simplified - use the same frequency
    } else if (bandwidth == 80) {
        // For 80MHz channels, calculate proper center frequency
        center_freq = freq; // Simplified
    } else if (bandwidth == 160) {
        // For 160MHz channels, calculate proper center frequency  
        center_freq = freq; // Simplified
    }

    // Get interface index (like iw does)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ERROR("Failed to create socket for ioctl");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    
    if (ioctl(sock, SIOCGIFINDEX, &ifr) != 0) {
        ERROR("Failed to get interface index for %s", iface);
        close(sock);
        return -1;
    }
    close(sock);

    struct nl_msg *msg;
    int err;

    // Allocate netlink message
    msg = nlmsg_alloc();
    if (!msg) {
        return -1;
    }

    // Setup SET_WIPHY command with all parameters (like iw channel command)
    genlmsg_put(msg, 0, 0, nl_family_id, 0, NLM_F_REQUEST, NL80211_CMD_SET_WIPHY, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq);
    nla_put_u32(msg, NL80211_ATTR_CHANNEL_WIDTH, channel_width);
    
    // Add center frequency for wider channels
    if (bandwidth >= 40) {
        nla_put_u32(msg, NL80211_ATTR_CENTER_FREQ1, center_freq);
    }

    INFO("Setting channel %u MHz (width %u MHz) on interface %s (ifindex=%d)", freq, bandwidth, iface, ifr.ifr_ifindex);

    // Send message
    err = nl_send_auto_complete(nl_sock, msg);
    nlmsg_free(msg);

    if (err < 0) {
        ERROR("Failed to set channel: %s", strerror(-err));
        return -1;
    }

    INFO("Set channel to %u MHz (width %u MHz) on interface %s", freq, bandwidth, iface);
    return 0;
}
#endif