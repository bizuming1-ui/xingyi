#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
class Preferences {
 public:
  bool begin(const char*, bool) {return true;}
  void end() {}
  bool isKey(const char* key) {return values.count(key)>0;}
  size_t getBytesLength(const char* key) {return values[key].size();}
  size_t getBytes(const char* key, void* dest, size_t len) {
    auto& value=values[key]; if(value.size()!=len)return 0;
    std::memcpy(dest,value.data(),len);return len;
  }
  size_t putBytes(const char* key, const void* data, size_t len) {
    const auto* p=static_cast<const uint8_t*>(data);
    values[key]=std::vector<uint8_t>(p,p+len);return len;
  }
 private:
  std::map<std::string,std::vector<uint8_t>> values;
};
