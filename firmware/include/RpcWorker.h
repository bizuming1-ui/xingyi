#pragma once
#include "RpcCodec.h"

// Must be implemented by an HTTPS transport running outside the GPIO/counting
// thread. It must verify the certificate/hostname, reject redirects, enforce
// bounded request time/response size, and never log request bodies or secrets.
// The endpoint is a constant RPC name, never a URL containing credentials.
class RpcExchange {
 public:
  enum class Status { Ok, NetworkError, Timeout, ResponseTooLarge };
  struct Response { Status status=Status::NetworkError; int httpStatus=0; size_t length=0; };
  virtual ~RpcExchange() = default;
  virtual Response post(const char* endpoint,const char* json,size_t jsonLength,char* response,size_t responseCapacity)=0;
};

// Synchronous execution for an isolated network worker only. No network
// implementation is supplied here and this must not run on the button loop.
class RpcWorker {
 public:
  enum class Result { Ok, InvalidRequest, TransportError, HttpError, InvalidResponse };
  struct Outcome {
    Result result; int httpStatus;
    Outcome(Result value=Result::InvalidRequest, int code=0) : result(value), httpStatus(code) {}
  };

  static Outcome execute(RpcExchange& transport, const SyncPlanner::Command& command, const char* secret, SyncPlanner::Reply& reply) {
    reply=SyncPlanner::Reply{};
    Sensitive request{};
    SensitiveResponse response{};
    if (RpcCodec::encode(command,secret,request.data,sizeof(request.data))!=RpcCodec::Result::Ok) return {};
    const char* endpoint=RpcCodec::endpoint(command.kind);
    const auto result=transport.post(endpoint,request.data,strlen(request.data),response.data,RpcCodec::MAX_RESPONSE_BYTES);
    if (result.status!=RpcExchange::Status::Ok || result.length>RpcCodec::MAX_RESPONSE_BYTES) return {Result::TransportError,result.httpStatus};
    if (result.httpStatus<200 || result.httpStatus>=300) return {Result::HttpError,result.httpStatus};
    if (RpcCodec::decode(command,response.data,result.length,reply)!=RpcCodec::Result::Ok) return {Result::InvalidResponse,result.httpStatus};
    return {Result::Ok,result.httpStatus};
  }
 private:
  struct Sensitive {
    char data[768]{};
    ~Sensitive() { RpcCodec::wipe(data,sizeof(data)); }
  };
  struct SensitiveResponse {
    char data[RpcCodec::MAX_RESPONSE_BYTES+1]{};
    ~SensitiveResponse() { RpcCodec::wipe(data,sizeof(data)); }
  };
};
