#include "menu_wifi_settings.h"
#include "log.h"
#include "input.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_WIFI_NETWORKS 10
#define MAX_SSID_LENGTH 64

// Password dialog variables
static lv_obj_t *password_dialog = NULL;
static lv_obj_t *password_textarea = NULL;
static lv_obj_t *password_keyboard = NULL;
static lv_group_t *password_group = NULL; // Input group for password dialog
static char current_ssid[MAX_SSID_LENGTH] = {0};

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0

static const char* module_name_str = "MENU WIFI SETTINGS";

static lv_obj_t *wifi_menu_container = NULL;
static lv_obj_t *network_list = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *status_label = NULL;
static lv_group_t *focus_group = NULL; // Minimal group for input focus
static lv_group_t *previous_group = NULL; // Previous input group to restore
static lv_timer_t *scan_timer = NULL; // Timer for checking scan completion

static int current_focus = 0; // 0 = first network, -1 = scan btn, -2 = back btn
static int current_column = 0; // 0 = left (networks), 1 = right (buttons)
static int left_focus = 0;    // Focus within left column (network index)
static int right_focus = 0;   // Focus within right column (0 = scan, 1 = back)
static lv_obj_t *network_items[MAX_WIFI_NETWORKS];

typedef struct {
    char ssid[MAX_SSID_LENGTH];
    int signal_strength;
    bool is_connected;
    bool is_secured;
} wifi_network_t;

static wifi_network_t wifi_networks[MAX_WIFI_NETWORKS];
static int wifi_network_count = 0;

// Function prototypes
static void show_password_dialog(const char* ssid);

// Асинхронне сканування мереж
static pthread_t scan_thread;
static bool scan_in_progress = false;
static bool scan_completed = false;
static pthread_mutex_t scan_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
static int scan_wifi_networks(void);
static void update_network_list(void);

// Функція фонового сканування мереж
static void* background_scan_thread(void* arg)
{
    DEBUG("Background WiFi scan started");
    
    pthread_mutex_lock(&scan_mutex);
    scan_in_progress = true;
    scan_completed = false;
    pthread_mutex_unlock(&scan_mutex);
    
    // Виконуємо сканування
    int result = scan_wifi_networks();
    
    pthread_mutex_lock(&scan_mutex);
    scan_in_progress = false;
    scan_completed = true;
    pthread_mutex_unlock(&scan_mutex);
    
    DEBUG("Background WiFi scan completed, found %d networks", result);
    return NULL;
}

// Запуск асинхронного сканування
static void start_background_scan(void)
{
    pthread_mutex_lock(&scan_mutex);
    if (!scan_in_progress) {
        scan_completed = false;
        int result = pthread_create(&scan_thread, NULL, background_scan_thread, NULL);
        if (result != 0) {
            ERROR("Failed to create background scan thread");
            scan_in_progress = false;
        }
        pthread_detach(scan_thread); // Автоматичне очищення після завершення
    }
    pthread_mutex_unlock(&scan_mutex);
}

// Перевірка чи завершилося фонове сканування
static bool check_scan_completed(void)
{
    bool completed;
    pthread_mutex_lock(&scan_mutex);
    completed = scan_completed;
    if (completed) {
        scan_completed = false; // Скидуємо прапор
    }
    pthread_mutex_unlock(&scan_mutex);
    return completed;
}

// Callback для таймера перевірки сканування
static void scan_timer_callback(lv_timer_t *timer)
{
    if (check_scan_completed()) {
        DEBUG("Scan completed, updating network list");
        update_network_list();
        
        // Зупиняємо таймер
        if (scan_timer) {
            lv_timer_del(scan_timer);
            scan_timer = NULL;
        }
    }
}


// Network Manager integration functions
static int scan_wifi_networks(void)
{
    FILE *fp;
    char buffer[512];
    wifi_network_count = 0;
    
    // First, scan for available networks
    fp = popen("nmcli dev wifi rescan", "r");
    if (fp) {
        pclose(fp);
        // Small delay to let scan complete
        usleep(500000); // 0.5 seconds
    }
    
    // Get list of available networks
    fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY,IN-USE dev wifi list", "r");
    if (!fp) {
        ERROR("Failed to execute nmcli command");
        return 0;
    }
    
    while (fgets(buffer, sizeof(buffer), fp) && wifi_network_count < MAX_WIFI_NETWORKS) {
        // Parse nmcli output format: SSID:SIGNAL:SECURITY:IN-USE
        char *ssid = strtok(buffer, ":");
        char *signal = strtok(NULL, ":");
        char *security = strtok(NULL, ":");
        char *in_use = strtok(NULL, ":\n");
        
        if (ssid && signal && strlen(ssid) > 0 && strcmp(ssid, "--") != 0) {
            strncpy(wifi_networks[wifi_network_count].ssid, ssid, MAX_SSID_LENGTH - 1);
            wifi_networks[wifi_network_count].ssid[MAX_SSID_LENGTH - 1] = '\0';
            wifi_networks[wifi_network_count].signal_strength = signal ? atoi(signal) : 0;
            wifi_networks[wifi_network_count].is_secured = (security && strlen(security) > 0 && strcmp(security, "--") != 0);
            wifi_networks[wifi_network_count].is_connected = (in_use && strcmp(in_use, "*") == 0);
            wifi_network_count++;
        }
    }
    
    pclose(fp);
    DEBUG("Found %d WiFi networks", wifi_network_count);
    return wifi_network_count;
}

static bool connect_to_network(const char* ssid)
{
    char command[512];
    FILE *fp;
    char output[1024] = {0};
    char line[256];
    int result = 0;
    
    DEBUG("Attempting to connect to network: %s", ssid);
    
    // First try to connect to known network (might be saved password)
    snprintf(command, sizeof(command), "nmcli dev wifi connect \"%s\" 2>&1", ssid);
    fp = popen(command, "r");
    if (fp) {
        // Read command output
        while (fgets(line, sizeof(line), fp) != NULL) {
            strncat(output, line, sizeof(output) - strlen(output) - 1);
        }
        result = pclose(fp);
        
        DEBUG("nmcli output: %s", output);
        DEBUG("nmcli exit code: %d", WEXITSTATUS(result));
        
        // Check for successful connection
        if (strstr(output, "successfully activated") || 
            strstr(output, "Connection successfully activated")) {
            INFO("Successfully connected to %s", ssid);
            
            // Update status label to show success
            if (status_label) {
                char status_text[128];
                snprintf(status_text, sizeof(status_text), "Connected to:\n%s", ssid);
                lv_label_set_text(status_label, status_text);
                lv_obj_set_style_text_color(status_label, lv_color_hex(0x4CAF50), 0); // Green
            }
            
            // Start background scan to update network list with new connection status
            start_background_scan();
            if (scan_timer) {
                lv_timer_del(scan_timer);
            }
            scan_timer = lv_timer_create(scan_timer_callback, 500, NULL);
            
            return true;
        }
        
        // Check if password/secrets are required
        if (strstr(output, "Secrets were required") || 
            strstr(output, "secrets were required") ||
            strstr(output, "password") || 
            strstr(output, "Password")) {
            DEBUG("Password required for %s, showing password dialog", ssid);
            show_password_dialog(ssid);
            return true; // Return true since we're handling it with dialog
        }
    }
    
    WARN("Failed to connect to %s: %s", ssid, output);
    
    // Update status label to show general failure (if not password-related)
    if (status_label && !strstr(output, "Secrets were required") && 
        !strstr(output, "secrets were required") && 
        !strstr(output, "password") && !strstr(output, "Password")) {
        lv_label_set_text(status_label, "Connection failed\nTry again");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xF44336), 0); // Red
    }
    
    return false;
}

static void disconnect_from_network(void)
{
    FILE *fp;
    int result = 0;
    
    DEBUG("Disconnecting from current network");
    
    fp = popen("nmcli dev disconnect wlan0", "r");
    if (fp) {
        result = pclose(fp);
        if (WIFEXITED(result) && WEXITSTATUS(result) == 0) {
            INFO("Successfully disconnected");
        } else {
            WARN("Failed to disconnect");
        }
    } else {
        ERROR("Failed to execute disconnect command");
    }
}
// Forward declarations
static void update_network_list(void);
static void network_item_clicked(lv_event_t *e);
static void scan_button_clicked(lv_event_t *e);
static void back_button_clicked(lv_event_t *e);
static void wifi_key_handler(lv_event_t *e);
static void update_focus_visual(void);
static void wifi_network_action(int network_index);

// Update network list display
static void update_network_list(void)
{
    if (!network_list) return;
    
    DEBUG("Updating network list");
    
    // Clear existing items
    lv_obj_clean(network_list);
    
    // Clear network_items array
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
        network_items[i] = NULL;
    }
    
    // Update status
    char *connected_ssid = NULL;
    for (int i = 0; i < wifi_network_count; i++) {
        if (wifi_networks[i].is_connected) {
            connected_ssid = wifi_networks[i].ssid;
            break;
        }
    }
    
    if (status_label) {
        if (scan_in_progress) {
            lv_label_set_text(status_label, "Scanning...");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0); // Orange
        } else if (connected_ssid) {
            char status_text[128];
            snprintf(status_text, sizeof(status_text), "Connected to:\n%s", connected_ssid);
            lv_label_set_text(status_label, status_text);
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x4CAF50), 0); // Green
        } else {
            lv_label_set_text(status_label, "Not connected\nSelect network");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0); // Gray
        }
    }
    
    // Add network items
    for (int i = 0; i < wifi_network_count && i < MAX_WIFI_NETWORKS; i++) {
        char network_label[128];
        
        // Format network label with connection status, security and signal strength
        if (wifi_networks[i].is_connected) {
            snprintf(network_label, sizeof(network_label), "[*] %s%s (%d%%)",
                    wifi_networks[i].ssid,
                    wifi_networks[i].is_secured ? " [SEC]" : "",
                    wifi_networks[i].signal_strength);
        } else {
            snprintf(network_label, sizeof(network_label), "    %s%s (%d%%)",
                    wifi_networks[i].ssid,
                    wifi_networks[i].is_secured ? " [SEC]" : "",
                    wifi_networks[i].signal_strength);
        }
        
        lv_obj_t *network_item = lv_list_add_btn(network_list, NULL, network_label);
        network_items[i] = network_item; // Store reference for manual focus
        lv_obj_set_user_data(network_item, (void*)(intptr_t)i); // Store network index
        lv_obj_add_event_cb(network_item, network_item_clicked, LV_EVENT_CLICKED, NULL);
        
        // Style for connected network
        if (wifi_networks[i].is_connected) {
            lv_obj_set_style_bg_color(network_item, lv_color_hex(0x2E7D32), 0); // Green background
            lv_obj_set_style_text_color(network_item, lv_color_white(), 0);
        } else {
            // Default network item styling
            lv_obj_set_style_bg_color(network_item, lv_color_make(30, 30, 30), LV_STATE_DEFAULT);
        }
        
        // Make sure it can be focused
        lv_obj_add_flag(network_item, LV_OBJ_FLAG_CLICKABLE);
        
        DEBUG("Added network item %d (%s)", i, wifi_networks[i].ssid);
    }
    
    // Set initial focus on left column, first network if available
    current_column = 0; // Start in left column
    if (wifi_network_count > 0) {
        left_focus = 0; // Focus on first network
    } else {
        left_focus = 0; // Will be handled properly by update_focus_visual
        current_column = 1; // Switch to right column if no networks
        right_focus = 0; // Focus on scan button
    }
    
    // Update visual focus
    update_focus_visual();
    
    DEBUG("Network list updated with %d networks, column: %d", wifi_network_count, current_column);
}

// Event handlers
static void network_item_clicked(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    int network_index = (int)(intptr_t)lv_obj_get_user_data(item);
    DEBUG("Network item clicked: index %d", network_index);
    wifi_network_action(network_index);
}

static void scan_button_clicked(lv_event_t *e)
{
    DEBUG("Scan button clicked - starting background scan");
    
    // Показуємо що сканування почалося
    if (status_label) {
        lv_label_set_text(status_label, "Scanning...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0); // Orange
    }
    
    // Запускаємо фонове сканування
    start_background_scan();
    
    // Запускаємо таймер для перевірки завершення
    if (scan_timer) {
        lv_timer_del(scan_timer);
    }
    scan_timer = lv_timer_create(scan_timer_callback, 500, NULL); // Перевіряємо кожні 500мс
}

static void back_button_clicked(lv_event_t *e)
{
    DEBUG("Back button clicked");
    hide_menu_wifi_settings(NULL);
}

static void wifi_key_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        
        DEBUG("WiFi key pressed: %u, column: %d", key, current_column);
        
        // Handle left/right navigation (switch columns)
        if (key == LV_KEY_LEFT || key == 3) { // LEFT arrow or joystick left
            current_column = 0; // Switch to left column (networks)
            DEBUG("Switched to left column (networks)");
            update_focus_visual();
            return;
        } else if (key == LV_KEY_RIGHT || key == 4) { // RIGHT arrow or joystick right
            current_column = 1; // Switch to right column (buttons)
            DEBUG("Switched to right column (buttons)");
            update_focus_visual();
            return;
        }
        
        // Handle up/down navigation within current column
        if (key == LV_KEY_UP || key == 1) { // UP arrow or joystick up
            if (current_column == 0) {
                // Navigate within networks
                if (wifi_network_count > 0) {
                    left_focus--;
                    if (left_focus < 0) {
                        left_focus = wifi_network_count - 1; // Wrap to last network
                    }
                    DEBUG("Left focus moved up to %d", left_focus);
                }
            } else {
                // Navigate within buttons
                right_focus--;
                if (right_focus < 0) {
                    right_focus = 1; // Wrap to back button
                }
                DEBUG("Right focus moved up to %d", right_focus);
            }
            update_focus_visual();
            return;
        } else if (key == LV_KEY_DOWN || key == 18) { // DOWN arrow or joystick down
            if (current_column == 0) {
                // Navigate within networks
                if (wifi_network_count > 0) {
                    left_focus++;
                    if (left_focus >= wifi_network_count) {
                        left_focus = 0; // Wrap to first network
                    }
                    DEBUG("Left focus moved down to %d", left_focus);
                }
            } else {
                // Navigate within buttons
                right_focus++;
                if (right_focus > 1) {
                    right_focus = 0; // Wrap to scan button
                }
                DEBUG("Right focus moved down to %d", right_focus);
            }
            update_focus_visual();
            return;
        } else if (key == LV_KEY_ENTER || key == 5) { // ENTER or joystick center
            if (current_column == 0) {
                // Activate network in left column
                if (left_focus >= 0 && left_focus < wifi_network_count) {
                    wifi_network_action(left_focus);
                    DEBUG("Activated network %d", left_focus);
                }
            } else {
                // Activate button in right column
                if (right_focus == 0) {
                    // Scan button
                    scan_button_clicked(NULL);
                    DEBUG("Activated scan button");
                } else if (right_focus == 1) {
                    // Back button
                    back_button_clicked(NULL);
                    DEBUG("Activated back button");
                }
            }
            return;
        }
        
        // Handle back button (B button or ESC)
        if (key == 7 || key == 11 || key == 27 || key == LV_KEY_ESC) {
            DEBUG("Back key pressed");
            hide_menu_wifi_settings(NULL);
            return;
        }
    }
}

static void update_focus_visual()
{
    // Clear all network highlights
    for (int i = 0; i < wifi_network_count && i < MAX_WIFI_NETWORKS; i++) {
        if (network_items[i] && lv_obj_is_valid(network_items[i])) {
            lv_obj_clear_state(network_items[i], LV_STATE_FOCUSED);
            lv_obj_set_style_bg_color(network_items[i], lv_color_make(30, 30, 30), LV_STATE_DEFAULT);
        }
    }
    
    // Clear button highlights
    if (scan_btn && lv_obj_is_valid(scan_btn)) {
        lv_obj_clear_state(scan_btn, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(scan_btn, lv_color_make(60, 60, 60), LV_STATE_DEFAULT);
    }
    if (back_btn && lv_obj_is_valid(back_btn)) {
        lv_obj_clear_state(back_btn, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 60, 60), LV_STATE_DEFAULT);
    }
    
    // Highlight based on current column and focus
    if (current_column == 0) {
        // Left column (networks) is active
        if (left_focus >= 0 && left_focus < wifi_network_count && 
            network_items[left_focus] && lv_obj_is_valid(network_items[left_focus])) {
            lv_obj_add_state(network_items[left_focus], LV_STATE_FOCUSED);
            lv_obj_set_style_bg_color(network_items[left_focus], lv_color_make(0, 120, 215), LV_STATE_DEFAULT);
            DEBUG("Highlighted network %d in left column", left_focus);
        }
    } else {
        // Right column (buttons) is active
        if (right_focus == 0) {
            // Highlight scan button
            if (scan_btn && lv_obj_is_valid(scan_btn)) {
                lv_obj_add_state(scan_btn, LV_STATE_FOCUSED);
                lv_obj_set_style_bg_color(scan_btn, lv_color_make(0, 120, 215), LV_STATE_DEFAULT);
                DEBUG("Highlighted scan button in right column");
            }
        } else if (right_focus == 1) {
            // Highlight back button
            if (back_btn && lv_obj_is_valid(back_btn)) {
                lv_obj_add_state(back_btn, LV_STATE_FOCUSED);
                lv_obj_set_style_bg_color(back_btn, lv_color_make(0, 120, 215), LV_STATE_DEFAULT);
                DEBUG("Highlighted back button in right column");
            }
        }
    }
}

static void wifi_network_action(int network_index)
{
    if (network_index < 0 || network_index >= wifi_network_count) {
        DEBUG("Invalid network index: %d", network_index);
        return;
    }
    
    DEBUG("Selected network %d: %s", network_index, wifi_networks[network_index].ssid);
    
    // Connect to selected network
    connect_to_network(wifi_networks[network_index].ssid);
}

lv_obj_t *show_menu_wifi_settings(lv_obj_t *parent)
{
    if (wifi_menu_container) {
        DEBUG("WiFi menu already exists, cleaning up first");
        hide_menu_wifi_settings(NULL);
    }
    
    // Reset all state variables to ensure clean start
    current_focus = 0;
    current_column = 0;
    left_focus = 0;
    right_focus = 0;
    wifi_network_count = 0;
    
    // Clear arrays
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
        network_items[i] = NULL;
    }
    
    DEBUG("Creating new WiFi menu");
    
    // Create main container
    wifi_menu_container = lv_obj_create(parent);
    lv_obj_set_size(wifi_menu_container, 800, 480);
    lv_obj_set_style_bg_color(wifi_menu_container, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(wifi_menu_container, LV_OPA_90, 0);
    lv_obj_set_style_radius(wifi_menu_container, 10, 0);
    lv_obj_center(wifi_menu_container);
    lv_obj_add_flag(wifi_menu_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_menu_container, wifi_key_handler, LV_EVENT_KEY, NULL);
    
    // Create title
    lv_obj_t *title = lv_label_create(wifi_menu_container);
    lv_label_set_text(title, "WiFi Networks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Create network list
    network_list = lv_list_create(wifi_menu_container);
    lv_obj_set_size(network_list, 480, 360);
    lv_obj_align(network_list, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(network_list, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(network_list, 0, 0);
    lv_obj_set_style_text_font(network_list, &lv_font_montserrat_20, 0);
    
    // Create right column container for buttons
    lv_obj_t *right_container = lv_obj_create(wifi_menu_container);
    lv_obj_set_size(right_container, 220, 360);
    lv_obj_set_pos(right_container, 520, 20);
    lv_obj_set_style_bg_color(right_container, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(right_container, 5, 0);
    
    // Create status info
    status_label = lv_label_create(right_container);
    lv_label_set_text(status_label, "Loading...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(status_label, 5, 10);
    lv_obj_set_size(status_label, 180, 60);
    
    // Create Scan button
    scan_btn = lv_btn_create(right_container);
    lv_obj_set_size(scan_btn, 150, 50);
    lv_obj_set_pos(scan_btn, 5, 75);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_24, 0);
    lv_obj_center(scan_label);
    lv_obj_add_event_cb(scan_btn, scan_button_clicked, LV_EVENT_CLICKED, NULL);
    
    // Create Back button
    back_btn = lv_btn_create(right_container);
    lv_obj_set_size(back_btn, 150, 50);
    lv_obj_set_pos(back_btn, 5, 175);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_button_clicked, LV_EVENT_CLICKED, NULL);
    
    // Set focus styles for buttons
    lv_obj_set_style_bg_color(scan_btn, lv_color_make(60, 60, 60), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 60, 60), LV_STATE_DEFAULT);
    
    // Initialize WiFi networks with background scan
    if (status_label) {
        lv_label_set_text(status_label, "Scanning...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0); // Orange
    }
    
    // Запускаємо фонове сканування при створенні меню
    start_background_scan();
    
    // Запускаємо таймер для перевірки завершення
    scan_timer = lv_timer_create(scan_timer_callback, 500, NULL);
    
    // Показуємо початковий пустий список
    update_network_list();
    
    // Create a temporary group just to set input focus on our container
    // Save previous input group first
    previous_group = ui_get_input_group();
    DEBUG("Saved previous input group: %p", previous_group);
    
    focus_group = lv_group_create();
    lv_group_add_obj(focus_group, wifi_menu_container);
    ui_set_input_group(focus_group);
    
    DEBUG("WiFi menu setup complete with manual focus management and input focus set");
    
    DEBUG("WiFi menu created successfully");
    return wifi_menu_container;
}

void hide_menu_wifi_settings(void* arg)
{
    if (wifi_menu_container) {
        DEBUG("Hiding WiFi menu");
        
        // Stop scan timer if running
        if (scan_timer) {
            lv_timer_del(scan_timer);
            scan_timer = NULL;
        }
        
        // Clear network items array first
        for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
            network_items[i] = NULL;
        }
        
        // Clear button references
        scan_btn = NULL;
        back_btn = NULL;
        network_list = NULL;
        status_label = NULL;
        
        // Delete the menu container (this will delete all child objects)
        lv_obj_del(wifi_menu_container);
        wifi_menu_container = NULL;
        
        // Clean up focus group and restore previous input group
        if (focus_group) {
            lv_group_del(focus_group);
            focus_group = NULL;
        }
        
        // Restore previous input group
        if (previous_group) {
            DEBUG("Restoring previous input group: %p", previous_group);
            ui_set_input_group(previous_group);
            previous_group = NULL;
        } else {
            DEBUG("No previous group to restore, setting NULL");
            ui_set_input_group(NULL);
        }
        DEBUG("Input group cleared");
        
        // Reset focus variables
        current_focus = 0;
        current_column = 0;
        left_focus = 0;
        right_focus = 0;
        
        DEBUG("WiFi menu cleaned up and focus restored");
    }
}

// Function to refresh WiFi networks list - can be called externally
void wifi_settings_refresh_networks(void)
{
    if (network_list) {
        DEBUG("Refreshing WiFi settings menu");
        scan_wifi_networks();
        update_network_list();
        DEBUG("Network scan completed for refresh");
    }
}

// Password dialog event handlers
static void password_textarea_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        if (password_keyboard) {
            lv_keyboard_set_textarea(password_keyboard, password_textarea);
        }
    }
}

static void password_keyboard_event_cb(lv_event_t* e) {
    lv_obj_t* kb = lv_event_get_target(e);
    
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        DEBUG("Keyboard event: %s", code == LV_EVENT_READY ? "READY" : "CANCEL");
        
        if (code == LV_EVENT_READY) {
            // Connect with password
            const char* password = lv_textarea_get_text(password_textarea);
            DEBUG("Connecting to %s with password", current_ssid);
            
            // Call nmcli to connect with password
            char cmd[512];
            char output[1024] = {0};
            char line[256];
            FILE *fp;
            
            snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect \"%s\" password \"%s\" 2>&1", 
                    current_ssid, password);
            
            fp = popen(cmd, "r");
            if (fp) {
                // Read command output
                while (fgets(line, sizeof(line), fp) != NULL) {
                    strncat(output, line, sizeof(output) - strlen(output) - 1);
                }
                int result = pclose(fp);
                
                DEBUG("Password connection output: %s", output);
                
                if (strstr(output, "successfully activated") || 
                    strstr(output, "Connection successfully activated")) {
                    INFO("Successfully connected to %s with password", current_ssid);
                    
                    // Update status label to show success
                    if (status_label) {
                        char status_text[128];
                        snprintf(status_text, sizeof(status_text), "Connected to:\n%s", current_ssid);
                        lv_label_set_text(status_label, status_text);
                        lv_obj_set_style_text_color(status_label, lv_color_hex(0x4CAF50), 0); // Green
                    }
                    
                    // Start background scan to update network list with new connection status
                    start_background_scan();
                    if (scan_timer) {
                        lv_timer_del(scan_timer);
                    }
                    scan_timer = lv_timer_create(scan_timer_callback, 500, NULL);
                    
                } else {
                    ERROR("Failed to connect to %s: %s", current_ssid, output);
                    
                    // Update status label to show failure
                    if (status_label) {
                        lv_label_set_text(status_label, "Connection failed\nCheck password");
                        lv_obj_set_style_text_color(status_label, lv_color_hex(0xF44336), 0); // Red
                    }
                }
            } else {
                ERROR("Failed to execute nmcli command");
                
                // Update status label to show error
                if (status_label) {
                    lv_label_set_text(status_label, "Connection error\nTry again");
                    lv_obj_set_style_text_color(status_label, lv_color_hex(0xF44336), 0); // Red
                }
            }
        }
        
        // Hide password dialog
        if (password_dialog) {
            lv_obj_delete(password_dialog);
            password_dialog = NULL;
            password_textarea = NULL;
            password_keyboard = NULL;
        }
        
        // Clean up password input group
        if (password_group) {
            lv_group_delete(password_group);
            password_group = NULL;
        }
        
        // Restore focus to main menu
        if (focus_group) {
            ui_set_input_group(focus_group);
        }
    }
}

static void show_password_dialog(const char* ssid) {
    DEBUG("Showing password dialog for SSID: %s", ssid);
    
    // Store SSID for later use
    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    
    // Create modal background
    password_dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_style_bg_color(password_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(password_dialog, LV_OPA_70, 0);
    lv_obj_set_size(password_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_center(password_dialog);
    lv_obj_add_flag(password_dialog, LV_OBJ_FLAG_CLICKABLE);
    
    // Create content container
    lv_obj_t* content = lv_obj_create(password_dialog);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x2A2A2A), 0); // Dark gray instead of white
    lv_obj_set_style_border_width(content, 2, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x555555), 0);
    lv_obj_set_size(content, LV_PCT(80), LV_PCT(70));
    lv_obj_center(content);
    
    // Title label
    lv_obj_t* title = lv_label_create(content);
    lv_label_set_text_fmt(title, "Connect to: %s", ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0); // White text on dark background
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Password label
    lv_obj_t* pwd_label = lv_label_create(content);
    lv_label_set_text(pwd_label, "Password:");
    lv_obj_set_style_text_font(pwd_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pwd_label, lv_color_white(), 0); // White text
    lv_obj_align(pwd_label, LV_ALIGN_TOP_LEFT, 10, 50);
    
    // Password textarea
    password_textarea = lv_textarea_create(content);
    lv_textarea_set_one_line(password_textarea, true);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_textarea_set_placeholder_text(password_textarea, "Enter WiFi password");
    lv_obj_set_size(password_textarea, LV_PCT(90), 40);
    lv_obj_align(password_textarea, LV_ALIGN_TOP_LEFT, 10, 80);
    lv_obj_set_style_text_font(password_textarea, &lv_font_montserrat_20, 0); // Set font size to 20
    lv_obj_add_event_cb(password_textarea, password_textarea_event_cb, LV_EVENT_ALL, NULL);
    
    // Virtual keyboard
    password_keyboard = lv_keyboard_create(content);
    lv_obj_set_size(password_keyboard, LV_PCT(95), LV_PCT(50));
    lv_obj_align(password_keyboard, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_font(password_keyboard, &lv_font_montserrat_20, 0); // Set keyboard font size to 20
    lv_keyboard_set_textarea(password_keyboard, password_textarea);
    lv_obj_add_event_cb(password_keyboard, password_keyboard_event_cb, LV_EVENT_ALL, NULL);
    
    // Create input group for password dialog
    password_group = lv_group_create();
    lv_group_add_obj(password_group, password_textarea);
    lv_group_add_obj(password_group, password_keyboard);
    
    // Switch input focus to password dialog
    ui_set_input_group(password_group);
    lv_group_focus_obj(password_keyboard); // Focus keyboard instead of textarea
    
    DEBUG("Password dialog created successfully with input group focused on keyboard");
}

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0