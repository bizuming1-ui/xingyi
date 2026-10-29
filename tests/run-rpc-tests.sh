#!/bin/sh
set -eu
: "${ARDUINOJSON_INCLUDE:?Set ARDUINOJSON_INCLUDE to ArduinoJson 6.21.5 src directory}"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
for test in rpc_codec rpc_worker sync_bridge free_rtos_mailbox http_response_decoder http_rpc_exchange; do
  g++ -std=c++11 -Wall -Wextra -Werror -I "$ARDUINOJSON_INCLUDE" -I tests/mocks "tests/$test.cpp" -o "$workdir/$test"
  "$workdir/$test"
done
