#include "../firmware/include/TimeProof.h"
#include <cassert>
#include <iostream>
#include <string>
struct RejectEntropy : SecureEntropy {bool fill(uint8_t*,size_t) override {return false;}};
struct Entropy : SecureEntropy {
 bool fill(uint8_t* b,size_t n) override {for(size_t i=0;i<n;++i)b[i]=static_cast<uint8_t>(i);return true;}
};
int main(){
 std::string secret,id,mode;if(!(std::cin>>secret>>id>>mode))return 1;
 Entropy entropy;TimeProof proof(entropy);
 RejectEntropy rejectedEntropy;TimeProof unavailable(rejectedEntropy);
 assert(!unavailable.begin(id.c_str(),secret.c_str(),0) && !unavailable.nonce());
 assert(!proof.begin("invalid",secret.c_str(),0) && !proof.nonce());
 assert(!proof.begin(id.c_str(),"bad",0) && !proof.nonce());
 const uint32_t started=mode=="wrap"?UINT32_MAX-20:100;
 assert(proof.begin(id.c_str(),secret.c_str(),started));std::cout<<proof.nonce()<<std::endl;
 std::string seconds,tag;if(!(std::cin>>seconds>>tag))return 2;
 uint64_t value=42;const uint32_t now=mode=="late"?started+5000:mode=="wrap"?15:started+2;
 bool ok=proof.verify(seconds.c_str(),tag.c_str(),now,value);
 assert(!proof.nonce());uint64_t unchanged=42;assert(!proof.verify(seconds.c_str(),tag.c_str(),now,unchanged));assert(unchanged==42);
 std::cout<<(ok?"accepted":"rejected")<<" "<<value<<std::endl;
}
