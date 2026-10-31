#pragma once
#include "dns.h"
using tcpip_callback_fn=void (*)(void*);
extern bool fakeQueueFull;
extern tcpip_callback_fn fakeTcpipCallback;
extern void* fakeTcpipContext;
inline err_t tcpip_try_callback(tcpip_callback_fn callback,void* ctx){
  if(fakeQueueFull)return ERR_MEM;
  fakeTcpipCallback=callback;fakeTcpipContext=ctx;return ERR_OK;
}
