#!/bin/sh
set -eu
: "${ARDUINOJSON_INCLUDE:?Set ARDUINOJSON_INCLUDE to ArduinoJson 6.21.5 src directory}"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
for test in rpc_codec rpc_worker; do
  g++ -std=c++11 -Wall -Wextra -Werror -I "$ARDUINOJSON_INCLUDE" "tests/$test.cpp" -o "$workdir/$test"
  "$workdir/$test"
done
