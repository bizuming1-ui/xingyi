#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Incremental, bounded HTTP/1.1 response framing only. Not a TLS client and
// not an HTTP transport. The caller must authenticate the server, use a fixed
// RPC host, impose an overall deadline and never follow redirects. This strict
// parser accepts Content-Length or chunked framing, but not close-delimited
// bodies, chunk extensions or nonempty trailers. Unsupported framing fails
// closed; callers must not pass partial output on to RpcCodec.
class HttpResponseDecoder {
 public:
  enum class Result { Incomplete, Complete, Invalid, TooLarge };
  static constexpr size_t MAX_HEADER_BYTES=2048;
  HttpResponseDecoder(char* output,size_t capacity) : output_(output),capacity_(capacity) {
    if (!output_ && capacity_) state_=State::Invalid;
  }
  ~HttpResponseDecoder() {
    volatile char* p=headers_;
    for(size_t i=0;i<sizeof(headers_);++i)p[i]=0;
  }
  Result feed(const char* data,size_t size) {
    if (!data && size) { state_=State::Invalid; return result(); }
    for (size_t i=0;i<size;++i) {
      if (state_==State::Done) { state_=State::Invalid; break; }
      if (state_==State::Invalid || state_==State::TooLarge) break;
      const unsigned char c=static_cast<unsigned char>(data[i]);
      switch(state_) {
        case State::Headers:
          if (headerSize_>=MAX_HEADER_BYTES) { state_=State::TooLarge; break; }
          headers_[headerSize_++]=static_cast<char>(c);
          if (headerSize_>=4 && memcmp(headers_+headerSize_-4,"\r\n\r\n",4)==0) parseHeaders();
          break;
        case State::Body:
          output_[length_++]=static_cast<char>(c);
          if (length_==expected_) state_=State::Done;
          break;
        case State::ChunkSize:
          if (c=='\r') { if (!digits_) state_=State::Invalid; else state_=State::ChunkSizeLF; break; }
          { const int n=hex(c);
            if (n<0 || digits_>=16) { state_=State::Invalid; break; }
            if (chunkSize_>(capacity_-length_)/16 ||
                (chunkSize_==(capacity_-length_)/16 && static_cast<size_t>(n)>(capacity_-length_)%16)) {
              state_=State::TooLarge; break;
            }
            chunkSize_=chunkSize_*16+static_cast<size_t>(n); ++digits_;
          }
          break;
        case State::ChunkSizeLF:
          if (c!='\n') state_=State::Invalid;
          else if (chunkSize_==0) state_=State::FinalCR;
          else { remaining_=chunkSize_; state_=State::ChunkBody; }
          break;
        case State::ChunkBody:
          output_[length_++]=static_cast<char>(c);
          if (--remaining_==0) state_=State::ChunkCR;
          break;
        case State::ChunkCR: state_=c=='\r'?State::ChunkLF:State::Invalid; break;
        case State::ChunkLF:
          if (c!='\n') state_=State::Invalid;
          else { chunkSize_=0; digits_=0; state_=State::ChunkSize; }
          break;
        case State::FinalCR: state_=c=='\r'?State::FinalLF:State::Invalid; break;
        case State::FinalLF: state_=c=='\n'?State::Done:State::Invalid; break;
        default: state_=State::Invalid; break;
      }
    }
    return result();
  }
  // Call on EOF or deadline. Incomplete messages are never successful.
  Result finish() { if (state_!=State::Done && state_!=State::TooLarge) state_=State::Invalid; return result(); }
  Result result() const {
    return state_==State::Done?Result::Complete:state_==State::Invalid?Result::Invalid:
      state_==State::TooLarge?Result::TooLarge:Result::Incomplete;
  }
  int statusCode() const { return status_; }
  size_t length() const { return length_; }
 private:
  enum class State { Headers, Body, ChunkSize, ChunkSizeLF, ChunkBody, ChunkCR, ChunkLF, FinalCR, FinalLF, Done, Invalid, TooLarge };
  State state_=State::Headers;
  char* output_; size_t capacity_;
  char headers_[MAX_HEADER_BYTES+1]{}; size_t headerSize_=0;
  size_t length_=0, expected_=0, chunkSize_=0, remaining_=0; unsigned digits_=0;
  int status_=0;
  static int hex(unsigned char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
  }
  static char lower(char c) { return c>='A'&&c<='Z'?static_cast<char>(c+('a'-'A')):c; }
  static bool equalNoCase(const char* a,const char* b) {
    while(*a && *b && lower(*a)==lower(*b)){++a;++b;} return *a==0 && *b==0;
  }
  static bool tokenChar(unsigned char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
      c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c=='\''||c=='*'||c=='+'||c=='-'||c=='.'||c=='^'||c=='_'||c=='`'||c=='|'||c=='~';
  }
  void parseHeaders() {
    headers_[headerSize_]='\0';
    // Reject embedded NUL, bare CR/LF and other controls in the header block.
    for (size_t i=0;i<headerSize_;++i) {
      const unsigned char c=static_cast<unsigned char>(headers_[i]);
      if ((c<32 && c!='\r' && c!='\n' && c!='\t') || c==127 ||
          (c=='\r' && (i+1>=headerSize_ || headers_[i+1]!='\n')) ||
          (c=='\n' && (i==0 || headers_[i-1]!='\r'))) { state_=State::Invalid; return; }
    }
    char* lineEnd=strstr(headers_,"\r\n");
    if (!lineEnd || lineEnd-headers_<12 || memcmp(headers_,"HTTP/1.1 ",9)!=0 ||
        headers_[9]<'1'||headers_[9]>'5'||headers_[10]<'0'||headers_[10]>'9'||headers_[11]<'0'||headers_[11]>'9'||
        (lineEnd-headers_>12 && headers_[12]!=' ')) { state_=State::Invalid; return; }
    status_=(headers_[9]-'0')*100+(headers_[10]-'0')*10+(headers_[11]-'0');
    // 1xx is interim and requires another status line; do not misinterpret it.
    if (status_<200) { state_=State::Invalid; return; }
    bool hasLength=false,hasTransfer=false; size_t contentLength=0;
    char* line=lineEnd+2;
    while (*line && !(line[0]=='\r' && line[1]=='\n')) {
      char* end=strstr(line,"\r\n");
      if(!end){state_=State::Invalid;return;} *end='\0';
      char* colon=strchr(line,':');
      if(!colon || colon==line){state_=State::Invalid;return;}
      for(char* p=line;p<colon;++p)if(!tokenChar(static_cast<unsigned char>(*p))){state_=State::Invalid;return;}
      *colon='\0'; char* value=colon+1; while(*value==' '||*value=='\t')++value;
      char* tail=end;while(tail>value&&(tail[-1]==' '||tail[-1]=='\t'))--tail;*tail='\0';
      if(equalNoCase(line,"content-length")) {
        if(hasLength || !*value){state_=State::Invalid;return;} hasLength=true;
        for(const char* p=value;*p;++p) {
          if(*p<'0'||*p>'9'){state_=State::Invalid;return;}
          const size_t digit=static_cast<size_t>(*p-'0');
          if(contentLength>capacity_/10 || (contentLength==capacity_/10 && digit>capacity_%10)) {state_=State::TooLarge;return;}
          contentLength=contentLength*10+digit;
        }
      } else if(equalNoCase(line,"transfer-encoding")) {
        if(hasTransfer || !equalNoCase(value,"chunked")){state_=State::Invalid;return;}hasTransfer=true;
      } else if(equalNoCase(line,"content-encoding") && !equalNoCase(value,"identity")) {
        state_=State::Invalid;return;
      }
      line=end+2;
    }
    if (hasLength==hasTransfer) { state_=State::Invalid; return; }
    if (hasTransfer) state_=State::ChunkSize;
    else { expected_=contentLength;state_=expected_?State::Body:State::Done; }
  }
};
