#pragma once
#include <stdint.h>
using esp_err_t=int;
constexpr int ESP_OK=0;
enum { WIFI_SCAN_TYPE_ACTIVE=1, WIFI_EVENT_SCAN_DONE=1 };
enum wifi_auth_mode_t {WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_WPA2_ENTERPRISE,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_OWE};
enum wifi_cipher_type_t {WIFI_CIPHER_TYPE_NONE,WIFI_CIPHER_TYPE_TKIP,WIFI_CIPHER_TYPE_CCMP,WIFI_CIPHER_TYPE_TKIP_CCMP};
struct wifi_ap_record_t{uint8_t ssid[33]{},bssid[6]{},primary=0;int8_t rssi=0;wifi_auth_mode_t authmode=WIFI_AUTH_OPEN;wifi_cipher_type_t pairwise_cipher=WIFI_CIPHER_TYPE_NONE,group_cipher=WIFI_CIPHER_TYPE_NONE;};
struct wifi_scan_config_t{uint8_t channel=0;bool show_hidden=false;int scan_type=0;struct {struct{uint32_t min=0,max=0;}active;}scan_time;};
struct wifi_event_sta_scan_done_t{int status;};
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);
esp_err_t esp_wifi_clear_ap_list();
