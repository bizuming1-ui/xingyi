#include "../firmware/include/RuntimeConfig.h"
#include <cassert>
#include <cstdio>
int main(){
 RuntimeConfig c{"network","password","project123.supabase.co","sb_publishable_test","10000000-0000-4000-8000-000000000001",
 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","-----BEGIN CERTIFICATE-----\nfixture",60000,9999};
 assert(c.valid());
 auto bad=c;bad.ssid="";assert(!bad.valid());bad=c;bad.password="short";assert(!bad.valid());
 bad=c;bad.host="project123.supabase.co.evil";assert(!bad.valid());bad=c;bad.host="evil\r\n.supabase.co";assert(!bad.valid());
 bad=c;bad.key="sb_secret_abc";assert(!bad.valid());bad=c;bad.key="sb_publishable_x\r\n";assert(!bad.valid());
 bad=c;bad.deviceId="10000000-0000-4000-8000-00000000000A";assert(!bad.valid());
 bad=c;bad.secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeF";assert(!bad.valid());
 bad=c;bad.ca="";assert(!bad.valid());bad=c;bad.timeLifetimeMs=0;assert(!bad.valid());bad=c;bad.clockDriftPpm=10000;assert(!bad.valid());
 puts("runtime configuration validation tests passed");
}
