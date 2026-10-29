#include "../firmware/include/HttpResponseDecoder.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using D=HttpResponseDecoder; using R=D::Result;
R decode(const std::string& input, size_t cap=64, bool bytewise=false) {
  char out[64]{}; assert(cap<=sizeof(out));D parser(out,cap);
  if(bytewise) for(char c:input) parser.feed(&c,1);
  else parser.feed(input.data(),input.size());
  return parser.finish();
}
int main() {
  const std::string fixed="HTTP/1.1 200 OK\r\nContent-Length: 7\r\nContent-Type: application/json\r\n\r\n{\"x\":1}";
  char out[16]{};D p(out,sizeof(out));
  for(size_t i=0;i<fixed.size();++i) {auto r=p.feed(&fixed[i],1);assert(r==(i+1==fixed.size()?R::Complete:R::Incomplete));}
  assert(p.finish()==R::Complete && p.statusCode()==200 && p.length()==7 && memcmp(out,"{\"x\":1}",7)==0);
  assert(decode(fixed,64,true)==R::Complete);
  const std::string chunks="HTTP/1.1 200 OK\r\ntRansfer-Encoding: Chunked\r\n\r\n3\r\n{\"x\r\n4\r\n\":1}\r\n0\r\n\r\n";
  char chunkOut[7]{};D q(chunkOut,sizeof(chunkOut));assert(q.feed(chunks.data(),chunks.size())==R::Complete);
  assert(q.length()==7 && memcmp(chunkOut,"{\"x\":1}",7)==0 && decode(chunks,7,true)==R::Complete);
  assert(decode(fixed,6)==R::TooLarge && decode(chunks,6,true)==R::TooLarge);
  assert(decode(fixed.substr(0,fixed.size()-1))==R::Invalid);
  assert(decode(fixed+"X")==R::Invalid);
  assert(decode("HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n")==R::Complete); // caller rejects 3xx
  assert(decode("HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Length: 1\r\ncontent-length: 1\r\n\r\na")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\na")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Length: 999999999999999999999999999999\r\n\r\n")==R::TooLarge);
  assert(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFFF\r\n")==R::TooLarge);
  assert(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;ext=1\r\na\r\n0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX: y\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\nContent-Length: 0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nBad Header: 1\r\nContent-Length: 0\r\n\r\n")==R::Invalid);
  assert(decode("HTTP/1.1 200 OK\r\nX: "+std::string(2100,'a')+"\r\nContent-Length: 0\r\n\r\n")==R::TooLarge);
  // Random fragmentation and malformed bytes must produce the same final
  // framing result; neither path may accept a truncated message.
  uint32_t seed=0xdeadbeef;
  auto random=[&seed]() { seed=seed*1664525u+1013904223u; return seed; };
  for (unsigned i=0;i<3000;++i) {
    std::string input=(i%2)?fixed:chunks;
    const size_t edits=1+random()%5;
    for(size_t j=0;j<edits;++j) { size_t pos=random()%input.size(); input[pos]=static_cast<char>(random()>>24); }
    if(i%5==0) input.resize(random()%input.size());
    assert(decode(input,64,false)==decode(input,64,true));
  }
  char empty[1]{};D invalid(empty,sizeof(empty));assert(invalid.feed(nullptr,1)==R::Invalid);
  std::cout<<"HTTP response framing tests passed\n";
}
