#pragma once
#include "MbedTlsChannel.h" // ConnectionClock and NetworkYield
#include "TimeProofCodec.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>

// ONE purpose only: obtain an authenticated time proof before the clock is
// trusted. The only application request this class can send is the fixed
// device_time_proof endpoint, with public device ID, fresh nonce and publishable
// key. It never writes the device secret or its hash to the network. TLS still
// requires the configured CA, signature chain and original host; *only* date
// errors are ignored because time is not yet known. The HMAC proof must verify
// before any time is returned. Never use this session for normal RPCs.
// Network-task-only; caller serializes Wi-Fi lifecycle and owns all objects.
// CA, DNS backend/resolver and credentials must remain valid for the whole call
// and DNS backend/resolver must outlive late callbacks. Crypto and scheduler
// calls cannot be forcibly preempted by the cooperative deadline.
class TimeBootstrapExchange {
 public:
  enum class Result { Ok, Timeout, Unavailable, InvalidProof };
  TimeBootstrapExchange(ConnectionClock& clock,NetworkYield& scheduler,NonblockingTcp& tcp,
                        ResolvedTcpConnect& resolved,TimeProof& proof)
    :clock_(clock),scheduler_(scheduler),tcp_(tcp),resolved_(resolved),proof_(proof){}
  TimeBootstrapExchange(const TimeBootstrapExchange&)=delete;
  TimeBootstrapExchange& operator=(const TimeBootstrapExchange&)=delete;
  Result fetch(const char* host,const char* key,const char* ca,const char* deviceId,const char* secret,
               uint32_t timeoutMs,uint64_t& seconds) {
    if(!timeoutMs || timeoutMs>TimeProof::MAX_ROUND_TRIP_MS || !validKey(key) || !ca ||
       !resolved_.usesTransport(tcp_) || resolved_.state()!=ResolvedTcpConnect::State::Idle ||
       tcp_.state()!=NonblockingTcp::State::Idle)return Result::Unavailable;
    const uint32_t start=clock_.nowMillis();
    if(!proof_.begin(deviceId,secret,start))return Result::Unavailable;
    if(!resolved_.begin(host,443,start,timeoutMs)){proof_.clear();return Result::Unavailable;}
    struct Close {ResolvedTcpConnect& r;TimeProof& p;~Close(){r.close();p.clear();}} cleanup{resolved_,proof_};
    if(expired(start,timeoutMs))return Result::Timeout;
    for(;;) {
      const auto state=resolved_.poll(clock_.nowMillis());
      if(expired(start,timeoutMs))return Result::Timeout;
      if(state==ResolvedTcpConnect::State::Connected)break;
      if(state==ResolvedTcpConnect::State::Timeout)return Result::Timeout;
      if(state!=ResolvedTcpConnect::State::Resolving && state!=ResolvedTcpConnect::State::Connecting)return Result::Unavailable;
      scheduler_.yield();
    }
    Context ctx;
    if(!setup(ctx,resolved_.hostname(),ca))return expired(start,timeoutMs)?Result::Timeout:Result::Unavailable;
    if(expired(start,timeoutMs))return Result::Timeout;
    for(;;) {
      const int rc=mbedtls_ssl_handshake(&ctx.ssl);
      if(expired(start,timeoutMs))return Result::Timeout;
      if(rc==0){if(!ctx.sawLeaf || mbedtls_ssl_get_verify_result(&ctx.ssl)!=0)return Result::Unavailable;break;}
      if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE)return Result::Unavailable;
      scheduler_.yield();
    }
    char body[160]{};if(!TimeProofCodec::request(proof_,body,sizeof(body)))return Result::Unavailable;
    char headers[512]{};
    const int n=snprintf(headers,sizeof(headers),
      "POST /rest/v1/rpc/device_time_proof HTTP/1.1\r\nHost: %s\r\napikey: %s\r\nContent-Type: application/json\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n",
      resolved_.hostname(),key,static_cast<unsigned long>(strlen(body)));
    if(n<0 || static_cast<size_t>(n)>=sizeof(headers))return Result::Unavailable;
    Result sent=writeAll(ctx,headers,static_cast<size_t>(n),start,timeoutMs);
    if(sent!=Result::Ok)return sent;
    sent=writeAll(ctx,body,strlen(body),start,timeoutMs);
    if(sent!=Result::Ok)return sent;
    char reply[256]{},block[256]{};HttpResponseDecoder parser(reply,sizeof(reply));
    for(;;) {
      if(expired(start,timeoutMs))return Result::Timeout;
      const int rc=mbedtls_ssl_read(&ctx.ssl,reinterpret_cast<unsigned char*>(block),sizeof(block));
      if(expired(start,timeoutMs))return Result::Timeout;
      if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE){scheduler_.yield();continue;}
      if(rc<=0 || static_cast<size_t>(rc)>sizeof(block))return Result::Unavailable;
      const auto parsed=parser.feed(block,static_cast<size_t>(rc));
      if(parsed==HttpResponseDecoder::Result::Complete){
        if(parser.statusCode()!=200)return Result::Unavailable;
        uint64_t verified=0;
        const bool ok=TimeProofCodec::response(proof_,reply,parser.length(),clock_.nowMillis(),verified);
        if(expired(start,timeoutMs))return Result::Timeout;
        if(!ok)return Result::InvalidProof;
        seconds=verified;return Result::Ok;
      }
      if(parsed!=HttpResponseDecoder::Result::Incomplete)return Result::Unavailable;
    }
  }
 private:
  struct Context {
    mbedtls_ssl_context ssl;mbedtls_ssl_config config;mbedtls_x509_crt ca;
    mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context rng;bool sawLeaf=false;
    Context(){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&ca);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);}
    ~Context(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&ca);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);}
  };
  ConnectionClock& clock_;NetworkYield& scheduler_;NonblockingTcp& tcp_;ResolvedTcpConnect& resolved_;TimeProof& proof_;
  bool expired(uint32_t start,uint32_t limit){return static_cast<uint32_t>(clock_.nowMillis()-start)>=limit;}
  static int verifyTimeOnly(void* p,mbedtls_x509_crt* cert,int depth,uint32_t* flags) {
    if(!p || !cert || !flags || depth<0)return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    if(depth==0)static_cast<Context*>(p)->sawLeaf=true;
    // Do not clear hostname, trust-chain, signature, key, or other errors.
    *flags &= ~(static_cast<uint32_t>(MBEDTLS_X509_BADCERT_EXPIRED)|static_cast<uint32_t>(MBEDTLS_X509_BADCERT_FUTURE));
    return 0;
  }
  static int sendCallback(void* p,const unsigned char* data,size_t n){
    auto r=static_cast<NonblockingTcp*>(p)->write(data,n>16384?16384:n);
    if(r.status==NonblockingTcp::IoStatus::Progress)return static_cast<int>(r.count);
    if(r.status==NonblockingTcp::IoStatus::WouldBlock)return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  static int recvCallback(void* p,unsigned char* data,size_t n){
    auto r=static_cast<NonblockingTcp*>(p)->read(data,n>16384?16384:n);
    if(r.status==NonblockingTcp::IoStatus::Progress)return static_cast<int>(r.count);
    if(r.status==NonblockingTcp::IoStatus::WouldBlock)return MBEDTLS_ERR_SSL_WANT_READ;
    if(r.status==NonblockingTcp::IoStatus::Closed)return 0;
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  bool setup(Context& c,const char* host,const char* ca) {
    size_t n=0;while(n<16384 && ca[n])++n;
    if(!n || n==16384 || mbedtls_x509_crt_parse(&c.ca,reinterpret_cast<const unsigned char*>(ca),n+1)!=0)return false;
    for(const mbedtls_x509_crt* cert=&c.ca;cert;cert=cert->next)if(!cert->ca_istrue)return false;
    const unsigned char personal[]="xingyi-time-bootstrap";
    if(mbedtls_ctr_drbg_seed(&c.rng,mbedtls_entropy_func,&c.entropy,personal,sizeof(personal)-1)!=0 ||
       mbedtls_ssl_config_defaults(&c.config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return false;
    mbedtls_ssl_conf_authmode(&c.config,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_verify(&c.config,&verifyTimeOnly,&c);
    mbedtls_ssl_conf_min_version(&c.config,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_ca_chain(&c.config,&c.ca,nullptr);mbedtls_ssl_conf_rng(&c.config,mbedtls_ctr_drbg_random,&c.rng);
    if(mbedtls_ssl_setup(&c.ssl,&c.config)!=0 || mbedtls_ssl_set_hostname(&c.ssl,host)!=0)return false;
    mbedtls_ssl_set_bio(&c.ssl,&tcp_,&sendCallback,&recvCallback,nullptr);return true;
  }
  Result writeAll(Context& c,const char* data,size_t n,uint32_t start,uint32_t timeoutMs){
    size_t sent=0;
    while(sent<n){
      if(expired(start,timeoutMs))return Result::Timeout;
      const int rc=mbedtls_ssl_write(&c.ssl,reinterpret_cast<const unsigned char*>(data+sent),n-sent);
      if(expired(start,timeoutMs))return Result::Timeout;
      if(rc==MBEDTLS_ERR_SSL_WANT_READ || rc==MBEDTLS_ERR_SSL_WANT_WRITE){scheduler_.yield();continue;}
      if(rc<=0 || static_cast<size_t>(rc)>n-sent)return Result::Unavailable;
      sent+=static_cast<size_t>(rc);
    }
    return Result::Ok;
  }
  static bool validKey(const char* s){
    if(!s || strncmp(s,"sb_publishable_",15)!=0)return false;
    size_t n=0;while(n<128 && s[n]){const char c=s[n];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;++n;}
    return n>15 && n<128;
  }
};
