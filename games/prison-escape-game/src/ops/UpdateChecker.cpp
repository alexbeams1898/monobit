#include "ops/UpdateChecker.h"

#include "Version.h"

#include <nlohmann/json.hpp>

#include <miniz.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

#include <SDL.h>

namespace UpdateChecker
{

// ---------------------------------------------------------------------------
// State: version check
// ---------------------------------------------------------------------------

static std::atomic<bool> sStarted{false};
static std::atomic<bool> sComplete{false};
static std::atomic<bool> sHasUpdate{false};
static std::string sLatestVersion;
static std::string sReleaseUrl;
static std::string sAssetUrl;

// ---------------------------------------------------------------------------
// State: download / install
// ---------------------------------------------------------------------------

static std::atomic<UpdateState> sState{UpdateState::Idle};
static std::atomic<int64_t> sBytesDownloaded{0};
static std::atomic<int64_t> sBytesTotal{-1};
static std::mutex sMessageMutex;
static std::string sStatusMsg;
static std::string sErrorMsg;

static void setStatusMsg(const std::string& msg)
{
    const std::lock_guard<std::mutex> lock(sMessageMutex);
    sStatusMsg = msg;
}

static void setError(const std::string& msg)
{
    {
        const std::lock_guard<std::mutex> lock(sMessageMutex);
        sErrorMsg = msg;
        sStatusMsg = msg;
    }
    sState.store(UpdateState::Failed);
}

// ---------------------------------------------------------------------------
// Version comparison
// ---------------------------------------------------------------------------

static std::vector<int> parseSemver(const std::string& v)
{
    std::vector<int> parts;
    std::istringstream ss(v);
    std::string token;
    while (std::getline(ss, token, '.'))
    {
        try
        {
            parts.push_back(std::stoi(token));
        }
        catch (...)
        {
            parts.push_back(0);
        }
    }
    while (parts.size() < 3)
        parts.push_back(0);
    return parts;
}

bool isNewer(const std::string& local, const std::string& remote)
{
    const auto lv = parseSemver(local);
    const auto rv = parseSemver(remote);
    for (int i = 0; i < 3; ++i)
    {
        if (rv[static_cast<std::size_t>(i)] > lv[static_cast<std::size_t>(i)])
            return true;
        if (rv[static_cast<std::size_t>(i)] < lv[static_cast<std::size_t>(i)])
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Testable helpers
// ---------------------------------------------------------------------------

std::string parseAssetDownloadUrl(const std::string& json_body)
{
    try
    {
        const auto j = nlohmann::json::parse(json_body);
        if (!j.contains("assets") || !j["assets"].is_array())
            return {};
        for (const auto& asset : j["assets"])
        {
            const std::string name = asset.value("name", std::string{});
            if (name.size() > 4 && name.substr(name.size() - 4) == ".zip")
                return asset.value("browser_download_url", std::string{});
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[UpdateChecker] JSON parse error: " << e.what() << "\n";
    }
    return {};
}

std::string stripTopLevelDir(const std::string& path, const std::string& prefix)
{
    if (!prefix.empty() && path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix)
        return path.substr(prefix.size());
    return path;
}

std::string formatBytes(uint64_t bytes)
{
    if (bytes < 1024)
        return std::to_string(bytes) + " B";
    char buf[32];
    constexpr uint64_t kMB = 1024ULL * 1024ULL;
    if (bytes < kMB)
    {
        const double kb = static_cast<double>(bytes) / 1024.0;
        std::snprintf(buf, sizeof(buf), "%.1f KB", kb);
        return buf;
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    return buf;
}

// ---------------------------------------------------------------------------
// HTTP fetch (platform-specific)
// ---------------------------------------------------------------------------

#ifdef _WIN32

static std::string fetchUrl(const wchar_t* host, const wchar_t* path)
{
    std::string result;

    HINTERNET session =
        WinHttpOpen(L"PrisonEscapeGame", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session)
        return result;

    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection)
    {
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                           0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0)
        {
            std::vector<char> buf(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(request, buf.data(), bytesAvailable, &bytesRead))
                result.append(buf.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

// ---------------------------------------------------------------------------
// URL parsing + file download (Windows only)
// ---------------------------------------------------------------------------

static bool parseUrl(const std::string& url, std::wstring& host, std::wstring& path)
{
    std::string rest = url;
    if (rest.substr(0, 8) == "https://")
        rest = rest.substr(8);
    else if (rest.substr(0, 7) == "http://")
        rest = rest.substr(7);
    else
        return false;

    const auto slash = rest.find('/');
    if (slash == std::string::npos)
        return false;

    const std::string h = rest.substr(0, slash);
    const std::string p = rest.substr(slash);
    host.assign(h.begin(), h.end());
    path.assign(p.begin(), p.end());
    return true;
}

static std::string getTempDir()
{
    wchar_t buf[MAX_PATH + 1];
    const DWORD len = GetTempPathW(MAX_PATH + 1, buf);
    if (len > 0)
    {
        const int sz = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len), nullptr, 0,
                                           nullptr, nullptr);
        std::string result(static_cast<std::size_t>(sz), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len), result.data(), sz, nullptr,
                            nullptr);
        return result;
    }
    return "C:\\Temp\\";
}

static bool downloadFile(const std::string& url, const std::string& destPath)
{
    std::wstring host;
    std::wstring path;
    if (!parseUrl(url, host, path))
        return false;

    HINTERNET session =
        WinHttpOpen(L"PrisonEscapeGame", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session)
        return false;

    // GitHub asset URLs redirect cross-host (github.com -> objects.githubusercontent.com).
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                     sizeof(redirectPolicy));

    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    const bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr);
    if (!ok)
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    // Read Content-Length if available.
    wchar_t clBuf[32] = {};
    DWORD clSize = sizeof(clBuf);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clBuf,
                            &clSize, WINHTTP_NO_HEADER_INDEX))
    {
        sBytesTotal.store(static_cast<int64_t>(_wtoi64(clBuf)));
    }

    HANDLE hFile = CreateFileA(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    int64_t totalRead = 0;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(req, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(req, buf.data(), bytesAvailable, &bytesRead))
        {
            DWORD written = 0;
            WriteFile(hFile, buf.data(), bytesRead, &written, nullptr);
            totalRead += static_cast<int64_t>(bytesRead);
            sBytesDownloaded.store(totalRead);

            const int64_t total = sBytesTotal.load();
            if (total > 0)
                setStatusMsg("Downloading... " + formatBytes(static_cast<uint64_t>(totalRead)) +
                             " / " + formatBytes(static_cast<uint64_t>(total)));
            else
                setStatusMsg("Downloading... " + formatBytes(static_cast<uint64_t>(totalRead)));
        }
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return totalRead > 0;
}

#else

static std::string fetchUrl(const char* /*host*/, const char* /*path*/)
{
    FILE* pipe = popen(
        "curl -s "
        "https://api.github.com/repos/alexbeams1898/prison-escape-game-releases/releases/latest",
        "r");
    if (!pipe)
        return {};
    std::string result;
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe))
        result += buf;
    pclose(pipe);
    return result;
}

static std::string getTempDir()
{
    return "/tmp/";
}

#endif

// ---------------------------------------------------------------------------
// Zip extraction (cross-platform via miniz)
// ---------------------------------------------------------------------------

static bool extractZip(const std::string& zipPath, const std::string& destDir)
{
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
    {
        std::cerr << "[UpdateChecker] Failed to open zip: " << zipPath
                  << " error: " << mz_zip_get_error_string(mz_zip_get_last_error(&zip)) << "\n";
        return false;
    }

    const int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));

    // Normalize backslashes to forward slashes (zip may use either separator).
    auto normalizePath = [](std::string s)
    {
        for (auto& c : s)
            if (c == '\\')
                c = '/';
        return s;
    };

    // Find the common top-level directory prefix.
    std::string prefix;
    if (numFiles > 0)
    {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, 0, &stat))
        {
            const std::string first = normalizePath(stat.m_filename);
            const auto slash = first.find('/');
            if (slash != std::string::npos)
                prefix = first.substr(0, slash + 1);
        }
    }

    for (int i = 0; i < numFiles; ++i)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(i), &stat))
            continue;

        std::string entryPath = stripTopLevelDir(normalizePath(stat.m_filename), prefix);
        if (entryPath.empty())
            continue;

        std::string fullPath = destDir;
        fullPath += '/';
        fullPath += entryPath;

        const bool isDir = stat.m_is_directory || (!entryPath.empty() && entryPath.back() == '/');
        if (isDir)
        {
            std::filesystem::create_directories(fullPath);
            continue;
        }

        std::filesystem::create_directories(std::filesystem::path(fullPath).parent_path());

        if (!mz_zip_reader_extract_to_file(&zip, static_cast<mz_uint>(i), fullPath.c_str(), 0))
        {
            std::cerr << "[UpdateChecker] Failed to extract: " << stat.m_filename << " -> "
                      << fullPath
                      << " error: " << mz_zip_get_error_string(mz_zip_get_last_error(&zip)) << "\n";
            mz_zip_reader_end(&zip);
            return false;
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

static void renameExeInStaging(const std::string& stagingDir)
{
    for (const auto& entry : std::filesystem::directory_iterator(stagingDir))
    {
        const std::string name = entry.path().filename().string();
        if (name.find("prison-escape-game-v") == 0 && name.size() > 4 &&
            name.substr(name.size() - 4) == ".exe")
        {
            std::filesystem::rename(entry.path(),
                                    std::filesystem::path(stagingDir) / "prison-break-game.exe");
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Background check thread
// ---------------------------------------------------------------------------

static void checkThread()
{
#ifdef _WIN32
    const std::string body = fetchUrl(
        L"api.github.com", L"/repos/alexbeams1898/prison-escape-game-releases/releases/latest");
#else
    const std::string body = fetchUrl(nullptr, nullptr);
#endif

    if (!body.empty())
    {
        try
        {
            const auto j = nlohmann::json::parse(body);
            std::string tag = j.value("tag_name", std::string{});
            if (!tag.empty() && tag[0] == 'v')
                tag = tag.substr(1);

            sLatestVersion = tag;
            sReleaseUrl = j.value("html_url", std::string{});
            sAssetUrl = parseAssetDownloadUrl(body);

            if (!tag.empty() && isNewer(GAME_VERSION, tag))
                sHasUpdate.store(true);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[UpdateChecker] Parse error: " << e.what() << "\n";
        }
    }

    sComplete.store(true);
}

// ---------------------------------------------------------------------------
// Background download + extract thread
// ---------------------------------------------------------------------------

static void downloadThread()
{
    const std::string tempDir = getTempDir();
    const std::string zipPath = tempDir + "prison-escape-game-update.zip";
    const std::string stagingDir = tempDir + "prison-escape-game-update";

    // Clean previous attempt.
    std::filesystem::remove(zipPath);
    std::filesystem::remove_all(stagingDir);

    sState.store(UpdateState::Downloading);
    sBytesDownloaded.store(0);
    sBytesTotal.store(-1);
    setStatusMsg("Starting download...");

#ifdef _WIN32
    if (!downloadFile(sAssetUrl, zipPath))
    {
        setError("Download failed. Check your internet connection.");
        return;
    }
#else
    setError("Auto-update is not supported on this platform.");
    return;
#endif

    sState.store(UpdateState::Extracting);
    setStatusMsg("Extracting...");

    std::filesystem::create_directories(stagingDir);
    if (!extractZip(zipPath, stagingDir))
    {
        setError("Failed to extract update archive.");
        std::filesystem::remove(zipPath);
        std::filesystem::remove_all(stagingDir);
        return;
    }

    renameExeInStaging(stagingDir);

    sState.store(UpdateState::ReadyToInstall);
    setStatusMsg("Ready to install.");
}

// ---------------------------------------------------------------------------
// Public API: version check
// ---------------------------------------------------------------------------

void startCheck()
{
    bool expected = false;
    if (!sStarted.compare_exchange_strong(expected, true))
        return;

    std::thread(checkThread).detach();
}

bool isComplete()
{
    return sComplete.load();
}

bool updateAvailable()
{
    return sHasUpdate.load();
}

std::string latestVersion()
{
    return sLatestVersion;
}

std::string releaseUrl()
{
    return sReleaseUrl;
}

// ---------------------------------------------------------------------------
// Public API: download + install
// ---------------------------------------------------------------------------

UpdateState updateState()
{
    return sState.load();
}

float downloadProgress()
{
    const int64_t total = sBytesTotal.load();
    if (total <= 0)
        return -1.0f;
    const int64_t downloaded = sBytesDownloaded.load();
    return static_cast<float>(static_cast<double>(downloaded) / static_cast<double>(total));
}

std::string statusMessage()
{
    const std::lock_guard<std::mutex> lock(sMessageMutex);
    return sStatusMsg;
}

std::string errorMessage()
{
    const std::lock_guard<std::mutex> lock(sMessageMutex);
    return sErrorMsg;
}

void startDownload()
{
    UpdateState expected = UpdateState::Idle;
    if (!sState.compare_exchange_strong(expected, UpdateState::Downloading))
        return;

    std::thread(downloadThread).detach();
}

void resetState()
{
    sState.store(UpdateState::Idle);
    sBytesDownloaded.store(0);
    sBytesTotal.store(-1);
    const std::lock_guard<std::mutex> lock(sMessageMutex);
    sStatusMsg.clear();
    sErrorMsg.clear();
}

bool installAndRelaunch()
{
#ifdef _WIN32
    char* basePath = SDL_GetBasePath();
    if (!basePath)
    {
        setError("Cannot determine install directory.");
        return false;
    }
    const std::string installDir(basePath);
    SDL_free(basePath);

    const std::string tempDir = getTempDir();
    const std::string stagingDir = tempDir + "prison-escape-game-update";
    const std::string batchPath = tempDir + "prison-escape-game-updater.bat";

    // Convert forward slashes to backslashes for the batch script.
    auto toBackslash = [](std::string s)
    {
        for (auto& c : s)
            if (c == '/')
                c = '\\';
        return s;
    };

    const std::string bsInstall = toBackslash(installDir);
    const std::string bsStaging = toBackslash(stagingDir);
    const std::string bsZip = toBackslash(tempDir + "prison-escape-game-update.zip");

    std::ofstream bat(batchPath);
    if (!bat.is_open())
    {
        setError("Cannot write updater script.");
        return false;
    }

    bat << "@echo off\n";
    bat << "title Updating Prison Escape Game...\n";
    bat << "echo Waiting for game to close...\n";
    bat << "ping 127.0.0.1 -n 4 >nul\n";
    bat << "echo Copying files...\n";
    bat << "xcopy /E /Y /I \"" << bsStaging << "\\*\" \"" << bsInstall << "\"\n";
    bat << "echo Cleaning up...\n";
    bat << "rmdir /S /Q \"" << bsStaging << "\"\n";
    bat << "del /Q \"" << bsZip << "\"\n";
    bat << "echo Launching game...\n";
    bat << "start \"\" \"" << bsInstall << "prison-break-game.exe\"\n";
    bat << "del \"%~f0\"\n";
    bat.close();

    const std::string bsBatch = toBackslash(batchPath);
    std::wstring wCmd = L"cmd.exe /C \"";
    wCmd.append(bsBatch.begin(), bsBatch.end());
    wCmd += L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(nullptr, wCmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                        nullptr, &si, &pi))
    {
        setError("Failed to launch updater.");
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    SDL_OpenURL(releaseUrl().c_str());
    return false;
#endif
}

} // namespace UpdateChecker
