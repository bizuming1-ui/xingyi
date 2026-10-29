#pragma once
#include "FreeRTOS.h"
#include <cstring>
struct StaticQueue_t { uint8_t* storage; size_t itemSize; bool full; };
using QueueHandle_t=StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(UBaseType_t length,UBaseType_t itemSize,uint8_t* storage,StaticQueue_t* queue) {
  if(length!=1 || !storage || !queue) return nullptr;
  queue->storage=storage;queue->itemSize=itemSize;queue->full=false;return queue;
}
inline BaseType_t xQueueSend(QueueHandle_t q,const void* item,unsigned wait) {
  if(!q || !item || wait || q->full)return pdFALSE;
  std::memcpy(q->storage,item,q->itemSize);q->full=true;return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q,void* item,unsigned wait) {
  if(!q || !item || wait || !q->full)return pdFALSE;
  std::memcpy(item,q->storage,q->itemSize);q->full=false;return pdTRUE;
}
