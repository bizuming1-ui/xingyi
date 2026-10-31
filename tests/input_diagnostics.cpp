#include "../firmware/include/InputDiagnostics.h"
#include <cassert>
int main(){
 InputDiagnostics d;d.sample(UINT32_MAX-10,1,0,1);d.sample(9,3,2,50);
 assert(d.maxGap()==20 && d.maxBatch()==3 && d.rejected()==2 && d.peakPending()==50);
 d.sample(19,1,1,3);assert(d.maxGap()==20 && d.maxBatch()==3 && d.rejected()==3 && d.peakPending()==50);
}
