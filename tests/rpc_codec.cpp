#include "../firmware/include/RpcCodec.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using K=SyncPlanner::Kind; using B=SyncPlanner::BarrierState; using C=RpcCodec::Result;
const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* order="00000000-0000-4000-8000-000000000001";
const char* binding="20000000-0000-4000-8000-000000000001";
const char* epoch="10000000-0000-4000-8000-000000000001";
const char* barrier="30000000-0000-4000-8000-000000000001";
const char* nonce="40000000-0000-4000-8000-000000000001";
void copy(char* out,const char* in){memcpy(out,in,37);}
SyncPlanner::Command command(K k){SyncPlanner::Command c{};c.kind=k;copy(c.binding,binding);copy(c.epoch,epoch);copy(c.barrier,barrier);copy(c.nonce,nonce);c.total=9007199254740991ULL;return c;}
C decode(const SyncPlanner::Command& cmd,const std::string& s,SyncPlanner::Reply& out){return RpcCodec::decode(cmd,s.data(),s.size(),out);}
int main(){
 char out[1024]; SyncPlanner::Reply reply{};
 auto upload=command(K::Upload); assert(strcmp(RpcCodec::endpoint(K::Upload),"device_ingest")==0);
 assert(RpcCodec::encode(upload,secret,out,sizeof(out))==C::Ok);
 StaticJsonDocument<1024> json;assert(!deserializeJson(json,out));
 assert(strcmp(json["p_secret"],secret)==0 && strcmp(json["p_binding"],binding)==0 && strcmp(json["p_epoch"],epoch)==0);
 assert(json["p_total"].as<uint64_t>()==9007199254740991ULL); assert(json["p_barrier"].isNull());
 RpcCodec::wipe(out,sizeof(out)); for(char c:out)assert(c==0);
 memset(out,'X',sizeof(out)); assert(RpcCodec::encode(upload,"bad",out,sizeof(out))==C::InvalidSecret);for(char c:out)assert(c==0);
 assert(RpcCodec::encode(upload,secret,out,5)==C::BufferTooSmall);
 const std::string validUpload=std::string("{\"accepted_total\":9007199254740991,\"binding_id\":\"")+binding+"\",\"order_id\":\""+order+"\"}";
 assert(decode(upload,validUpload,reply)==C::Ok && reply.success && reply.acceptedTotal==9007199254740991ULL);
 assert(decode(upload,validUpload+"junk",reply)==C::InvalidResponse && !reply.success);
 assert(decode(upload,validUpload+"{}",reply)==C::InvalidResponse);
 assert(decode(upload,"{\"accepted_total\":-1,\"binding_id\":\""+std::string(binding)+"\"}",reply)==C::InvalidResponse);
 assert(decode(upload,"{\"accepted_total\":9007199254740992,\"binding_id\":\""+std::string(binding)+"\"}",reply)==C::InvalidResponse);
 assert(decode(upload,"{\"accepted_total\":1.0,\"binding_id\":\""+std::string(binding)+"\"}",reply)==C::InvalidResponse);
 assert(decode(upload,"{\"accepted_total\":1.5,\"binding_id\":\""+std::string(binding)+"\"}",reply)==C::InvalidResponse);
 assert(decode(upload,"{\"accepted_total\":1,\"binding_id\":\"00000000-0000-4000-8000-000000000001\"}",reply)==C::InvalidResponse);
 auto invalid=upload;memset(invalid.binding,'a',37);assert(RpcCodec::encode(invalid,secret,out,sizeof(out))==C::InvalidCommand);assert(decode(invalid,validUpload,reply)==C::InvalidResponse);
 auto poll=command(K::Poll);
 std::string pollJson=std::string("{\"desired_order_id\":\"")+order+"\",\"desired_binding_id\":\""+binding+"\",\"desired_binding_revision\":2,\"barrier_id\":\""+barrier+"\",\"barrier_order_id\":\""+order+"\",\"barrier_state\":\"requested\"}";
 assert(decode(poll,pollJson,reply)==C::Ok && reply.desiredRevision==2 && reply.barrierState==B::Requested && strcmp(reply.barrier,barrier)==0);
 assert(decode(poll,"{\"desired_order_id\":null,\"desired_binding_id\":null,\"desired_binding_revision\":null,\"barrier_id\":null,\"barrier_order_id\":null,\"barrier_state\":null}",reply)==C::Ok && reply.desiredRevision==0);
 assert(decode(poll,"{\"desired_order_id\":null,\"desired_binding_id\":\"bad\",\"desired_binding_revision\":1}",reply)==C::InvalidResponse);
 auto freeze=command(K::Freeze); auto renew=command(K::Renew);auto status=command(K::BarrierStatus);auto abort=command(K::Abort);
 assert(RpcCodec::encode(freeze,secret,out,sizeof(out))==C::Ok);json.clear();assert(!deserializeJson(json,out));assert(strcmp(json["p_barrier"],barrier)==0);
 assert(RpcCodec::encode(renew,secret,out,sizeof(out))==C::Ok);json.clear();assert(!deserializeJson(json,out));assert(strcmp(json["p_nonce"],nonce)==0);
 std::string ready=std::string("{\"state\":\"ready\",\"lease_nonce\":\"")+nonce+"\"}";
 assert(decode(freeze,ready,reply)==C::Ok && reply.barrierState==B::Ready && strcmp(reply.nonce,nonce)==0);
 assert(decode(renew,"{\"state\":\"ready\",\"lease_nonce\":null}",reply)==C::InvalidResponse);
 assert(decode(abort,ready,reply)==C::InvalidResponse);
 assert(decode(abort,"{\"state\":\"cancelled\"}",reply)==C::Ok && reply.barrierState==B::Cancelled);
 assert(decode(status,std::string("{\"id\":\"")+barrier+"\",\"state\":\"expired\"}",reply)==C::Ok && reply.barrierState==B::Expired);
 assert(decode(status,"{\"id\":\"00000000-0000-4000-8000-000000000001\",\"state\":\"completed\"}",reply)==C::InvalidResponse);
 assert(decode(freeze,"{\"state\":\"completed\"}",reply)==C::InvalidResponse);
 assert(decode(freeze,std::string(2049,' '),reply)==C::InvalidResponse);
 std::cout<<"RPC codec tests passed\n";
}
