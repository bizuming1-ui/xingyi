#include "../firmware/include/EspWifiEntropy.h"
#include <cassert>
#include <cstring>
#include <iostream>
FakeWifi WiFi;unsigned randomCalls=0;
int main() {
  EspWifiEntropy entropy;uint8_t out[16]{};
  assert(!entropy.fill(nullptr,16) && !entropy.fill(out,0) && randomCalls==0);
  WiFi.first=0;assert(!entropy.fill(out,sizeof(out)) && randomCalls==0);
  WiFi.calls=0;WiFi.first=WL_CONNECTED;WiFi.second=0;
  assert(!entropy.fill(out,sizeof(out)) && randomCalls==1);
  for(auto b:out)assert(b==0);
  WiFi.calls=0;WiFi.second=WL_CONNECTED;
  assert(entropy.fill(out,sizeof(out)) && randomCalls==2 && out[0]==1 && out[15]==16);
  WiFi.calls=0;RandomEpochSource epochs(entropy);char uuid[37]{};
  assert(epochs.generate(uuid) && strcmp(uuid,"01020304-0506-4708-890a-0b0c0d0e0f10")==0);
  std::cout<<"ESP Wi-Fi entropy mock tests passed\n";
}
