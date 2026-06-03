/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "bt_input.h"

#include <inttypes.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "esp_hidd_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define HID_MOUSE_REPORT_SIZE 4
#define HFP_AUDIO_RINGBUF_SIZE 4096
#define HID_DROP_LOG_MS 1000

static const char *TAG = "bt_input";

static uint8_t s_hid_mouse_descriptor[] = {
    0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,  // USAGE (Mouse)
    0xa1, 0x01,  // COLLECTION (Application)
    0x09, 0x01,  //   USAGE (Pointer)
    0xa1, 0x00,  //   COLLECTION (Physical)
    0x05, 0x09,  //     USAGE_PAGE (Button)
    0x19, 0x01,  //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,  //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,  //     LOGICAL_MINIMUM (0)
    0x25, 0x01,  //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,  //     REPORT_COUNT (3)
    0x75, 0x01,  //     REPORT_SIZE (1)
    0x81, 0x02,  //     INPUT (Data,Var,Abs)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x75, 0x05,  //     REPORT_SIZE (5)
    0x81, 0x03,  //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,  //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,  //     USAGE (X)
    0x09, 0x31,  //     USAGE (Y)
    0x09, 0x38,  //     USAGE (Wheel)
    0x15, 0x81,  //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,  //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x03,  //     REPORT_COUNT (3)
    0x81, 0x06,  //     INPUT (Data,Var,Rel)
    0xc0,        //   END_COLLECTION
    0xc0,        // END_COLLECTION
};

typedef struct {
    bool bt_ready;
    bool hid_ready;
    bool hid_connected;
    bool hfp_ready;
    bool hfp_slc_connected;
    bool hfp_audio_connected;
    bool discoverable;
    bool mic_enabled;
    uint8_t protocol_mode;
    uint8_t mouse_report[HID_MOUSE_REPORT_SIZE];
    SemaphoreHandle_t lock;
    RingbufHandle_t audio_rb;
} bt_input_state_t;

static bt_input_state_t s_state = {
    .discoverable  = true,
    .protocol_mode = ESP_HIDD_REPORT_MODE,
};

static esp_hidd_app_param_t s_hid_app = {
    .name          = "JoyMouse",
    .description   = "StickC JoyMic Mouse",
    .provider      = "M5Stack",
    .subclass      = ESP_HID_CLASS_MIC,
    .desc_list     = s_hid_mouse_descriptor,
    .desc_list_len = sizeof(s_hid_mouse_descriptor),
};

static esp_hidd_qos_param_t s_hid_qos = {0};

static void bt_input_lock(void)
{
    if (s_state.lock != NULL) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
    }
}

static void bt_input_unlock(void)
{
    if (s_state.lock != NULL) {
        xSemaphoreGive(s_state.lock);
    }
}

static void bt_input_update_scan_mode(void)
{
    if (!s_state.bt_ready) {
        return;
    }
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                             s_state.discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
}

static bool bt_input_check_report(uint8_t report_id, uint8_t report_type)
{
    if (report_type != ESP_HIDD_REPORT_TYPE_INPUT) {
        esp_bt_hid_device_report_error(ESP_HID_PAR_HANDSHAKE_RSP_ERR_INVALID_PARAM);
        return false;
    }

    if (s_state.protocol_mode == ESP_HIDD_BOOT_MODE) {
        if (report_id == ESP_HIDD_BOOT_REPORT_ID_MOUSE) {
            return true;
        }
    } else if (report_id == 0) {
        return true;
    }

    esp_bt_hid_device_report_error(ESP_HID_PAR_HANDSHAKE_RSP_ERR_INVALID_REP_ID);
    return false;
}

static void bt_input_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BT auth success: %s", param->auth_cmpl.device_name);
            s_state.discoverable = false;
            bt_input_update_scan_mode();
        } else {
            ESP_LOGE(TAG, "BT auth failed: %d", param->auth_cmpl.stat);
            s_state.discoverable = true;
            bt_input_update_scan_mode();
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "BT passkey: %06" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "BT scan mode changed: %d", param->mode_chg.mode);
        break;
    default:
        break;
    }
}

static uint32_t bt_input_hfp_outgoing_cb(uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return 0;
    }

    if (!s_state.mic_enabled || s_state.audio_rb == NULL) {
        memset(buf, 0, len);
        return len;
    }

    uint32_t copied = 0;
    while (copied < len) {
        size_t item_size = 0;
        uint8_t *data = xRingbufferReceiveUpTo(s_state.audio_rb, &item_size, 0, len - copied);
        if (data == NULL || item_size == 0) {
            break;
        }
        memcpy(buf + copied, data, item_size);
        copied += item_size;
        vRingbufferReturnItem(s_state.audio_rb, data);
    }

    if (copied < len) {
        memset(buf + copied, 0, len - copied);
    }
    return len;
}

static void bt_input_hfp_incoming_cb(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
}

static void bt_input_audio_open(void)
{
    if (s_state.audio_rb == NULL) {
        s_state.audio_rb = xRingbufferCreate(HFP_AUDIO_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    } else {
        bt_input_hfp_audio_reset();
    }
}

static void bt_input_audio_close(void)
{
    if (s_state.audio_rb != NULL) {
        vRingbufferDelete(s_state.audio_rb);
        s_state.audio_rb = NULL;
    }
}

static void bt_input_hfp_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    switch (event) {
    case ESP_HF_CLIENT_PROF_STATE_EVT:
        s_state.hfp_ready = (param->prof_stat.state == ESP_HF_INIT_SUCCESS);
        ESP_LOGI(TAG, "HFP profile state: %d", param->prof_stat.state);
        break;
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        s_state.hfp_slc_connected = (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED);
        if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            s_state.hfp_slc_connected = false;
            s_state.hfp_audio_connected = false;
            bt_input_audio_close();
        }
        ESP_LOGI(TAG, "HFP connection state: %d", param->conn_stat.state);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED ||
            param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
            s_state.hfp_audio_connected = true;
            esp_hf_client_register_data_callback(bt_input_hfp_incoming_cb, bt_input_hfp_outgoing_cb);
            bt_input_audio_open();
        } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
            s_state.hfp_audio_connected = false;
            bt_input_audio_close();
        }
        ESP_LOGI(TAG, "HFP audio state: %d", param->audio_stat.state);
        break;
    default:
        break;
    }
}

static void bt_input_hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDD_INIT_EVT:
        if (param->init.status == ESP_HIDD_SUCCESS) {
            esp_bt_hid_device_register_app(&s_hid_app, &s_hid_qos, &s_hid_qos);
        } else {
            ESP_LOGE(TAG, "HID init failed: %d", param->init.status);
        }
        break;
    case ESP_HIDD_REGISTER_APP_EVT:
        if (param->register_app.status == ESP_HIDD_SUCCESS) {
            s_state.hid_ready = true;
            ESP_LOGI(TAG, "HID register success in_use=%d", param->register_app.in_use);
            bt_input_update_scan_mode();
            if (param->register_app.in_use) {
                ESP_LOGI(TAG, "HID reconnecting to bonded host");
                esp_bt_hid_device_connect(param->register_app.bd_addr);
            }
        } else {
            ESP_LOGE(TAG, "HID register failed: %d", param->register_app.status);
        }
        break;
    case ESP_HIDD_OPEN_EVT:
        if (param->open.status == ESP_HIDD_SUCCESS) {
            s_state.hid_connected = (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTED);
            if (s_state.hid_connected) {
                s_state.discoverable = false;
                bt_input_update_scan_mode();
            }
        }
        ESP_LOGI(TAG, "HID open state: %d status: %d", param->open.conn_status, param->open.status);
        break;
    case ESP_HIDD_CLOSE_EVT:
        s_state.hid_connected = false;
        memset(s_state.mouse_report, 0, sizeof(s_state.mouse_report));
        ESP_LOGI(TAG, "HID close state: %d status: %d", param->close.conn_status, param->close.status);
        break;
    case ESP_HIDD_GET_REPORT_EVT:
        ESP_LOGI(TAG, "HID get report id=%u type=%u protocol=%u", param->get_report.report_id,
                 param->get_report.report_type, s_state.protocol_mode);
        if (bt_input_check_report(param->get_report.report_id, param->get_report.report_type)) {
            uint8_t report_id = 0;
            uint16_t report_len = HID_MOUSE_REPORT_SIZE;
            if (s_state.protocol_mode == ESP_HIDD_BOOT_MODE) {
                report_id = ESP_HIDD_BOOT_REPORT_ID_MOUSE;
                report_len = ESP_HIDD_BOOT_REPORT_SIZE_MOUSE - 1;
            }
            esp_bt_hid_device_send_report(param->get_report.report_type, report_id, report_len, s_state.mouse_report);
        }
        break;
    case ESP_HIDD_SET_PROTOCOL_EVT:
        s_state.protocol_mode = param->set_protocol.protocol_mode;
        ESP_LOGI(TAG, "HID protocol mode=%u", s_state.protocol_mode);
        break;
    case ESP_HIDD_VC_UNPLUG_EVT:
        s_state.hid_connected = false;
        s_state.discoverable = true;
        bt_input_update_scan_mode();
        ESP_LOGI(TAG, "HID virtual cable unplugged");
        break;
    default:
        break;
    }
}

void bt_input_init(void)
{
    if (s_state.bt_ready) {
        return;
    }

    if (s_state.lock == NULL) {
        s_state.lock = xSemaphoreCreateMutex();
    }

    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "release BLE memory: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BT SCO HCI data path failed: %s", esp_err_to_name(ret));
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bt_gap_register_callback(bt_input_gap_cb);
    esp_bt_gap_set_device_name(BT_INPUT_DEVICE_NAME);

    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_POINTING;
    cod.service = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_CAPTURING | ESP_BT_COD_SRVC_TELEPHONY;
    esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(iocap));

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    esp_hf_client_register_callback(bt_input_hfp_cb);
    ret = esp_hf_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HFP init failed: %s", esp_err_to_name(ret));
    }

    esp_bt_hid_device_register_callback(bt_input_hidd_cb);
    ret = esp_bt_hid_device_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID init start failed: %s", esp_err_to_name(ret));
    }

    s_state.bt_ready = true;
    bt_input_update_scan_mode();

    ESP_LOGI(TAG, "BT ready, name=%s", BT_INPUT_DEVICE_NAME);
}

bool bt_input_is_ready(void)
{
    return s_state.bt_ready;
}

bool bt_input_hid_connected(void)
{
    return s_state.hid_connected;
}

bool bt_input_hfp_connected(void)
{
    return s_state.hfp_slc_connected;
}

bool bt_input_hfp_audio_connected(void)
{
    return s_state.hfp_audio_connected;
}

bool bt_input_is_discoverable(void)
{
    return s_state.discoverable;
}

const char *bt_input_hid_status_text(void)
{
    if (s_state.hid_connected) {
        return "OK";
    }
    if (!s_state.hid_ready) {
        return "INIT";
    }
    return s_state.discoverable ? "PAIR" : "WAIT";
}

const char *bt_input_hfp_status_text(void)
{
    if (s_state.hfp_slc_connected) {
        return "SLC";
    }
    if (!s_state.hfp_ready) {
        return "INIT";
    }
    return s_state.discoverable ? "PAIR" : "WAIT";
}

const char *bt_input_audio_status_text(void)
{
    return s_state.hfp_audio_connected ? "ON" : "OFF";
}

void bt_input_set_discoverable(bool discoverable)
{
    s_state.discoverable = discoverable;
    bt_input_update_scan_mode();
    ESP_LOGI(TAG, "BT discoverable=%d hid_ready=%d hid_connected=%d hfp_ready=%d hfp_connected=%d",
             s_state.discoverable, s_state.hid_ready, s_state.hid_connected, s_state.hfp_ready,
             s_state.hfp_slc_connected);
}

void bt_input_mouse_send(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    static TickType_t last_drop_log = 0;

    if (!s_state.hid_connected) {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_drop_log) >= pdMS_TO_TICKS(HID_DROP_LOG_MS)) {
            ESP_LOGW(TAG, "mouse report dropped: hid disconnected ready=%d discoverable=%d buttons=%u dx=%d dy=%d wheel=%d",
                     s_state.hid_ready, s_state.discoverable, buttons, dx, dy, wheel);
            last_drop_log = now;
        }
        return;
    }

    bt_input_lock();
    s_state.mouse_report[0] = buttons;
    s_state.mouse_report[1] = (uint8_t)dx;
    s_state.mouse_report[2] = (uint8_t)dy;
    s_state.mouse_report[3] = (uint8_t)wheel;

    uint8_t report_id = 0;
    uint16_t report_len = HID_MOUSE_REPORT_SIZE;
    if (s_state.protocol_mode == ESP_HIDD_BOOT_MODE) {
        report_id = ESP_HIDD_BOOT_REPORT_ID_MOUSE;
        report_len = ESP_HIDD_BOOT_REPORT_SIZE_MOUSE - 1;
    }

    esp_err_t ret = esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, report_id, report_len,
                                                  s_state.mouse_report);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mouse report send failed: %s id=%u len=%u buttons=%u dx=%d dy=%d wheel=%d",
                 esp_err_to_name(ret), report_id, report_len, buttons, dx, dy, wheel);
    }
    bt_input_unlock();
}

void bt_input_hfp_mic_set_enabled(bool enabled)
{
    s_state.mic_enabled = enabled;
    if (!enabled) {
        bt_input_hfp_audio_reset();
    }
}

void bt_input_hfp_audio_reset(void)
{
    if (s_state.audio_rb == NULL) {
        return;
    }

    while (1) {
        size_t item_size = 0;
        uint8_t *data = xRingbufferReceive(s_state.audio_rb, &item_size, 0);
        if (data == NULL) {
            break;
        }
        vRingbufferReturnItem(s_state.audio_rb, data);
    }
}

void bt_input_hfp_feed_pcm(const int16_t *samples, size_t sample_count)
{
    if (!s_state.hfp_audio_connected || !s_state.mic_enabled || s_state.audio_rb == NULL || samples == NULL ||
        sample_count == 0) {
        return;
    }

    const size_t bytes = sample_count * sizeof(samples[0]);
    if (xRingbufferSend(s_state.audio_rb, samples, bytes, 0) != pdTRUE) {
        size_t item_size = 0;
        uint8_t *old = xRingbufferReceiveUpTo(s_state.audio_rb, &item_size, 0, bytes);
        if (old != NULL) {
            vRingbufferReturnItem(s_state.audio_rb, old);
        }
        xRingbufferSend(s_state.audio_rb, samples, bytes, 0);
    }

    esp_hf_client_outgoing_data_ready();
}
