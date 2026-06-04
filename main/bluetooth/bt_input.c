/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "bt_input.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "bt_hid_mouse.h"
#include "bt_pairing_status.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "esp_hidd_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define HFP_AUDIO_RINGBUF_SIZE 8192
#define HFP_AUDIO_CVSD_SAMPLE_RATE 8000
#define HFP_AUDIO_MSBC_SAMPLE_RATE 16000
#define HFP_AUDIO_CVSD_PACKET_LEN 120
#define HFP_AUDIO_MSBC_PCM_PACKET_LEN 240
#define HFP_AUDIO_TX_TIMER_US 1000
#define HFP_AUDIO_PREBUFFER_BYTES 2560
#define HFP_AUDIO_HIGH_WATERMARK_BYTES 5120
#define HFP_AUDIO_RETRY_MS 1500
#define HFP_AUDIO_REOPEN_SETTLE_MS 300
#define HFP_AUDIO_CONNECT_TIMEOUT_MS 3000
#define HFP_PACKET_STATS_LOG_MS 2000
#define HID_DROP_LOG_MS 1000

static const char *TAG = "bt_input";

typedef struct {
    bool bt_ready;
    bool hid_ready;
    bool hid_connected;
    bool hfp_ready;
    bool hfp_slc_connected;
    bool hfp_audio_connected;
    bool hfp_audio_connecting;
    bool hfp_has_peer;
    bool hid_reconnect_attempted;
    bool hfp_reconnect_attempted;
    bool discoverable;
    bool pairing_active;
    bool mic_enabled;
    bool hfp_audio_msbc;
    bool hfp_vrec_requested;
    bool hfp_vrec_active;
    uint8_t protocol_mode;
    uint32_t hfp_peer_features;
    esp_bd_addr_t hfp_peer_bda;
    uint16_t hfp_sync_conn_handle;
    TickType_t hfp_last_pkt_stat_req;
    TickType_t hfp_next_audio_req;
    TickType_t hfp_audio_connect_started;
    uint32_t hfp_audio_req_attempts;
    uint8_t mouse_report[BT_HID_MOUSE_REPORT_SIZE];
    SemaphoreHandle_t lock;
    bool audio_open;
} bt_input_state_t;

static bt_input_state_t s_state = {
    .discoverable  = true,
    .protocol_mode = ESP_HIDD_REPORT_MODE,
};

static uint32_t s_hfp_feed_calls;
static uint32_t s_hfp_feed_bytes;
static uint32_t s_hfp_feed_drops;
static uint32_t s_hfp_cb_calls;
static uint32_t s_hfp_cb_bytes;
static uint32_t s_hfp_cb_underruns;
static uint32_t s_hfp_cb_budget_empty;
static uint32_t s_hfp_timer_ticks;
static TickType_t s_hfp_last_stats_log;
static uint8_t s_hfp_audio_fifo[HFP_AUDIO_RINGBUF_SIZE];
static size_t s_hfp_audio_head;
static size_t s_hfp_audio_tail;
static size_t s_hfp_audio_used;
static portMUX_TYPE s_hfp_audio_mux = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_hfp_tx_timer;
static volatile uint32_t s_hfp_tx_budget_packets;
static volatile uint32_t s_hfp_tx_credit_bytes;
static volatile uint32_t s_hfp_tx_packet_len = HFP_AUDIO_CVSD_PACKET_LEN;
static volatile bool s_hfp_tx_started;
static int16_t s_hfp_last_sample;

static void bt_input_hfp_audio_disconnect(void);

const char *bt_input_hfp_codec_text(void)
{
    return s_state.hfp_audio_msbc ? "mSBC" : "CVSD";
}

uint32_t bt_input_hfp_pcm_sample_rate(void)
{
    return s_state.hfp_audio_msbc ? HFP_AUDIO_MSBC_SAMPLE_RATE : HFP_AUDIO_CVSD_SAMPLE_RATE;
}

static uint32_t bt_input_hfp_pcm_bytes_per_second(void)
{
    return bt_input_hfp_pcm_sample_rate() * sizeof(int16_t);
}

static uint32_t bt_input_hfp_default_packet_len(void)
{
    return s_state.hfp_audio_msbc ? HFP_AUDIO_MSBC_PCM_PACKET_LEN : HFP_AUDIO_CVSD_PACKET_LEN;
}

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

static char *bt_input_bda_to_str(const esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool bt_input_get_first_bonded_bda(esp_bd_addr_t bda)
{
    if (bda == NULL) {
        return false;
    }

    int dev_num = esp_bt_gap_get_bond_device_num();
    if (dev_num <= 0) {
        return false;
    }

    esp_bd_addr_t bonded_bda;
    int list_num = 1;
    esp_err_t ret = esp_bt_gap_get_bond_device_list(&list_num, &bonded_bda);
    if (ret != ESP_OK || list_num <= 0) {
        ESP_LOGW(TAG, "bond lookup failed: ret=%s list_num=%d", esp_err_to_name(ret), list_num);
        return false;
    }

    memcpy(bda, bonded_bda, sizeof(esp_bd_addr_t));
    return true;
}

static void bt_input_try_hfp_reconnect(const char *reason)
{
    if (!s_state.hfp_ready || s_state.hfp_slc_connected || s_state.hfp_reconnect_attempted) {
        return;
    }

    esp_bd_addr_t bda;
    if (!bt_input_get_first_bonded_bda(bda)) {
        return;
    }

    char bda_text[18] = {0};
    s_state.hfp_reconnect_attempted = true;
    esp_err_t ret = esp_hf_client_connect(bda);
    ESP_LOGI(TAG, "HFP reconnect %s peer=%s ret=%s", reason,
             bt_input_bda_to_str(bda, bda_text, sizeof(bda_text)), esp_err_to_name(ret));
}

static void bt_input_try_hid_reconnect(const char *reason)
{
    if (!s_state.hid_ready || s_state.hid_connected || s_state.hid_reconnect_attempted) {
        return;
    }

    esp_bd_addr_t bda;
    if (!bt_input_get_first_bonded_bda(bda)) {
        return;
    }

    char bda_text[18] = {0};
    s_state.hid_reconnect_attempted = true;
    esp_err_t ret = esp_bt_hid_device_connect(bda);
    ESP_LOGI(TAG, "HID reconnect %s peer=%s ret=%s", reason,
             bt_input_bda_to_str(bda, bda_text, sizeof(bda_text)), esp_err_to_name(ret));
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

static void bt_input_hfp_fifo_reset(void)
{
    portENTER_CRITICAL(&s_hfp_audio_mux);
    s_hfp_audio_head = 0;
    s_hfp_audio_tail = 0;
    s_hfp_audio_used = 0;
    portEXIT_CRITICAL(&s_hfp_audio_mux);
}

static size_t bt_input_hfp_fifo_used(void)
{
    size_t used;
    portENTER_CRITICAL(&s_hfp_audio_mux);
    used = s_hfp_audio_used;
    portEXIT_CRITICAL(&s_hfp_audio_mux);
    return used;
}

static size_t bt_input_hfp_fifo_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    size_t dropped = 0;
    portENTER_CRITICAL(&s_hfp_audio_mux);
    if (len > HFP_AUDIO_RINGBUF_SIZE) {
        data += (len - HFP_AUDIO_RINGBUF_SIZE);
        dropped += (len - HFP_AUDIO_RINGBUF_SIZE);
        len = HFP_AUDIO_RINGBUF_SIZE;
    }

    size_t free_bytes = HFP_AUDIO_RINGBUF_SIZE - s_hfp_audio_used;
    if (free_bytes < len) {
        size_t drop_old = len - free_bytes;
        s_hfp_audio_tail = (s_hfp_audio_tail + drop_old) % HFP_AUDIO_RINGBUF_SIZE;
        s_hfp_audio_used -= drop_old;
        dropped += drop_old;
    }

    size_t first = HFP_AUDIO_RINGBUF_SIZE - s_hfp_audio_head;
    if (first > len) {
        first = len;
    }
    memcpy(&s_hfp_audio_fifo[s_hfp_audio_head], data, first);
    if (len > first) {
        memcpy(s_hfp_audio_fifo, data + first, len - first);
    }
    s_hfp_audio_head = (s_hfp_audio_head + len) % HFP_AUDIO_RINGBUF_SIZE;
    s_hfp_audio_used += len;
    portEXIT_CRITICAL(&s_hfp_audio_mux);

    return dropped;
}

static size_t bt_input_hfp_fifo_read(uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }

    size_t read_len = 0;
    portENTER_CRITICAL(&s_hfp_audio_mux);
    if (s_hfp_audio_used > 0) {
        read_len = s_hfp_audio_used < len ? s_hfp_audio_used : len;
        read_len &= ~(sizeof(int16_t) - 1U);
        size_t first = HFP_AUDIO_RINGBUF_SIZE - s_hfp_audio_tail;
        if (first > read_len) {
            first = read_len;
        }
        if (read_len > 0) {
            memcpy(data, &s_hfp_audio_fifo[s_hfp_audio_tail], first);
            if (read_len > first) {
                memcpy(data + first, s_hfp_audio_fifo, read_len - first);
            }
            s_hfp_audio_tail = (s_hfp_audio_tail + read_len) % HFP_AUDIO_RINGBUF_SIZE;
            s_hfp_audio_used -= read_len;
        }
    }
    portEXIT_CRITICAL(&s_hfp_audio_mux);

    return read_len;
}

static void bt_input_hfp_fill_fade_silence(uint8_t *buf, uint32_t len)
{
    uint32_t sample_count = len / sizeof(int16_t);
    if (sample_count == 0) {
        if (len > 0) {
            memset(buf, 0, len);
        }
        s_hfp_last_sample = 0;
        return;
    }

    int32_t last = s_hfp_last_sample;
    for (uint32_t i = 0; i < sample_count; i++) {
        int32_t sample = (last * (int32_t)(sample_count - i)) / (int32_t)(sample_count + 1);
        uint16_t packed = (uint16_t)(int16_t)sample;
        uint32_t offset = i * sizeof(int16_t);
        buf[offset] = (uint8_t)(packed & 0xff);
        buf[offset + 1] = (uint8_t)(packed >> 8);
    }
    if ((len & 1U) != 0) {
        buf[len - 1] = 0;
    }
    s_hfp_last_sample = 0;
}

static void bt_input_hfp_remember_last_sample(const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len < sizeof(int16_t)) {
        return;
    }
    uint32_t offset = len - sizeof(int16_t);
    s_hfp_last_sample = (int16_t)((uint16_t)buf[offset] | ((uint16_t)buf[offset + 1] << 8));
}

static void bt_input_hfp_log_audio_stats(const char *reason)
{
    TickType_t now = xTaskGetTickCount();
    if (s_hfp_last_stats_log != 0 && (now - s_hfp_last_stats_log) < pdMS_TO_TICKS(1000)) {
        return;
    }
    s_hfp_last_stats_log = now;

    ESP_LOGI(TAG,
             "HFP audio stats %s: slc=%d audio=%d connecting=%d mic=%d open=%d codec=%s rate=%" PRIu32
             " sync=0x%04x pkt=%" PRIu32 " used=%u feed=%" PRIu32 "/%" PRIu32 "B cb=%" PRIu32 "/%" PRIu32
             "B tick=%" PRIu32 " underrun=%" PRIu32 " budget0=%" PRIu32 " drop=%" PRIu32,
             reason, s_state.hfp_slc_connected, s_state.hfp_audio_connected, s_state.hfp_audio_connecting,
             s_state.mic_enabled, s_state.audio_open, bt_input_hfp_codec_text(), bt_input_hfp_pcm_sample_rate(), s_state.hfp_sync_conn_handle,
             s_hfp_tx_packet_len, (unsigned)s_hfp_audio_used,
             s_hfp_feed_calls, s_hfp_feed_bytes, s_hfp_cb_calls, s_hfp_cb_bytes, s_hfp_timer_ticks,
             s_hfp_cb_underruns, s_hfp_cb_budget_empty, s_hfp_feed_drops);

    s_hfp_feed_calls    = 0;
    s_hfp_feed_bytes    = 0;
    s_hfp_feed_drops    = 0;
    s_hfp_cb_calls      = 0;
    s_hfp_cb_bytes      = 0;
    s_hfp_cb_underruns  = 0;
    s_hfp_cb_budget_empty = 0;
    s_hfp_timer_ticks = 0;
}

static void bt_input_hfp_request_packet_stats(void)
{
    if (!s_state.hfp_audio_connected || s_state.hfp_sync_conn_handle == 0) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_state.hfp_last_pkt_stat_req != 0 &&
        (now - s_state.hfp_last_pkt_stat_req) < pdMS_TO_TICKS(HFP_PACKET_STATS_LOG_MS)) {
        return;
    }

    s_state.hfp_last_pkt_stat_req = now;
    esp_err_t ret = esp_hf_client_pkt_stat_nums_get(s_state.hfp_sync_conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HFP packet stats request failed: %s sync=0x%04x audio=%d",
                 esp_err_to_name(ret), s_state.hfp_sync_conn_handle, s_state.hfp_audio_connected);
    }
}

static bool bt_input_hfp_audio_request_internal(const char *reason)
{
    if (!s_state.mic_enabled) {
        return false;
    }
    if (!s_state.hfp_slc_connected || !s_state.hfp_has_peer) {
        return false;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_state.hfp_audio_connected) {
        if (s_state.hfp_audio_msbc || (s_state.hfp_peer_features & ESP_HF_CLIENT_PEER_FEAT_CODEC) == 0) {
            return true;
        }
        ESP_LOGW(TAG, "HFP audio is CVSD while peer supports codec; reconnecting for mSBC reason=%s",
                 reason);
        bt_input_hfp_audio_disconnect();
        s_state.hfp_next_audio_req = now + pdMS_TO_TICKS(HFP_AUDIO_REOPEN_SETTLE_MS);
        return false;
    }

    if (s_state.hfp_audio_connecting) {
        if (s_state.hfp_audio_connect_started != 0 &&
            (now - s_state.hfp_audio_connect_started) >= pdMS_TO_TICKS(HFP_AUDIO_CONNECT_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "HFP audio request timeout: reason=%s attempt=%" PRIu32 " retry now",
                     reason, s_state.hfp_audio_req_attempts);
            s_state.hfp_audio_connecting = false;
            s_state.hfp_audio_connect_started = 0;
            s_state.hfp_next_audio_req = 0;
        } else {
            return true;
        }
    }

    if (s_state.hfp_next_audio_req != 0 && now < s_state.hfp_next_audio_req) {
        return false;
    }

    s_state.hfp_audio_connecting = true;
    s_state.hfp_audio_connect_started = now;
    s_state.hfp_audio_msbc = false;
    s_state.hfp_sync_conn_handle = 0;
    s_state.hfp_audio_req_attempts++;
    esp_err_t ret = esp_hf_client_connect_audio(s_state.hfp_peer_bda);
    if (ret != ESP_OK) {
        s_state.hfp_audio_connecting = false;
        s_state.hfp_audio_connect_started = 0;
        s_state.hfp_next_audio_req = now + pdMS_TO_TICKS(HFP_AUDIO_RETRY_MS);
        ESP_LOGW(TAG, "HFP audio request %s failed: %s attempt=%" PRIu32 " retry_ms=%u",
                 reason, esp_err_to_name(ret), s_state.hfp_audio_req_attempts, HFP_AUDIO_RETRY_MS);
        return false;
    }

    ESP_LOGI(TAG, "HFP audio request %s: %s attempt=%" PRIu32, reason, esp_err_to_name(ret),
             s_state.hfp_audio_req_attempts);
    return true;
}

static void bt_input_hfp_audio_schedule_retry(const char *reason)
{
    s_state.hfp_audio_connecting = false;
    s_state.hfp_audio_connect_started = 0;
    s_state.hfp_next_audio_req = xTaskGetTickCount() + pdMS_TO_TICKS(HFP_AUDIO_RETRY_MS);
    ESP_LOGI(TAG, "HFP audio retry scheduled: %s retry_ms=%u", reason, HFP_AUDIO_RETRY_MS);
}

static void bt_input_hfp_vrec_stop(const char *reason)
{
    if (!s_state.hfp_slc_connected || (!s_state.hfp_vrec_requested && !s_state.hfp_vrec_active)) {
        s_state.hfp_vrec_requested = false;
        s_state.hfp_vrec_active = false;
        return;
    }

    esp_err_t ret = esp_hf_client_stop_voice_recognition();
    ESP_LOGI(TAG, "HFP vrec stop %s: %s", reason, esp_err_to_name(ret));
    s_state.hfp_vrec_requested = false;
    s_state.hfp_vrec_active = false;
}

static void bt_input_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        s_state.pairing_active = false;
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            char bda[18] = {0};
            ESP_LOGI(TAG, "BT auth success: %s [%s]", param->auth_cmpl.device_name,
                     bt_input_bda_to_str(param->auth_cmpl.bda, bda, sizeof(bda)));
            s_state.discoverable = false;
            bt_input_update_scan_mode();
        } else {
            ESP_LOGE(TAG, "BT auth failed: %d", param->auth_cmpl.stat);
            s_state.discoverable = true;
            bt_input_update_scan_mode();
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        s_state.pairing_active = true;
        esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        s_state.pairing_active = true;
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        s_state.pairing_active = true;
        ESP_LOGI(TAG, "BT passkey: %06" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "BT scan mode changed: %d", param->mode_chg.mode);
        break;
    case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT: {
        char bda[18] = {0};
        ESP_LOGI(TAG, "BT remove bond status=%d bda=%s", param->remove_bond_dev_cmpl.status,
                 bt_input_bda_to_str(param->remove_bond_dev_cmpl.bda, bda, sizeof(bda)));
        break;
    }
    default:
        break;
    }
}

static void bt_input_hfp_tx_timer_cb(void *arg)
{
    (void)arg;

    if (!s_state.hfp_audio_connected || !s_state.audio_open) {
        return;
    }

    uint32_t packet_len = s_hfp_tx_packet_len;
    if (packet_len == 0) {
        packet_len = bt_input_hfp_default_packet_len();
    }

    if (!s_state.mic_enabled) {
        s_hfp_tx_started = true;
    } else if (!s_hfp_tx_started) {
        size_t used = bt_input_hfp_fifo_used();
        size_t prebuffer = HFP_AUDIO_PREBUFFER_BYTES;
        if (prebuffer < packet_len) {
            prebuffer = packet_len;
        }
        if (used < prebuffer) {
            return;
        }
        s_hfp_tx_started = true;
        s_hfp_tx_credit_bytes = 0;
    }

    uint32_t credit = s_hfp_tx_credit_bytes +
                      (bt_input_hfp_pcm_bytes_per_second() / (1000000 / HFP_AUDIO_TX_TIMER_US));
    uint32_t max_credit = packet_len * 2;
    if (credit > max_credit) {
        credit = max_credit;
    }

    if (credit < packet_len || s_hfp_tx_budget_packets != 0) {
        s_hfp_tx_credit_bytes = credit;
        return;
    }

    s_hfp_tx_credit_bytes = credit - packet_len;
    s_hfp_tx_budget_packets = 1;
    s_hfp_timer_ticks++;
    bt_input_hfp_request_packet_stats();
    esp_hf_client_outgoing_data_ready();
}

static uint32_t bt_input_hfp_outgoing_cb(uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return 0;
    }

    s_hfp_tx_packet_len = len;

    if (!s_state.audio_open) {
        s_hfp_cb_underruns++;
        bt_input_hfp_log_audio_stats("cb disabled");
        return 0;
    }

    bool budget_empty = (s_hfp_tx_budget_packets == 0);
    if (budget_empty) {
        s_hfp_cb_budget_empty++;
    } else {
        s_hfp_tx_budget_packets--;
    }

    if (!s_state.mic_enabled) {
        bt_input_hfp_fill_fade_silence(buf, len);
        s_hfp_cb_calls++;
        s_hfp_cb_bytes += len;
        bt_input_hfp_log_audio_stats(budget_empty ? "cb mute_budget" : "cb mute");
        return len;
    }

    size_t read_len = bt_input_hfp_fifo_read(buf, len);
    if (read_len < len) {
        s_hfp_cb_underruns++;
        if (read_len > 0) {
            bt_input_hfp_remember_last_sample(buf, read_len);
        }
        bt_input_hfp_fill_fade_silence(buf + read_len, len - read_len);
        s_hfp_cb_calls++;
        s_hfp_cb_bytes += len;
        bt_input_hfp_log_audio_stats(read_len > 0 ? "cb partial" :
                                     (budget_empty ? "cb budget_fill" : "cb underrun"));
        return len;
    }

    bt_input_hfp_remember_last_sample(buf, len);
    s_hfp_cb_calls++;
    s_hfp_cb_bytes += len;
    bt_input_hfp_log_audio_stats(budget_empty ? "cb budget_send" : "cb send");
    return len;
}

static void bt_input_hfp_incoming_cb(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
}

static void bt_input_audio_open(void)
{
    s_state.audio_open = true;
    bt_input_hfp_audio_reset();

    if (s_hfp_tx_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = bt_input_hfp_tx_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "hfp_tx",
            .skip_unhandled_events = true,
        };
        esp_err_t ret = esp_timer_create(&timer_args, &s_hfp_tx_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HFP tx timer create failed: %s", esp_err_to_name(ret));
        }
    }

    if (s_hfp_tx_timer != NULL) {
        esp_timer_stop(s_hfp_tx_timer);
        esp_err_t ret = esp_timer_start_periodic(s_hfp_tx_timer, HFP_AUDIO_TX_TIMER_US);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "HFP tx timer start failed: %s", esp_err_to_name(ret));
        }
    }

    s_hfp_feed_calls = 0;
    s_hfp_feed_bytes = 0;
    s_hfp_feed_drops = 0;
    s_hfp_cb_calls = 0;
    s_hfp_cb_bytes = 0;
    s_hfp_cb_underruns = 0;
    s_hfp_cb_budget_empty = 0;
    s_hfp_timer_ticks = 0;
    s_hfp_last_stats_log = 0;
    s_hfp_tx_budget_packets = 0;
    s_hfp_tx_credit_bytes = 0;
    s_hfp_tx_packet_len = bt_input_hfp_default_packet_len();
    s_hfp_tx_started = false;
    s_hfp_last_sample = 0;
    s_state.hfp_last_pkt_stat_req = 0;
}

static void bt_input_audio_close(void)
{
    if (s_hfp_tx_timer != NULL) {
        esp_timer_stop(s_hfp_tx_timer);
    }
    s_state.audio_open = false;
    s_hfp_tx_budget_packets = 0;
    s_hfp_tx_credit_bytes = 0;
    s_hfp_tx_started = false;
    s_hfp_last_sample = 0;
    s_state.hfp_last_pkt_stat_req = 0;
    bt_input_hfp_fifo_reset();
}

static void bt_input_hfp_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    switch (event) {
    case ESP_HF_CLIENT_PROF_STATE_EVT:
        s_state.hfp_ready = (param->prof_stat.state == ESP_HF_INIT_SUCCESS);
        ESP_LOGI(TAG, "HFP profile state: %d", param->prof_stat.state);
        bt_input_try_hfp_reconnect("profile ready");
        break;
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED ||
            param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED) {
            memcpy(s_state.hfp_peer_bda, param->conn_stat.remote_bda, sizeof(s_state.hfp_peer_bda));
            s_state.hfp_has_peer = true;
        }
        s_state.hfp_slc_connected = (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED);
        if (s_state.hfp_slc_connected) {
            s_state.pairing_active = false;
            s_state.hfp_peer_features = param->conn_stat.peer_feat;
            esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_MIC, 15);
            if (s_state.mic_enabled) {
                bt_input_hfp_audio_request_internal("after SLC mic");
            }
        }
        if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            s_state.hfp_slc_connected = false;
            s_state.hfp_audio_connected = false;
            s_state.hfp_audio_connecting = false;
            s_state.hfp_has_peer = false;
            s_state.hfp_audio_msbc = false;
            s_state.hfp_vrec_requested = false;
            s_state.hfp_vrec_active = false;
            s_state.hfp_peer_features = 0;
            s_state.hfp_sync_conn_handle = 0;
            s_state.hfp_last_pkt_stat_req = 0;
            s_state.hfp_next_audio_req = 0;
            s_state.hfp_audio_connect_started = 0;
            s_state.hfp_audio_req_attempts = 0;
            bt_input_audio_close();
        }
        ESP_LOGI(TAG, "HFP connection state: %d peer_feat=0x%" PRIx32 " codec_neg=%d ecnr=%d chld=0x%" PRIx32,
                 param->conn_stat.state, param->conn_stat.peer_feat,
                 (param->conn_stat.peer_feat & ESP_HF_CLIENT_PEER_FEAT_CODEC) != 0,
                 (param->conn_stat.peer_feat & ESP_HF_CLIENT_PEER_FEAT_ECNR) != 0,
                 param->conn_stat.chld_feat);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED ||
            param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
            s_state.hfp_audio_connected = true;
            s_state.hfp_audio_connecting = false;
            s_state.hfp_audio_connect_started = 0;
            s_state.hfp_audio_msbc =
                (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC);
            s_state.hfp_sync_conn_handle = param->audio_stat.sync_conn_handle;
            s_state.hfp_last_pkt_stat_req = 0;
            s_state.hfp_next_audio_req = 0;
            s_state.hfp_audio_req_attempts = 0;
            if (!s_state.mic_enabled) {
                ESP_LOGI(TAG, "HFP audio opened while mic disabled; sending silence codec=%s rate=%" PRIu32,
                         bt_input_hfp_codec_text(), bt_input_hfp_pcm_sample_rate());
            }
            esp_hf_client_register_data_callback(bt_input_hfp_incoming_cb, bt_input_hfp_outgoing_cb);
            bt_input_audio_open();
        } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTING) {
            s_state.hfp_audio_connecting = true;
        } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
            s_state.hfp_audio_connected = false;
            s_state.hfp_audio_msbc = false;
            s_state.hfp_sync_conn_handle = 0;
            s_state.hfp_last_pkt_stat_req = 0;
            bt_input_audio_close();
            if (s_state.hfp_audio_connecting && s_state.mic_enabled &&
                s_state.hfp_slc_connected && s_state.hfp_has_peer) {
                bt_input_hfp_audio_schedule_retry("disconnect while connecting");
            } else {
                s_state.hfp_audio_connecting = false;
                s_state.hfp_audio_connect_started = 0;
            }
        }
        ESP_LOGI(TAG, "HFP audio state: %d codec=%s rate=%" PRIu32 " sync=0x%04x",
                 param->audio_stat.state, bt_input_hfp_codec_text(), bt_input_hfp_pcm_sample_rate(),
                 param->audio_stat.sync_conn_handle);
        break;
    case ESP_HF_CLIENT_BVRA_EVT:
        s_state.hfp_vrec_active = (param->bvra.value == ESP_HF_VR_STATE_ENABLED);
        s_state.hfp_vrec_requested = s_state.hfp_vrec_active;
        ESP_LOGI(TAG, "HFP vrec state: %d active=%d mic=%d audio=%d",
                 param->bvra.value, s_state.hfp_vrec_active, s_state.mic_enabled,
                 s_state.hfp_audio_connected);
        break;
    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        ESP_LOGI(TAG, "HFP AT response: code=%d cme=%d vrec_req=%d vrec=%d audio=%d",
                 param->at_response.code, param->at_response.cme, s_state.hfp_vrec_requested,
                 s_state.hfp_vrec_active, s_state.hfp_audio_connected);
        if (param->at_response.code != ESP_HF_AT_RESPONSE_CODE_OK && !s_state.hfp_vrec_active) {
            s_state.hfp_vrec_requested = false;
        }
        break;
    case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT:
        ESP_LOGI(TAG,
                 "HFP packet stats: rx_total=%" PRIu32 " rx_ok=%" PRIu32 " rx_err=%" PRIu32
                 " rx_none=%" PRIu32 " rx_lost=%" PRIu32 " tx_total=%" PRIu32 " tx_discard=%" PRIu32,
                 param->pkt_nums.rx_total, param->pkt_nums.rx_correct, param->pkt_nums.rx_err,
                 param->pkt_nums.rx_none, param->pkt_nums.rx_lost, param->pkt_nums.tx_total,
                 param->pkt_nums.tx_discarded);
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
            esp_hidd_qos_param_t *qos = bt_hid_mouse_qos_param();
            esp_bt_hid_device_register_app(bt_hid_mouse_app_param(), qos, qos);
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
                s_state.hid_reconnect_attempted = true;
            } else {
                bt_input_try_hid_reconnect("register");
            }
        } else {
            ESP_LOGE(TAG, "HID register failed: %d", param->register_app.status);
        }
        break;
    case ESP_HIDD_OPEN_EVT:
        if (param->open.status == ESP_HIDD_SUCCESS) {
            s_state.hid_connected = (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTED);
            if (s_state.hid_connected) {
                char bda[18] = {0};
                s_state.pairing_active = false;
                s_state.discoverable = false;
                bt_input_update_scan_mode();
                ESP_LOGI(TAG, "HID connected peer=%s",
                         bt_input_bda_to_str(param->open.bd_addr, bda, sizeof(bda)));
            }
        }
        ESP_LOGI(TAG, "HID open state: %d status: %d", param->open.conn_status, param->open.status);
        break;
    case ESP_HIDD_CLOSE_EVT:
        s_state.hid_connected = false;
        memset(s_state.mouse_report, 0, sizeof(s_state.mouse_report));
        ESP_LOGI(TAG, "HID close state: %d status: %d", param->close.conn_status, param->close.status);
        break;
    case ESP_HIDD_SEND_REPORT_EVT:
        if (param->send_report.status != ESP_HIDD_SUCCESS) {
            ESP_LOGW(TAG, "HID send report failed status=%d reason=%u type=%u id=%u",
                     param->send_report.status, param->send_report.reason,
                     param->send_report.report_type, param->send_report.report_id);
        }
        break;
    case ESP_HIDD_GET_REPORT_EVT:
        ESP_LOGI(TAG, "HID get report id=%u type=%u protocol=%u", param->get_report.report_id,
                 param->get_report.report_type, s_state.protocol_mode);
        if (bt_input_check_report(param->get_report.report_id, param->get_report.report_type)) {
            uint8_t report_id = 0;
            uint16_t report_len = BT_HID_MOUSE_REPORT_SIZE;
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
        ESP_LOGI(TAG, "HID virtual cable unplugged status=%d", param->vc_unplug.status);
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
    bt_input_try_hfp_reconnect("bt ready");
    bt_input_try_hid_reconnect("bt ready");

    char bda[18] = {0};
    ESP_LOGI(TAG, "BT ready, name=%s addr=%s bonds=%d", BT_INPUT_DEVICE_NAME,
             bt_input_bda_to_str((const uint8_t *)esp_bt_dev_get_address(), bda, sizeof(bda)),
             esp_bt_gap_get_bond_device_num());
}

bool bt_input_hid_connected(void)
{
    return s_state.hid_connected;
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
    if (s_state.hfp_audio_connected) {
        return "ON";
    }
    if (s_state.hfp_audio_connecting) {
        return "TRY";
    }
    return "OFF";
}

const char *bt_input_pairing_status_text(void)
{
    bool paired_or_connected = s_state.hid_connected || s_state.hfp_slc_connected;
    if (!paired_or_connected && s_state.bt_ready && esp_bt_gap_get_bond_device_num() > 0) {
        paired_or_connected = true;
    }

    return bt_pairing_status_text(
        bt_pairing_status_resolve(s_state.discoverable, s_state.pairing_active, paired_or_connected));
}

void bt_input_set_discoverable(bool discoverable)
{
    s_state.discoverable = discoverable;
    s_state.pairing_active = false;
    bt_input_update_scan_mode();
    ESP_LOGI(TAG, "BT discoverable=%d pairing=%d hid_ready=%d hid_connected=%d hfp_ready=%d hfp_connected=%d",
             s_state.discoverable, s_state.pairing_active, s_state.hid_ready,
             s_state.hid_connected, s_state.hfp_ready, s_state.hfp_slc_connected);
}

void bt_input_clear_bonds(void)
{
    if (!s_state.bt_ready) {
        ESP_LOGW(TAG, "clear bonds skipped: BT not ready");
        return;
    }

    if (s_state.hid_ready) {
        esp_err_t unplug_ret = esp_bt_hid_device_virtual_cable_unplug();
        ESP_LOGI(TAG, "HID virtual cable unplug request: %s", esp_err_to_name(unplug_ret));
    }

    int dev_num = esp_bt_gap_get_bond_device_num();
    if (dev_num <= 0) {
        ESP_LOGI(TAG, "clear bonds: no bonded BR/EDR device");
        s_state.discoverable = true;
        s_state.pairing_active = false;
        bt_input_update_scan_mode();
        return;
    }

    esp_bd_addr_t *dev_list = (esp_bd_addr_t *)calloc(dev_num, sizeof(esp_bd_addr_t));
    if (dev_list == NULL) {
        ESP_LOGE(TAG, "clear bonds: alloc failed num=%d", dev_num);
        return;
    }

    int list_num = dev_num;
    esp_err_t ret = esp_bt_gap_get_bond_device_list(&list_num, dev_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "clear bonds: get list failed: %s", esp_err_to_name(ret));
        free(dev_list);
        return;
    }

    for (int i = 0; i < list_num; ++i) {
        char bda[18] = {0};
        ESP_LOGI(TAG, "clear bonds: remove %s", bt_input_bda_to_str(dev_list[i], bda, sizeof(bda)));
        ret = esp_bt_gap_remove_bond_device(dev_list[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "clear bonds: remove failed %s ret=%s", bda, esp_err_to_name(ret));
        }
    }
    free(dev_list);

    s_state.discoverable = true;
    s_state.pairing_active = false;
    bt_input_update_scan_mode();
}

void bt_input_mouse_send(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan)
{
    static TickType_t last_drop_log = 0;

    if (!s_state.hid_connected) {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_drop_log) >= pdMS_TO_TICKS(HID_DROP_LOG_MS)) {
            ESP_LOGW(TAG, "mouse report dropped: hid disconnected ready=%d discoverable=%d buttons=%u dx=%d dy=%d wheel=%d pan=%d",
                     s_state.hid_ready, s_state.discoverable, buttons, dx, dy, wheel, pan);
            last_drop_log = now;
        }
        return;
    }

    bt_input_lock();
    s_state.mouse_report[0] = buttons;
    s_state.mouse_report[1] = (uint8_t)dx;
    s_state.mouse_report[2] = (uint8_t)dy;
    s_state.mouse_report[3] = (uint8_t)wheel;
    s_state.mouse_report[4] = (uint8_t)pan;

    uint8_t report_id = 0;
    uint16_t report_len = BT_HID_MOUSE_REPORT_SIZE;
    if (s_state.protocol_mode == ESP_HIDD_BOOT_MODE) {
        report_id = ESP_HIDD_BOOT_REPORT_ID_MOUSE;
        report_len = ESP_HIDD_BOOT_REPORT_SIZE_MOUSE - 1;
    }

    esp_err_t ret = esp_bt_hid_device_send_report(ESP_HIDD_REPORT_TYPE_INTRDATA, report_id, report_len,
                                                  s_state.mouse_report);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mouse report send failed: %s id=%u len=%u buttons=%u dx=%d dy=%d wheel=%d pan=%d",
                 esp_err_to_name(ret), report_id, report_len, buttons, dx, dy, wheel, pan);
    }
    bt_input_unlock();
}

void bt_input_hfp_mic_set_enabled(bool enabled)
{
    s_state.mic_enabled = enabled;
    if (!enabled) {
        bt_input_hfp_vrec_stop("mic disabled");
        bt_input_hfp_audio_reset();
        s_state.hfp_audio_connecting = false;
        s_state.hfp_audio_connect_started = 0;
        s_state.hfp_next_audio_req = 0;
        ESP_LOGI(TAG, "HFP mic disabled: audio=%d open=%d codec=%s rate=%" PRIu32 " keep_silence=1",
                 s_state.hfp_audio_connected, s_state.audio_open, bt_input_hfp_codec_text(),
                 bt_input_hfp_pcm_sample_rate());
    } else {
        s_state.hfp_vrec_requested = false;
        s_state.hfp_vrec_active = false;
        s_state.hfp_audio_connect_started = 0;
        if (!s_state.hfp_audio_connected) {
            s_state.hfp_audio_connecting = false;
        }
        s_state.hfp_next_audio_req = 0;
        bt_input_hfp_audio_reset();
        bool audio_requested = bt_input_hfp_audio_request_internal("on mic enable");
        ESP_LOGI(TAG, "HFP mic enabled: audio_req=%d audio=%d codec=%s rate=%" PRIu32,
                 audio_requested, s_state.hfp_audio_connected, bt_input_hfp_codec_text(),
                 bt_input_hfp_pcm_sample_rate());
    }
}

static void bt_input_hfp_audio_disconnect(void)
{
    if (!s_state.hfp_has_peer || (!s_state.hfp_audio_connected && !s_state.hfp_audio_connecting)) {
        return;
    }

    esp_err_t ret = esp_hf_client_disconnect_audio(s_state.hfp_peer_bda);
    s_state.hfp_audio_connected = false;
    s_state.hfp_audio_connecting = false;
    s_state.hfp_audio_msbc = false;
    s_state.hfp_sync_conn_handle = 0;
    s_state.hfp_next_audio_req = 0;
    bt_input_audio_close();
    ESP_LOGI(TAG, "HFP audio disconnect: %s", esp_err_to_name(ret));
}

void bt_input_hfp_audio_reset(void)
{
    bt_input_hfp_fifo_reset();
    s_hfp_tx_budget_packets = 0;
    s_hfp_tx_credit_bytes = 0;
    s_hfp_tx_started = false;
    s_hfp_last_sample = 0;
}

void bt_input_hfp_feed_pcm(const int16_t *samples, size_t sample_count)
{
    if (!s_state.hfp_audio_connected || !s_state.mic_enabled || !s_state.audio_open || samples == NULL ||
        sample_count == 0) {
        bt_input_hfp_audio_request_internal("feed");
        s_hfp_feed_drops++;
        bt_input_hfp_log_audio_stats("feed disabled");
        return;
    }

    const size_t bytes = sample_count * sizeof(samples[0]);
    size_t used_before = bt_input_hfp_fifo_used();
    if ((used_before + bytes) > HFP_AUDIO_HIGH_WATERMARK_BYTES) {
        s_hfp_feed_drops++;
        bt_input_hfp_log_audio_stats("feed high_water");
        return;
    }

    size_t dropped = bt_input_hfp_fifo_write((const uint8_t *)samples, bytes);
    if (dropped > 0) {
        s_hfp_feed_drops++;
    }

    s_hfp_feed_calls++;
    s_hfp_feed_bytes += bytes;
    bt_input_hfp_log_audio_stats(dropped > 0 ? "feed drop_old" : "feed");
}
