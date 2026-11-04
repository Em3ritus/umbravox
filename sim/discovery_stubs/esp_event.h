#pragma once
#include "esp_wifi.h"
using esp_event_base_t=int;
using esp_event_handler_instance_t=int;
constexpr int WIFI_EVENT=1;
using Handler=void(*)(void*,esp_event_base_t,int32_t,void*);
esp_err_t esp_event_handler_instance_register(int,int,Handler,void*,int*);
