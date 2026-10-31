#include "../firmware/include/NonblockingTcp.h"
#include <cassert>
#include <iostream>
struct Ops : TcpOps {
 int opened=0,closed=0;Connect connectResult=Connect::Pending,statusResult=Connect::Pending;int sendResult=-2,receiveResult=-2;
 int openNonblocking() override {++opened;return 7;}
 Connect connect(int,const uint8_t*,uint16_t) override {return connectResult;}
 Connect connectionStatus(int) override {return statusResult;}
 int send(int,const uint8_t*,size_t) override {return sendResult;}
 int receive(int,uint8_t*,size_t) override {return receiveResult;}
 void close(int) override {++closed;}
};
int main(){
 Ops ops;NonblockingTcp c(ops);const uint8_t ip[]={1,2,3,4};uint8_t buf[8]{};
 assert(!c.start(nullptr,443,0,1000) && !c.start(ip,0,0,1000) && ops.opened==0);
 assert(c.start(ip,443,UINT32_MAX-50,100));assert(c.poll(48)==NonblockingTcp::State::Connecting);
 assert(c.poll(49)==NonblockingTcp::State::Timeout && ops.closed==1);
 assert(c.poll(100)==NonblockingTcp::State::Timeout);c.close();
 assert(c.start(ip,443,100,1000));ops.statusResult=TcpOps::Connect::Ready;
 assert(c.poll(101)==NonblockingTcp::State::Connected);
 assert(c.write(buf,sizeof(buf)).status==NonblockingTcp::IoStatus::WouldBlock);
 ops.sendResult=3;auto result=c.write(buf,sizeof(buf));assert(result.status==NonblockingTcp::IoStatus::Progress && result.count==3);
 ops.receiveResult=0;assert(c.read(buf,sizeof(buf)).status==NonblockingTcp::IoStatus::Closed && ops.closed==2);
 assert(c.read(buf,sizeof(buf)).status==NonblockingTcp::IoStatus::Error);c.close();
 ops.connectResult=TcpOps::Connect::Error;assert(!c.start(ip,443,0,1000) && ops.closed==3);
 c.close();ops.connectResult=TcpOps::Connect::Ready;assert(c.start(ip,443,0,1000));ops.sendResult=9;
 assert(c.write(buf,sizeof(buf)).status==NonblockingTcp::IoStatus::Error && ops.closed==4);
 std::cout<<"nonblocking TCP state tests passed\n";
}
