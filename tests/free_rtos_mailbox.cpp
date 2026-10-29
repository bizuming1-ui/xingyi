#include "../firmware/include/FreeRtosSyncMailbox.h"
#include <cassert>
#include <iostream>
#include <type_traits>
int main() {
  static_assert(!std::is_copy_constructible<FreeRtosSyncMailbox>::value,"static queues cannot be copied");
  FreeRtosSyncMailbox box;assert(box.valid());
  SyncPlanner::Command command{};command.kind=SyncPlanner::Kind::Upload;command.ticket=42;command.total=97;
  assert(box.submit(command));assert(!box.submit(command));
  SyncPlanner::Command received{};assert(box.take(received));
  assert(received.kind==command.kind && received.ticket==42 && received.total==97);
  assert(!box.take(received));
  SyncMailbox::Completion result{};result.ticket=42;result.reply.success=true;result.reply.acceptedTotal=97;
  assert(box.publish(result));assert(!box.publish(result));
  SyncMailbox::Completion collected{};assert(box.collect(collected));
  assert(collected.ticket==42 && collected.reply.success && collected.reply.acceptedTotal==97);
  assert(!box.collect(collected));
  std::cout<<"FreeRTOS mailbox mock tests passed\n";
}
