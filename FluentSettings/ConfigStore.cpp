// ConfigStore.cpp — JSON settings persistence. json.hpp is confined to this
// file. Independent of the FluentUI rendering library.

#include "ConfigStore.h"

#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <cstdio>

#pragma comment(lib, "shell32.lib")

using json = nlohmann::json;

namespace fluent {

namespace {
constexpr int kConfigVersion = 1;

// Local trace helper (this module does not depend on FluentUI's fl_common).
void Trace(const char* msg) {
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[FluentSettings] %s\n", msg);
    OutputDebugStringA(buf);
}

// UTF-16 <-> UTF-8 (JSON stores UTF-8; the Win32 API is UTF-16).
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n,
                        nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// Full path of the running exe's directory (with trailing backslash).
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
} // namespace

struct ConfigStore::Impl {
    json root = json::object();
};

ConfigStore::ConfigStore() : impl_(std::make_unique<Impl>()) {}
ConfigStore::~ConfigStore() = default;

bool ConfigStore::Initialize(const wchar_t* appName) {
    std::wstring app = appName ? appName : L"FluentUI";

    // Portable mode: portable.txt next to the exe -> config beside the exe.
    std::wstring exeDir = ExeDir();
    portable_ = FileExists(exeDir + L"portable.txt");
    if (portable_) {
        path_ = exeDir + L"config.json";
    } else {
        wchar_t* appData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                           &appData)) &&
            appData) {
            path_ = std::wstring(appData) + L"\\" + app + L"\\config.json";
            CoTaskMemFree(appData);
        } else {
            // Last resort: fall back to the exe directory.
            path_ = exeDir + L"config.json";
        }
    }

    if (!FileExists(path_)) {
        Trace("no existing config, starting from defaults");
        impl_->root = json::object();
        return true;
    }

    // Read and parse. On any failure, back up the offending file and start fresh
    // rather than overwriting it (corrupt config -> backup + default).
    std::ifstream in(path_.c_str(), std::ios::binary);
    bool ok = false;
    if (in) {
        try {
            json parsed = json::parse(in, nullptr, /*allow_exceptions*/ true,
                                      /*ignore_comments*/ true);
            int ver = parsed.value("version", 0);
            if (ver > kConfigVersion) {
                Trace("config version newer than supported");
            } else {
                impl_->root = std::move(parsed);
                ok = true;
            }
            // (Future: migrate fields when ver < kConfigVersion here.)
        } catch (const std::exception&) {
            Trace("config parse failed");
        }
    }
    in.close();

    if (!ok) {
        std::wstring backup = path_ + L".bak";
        CopyFileW(path_.c_str(), backup.c_str(), FALSE);
        Trace("backed up unreadable config, using defaults");
        impl_->root = json::object();
    }
    return true;
}

bool ConfigStore::Save() {
    impl_->root["version"] = kConfigVersion;

    // Ensure the parent directory exists (no-op in portable mode).
    size_t slash = path_.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        std::wstring dir = path_.substr(0, slash);
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    }

    // Write to a temp file then atomically replace, so a crash mid-write can't
    // corrupt the existing config.
    std::wstring tmp = path_ + L".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) {
            Trace("open temp config for write failed");
            return false;
        }
        out << impl_->root.dump(2);
    }
    if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        Trace("replace config failed");
        return false;
    }
    return true;
}

// --- Typed accessors -------------------------------------------------------

bool ConfigStore::GetBool(const char* key, bool fallback) const {
    auto it = impl_->root.find(key);
    return (it != impl_->root.end() && it->is_boolean()) ? it->get<bool>()
                                                         : fallback;
}

long long ConfigStore::GetInt(const char* key, long long fallback) const {
    auto it = impl_->root.find(key);
    return (it != impl_->root.end() && it->is_number_integer())
               ? it->get<long long>()
               : fallback;
}

double ConfigStore::GetDouble(const char* key, double fallback) const {
    auto it = impl_->root.find(key);
    return (it != impl_->root.end() && it->is_number()) ? it->get<double>()
                                                        : fallback;
}

std::wstring ConfigStore::GetString(const char* key,
                                    const std::wstring& fallback) const {
    auto it = impl_->root.find(key);
    return (it != impl_->root.end() && it->is_string())
               ? FromUtf8(it->get<std::string>())
               : fallback;
}

void ConfigStore::SetBool(const char* key, bool value) {
    impl_->root[key] = value;
}
void ConfigStore::SetInt(const char* key, long long value) {
    impl_->root[key] = value;
}
void ConfigStore::SetDouble(const char* key, double value) {
    impl_->root[key] = value;
}
void ConfigStore::SetString(const char* key, const std::wstring& value) {
    impl_->root[key] = ToUtf8(value);
}

bool ConfigStore::Has(const char* key) const {
    return impl_->root.find(key) != impl_->root.end();
}

} // namespace fluent
