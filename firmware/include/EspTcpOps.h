#pragma once
#include "NonblockingTcp.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

// Numeric IPv4 only; no DNS is performed here. Network task only. The caller
// must build authenticated TLS with original hostname verification on top.
class EspTcpOps final : public TcpOps {
 public:
  int openNonblocking() override {
    const int fd=::lwip_socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd<0)return -1;
    const int flags=::lwip_fcntl(fd,F_GETFL,0);
    if(flags<0 || ::lwip_fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){::lwip_close(fd);return -1;}
    return fd;
  }
  Connect connect(int fd,const uint8_t ip[4],uint16_t port) override {
    sockaddr_in dest{};dest.sin_family=AF_INET;
    const uint8_t portBytes[2]={static_cast<uint8_t>(port>>8),static_cast<uint8_t>(port)};
    memcpy(&dest.sin_port,portBytes,2);
    // Copy network-order bytes rather than depending on host byte order.
    memcpy(&dest.sin_addr.s_addr,ip,4);
    if(::lwip_connect(fd,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==0)return Connect::Ready;
    return pending(errno)?Connect::Pending:Connect::Error;
  }
  Connect connectionStatus(int fd) override {
    if(fd<0 || fd>=FD_SETSIZE)return Connect::Error;
    fd_set writable,errors;FD_ZERO(&writable);FD_ZERO(&errors);FD_SET(fd,&writable);FD_SET(fd,&errors);
    timeval zero{};
    const int ready=::lwip_select(fd+1,nullptr,&writable,&errors,&zero);
    if(ready<0)return errno==EINTR?Connect::Pending:Connect::Error;
    if(ready==0)return Connect::Pending;
    int error=0;socklen_t size=sizeof(error);
    if(::lwip_getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || size!=sizeof(error))return Connect::Error;
    if(error==0 && FD_ISSET(fd,&writable) && !FD_ISSET(fd,&errors))return Connect::Ready;
    return pending(error)?Connect::Pending:Connect::Error;
  }
  int send(int fd,const uint8_t* data,size_t n) override {
    const int result=static_cast<int>(::lwip_send(fd,data,n,0));return result<0?(wouldBlock(errno)?-2:-1):result;
  }
  int receive(int fd,uint8_t* data,size_t n) override {
    const int result=static_cast<int>(::lwip_recv(fd,data,n,0));return result<0?(wouldBlock(errno)?-2:-1):result;
  }
  void close(int fd) override {::lwip_close(fd);}
 private:
  static bool wouldBlock(int error){return error==EAGAIN || error==EWOULDBLOCK || error==EINTR;}
  static bool pending(int error){return wouldBlock(error) || error==EINPROGRESS || error==EALREADY;}
};
