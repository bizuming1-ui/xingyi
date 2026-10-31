#pragma once
#include "RpcWorker.h"
#include "HttpResponseDecoder.h"
#include "PollYieldPolicy.h"
#include <stdio.h>

// Adapter contract, NOT a TLS implementation. Every operation must obey the
// supplied remaining deadline. connectVerified must fail unless the configured
// trust anchor AND the exact host name have been verified, including valid
// certificate time; insecure or plaintext fallback is forbidden. Return -2 for
// timeout, -1 for error, 0 for EOF, positive byte count for success.
class VerifiedTlsChannel {
 public:
  enum class Connect { Ok, Error, Timeout };
  virtual ~VerifiedTlsChannel()=default;
  virtual uint32_t nowMillis()=0;
  virtual Connect connectVerified(const char* host,uint16_t port,uint32_t timeoutMs)=0;
  virtual int write(const char* data,size_t length,uint32_t timeoutMs)=0;
  virtual int read(char* data,size_t capacity,uint32_t timeoutMs)=0;
  // Optional cooperative cancellation; -3 means locally interrupted.
  virtual int readInterruptible(char* data,size_t capacity,uint32_t timeoutMs,const ReadInterrupt*) {return read(data,capacity,timeoutMs);}
  virtual void close()=0;
  virtual bool canReuse(){return false;}
};

// Fixed-origin HTTP exchange with optional verified connection reuse. No redirects.
// No connection is established until config and the endpoint have been checked.
// Runtime must provide a real verified TLS channel and synchronized clock.
class HttpRpcExchange final : public RpcExchange {
 public:
  HttpRpcExchange(VerifiedTlsChannel& channel,const char* host,const char* publishableKey,uint32_t timeoutMs,uint32_t dataTimeoutMs=0,bool keepAlive=false,PollYieldPolicy* pollYield=nullptr)
    : channel_(channel),host_(host),key_(publishableKey),timeoutMs_(timeoutMs),dataTimeoutMs_(dataTimeoutMs?dataTimeoutMs:timeoutMs),activeTimeoutMs_(timeoutMs),keepAlive_(keepAlive),pollYield_(pollYield) {}
  ~HttpRpcExchange() override {close();}
  void close() override {if(connected_){channel_.close();connected_=false;}}
  enum class Stage { Idle, Validate, Connect, Send, Receive, Complete };
  Stage lastStage() const {return stage_;}
  Status lastStatus() const {return status_;}
  int lastHttpStatus() const {return httpStatus_;}
  bool lastReused() const {return lastReused_;}
  bool lastPollYielded() const {return lastPollYielded_;}
  void validPoll() override {if(pollYield_)pollYield_->validPoll();}
  Response post(const char* endpoint,const char* json,size_t jsonLength,char* response,size_t responseCapacity) override {
    Response result{};stage_=Stage::Validate;status_=Status::NetworkError;httpStatus_=0;lastReused_=false;lastPollYielded_=false;
    struct Capture { HttpRpcExchange& owner; Response& response;
      ~Capture(){owner.status_=response.status;owner.httpStatus_=response.httpStatus;}
    } capture{*this,result};
    if(!validHost(host_) || !validKey(key_) || !validEndpoint(endpoint) || !json || jsonLength==0 || jsonLength>767 ||
       !response || responseCapacity>RpcCodec::MAX_RESPONSE_BYTES || timeoutMs_==0 || timeoutMs_>30000 || dataTimeoutMs_==0 || dataTimeoutMs_>30000) return result;
    // Status, upload, abort and barrier-status recovery may need more time on
    // slow links. Neither abort nor status grants or extends a lease. Freeze
    // and renew retain the short budget and server-side lease validation.
    // A timeout never counts as an acknowledgement or as a terminal outcome.
    activeTimeoutMs_=(strcmp(endpoint,RpcCodec::endpoint(SyncPlanner::Kind::Poll))==0 ||
                      strcmp(endpoint,RpcCodec::endpoint(SyncPlanner::Kind::Upload))==0 ||
                      strcmp(endpoint,RpcCodec::endpoint(SyncPlanner::Kind::BarrierStatus))==0 ||
                      strcmp(endpoint,RpcCodec::endpoint(SyncPlanner::Kind::Abort))==0)?dataTimeoutMs_:timeoutMs_;
    char headers[768]{};
    const int n=snprintf(headers,sizeof(headers),
      "POST /rest/v1/rpc/%s HTTP/1.1\r\nHost: %s\r\napikey: %s\r\nContent-Type: application/json\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: %s\r\nContent-Length: %lu\r\n\r\n",
      endpoint,host_,key_,keepAlive_?"keep-alive":"close",static_cast<unsigned long>(jsonLength));
    if(n<0 || static_cast<size_t>(n)>=sizeof(headers)) return result;
    const bool isPoll=strcmp(endpoint,RpcCodec::endpoint(SyncPlanner::Kind::Poll))==0;
    if(isPoll && pollYield_)pollYield_->beginPoll();
    const uint32_t start=channel_.nowMillis();
    struct Closer { HttpRpcExchange& owner; bool keep; explicit Closer(HttpRpcExchange& x):owner(x),keep(false){} ~Closer(){if(!keep)owner.close();} } closer(*this);
    stage_=Stage::Connect;
    if(connected_ && (!keepAlive_ || static_cast<uint32_t>(start-lastUsed_)>=5000 || !channel_.canReuse()))close();
    lastReused_=connected_;
    if(!connected_) {
      const auto connected=channel_.connectVerified(host_,443,activeTimeoutMs_);
      if(connected!=VerifiedTlsChannel::Connect::Ok) {channel_.close();result.status=connected==VerifiedTlsChannel::Connect::Timeout?Status::Timeout:Status::NetworkError;return result;}
      connected_=true;
    }
    if(!remaining(start)) { result.status=Status::Timeout; return result; }
    stage_=Stage::Send;
    int failure=writeAll(headers,static_cast<size_t>(n),start);
    if(!failure) failure=writeAll(json,jsonLength,start);
    if(failure) { result.status=failure==-2?Status::Timeout:Status::NetworkError; return result; }
    stage_=Stage::Receive;
    HttpResponseDecoder parser(response,responseCapacity);
    struct ReadBuffer { char data[256]{}; ~ReadBuffer(){RpcCodec::wipe(data,sizeof(data));} } block;
    for (;;) {
      const uint32_t left=remaining(start);
      if(!left){result.status=Status::Timeout;return result;}
      const int count=isPoll && pollYield_?channel_.readInterruptible(block.data,sizeof(block.data),left,pollYield_):channel_.read(block.data,sizeof(block.data),left);
      if(count==-3 && isPoll && pollYield_) {pollYield_->yielded();lastPollYielded_=true;result.status=Status::Interrupted;return result;}
      if(!remaining(start)){result.status=Status::Timeout;return result;}
      if(count<=0 || static_cast<size_t>(count)>sizeof(block.data)) {
        result.status=count==-2?Status::Timeout:Status::NetworkError; return result;
      }
      const auto parsed=parser.feed(block.data,static_cast<size_t>(count));
      if(parsed==HttpResponseDecoder::Result::Complete) {
        stage_=Stage::Complete;result.status=Status::Ok;result.httpStatus=parser.statusCode();result.length=parser.length();
        closer.keep=keepAlive_ && parser.reusable() && result.httpStatus>=200 && result.httpStatus<300 && channel_.canReuse();
        if(closer.keep)lastUsed_=channel_.nowMillis();
        return result;
      }
      if(parsed==HttpResponseDecoder::Result::TooLarge){result.status=Status::ResponseTooLarge;return result;}
      if(parsed==HttpResponseDecoder::Result::Invalid){result.status=Status::NetworkError;return result;}
    }
  }
 private:
  VerifiedTlsChannel& channel_; const char* host_;const char* key_;uint32_t timeoutMs_,dataTimeoutMs_,activeTimeoutMs_;
  bool keepAlive_=false,connected_=false,lastReused_=false,lastPollYielded_=false;PollYieldPolicy* pollYield_=nullptr;uint32_t lastUsed_=0;
  Stage stage_=Stage::Idle;Status status_=Status::NetworkError;int httpStatus_=0;
  uint32_t remaining(uint32_t start) {const uint32_t elapsed=channel_.nowMillis()-start;return elapsed>=activeTimeoutMs_?0:activeTimeoutMs_-elapsed;}
  int writeAll(const char* data,size_t length,uint32_t start) {
    size_t sent=0;
    while(sent<length) {
      const uint32_t left=remaining(start);if(!left)return -2;
      const int n=channel_.write(data+sent,length-sent,left);
      if(!remaining(start))return -2;
      if(n<=0 || static_cast<size_t>(n)>length-sent)return n==-2?-2:-1;
      sent+=static_cast<size_t>(n);
    }
    return 0;
  }
  static bool validEndpoint(const char* endpoint) {
    if(!endpoint)return false;
    using K=SyncPlanner::Kind;
    const K allowed[]={K::Poll,K::Upload,K::Freeze,K::BarrierStatus,K::Renew,K::Abort};
    for(auto kind:allowed)if(strcmp(endpoint,RpcCodec::endpoint(kind))==0)return true;
    return false;
  }
  static bool validHost(const char* s) {
    if(!s)return false;
    size_t n=0;while(n<80 && s[n])++n;
    const char suffix[]=".supabase.co";const size_t suffixSize=sizeof(suffix)-1;
    if(n<=suffixSize || n>=80 || n-suffixSize>63 || memcmp(s+n-suffixSize,suffix,suffixSize)!=0)return false;
    // Exactly one lowercase project-ref label, no port, dots or header syntax.
    for(size_t i=0;i<n-suffixSize;++i)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')))return false;
    return true;
  }
  static bool validKey(const char* s) {
    if(!s || strncmp(s,"sb_publishable_",15)!=0)return false;
    size_t n=0;while(n<128 && s[n]) {const char c=s[n];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;++n;}
    return n>15 && n<128;
  }
};
