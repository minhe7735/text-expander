#include <zmk/hid_utils.h>
#include <zmk/endpoints.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap_utils.h>

LOG_MODULE_REGISTER(hid_utils, LOG_LEVEL_DBG);

int send_and_flush_key_action(uint32_t keycode, bool pressed) {
    LOG_DBG("Sending key action: keycode=0x%04X, pressed=%s", keycode, pressed ? "true" : "false");
    int ret = send_key_action(keycode, pressed);
    if (ret < 0) {
        LOG_ERR("Failed to send key action: %d", ret);
        return ret;
    }
    
    LOG_DBG("Flushing HID report for usage page 0x%02X", HID_USAGE_KEY);
    return zmk_endpoints_send_report(HID_USAGE_KEY);
}
