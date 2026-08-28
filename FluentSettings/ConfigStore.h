// ConfigStore.h — generic JSON-backed settings store (FluentSettings module).
//
// This is the application-layer persistence facility, deliberately INDEPENDENT
// of the FluentUI rendering library — the analogue of WPF's Properties.Settings
// or WinUI's ApplicationData.LocalSettings. It knows about files, %APPDATA%,
// portable mode and JSON; it knows nothing about windows, DPI or controls.
//
// Storage location:
//   * If "portable.txt" sits next to the exe, write config.json next to the exe
//     (USB-friendly).
//   * Otherwise %APPDATA%\<AppName>\config.json (multi-user isolation).
//
// The JSON has a top-level integer "version" for future migration. A corrupt or
// version-incompatible file is backed up (config.json.bak) before falling back
// to defaults — the user's file is never overwritten blindly. Saves are atomic
// (write temp + MoveFileEx replace).
//
// Values are addressed by string key at the top level. For grouped data (e.g. a
// window placement) the caller uses a flat naming convention such as "window.x"
// — the store stays a simple key/value bag and imposes no schema.
#pragma once

#include <windows.h>
#include <string>
#include <memory>

namespace fluent {

class ConfigStore {
public:
    // Resolve the storage path (portable vs %APPDATA%) and load config.json into
    // memory. Always succeeds: a missing/corrupt file yields empty defaults.
    bool Initialize(const wchar_t* appName);

    // Persist the in-memory config to disk (pretty-printed, atomic replace).
    bool Save();

    bool Portable() const { return portable_; }
    const std::wstring& Path() const { return path_; }

    // Typed accessors. The getters return `fallback` when the key is absent or
    // has the wrong type.
    bool GetBool(const char* key, bool fallback) const;
    long long GetInt(const char* key, long long fallback) const;
    double GetDouble(const char* key, double fallback) const;
    std::wstring GetString(const char* key, const std::wstring& fallback) const;

    void SetBool(const char* key, bool value);
    void SetInt(const char* key, long long value);
    void SetDouble(const char* key, double value);
    void SetString(const char* key, const std::wstring& value);

    bool Has(const char* key) const;

    ConfigStore();
    ~ConfigStore();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::wstring path_;
    bool portable_ = false;
};

} // namespace fluent
