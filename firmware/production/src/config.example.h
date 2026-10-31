#pragma once
// COPY to config.local.h on your own computer. NEVER commit or share it.
// Device functions must be deployed to a TEST project and a device provisioned
// before this candidate can connect. These placeholders intentionally fail.
static constexpr char WIFI_SSID[] = "";
static constexpr char WIFI_PASSWORD[] = "";
static constexpr char SUPABASE_HOST[] = "xypisxdlxcqyavixdiji.supabase.co";
static constexpr char SUPABASE_PUBLISHABLE_KEY[] = "sb_publishable_Wn_srpPJ3lIYOMWwDASRyQ_mqp37Pnc";
static constexpr char DEVICE_ID[] = "";
static constexpr char DEVICE_SECRET[] = ""; // 64 lowercase hex, not service-role key
// Test project root: GTS Root R4, SHA-256 349DFA4058C5E263123B398AE795573C4E1313C83FE68F93556CD5E8031B3C7D.
// Verified against the isolated endpoint in CI; it can change and requires maintenance.
static constexpr char SUPABASE_CA_PEM[] = R"CERT(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)CERT";
// Candidate values are NOT yet backed by measured/guaranteed hardware limits.
static constexpr uint32_t TIME_LIFETIME_MS = 60000;
static constexpr uint32_t CLOCK_DRIFT_PPM = 9999;
