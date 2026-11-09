#pragma once
// Copy to ignored cloud_secrets.h only after the cloud endpoint is deployed.
constexpr char CLOUD_URL[] = "https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev/api/ingest";
constexpr char CLOUD_INGEST_KEY[] = "";
// Verified CA chain for the actual cloud host; never use setInsecure().
constexpr char CLOUD_CA_PEM[] = R"PEM()PEM";
