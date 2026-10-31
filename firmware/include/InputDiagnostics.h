#pragma once
#include <stdint.h>

// Counting-task-only, diagnostic observation of sampled input and backlog.
// Gaps cannot reveal unseen edges or prove that physical presses were counted.
class InputDiagnostics {
 public:
  void sample(uint32_t now,uint32_t accepted,uint32_t rejected,uint64_t pending) {
    if(seen_) {const uint32_t gap=now-last_;if(gap>maxGap_)maxGap_=gap;}
    else seen_=true;
    last_=now;if(accepted>maxBatch_)maxBatch_=accepted;
    if(rejected>0)rejected_+=rejected;
    if(pending>peakPending_)peakPending_=pending;
  }
  uint32_t maxGap() const{return maxGap_;}
  uint32_t maxBatch() const{return maxBatch_;}
  uint32_t rejected() const{return rejected_;}
  uint64_t peakPending() const{return peakPending_;}
 private:
  bool seen_=false;uint32_t last_=0,maxGap_=0,maxBatch_=0,rejected_=0;uint64_t peakPending_=0;
};
