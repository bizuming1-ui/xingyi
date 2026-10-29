#!/bin/sh
set -eu
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
for test in button_counter durable_counter freeze_counter sync_planner preferences_counter_storage random_epoch_source esp_wifi_entropy wifi_supervisor esp_wifi_station device_input; do
  g++ -std=c++17 -Wall -Wextra -Werror -I tests/mocks "tests/$test.cpp" -o "$workdir/$test"
  "$workdir/$test"
done
