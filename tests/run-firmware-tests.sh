#!/bin/sh
set -eu
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
for test in monitor_event runtime_config button_counter durable_counter freeze_counter sync_planner preferences_counter_storage random_epoch_source esp_wifi_entropy wifi_supervisor esp_wifi_station device_input async_dns esp_async_dns nonblocking_tcp esp_tcp_ops resolved_tcp_connect trusted_time_window esp_monotonic_millis radio_lifecycle_guard freertos_radio_lifecycle; do
  g++ -std=c++17 -pthread -Wall -Wextra -Werror -I tests/mocks "tests/$test.cpp" -o "$workdir/$test"
  "$workdir/$test"
done
