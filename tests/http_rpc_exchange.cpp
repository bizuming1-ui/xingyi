#include "../firmware/include/HttpRpcExchange.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

struct FakeTls : VerifiedTlsChannel {
  std::string sent, incoming; size_t cursor=0;uint32_t time=0; int closes=0, connects=0, calls=0; int maxWrite=11,maxRead=7;bool failConnect=false,timeoutRead=false, late=false;
  uint32_t nowMillis() override {return time;}
  Connect connectVerified(const char* host,uint16_t port,uint32_t timeout) override {++connects;assert(strcmp(host,"project123.supabase.co")==0 && port==443 && timeout>0);return failConnect?Connect::Error:Connect::Ok;}
  int write(const char* data,size_t length,uint32_t timeout) override {assert(timeout);++calls;size_t n=length<static_cast<size_t>(maxWrite)?length:static_cast<size_t>(maxWrite);sent.append(data,n);++time;return static_cast<int>(n);}
  int read(char* out,size_t cap,uint32_t timeout) override {assert(timeout);++calls;if(timeoutRead)return -2;if(late)time+=timeout;else ++time;if(cursor==incoming.size())return 0;size_t n=incoming.size()-cursor;if(n>cap)n=cap;if(n>static_cast<size_t>(maxRead))n=maxRead;memcpy(out,incoming.data()+cursor,n);cursor+=n;return static_cast<int>(n);}
  void close() override {++closes;}
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
  std::cout<<"HTTP RPC exchange tests passed\n";
}
