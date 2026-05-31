#pragma once
#include <stdint.h>

enum ble_state_t {
    BLE_STATE_INIT,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
};

void ble_init(void);
void ble_tick(void);
ble_state_t ble_get_state(void);
const char* ble_get_device_name(void);
const char* ble_get_mac_address(void);
void ble_clear_bonds(void);
bool ble_has_data(void);
const char* ble_get_data(void);
void ble_send_ack(void);
void ble_send_nack(void);
void ble_request_refresh(void);

// AI chat — request a preset question by index; response arrives in chunks.
void ble_send_chat_request(uint8_t question_idx);
bool ble_chat_has_update(void);   // true once when a new chunk/end/error arrived
const char* ble_chat_text(void);  // accumulated response text so far
bool ble_chat_is_complete(void);
bool ble_chat_has_error(void);

// Request the host to open the Claude app
void ble_send_open_app(void);

// Yeelight control — executed by the host daemon.
void ble_send_light_power(bool on);
void ble_send_light_brightness(uint8_t brightness);
void ble_send_light_color(uint8_t r, uint8_t g, uint8_t b);
void ble_send_light_scene(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b);
void ble_send_light_alert_red(void);

// BLE HID keyboard
void ble_keyboard_press(uint8_t key, uint8_t modifier);
void ble_keyboard_release(void);
