#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Storage is a pair of independent atomic blobs (e.g. separate ESP32 NVS keys).
// read: 1 = exact blob read, 0 = key absent, -1 = corrupt/IO error.
// write returns true only if the new blob was committed durably.
class CounterStorage {
 public:
  virtual ~CounterStorage() = default;
  virtual int read(unsigned slot, void* data, size_t size) = 0;
  virtual bool write(unsigned slot, const void* data, size_t size) = 0;
};

// Per-binding monotonically increasing totals are meant for idempotent server
// synchronization. This is local storage logic only, not the server protocol.
class DurableCounter {
 public:
  static constexpr uint64_t MAX_COUNT = 9007199254740991ULL;
  enum class Result { Ok, NotReady, Invalid, Pending, Frozen, Full, StorageError };
  // Only pass a terminal outcome obtained from an authenticated server response.
  enum class TerminalOutcome { Completed, Cancelled, Expired };
  struct Snapshot {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint64_t total;
    uint64_t acknowledged;
    uint64_t bindingRevision;
    uint64_t frozenTotal;
    char orderId[37];
    char epochId[37];
    char bindingId[37];
    char barrierId[37];
    uint8_t reserved[4];
    uint32_t crc;
  };

  explicit DurableCounter(CounterStorage& storage) : store_(storage) { memset(&state_, 0, sizeof(state_)); }

  // Empty storage is valid but unbound. A read/CRC error must not silently
  // initialize a blank counter or wipe previously persisted work.
  Result load() {
    loaded_ = false;
    Snapshot a{}, b{};
    const int ra = store_.read(0, &a, sizeof(a));
    const int rb = store_.read(1, &b, sizeof(b));
    if (ra < 0 || rb < 0) return Result::StorageError;
    if ((ra == 1 && !valid(a)) || (rb == 1 && !valid(b))) return Result::StorageError;
    if (ra == 0 && rb == 0) { memset(&state_, 0, sizeof(state_)); active_ = 1; loaded_ = true; return Result::Ok; }
    if (ra == 1 && rb == 1 && a.generation == b.generation && !equivalent(a, b)) return Result::StorageError;
    if (rb == 1 && (ra == 0 || b.generation > a.generation)) { state_ = b; active_ = 1; }
    else { state_ = a; active_ = 0; }
    loaded_ = true;
    return Result::Ok;
  }

  Result bind(const char* order, uint64_t revision, const char* binding, const char* epoch) {
    if (!loaded_) return Result::NotReady;
    if (!uuid(order) || !uuid(binding) || !uuid(epoch) || revision == 0 || revision > MAX_COUNT) return Result::Invalid;
    if (isBound() && strcmp(state_.orderId, order) == 0 && state_.bindingRevision == revision && strcmp(state_.bindingId, binding) == 0 && strcmp(state_.epochId, epoch) == 0) return Result::Ok;
    if (isFrozen()) return Result::Frozen;
    if (isBound() && state_.total != state_.acknowledged) return Result::Pending;
    if (isBound() && revision <= state_.bindingRevision) return Result::Invalid;
    Snapshot next = state_;
    memcpy(next.orderId, order, 37); memcpy(next.bindingId, binding, 37); memcpy(next.epochId, epoch, 37);
    next.bindingRevision = revision; next.total = 0; next.acknowledged = 0;
    return persist(next);
  }

  Result add(uint32_t amount) {
    if (!loaded_ || !isBound()) return Result::NotReady;
    if (!amount) return Result::Invalid;
    if (state_.total > MAX_COUNT - amount) return Result::Full;
    Snapshot next = state_; next.total += amount;
    return persist(next);
  }

  Result acknowledge(const char* epoch, uint64_t total) {
    if (!loaded_ || !isBound()) return Result::NotReady;
    if (!uuid(epoch) || strcmp(state_.epochId, epoch) != 0 || total > state_.total) return Result::Invalid;
    if (isFrozen() && total > state_.frozenTotal) return Result::Frozen;
    if (total <= state_.acknowledged) return Result::Ok; // duplicate/out-of-order response
    Snapshot next = state_; next.acknowledged = total;
    return persist(next);
  }

  // Persist before notifying the server. While frozen, newly pressed counts
  // remain durable in total but are withheld from upload as the next batch.
  Result beginFreeze(const char* barrier) {
    if (!loaded_ || !isBound()) return Result::NotReady;
    if (!uuid(barrier)) return Result::Invalid;
    if (isFrozen()) return strcmp(state_.barrierId,barrier)==0 ? Result::Ok : Result::Frozen;
    if (pending()!=0) return Result::Pending;
    Snapshot next=state_; memcpy(next.barrierId,barrier,37); next.frozenTotal=next.total;
    return persist(next);
  }

  // This only releases local state; the caller MUST first obtain the matching
  // authenticated completed/cancelled/expired status from the server. A local
  // timeout or a reset is never sufficient evidence to unfreeze.
  Result finishFreeze(const char* barrier, TerminalOutcome outcome) {
    if (!loaded_ || !isBound()) return Result::NotReady;
    if (!uuid(barrier) || !isFrozen() || strcmp(state_.barrierId,barrier)!=0) return Result::Invalid;
    switch(outcome) {
      case TerminalOutcome::Completed: case TerminalOutcome::Cancelled: case TerminalOutcome::Expired: break;
      default: return Result::Invalid;
    }
    Snapshot next=state_; memset(next.barrierId,0,sizeof(next.barrierId)); next.frozenTotal=0;
    return persist(next);
  }

  bool isReady() const { return loaded_ && isBound(); }
  bool isFrozen() const { return state_.barrierId[0] != '\0'; }
  bool canUpload() const { return isReady() && !isFrozen(); }
  // On a physical press after freezing do not send freeze/renew again. Cancel
  // remotely if possible; if commit wins the race, deferred counts remain.
  bool canReportFrozen() const { return loaded_ && isFrozen() && state_.total == state_.frozenTotal && state_.acknowledged == state_.frozenTotal; }
  uint64_t uploadableTotal() const { return isFrozen() ? state_.frozenTotal : state_.total; }

  const Snapshot& state() const { return state_; }
  bool isBound() const { return state_.orderId[0] != '\0'; }
  uint64_t pending() const { return state_.total - state_.acknowledged; }

 private:
  CounterStorage& store_;
  Snapshot state_{};
  unsigned active_ = 1;
  bool loaded_ = false;
  static constexpr uint32_t MAGIC = 0x58494354;
  static bool uuid(const char* s) {
    if (!s) return false;
    for (unsigned i=0; i<36; ++i) {
      const char c = s[i]; if (!c) return false;
      if (i==8 || i==13 || i==18 || i==23) { if (c!='-') return false; }
      else if (!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'))) return false;
    }
    return s[36] == '\0';
  }
  static uint32_t checksum(const Snapshot& s) {
    const auto* data = reinterpret_cast<const uint8_t*>(&s); uint32_t crc = 0xffffffffu;
    for (size_t i=0; i<offsetof(Snapshot, crc); ++i) {
      crc ^= data[i];
      for (unsigned j=0; j<8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  }
  static bool equivalent(const Snapshot& a, const Snapshot& b) {
    return a.magic==b.magic && a.version==b.version && a.generation==b.generation && a.total==b.total && a.acknowledged==b.acknowledged && a.bindingRevision==b.bindingRevision && memcmp(a.orderId,b.orderId,sizeof(a.orderId))==0 && memcmp(a.epochId,b.epochId,sizeof(a.epochId))==0 && a.frozenTotal==b.frozenTotal && memcmp(a.bindingId,b.bindingId,sizeof(a.bindingId))==0 && memcmp(a.barrierId,b.barrierId,sizeof(a.barrierId))==0 && memcmp(a.reserved,b.reserved,sizeof(a.reserved))==0 && a.crc==b.crc;
  }
  static bool valid(const Snapshot& s) {
    return s.magic==MAGIC && s.version==2 && s.generation && s.total<=MAX_COUNT && s.acknowledged<=s.total && s.bindingRevision && s.bindingRevision<=MAX_COUNT && uuid(s.orderId) && uuid(s.epochId) && uuid(s.bindingId) && ((s.barrierId[0]=='\0' && s.frozenTotal==0) || (uuid(s.barrierId) && s.frozenTotal==s.acknowledged && s.frozenTotal<=s.total)) && s.crc==checksum(s);
  }
  Result persist(Snapshot next) {
    if (state_.generation == UINT64_MAX) return Result::Full;
    next.magic=MAGIC; next.version=2; next.generation=state_.generation+1; next.crc=checksum(next);
    const unsigned target=active_^1u;
    if (!store_.write(target, &next, sizeof(next))) { loaded_=false; return Result::StorageError; }
    Snapshot verified{};
    if (store_.read(target, &verified, sizeof(verified))!=1 || !valid(verified) || !equivalent(verified,next)) { loaded_=false; return Result::StorageError; }
    state_=next; active_=target;
    return Result::Ok;
  }
};
