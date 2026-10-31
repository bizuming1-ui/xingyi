#include "../firmware/include/HttpRpcExchange.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

struct FakeTls : VerifiedTlsChannel {
  std::string sent, incoming; size_t cursor=0;uint32_t time=0; int closes=0, connects=0, calls=0; int maxWrite=11,maxRead=7;bool failConnect=false,timeoutRead=false, late=false,allowReuse=false,failWrite=false;uint32_t readDelay=0;
  uint32_t nowMillis() override {return time;}
  Connect connectVerified(const char* host,uint16_t port,uint32_t timeout) override {++connects;assert(strcmp(host,"project123.supabase.co")==0 && port==443 && timeout>0);return failConnect?Connect::Error:Connect::Ok;}
  int write(const char* data,size_t length,uint32_t timeout) override {assert(timeout);++calls;if(failWrite)return -1;size_t n=length<static_cast<size_t>(maxWrite)?length:static_cast<size_t>(maxWrite);sent.append(data,n);++time;return static_cast<int>(n);}
  int read(char* out,size_t cap,uint32_t timeout) override {assert(timeout);++calls;if(timeoutRead)return -2;if(late)time+=timeout;else {time+=readDelay+1;readDelay=0;}if(cursor==incoming.size())return 0;size_t n=incoming.size()-cursor;if(n>cap)n=cap;if(n>static_cast<size_t>(maxRead))n=maxRead;memcpy(out,incoming.data()+cursor,n);cursor+=n;return static_cast<int>(n);}
  PollYieldPolicy* simulateCount=nullptr;int interruptedReads=0;bool simulateStall=false;
  int readInterruptible(char* out,size_t cap,uint32_t timeout,const ReadInterrupt* interrupt) override {
    if(simulateCount){simulateCount->accepted();simulateCount=nullptr;}
    if(interrupt && interrupt->interrupted()){++interruptedReads;return -3;}
    if(simulateStall && interrupt){time+=250;if(interrupt->interrupted()){++interruptedReads;return -3;}}
    return read(out,cap,timeout);
  }
  void close() override {++closes;}
  bool canReuse() override {return allowReuse;}
};
const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* key="sb_publishable_public-key";
int main(){
  FakeTls socket; HttpRpcExchange exchange(socket,"project123.supabase.co",key,5000);
  const std::string body="{\"desired_order_id\":null,\"desired_binding_id\":null,\"desired_binding_revision\":null}";
  socket.incoming="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
  SyncPlanner::Command cmd{};cmd.kind=SyncPlanner::Kind::Poll;SyncPlanner::Reply reply{};
  auto outcome=RpcWorker::execute(exchange,cmd,secret,reply);
  assert(outcome.result==RpcWorker::Result::Ok && reply.success && socket.closes==1);
  assert(exchange.lastStage()==HttpRpcExchange::Stage::Complete && exchange.lastStatus()==RpcExchange::Status::Ok && exchange.lastHttpStatus()==200);
  assert(exchange.connectMillis()==0 && exchange.sentMillis()>0 && exchange.firstByteMillis()>=exchange.sentMillis() && exchange.receivedBytes()>0);
  assert(socket.sent.find("POST /rest/v1/rpc/device_status HTTP/1.1\r\nHost: project123.supabase.co\r\napikey: sb_publishable_public-key\r\n") == 0);
  assert(socket.sent.find("Authorization:")==std::string::npos);
  assert(socket.sent.find(secret)>socket.sent.find("\r\n\r\n"));
  FakeTls invalid; HttpRpcExchange wrongHost(invalid,"project123.supabase.co.evil.com",key,1000);char output[32]{};
  assert(wrongHost.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && invalid.connects==0);
  HttpRpcExchange secretKey(invalid,"project123.supabase.co","sb_secret_key",1000);
  assert(secretKey.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && invalid.connects==0);
  HttpRpcExchange headerInjection(invalid,"project123.supabase.co","sb_publishable_a\r\nInjected: x",1000);
  assert(headerInjection.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && invalid.connects==0);
  HttpRpcExchange correct(invalid,"project123.supabase.co",key,1000);
  assert(correct.post("../device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && invalid.connects==0);
  invalid.failConnect=true;assert(correct.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && invalid.closes==1);
  assert(correct.lastStage()==HttpRpcExchange::Stage::Connect && correct.lastStatus()==RpcExchange::Status::NetworkError && correct.lastHttpStatus()==0);
  FakeTls redirect;redirect.incoming="HTTP/1.1 302 Found\r\nLocation: https://evil.example/\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange redirection(redirect,"project123.supabase.co",key,5000);
  outcome=RpcWorker::execute(redirection,cmd,secret,reply);assert(outcome.result==RpcWorker::Result::HttpError && redirect.connects==1 && !reply.success);
  FakeTls truncated;truncated.incoming="HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n{}";
  HttpRpcExchange shortResponse(truncated,"project123.supabase.co",key,5000);
  assert(shortResponse.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError && truncated.closes==1);
  FakeTls oversized;oversized.incoming="HTTP/1.1 200 OK\r\nContent-Length: 33\r\n\r\n";
  HttpRpcExchange large(oversized,"project123.supabase.co",key,5000);
  assert(large.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::ResponseTooLarge);
  FakeTls slow;slow.late=true;slow.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange deadline(slow,"project123.supabase.co",key,5000);
  assert(deadline.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Timeout && slow.closes==1);
  FakeTls stalled;stalled.timeoutRead=true;HttpRpcExchange timeout(stalled,"project123.supabase.co",key,5000);
  assert(timeout.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Timeout && stalled.closes==1);
  assert(timeout.lastStage()==HttpRpcExchange::Stage::Receive && timeout.lastStatus()==RpcExchange::Status::Timeout);
  // A delayed response is accepted only by the longer poll/upload budget;
  // lease-related endpoints keep the original budget and still time out.
  FakeTls data;data.readDelay=3500;data.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange longData(data,"project123.supabase.co",key,3000,8000);
  assert(longData.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  FakeTls upload;upload.readDelay=3500;upload.incoming=data.incoming;
  HttpRpcExchange longUpload(upload,"project123.supabase.co",key,3000,8000);
  assert(longUpload.post("device_ingest","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  FakeTls abort;abort.readDelay=3500;abort.incoming=data.incoming;
  HttpRpcExchange slowAbort(abort,"project123.supabase.co",key,3000,8000);
  assert(slowAbort.post(RpcCodec::endpoint(SyncPlanner::Kind::Abort),"{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  FakeTls state;state.readDelay=3500;state.incoming=data.incoming;
  HttpRpcExchange slowState(state,"project123.supabase.co",key,3000,8000);
  assert(slowState.post(RpcCodec::endpoint(SyncPlanner::Kind::BarrierStatus),"{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  FakeTls lease;lease.readDelay=3500;lease.incoming=data.incoming;
  HttpRpcExchange shortLease(lease,"project123.supabase.co",key,3000,8000);
  assert(shortLease.post(RpcCodec::endpoint(SyncPlanner::Kind::Renew),"{}",2,output,sizeof(output)).status==RpcExchange::Status::Timeout);
  // Verified connections may be reused; server close and all errors force
  // a close, and an uncertain write is never retried by the transport.
  FakeTls reuse;reuse.allowReuse=true;reuse.maxRead=1;
  reuse.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nHTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange persistent(reuse,"project123.supabase.co",key,5000,5000,true);
  assert(persistent.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  assert(reuse.connects==1 && reuse.closes==0);
  assert(persistent.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  assert(reuse.connects==1 && reuse.closes==1);
  FakeTls stale;stale.allowReuse=true;stale.maxRead=1;stale.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange uncertain(stale,"project123.supabase.co",key,5000,5000,true);
  assert(uncertain.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  stale.failWrite=true;const int before=stale.connects;
  assert(uncertain.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::NetworkError);
  assert(stale.connects==before && stale.closes==1);
  FakeTls expired;expired.allowReuse=true;expired.maxRead=1;expired.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  HttpRpcExchange aged(expired,"project123.supabase.co",key,5000,5000,true);
  assert(aged.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  expired.time+=5000;assert(aged.post("device_status","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  assert(expired.connects==2 && expired.closes==1);
  // One valid, fully decoded poll opens the gate. A count accepted while a
  // subsequent status read is in flight may cancel ONLY that read; the failed
  // poll cannot acknowledge counts or keep its socket. The next status read
  // is not cancellable until another decoded success, preventing starvation.
  PollYieldPolicy priority;FakeTls preempt;preempt.allowReuse=true;preempt.maxRead=256;
  preempt.incoming="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
  HttpRpcExchange prioritized(preempt,"project123.supabase.co",key,5000,5000,true,&priority);
  assert(RpcWorker::execute(prioritized,cmd,secret,reply).result==RpcWorker::Result::Ok);
  preempt.simulateCount=&priority;preempt.simulateStall=true;
  assert(RpcWorker::execute(prioritized,cmd,secret,reply).result==RpcWorker::Result::TransportError);
  preempt.simulateStall=false;
  assert(prioritized.lastPollYielded() && preempt.interruptedReads==1 && preempt.closes==1 && !reply.success);
  preempt.incoming="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;preempt.cursor=0;
  preempt.simulateCount=&priority;
  assert(RpcWorker::execute(prioritized,cmd,secret,reply).result==RpcWorker::Result::Ok);
  assert(!prioritized.lastPollYielded() && preempt.interruptedReads==1);
  // A response arriving within the grace interval completes and is reusable.
  preempt.incoming="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;preempt.cursor=0;preempt.simulateCount=&priority;
  assert(RpcWorker::execute(prioritized,cmd,secret,reply).result==RpcWorker::Result::Ok);
  assert(!prioritized.lastPollYielded() && preempt.interruptedReads==1);
  // Uploads, renewals and all other requests do not use the interruptible read.
  preempt.incoming="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";preempt.cursor=0;preempt.simulateCount=&priority;
  assert(prioritized.post("device_ingest","{}",2,output,sizeof(output)).status==RpcExchange::Status::Ok);
  assert(preempt.simulateCount==&priority && preempt.interruptedReads==1);
  std::cout<<"HTTP RPC exchange tests passed\n";
}
