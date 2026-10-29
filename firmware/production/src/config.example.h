#pragma once
// COPY to config.local.h on your own computer. NEVER commit or share it.
// Device functions must be deployed to a TEST project and a device provisioned
// before this candidate can connect. These placeholders intentionally fail.
static constexpr char WIFI_SSID[] = "";
static constexpr char WIFI_PASSWORD[] = "";
static constexpr char SUPABASE_HOST[] = "cdaplcswcdjfbkdbabji.supabase.co";
static constexpr char SUPABASE_PUBLISHABLE_KEY[] = "sb_publishable_-YtgoTHY0IPYXiVb0L-oSA_hN4xnlZ7";
static constexpr char DEVICE_ID[] = "";
static constexpr char DEVICE_SECRET[] = ""; // 64 lowercase hex, not service-role key
// Obtain and verify the correct CA for the actual endpoint out of band.
static constexpr char SUPABASE_CA_PEM[] = "";
// Candidate values are NOT yet backed by measured/guaranteed hardware limits.
static constexpr uint32_t TIME_LIFETIME_MS = 60000;
static constexpr uint32_t CLOCK_DRIFT_PPM = 9999;
