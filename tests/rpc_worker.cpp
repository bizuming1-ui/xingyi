#include "../firmware/include/RpcWorker.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using K=SyncPlanner::Kind;
const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* binding="20000000-0000-4000-8000-000000000001";
const char* epoch="10000000-0000-4000-8000-000000000001";
struct FakeExchange:RpcExchange {
  Response next{}; std::string response; int calls=0;
  Response post(const char* endpoint,const char* body,size_t bodyLength,char* output,size_t capacity) override {
    ++calls; assert(strcmp(endpoint,"device_ingest")==0); assert(strstr(endpoint,secret)==nullptr);
    StaticJsonDocument<1024> request; assert(!deserializeJson(request,body,bodyLength));
    assert(strcmp(request["p_secret"],secret)==0); assert(strcmp(request["p_binding"],binding)==0);
    assert(bodyLength==strlen(body));
    if(response.size()<=capacity)memcpy(output,response.data(),response.size());
    return next;
  }
};
int main() {
  SyncPlanner::Command cmd{};cmd.kind=K::Upload;memcpy(cmd.binding,binding,37);memcpy(cmd.epoch,epoch,37);cmd.total=8;
  SyncPlanner::Reply reply{};FakeExchange fake;
  auto o=RpcWorker::execute(fake,cmd,"bad",reply);assert(o.result==RpcWorker::Result::InvalidRequest && !reply.success && fake.calls==0);
  fake.next.status=RpcExchange::Status::Timeout;
  o=RpcWorker::execute(fake,cmd,secret,reply);assert(o.result==RpcWorker::Result::TransportError && !reply.success);
  fake.next.status=RpcExchange::Status::Ok;fake.next.httpStatus=401;
  fake.response="{\"message\":\"unauthorized\"}";fake.next.length=fake.response.size();
  o=RpcWorker::execute(fake,cmd,secret,reply);assert(o.result==RpcWorker::Result::HttpError && o.httpStatus==401 && !reply.success);
  fake.next.httpStatus=200;fake.response="{broken";fake.next.length=fake.response.size();
  o=RpcWorker::execute(fake,cmd,secret,reply);assert(o.result==RpcWorker::Result::InvalidResponse && !reply.success);
  fake.response=std::string("{\"accepted_total\":8,\"binding_id\":\"")+binding+"\"}";fake.next.length=fake.response.size();
  o=RpcWorker::execute(fake,cmd,secret,reply);assert(o.result==RpcWorker::Result::Ok && reply.success && reply.acceptedTotal==8);
  fake.next.length=RpcCodec::MAX_RESPONSE_BYTES+1;
  o=RpcWorker::execute(fake,cmd,secret,reply);assert(o.result==RpcWorker::Result::TransportError && !reply.success);
  std::cout<<"RPC worker tests passed\n";
}
