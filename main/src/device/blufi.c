/*
 * blufi.c
 *
 *  Created on: 2018-03-30 16:40
 *      Author: Jack Chen <redchenjs@live.com>
 */

#include <string.h>

#include "esp_bt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_blufi_api.h"
#include "esp_gap_ble_api.h"

#include "device/blufi.h"
#include "system/event.h"
#include "tasks/blufi_security.h"

#define TAG "blufi-0"

uint8_t blufi0_status = BLUFI_DISCONNECTED;

#define BLE_DEVICE_NAME CONFIG_BLE_DEVICE_NAME

static uint8_t blufi_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};

static esp_ble_adv_data_t blufi_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x100,
    .max_interval = 0x100,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = blufi_service_uuid128,
    .flag = 0x6
};

static esp_ble_adv_params_t blufi_adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* connect info */
    static uint8_t server_if;
    static uint16_t conn_id;
    static wifi_config_t sta_config;

    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
        esp_ble_gap_config_adv_data(&blufi_adv_data);
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        server_if = param->connect.server_if;
        conn_id = param->connect.conn_id;
        esp_ble_gap_stop_advertising();
        blufi_security_init();
        blufi0_status = BLUFI_CONNECTED;
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        blufi0_status = BLUFI_DISCONNECTED;
        blufi_security_deinit();
        esp_ble_gap_start_advertising(&blufi_adv_params);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        esp_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        ESP_LOGE(TAG, "report error, error code %d", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);

        if (gl_sta_connected) {
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = gl_sta_ssid;
            info.sta_ssid_len = gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        esp_blufi_close(server_if, conn_id);
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        break;
    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGW(TAG, "recv sta bssid %s", sta_config.sta.ssid);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGW(TAG, "recv sta ssid %s", sta_config.sta.ssid);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        ESP_LOGW(TAG, "recv sta password %s", sta_config.sta.password);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        wifi_scan_config_t scan_conf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_conf, true));
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        ESP_LOGW(TAG, "recv custom data %d", param->custom_data.data_len);
        esp_log_buffer_hex("custom data", param->custom_data.data, param->custom_data.data_len);
        break;
    case ESP_BLUFI_EVENT_RECV_USERNAME:
        break;
    case ESP_BLUFI_EVENT_RECV_CA_CERT:
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        break;
    case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        break;;
    case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        break;
    default:
        break;
    }
}

static void blufi_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&blufi_adv_params);
        break;
    default:
        break;
    }
}

static esp_blufi_callbacks_t blufi_callbacks = {
    .event_cb = blufi_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

void blufi0_init(void)
{
    ESP_LOGI(TAG, "start blufi version %04x", esp_blufi_get_version());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(blufi_gap_event_handler));
    ESP_ERROR_CHECK(esp_blufi_register_callbacks(&blufi_callbacks));
    esp_blufi_profile_init();
}