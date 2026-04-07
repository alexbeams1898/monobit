#pragma once

#include <cstdint>
#include <string>

namespace UpdateChecker
{

// ---------------------------------------------------------------------------
// Version check (existing API)
// ---------------------------------------------------------------------------

void startCheck();
bool isComplete();
bool updateAvailable();
std::string latestVersion();
std::string releaseUrl();
bool isNewer(const std::string& local, const std::string& remote);

// ---------------------------------------------------------------------------
// Self-update download + install
// ---------------------------------------------------------------------------

enum class UpdateState
{
    Idle,
    Downloading,
    Extracting,
    ReadyToInstall,
    Failed
};

// Current state of the download/install pipeline.
UpdateState updateState();

// Download progress (0.0 to 1.0). Returns -1.0 if total size is unknown.
float downloadProgress();

// Human-readable status (e.g. "Downloading... 4.2 / 12.1 MB").
std::string statusMessage();

// Error message when state == Failed.
std::string errorMessage();

// Start the background download + extract pipeline.
void startDownload();

// Write the updater batch script, spawn it, caller should exit(0) after.
// Only valid when state == ReadyToInstall. Returns true on success.
bool installAndRelaunch();

// Reset back to Idle (e.g. to retry after failure).
void resetState();

// ---------------------------------------------------------------------------
// Testable helpers
// ---------------------------------------------------------------------------

// Extract the .zip asset's browser_download_url from a GitHub releases JSON.
std::string parseAssetDownloadUrl(const std::string& json_body);

// Strip a top-level directory prefix from a zip entry path.
std::string stripTopLevelDir(const std::string& path, const std::string& prefix);

// Format a byte count as human-readable (e.g. "4.2 MB").
std::string formatBytes(uint64_t bytes);

} // namespace UpdateChecker
