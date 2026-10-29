#pragma once
#include "NonblockingTcp.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <time.h>
#include <string.h>

// TLS engine on an already-connected nonblocking TCP stream. Exclusively owned
// by the network task. This is NOT yet an HttpRpcExchange channel: DNS/TCP,
// trusted time, a provisioned CA, scheduling and an overall deadline are the
// caller's responsibility. No application bytes can be written before a
// successful verified handshake. Cryptographic calls may consume CPU time.
class MbedTlsSession {
 public:
  enum class State { Idle, Handshaking, Verified, Failed };
  enum class Status { Progress, WantIo, Closed, Error };
  struct Result { Status status;size_t count;Result(Status s,size_t n=0):status(s),count(n){} };
  explicit MbedTlsSession(NonblockingTcp& tcp):tcp_(tcp) {
    mbedtls_ssl_init(&ssl_);mbedtls_ssl_config_init(&config_);mbedtls_x509_crt_init(&ca_);
    mbedtls_entropy_init(&entropy_);mbedtls_ctr_drbg_init(&rng_);
  }
  ~MbedTlsSession(){reset();}
  MbedTlsSession(const MbedTlsSession&)=delete;
  MbedTlsSession& operator=(const MbedTlsSession&)=delete;
  State state() const {return state_;}
  bool usesTransport(const NonblockingTcp& tcp) const {return &tcp_==&tcp;}
  // timeTrusted must only be true after the platform has established a
  // trustworthy wall clock. A plausible clock alone does not prove this.
  bool begin(const char* originalHost,const char* trustedCaPem,bool timeTrusted) {
    if(state_!=State::Idle || tcp_.state()!=NonblockingTcp::State::Connected)return false;
#if !defined(MBEDTLS_HAVE_TIME_DATE)
    (void)originalHost;(void)trustedCaPem;(void)timeTrusted;return fail();
#else
    const time_t now=time(nullptr);
    if(!timeTrusted || now<1704067200 || !validHost(originalHost) || !trustedCaPem)return fail();
    size_t pemLen=0;while(pemLen<16384 && trustedCaPem[pemLen])++pemLen;
    if(!pemLen || pemLen==16384)return fail();
    // A nonzero parse return can mean only a subset of certificates parsed;
    // reject it rather than silently accepting an incomplete trust store.
    if(mbedtls_x509_crt_parse(&ca_,reinterpret_cast<const unsigned char*>(trustedCaPem),pemLen+1)!=0)return fail();
    for(const mbedtls_x509_crt* cert=&ca_;cert;cert=cert->next)if(!cert->ca_istrue)return fail();
    const unsigned char personalization[]="xingyi-device-tls";
    if(mbedtls_ctr_drbg_seed(&rng_,mbedtls_entropy_func,&entropy_,personalization,sizeof(personalization)-1)!=0 ||
       mbedtls_ssl_config_defaults(&config_,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return fail();
    mbedtls_ssl_conf_authmode(&config_,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_version(&config_,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_ca_chain(&config_,&ca_,nullptr);
    mbedtls_ssl_conf_rng(&config_,mbedtls_ctr_drbg_random,&rng_);
    if(mbedtls_ssl_setup(&ssl_,&config_)!=0 || mbedtls_ssl_set_hostname(&ssl_,originalHost)!=0)return fail();
    mbedtls_ssl_set_bio(&ssl_,this,&sendCallback,&recvCallback,nullptr);
    state_=State::Handshaking;return true;
#endif
  }
  State handshake() {
    if(state_!=State::Handshaking)return state_;
    const int rc=mbedtls_ssl_handshake(&ssl_);
    if(rc==0) {
      if(mbedtls_ssl_get_verify_result(&ssl_)!=0){fail();return state_;}
      state_=State::Verified;
    } else if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE)fail();
    return state_;
  }
  // After WantIo, retry the same buffer and length until this write completes.
  Result write(const uint8_t* data,size_t length) {
    if(state_!=State::Verified || !data || !length || length>16384)return {Status::Error};
    return interpret(mbedtls_ssl_write(&ssl_,data,length));
  }
  Result read(uint8_t* data,size_t capacity) {
    if(state_!=State::Verified || !data || !capacity || capacity>16384)return {Status::Error};
    return interpret(mbedtls_ssl_read(&ssl_,data,capacity));
  }
  // Does not send close_notify: shutdown must not block. A new session
  // requires a fresh TCP connection. No TLS context is reused across peers.
  void reset() {
    mbedtls_ssl_free(&ssl_);mbedtls_ssl_config_free(&config_);mbedtls_x509_crt_free(&ca_);
    mbedtls_ctr_drbg_free(&rng_);mbedtls_entropy_free(&entropy_);
    mbedtls_ssl_init(&ssl_);mbedtls_ssl_config_init(&config_);mbedtls_x509_crt_init(&ca_);
    mbedtls_entropy_init(&entropy_);mbedtls_ctr_drbg_init(&rng_);
    tcp_.close();state_=State::Idle;
  }
 private:
  NonblockingTcp& tcp_;State state_=State::Idle;
  mbedtls_ssl_context ssl_;mbedtls_ssl_config config_;mbedtls_x509_crt ca_;
  mbedtls_entropy_context entropy_;mbedtls_ctr_drbg_context rng_;
  bool fail(){tcp_.close();state_=State::Failed;return false;}
  Result interpret(int rc) {
    if(rc>0)return {Status::Progress,static_cast<size_t>(rc)};
    if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE)return {Status::WantIo};
    if(rc==0 || rc==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY){tcp_.close();state_=State::Failed;return {Status::Closed};}
    fail();return {Status::Error};
  }
  static int sendCallback(void* context,const unsigned char* data,size_t len) {
    auto r=static_cast<MbedTlsSession*>(context)->tcp_.write(data,len>16384?16384:len);
    if(r.status==NonblockingTcp::IoStatus::Progress)return static_cast<int>(r.count);
    if(r.status==NonblockingTcp::IoStatus::WouldBlock)return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  static int recvCallback(void* context,unsigned char* data,size_t len) {
    auto r=static_cast<MbedTlsSession*>(context)->tcp_.read(data,len>16384?16384:len);
    if(r.status==NonblockingTcp::IoStatus::Progress)return static_cast<int>(r.count);
    if(r.status==NonblockingTcp::IoStatus::WouldBlock)return MBEDTLS_ERR_SSL_WANT_READ;
    if(r.status==NonblockingTcp::IoStatus::Closed)return 0;
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  static bool validHost(const char* host) {
    if(!host)return false;
    size_t n=0;while(n<80 && host[n])++n;
    const char suffix[]=".supabase.co";const size_t tail=sizeof(suffix)-1;
    if(n<=tail || n>=80 || n-tail>63 || memcmp(host+n-tail,suffix,tail)!=0)return false;
    for(size_t i=0;i<n-tail;++i)if(!((host[i]>='a'&&host[i]<='z')||(host[i]>='0'&&host[i]<='9')))return false;
    return true;
  }
};
