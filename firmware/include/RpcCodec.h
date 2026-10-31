#pragma once
// All 64-bit counts must be preserved on the ESP32. Never use a floating point
// number for a production total.
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif
#include <ArduinoJson.h>
#include "SyncPlanner.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// JSON mapping only. Does not transmit requests, implement TLS, or hold secrets.
// Device RPCs must be POSTed over verified HTTPS to a trusted Supabase origin.
class RpcCodec {
 public:
  enum class Result { Ok, InvalidCommand, InvalidSecret, BufferTooSmall, InvalidResponse };
  static constexpr size_t MAX_RESPONSE_BYTES=2048;

  static const char* endpoint(SyncPlanner::Kind kind) {
    using K=SyncPlanner::Kind;
    switch(kind) {
      case K::Poll: return "device_status";
      case K::Upload: return "device_ingest";
      case K::Freeze: return "device_confirmation_freeze";
      case K::BarrierStatus: return "device_confirmation_status";
      case K::Renew: return "device_confirmation_renew";
      case K::Abort: return "device_confirmation_abort";
      default: return nullptr;
    }
  }

  // output contains a credential: callers must never log it and must erase the
  // buffer after use. The caller provides the credential from private storage.
  static Result encode(const SyncPlanner::Command& cmd, const char* secret, char* output, size_t capacity) {
    wipe(output,capacity);
    if (!secretValid(secret)) return Result::InvalidSecret;
    if (!commandValid(cmd)) return Result::InvalidCommand;
    using K=SyncPlanner::Kind;
    StaticJsonDocument<768> json;
    json["p_secret"]=secret;
    if (cmd.kind==K::Upload || cmd.kind==K::Freeze || cmd.kind==K::Renew) {
      json["p_binding"]=cmd.binding; json["p_epoch"]=cmd.epoch; json["p_total"]=cmd.total;
    }
    if (cmd.kind==K::Freeze || cmd.kind==K::BarrierStatus || cmd.kind==K::Renew || cmd.kind==K::Abort) json["p_barrier"]=cmd.barrier;
    if (cmd.kind==K::Renew) json["p_nonce"]=cmd.nonce;
    if (json.overflowed()) return Result::BufferTooSmall;
    const size_t need=measureJson(json)+1;
    if (!output || capacity<need) return Result::BufferTooSmall;
    if (serializeJson(json,output,capacity)!=need-1) { output[0]='\0'; return Result::BufferTooSmall; }
    return Result::Ok;
  }

  // Explicitly clear credential-bearing buffers immediately after transmission.
  static void wipe(char* buffer,size_t size) {
    volatile char* p=buffer;
    if (p) for(size_t i=0;i<size;++i) p[i]=0;
  }

  // Parse only successful HTTP 2xx responses. Non-2xx, oversized, truncated,
  // or malformed replies are failures: the planner must not acknowledge them.
  static Result decode(const SyncPlanner::Command& cmd, const char* body, size_t length, SyncPlanner::Reply& result) {
    result=SyncPlanner::Reply{};
    if (!commandValid(cmd) || !body || length==0 || length>MAX_RESPONSE_BYTES || !singleObject(body,length)) return Result::InvalidResponse;
    StaticJsonDocument<1536> json;
    if (deserializeJson(json,body,length) || json.overflowed() || !json.is<JsonObject>()) return Result::InvalidResponse;
    using K=SyncPlanner::Kind;
    using B=SyncPlanner::BarrierState;
    SyncPlanner::Reply parsed{};
    if (cmd.kind==K::Poll) {
      const auto order=json["desired_order_id"], binding=json["desired_binding_id"], rev=json["desired_binding_revision"];
      if (!(order.isNull() && binding.isNull() && rev.isNull())) {
        if (!copyUuid(parsed.desiredOrder,order) || !copyUuid(parsed.desiredBinding,binding) || !readCount(rev,parsed.desiredRevision) || parsed.desiredRevision==0) return Result::InvalidResponse;
      }
      const auto barrier=json["barrier_id"], state=json["barrier_state"], barrierOrder=json["barrier_order_id"];
      if (barrier.isNull() && state.isNull() && barrierOrder.isNull()) parsed.barrierState=B::None;
      else {
        char orderId[37]{};
        if (!copyUuid(parsed.barrier,barrier) || !copyUuid(orderId,barrierOrder) || !stateValue(state,parsed.barrierState)) return Result::InvalidResponse;
        if (parsed.barrierState!=B::Requested && parsed.barrierState!=B::Ready && parsed.barrierState!=B::Expired) return Result::InvalidResponse;
      }
    } else if (cmd.kind==K::Upload) {
      uint64_t accepted=0; char binding[37]{};
      if (!readCount(json["accepted_total"],accepted) || !copyUuid(binding,json["binding_id"]) || strcmp(binding,cmd.binding)!=0) return Result::InvalidResponse;
      parsed.acceptedTotal=accepted;
    } else {
      if (!stateValue(json["state"],parsed.barrierState)) return Result::InvalidResponse;
      if (cmd.kind==K::BarrierStatus) {
        char barrier[37]{};
        if (!copyUuid(barrier,json["id"]) || strcmp(barrier,cmd.barrier)!=0) return Result::InvalidResponse;
      }
      if (cmd.kind==K::Freeze && parsed.barrierState!=B::Ready && parsed.barrierState!=B::Expired) return Result::InvalidResponse;
      if (cmd.kind==K::Renew && parsed.barrierState!=B::Ready && parsed.barrierState!=B::Expired && parsed.barrierState!=B::Completed && parsed.barrierState!=B::Cancelled) return Result::InvalidResponse;
      if (cmd.kind==K::Abort && parsed.barrierState!=B::Completed && parsed.barrierState!=B::Cancelled) return Result::InvalidResponse;
      if (cmd.kind==K::BarrierStatus && parsed.barrierState==B::None) return Result::InvalidResponse;
      if (parsed.barrierState==B::Ready && !copyUuid(parsed.nonce,json["lease_nonce"])) return Result::InvalidResponse;
    }
    parsed.success=true; result=parsed;
    return Result::Ok;
  }

 private:
  static bool commandValid(const SyncPlanner::Command& cmd) {
    using K=SyncPlanner::Kind;
    if (!endpoint(cmd.kind)) return false;
    if ((cmd.kind==K::Upload || cmd.kind==K::Freeze || cmd.kind==K::Renew) && (!uuid(cmd.binding) || !uuid(cmd.epoch) || cmd.total>DurableCounter::MAX_COUNT)) return false;
    if ((cmd.kind==K::Freeze || cmd.kind==K::BarrierStatus || cmd.kind==K::Renew || cmd.kind==K::Abort) && !uuid(cmd.barrier)) return false;
    return cmd.kind!=K::Renew || uuid(cmd.nonce);
  }
  // ArduinoJson accepts trailing data by default. Require exactly one top-level
  // object, with only JSON whitespace after it, before decoding any result.
  static bool singleObject(const char* text,size_t len) {
    size_t i=0; while(i<len && space(text[i]))++i;
    if(i==len || text[i]!='{')return false;
    unsigned depth=0; bool quoted=false, escaped=false;
    for(;i<len;++i) {
      const char c=text[i];
      if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
      if(c=='"'){quoted=true;continue;}
      if(c=='{' || c=='[')++depth;
      else if(c=='}' || c==']'){
        if(depth==0)return false;
        if(--depth==0){if(c!='}')return false; ++i;while(i<len && space(text[i]))++i;return i==len;}
      }
    }
    return false;
  }
  static bool space(char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}
  static bool secretValid(const char* s) {
    if (!s) return false;
    for (unsigned i=0;i<64;++i) {char c=s[i];if(!c || !((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;}
    return s[64]=='\0';
  }
  static bool uuid(const char* s) {
    if (!s) return false;
    for (unsigned i=0;i<36;++i) {char c=s[i];if(!c)return false;
      if (i==8||i==13||i==18||i==23) {if(c!='-')return false;}
      else if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false;}
    return s[36]=='\0';
  }
  static bool copyUuid(char out[37], JsonVariantConst v) {
    if (!v.is<const char*>()) return false;
    const char* s=v.as<const char*>(); if(!uuid(s))return false;
    memcpy(out,s,37); return true;
  }
  static bool readCount(JsonVariantConst v, uint64_t& out) {
    if (!v.is<uint64_t>()) return false;
    out=v.as<uint64_t>(); return out<=DurableCounter::MAX_COUNT;
  }
  static bool stateValue(JsonVariantConst v, SyncPlanner::BarrierState& state) {
    if (!v.is<const char*>())return false;
    const char* s=v.as<const char*>(); using B=SyncPlanner::BarrierState;
    if (strcmp(s,"requested")==0) state=B::Requested;
    else if (strcmp(s,"ready")==0) state=B::Ready;
    else if (strcmp(s,"completed")==0) state=B::Completed;
    else if (strcmp(s,"cancelled")==0) state=B::Cancelled;
    else if (strcmp(s,"expired")==0) state=B::Expired;
    else return false;
    return true;
  }
};
