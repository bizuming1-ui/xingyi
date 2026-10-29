#pragma once
#include <stdint.h>
extern int64_t mock_esp_timer_us;
inline int64_t esp_timer_get_time(){return mock_esp_timer_us;}
