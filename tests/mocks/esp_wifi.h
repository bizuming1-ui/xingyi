#pragma once
using esp_err_t=int;
constexpr esp_err_t ESP_OK=0;
enum wifi_ps_type_t {WIFI_PS_NONE=0,WIFI_PS_MIN_MODEM=1};
extern int mock_ps_set_result,mock_ps_get_result,mock_ps_set_calls;
extern bool mock_ps_set_ignored;
extern wifi_ps_type_t mock_ps_value;
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t value) {++mock_ps_set_calls;
extern bool mock_ps_set_ignored;if(mock_ps_set_result==ESP_OK && !mock_ps_set_ignored)mock_ps_value=value;return mock_ps_set_result;}
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* value) {if(mock_ps_get_result==ESP_OK)*value=mock_ps_value;return mock_ps_get_result;}
