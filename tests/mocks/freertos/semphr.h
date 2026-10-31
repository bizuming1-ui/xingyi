#pragma once
#include "FreeRTOS.h"
struct StaticSemaphore_t {bool taken=false;};
using SemaphoreHandle_t=StaticSemaphore_t*;
extern bool mock_mutex_creation_fails;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s){return mock_mutex_creation_fails?nullptr:s;}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){if(!s || timeout || s->taken)return pdFALSE;s->taken=true;return pdTRUE;}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s){if(!s || !s->taken)return pdFALSE;s->taken=false;return pdTRUE;}
