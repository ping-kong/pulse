// data_root.h - Where Pulse keeps its data.
//
// Modules that need a directory under the Pulse data root ask this header
// instead of building %LOCALAPPDATA%\Pulse themselves. Portable copies, and
// anyone who keeps Windows off their data drive, redirect the whole tree with
// either of two mechanisms:
//
//   PULSE_DATA_DIR=D:\PulseData   environment variable, wins everywhere
//   pulse.ini next to the exe     data_dir=.                (the exe folder)
//                                 data_dir=D:\PulseData
//                                 data_dir=..\PulseData    (relative to the exe)
//
// Without both, storage stays at %LOCALAPPDATA%\Pulse, so installed copies
// behave exactly as before. A machine-wide root (ProgramData\Pulse, used by
// the PulseIndex service) is deliberately not redirected: the service exe
// lives in the install directory and services do not inherit user
// environment variables.
//
// Resolution is cached per process; every executable in a folder resolves the
// same way, which is what keeps the portable hosts in agreement without
// passing anything through child-process environments.
//
// Header-only on purpose: the app, the preview/index/shell hosts and the
// tests are separate executables and must not gain link dependencies.
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

namespace pulse::storage {

inline constexpr const wchar_t kDataRootEnvVar[] = L"PULSE_DATA_DIR";
inline constexpr const wchar_t kDataRootIniName[] = L"pulse.ini";
inline constexpr const wchar_t kDataRootIniKey[] = L"data_dir";

namespace detail {

// Directory of the running executable, without a trailing backslash (drive
// roots keep theirs: "C:\").
inline std::wstring ExecutableDirectory() {
    wchar_t path[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return {};
    const std::wstring value(path, length);
    const size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return slash <= 3 ? value.substr(0, slash + 1) : value.substr(0, slash);
}

inline bool SameText(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

inline std::wstring TrimSpace(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t");
    return text.substr(first, last - first + 1);
}

inline bool IsAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    return path.rfind(L"\\\\", 0) == 0;
}

// Expands %VAR% references and makes relative paths absolute against base.
inline std::wstring ResolvePath(const std::wstring& path, const std::wstring& base) {
    if (path.empty()) return {};
    wchar_t expanded[32768]{};
    const DWORD expanded_length = ExpandEnvironmentStringsW(path.c_str(), expanded, ARRAYSIZE(expanded));
    std::wstring value = expanded_length && expanded_length < ARRAYSIZE(expanded)
        ? std::wstring(expanded, expanded_length - 1) : path;
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    if (value.empty() || IsAbsolutePath(value)) return value;
    const std::wstring combined = base.empty() ? value : base + L"\\" + value;
    wchar_t absolute[32768]{};
    const DWORD absolute_length = GetFullPathNameW(combined.c_str(), ARRAYSIZE(absolute), absolute, nullptr);
    if (!absolute_length || absolute_length >= ARRAYSIZE(absolute)) return {};
    return std::wstring(absolute, absolute_length);
}

// Creates every missing directory of an absolute path; false when the final
// directory does not exist afterwards.
inline bool CreateDirectoryChain(const std::wstring& directory) {
    // Intermediate failures are expected (drive roots, \\server on UNC paths);
    // only the final directory has to exist.
    for (size_t at = directory.find_first_of(L"\\/", 3); ; at = directory.find_first_of(L"\\/", at + 1)) {
        CreateDirectoryW(directory.substr(0, at).c_str(), nullptr);
        if (at == std::wstring::npos) break;
    }
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// A redirected root only counts when Pulse can write to it: a read-only or
// missing drive would otherwise turn every later write into a failure.
inline bool DirectoryWritable(const std::wstring& directory) {
    if (directory.empty()) return false;
    const std::wstring probe = directory + L"\\.pulse-root-test-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

// data_dir=<path> from the ini next to the executable; empty when the file,
// the key or a usable value is absent. UTF-8 (with or without BOM) and
// UTF-16LE with BOM are accepted, which covers Notepad and PowerShell saves.
inline std::wstring IniDataDir(const std::wstring& directory) {
    HANDLE file = CreateFileW((directory + L"\\" + kDataRootIniName).c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char buffer[4096]{};
    DWORD read = 0;
    std::string bytes;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read) bytes.append(buffer, read);
    CloseHandle(file);
    if (bytes.empty()) return {};
    std::wstring text;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        const wchar_t* wide = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        text.assign(wide, (bytes.size() - 2) / sizeof(wchar_t));
    } else {
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                               static_cast<int>(bytes.size()), nullptr, 0);
        if (length <= 0) return {};
        text.resize(static_cast<size_t>(length));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                            static_cast<int>(bytes.size()), text.data(), length);
    }
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(L"\n", at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = TrimSpace(line);
        if (line.empty() || line.front() == L'#' || line.front() == L';') continue;
        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        if (!SameText(TrimSpace(line.substr(0, equals)), kDataRootIniKey)) continue;
        std::wstring value = TrimSpace(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

} // namespace detail

inline const std::wstring& Resolve() {
    static const std::wstring root = [] {
        // 1. Explicit environment override.
        wchar_t env[32768]{};
        const DWORD env_length = GetEnvironmentVariableW(kDataRootEnvVar, env, ARRAYSIZE(env));
        if (env_length && env_length < ARRAYSIZE(env)) {
            const std::wstring candidate = detail::ResolvePath(std::wstring(env, env_length), L"");
            if (!candidate.empty() && detail::CreateDirectoryChain(candidate) &&
                detail::DirectoryWritable(candidate))
                return candidate;
        }
        // 2. pulse.ini next to the executable.
        const std::wstring directory = detail::ExecutableDirectory();
        if (!directory.empty()) {
            const std::wstring configured = detail::IniDataDir(directory);
            if (!configured.empty()) {
                const std::wstring candidate = detail::ResolvePath(configured, directory);
                if (!candidate.empty() && detail::CreateDirectoryChain(candidate) &&
                    detail::DirectoryWritable(candidate))
                    return candidate;
            }
        }
        // 3. Legacy: %LOCALAPPDATA%\Pulse. The environment variable comes
        // first so test redirects keep isolating every module uniformly.
        wchar_t local[32768]{};
        const DWORD local_length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, ARRAYSIZE(local));
        std::wstring parent = local_length && local_length < ARRAYSIZE(local)
            ? std::wstring(local, local_length) : std::wstring();
        if (parent.empty()) {
            wchar_t known[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, known)))
                parent = known;
        }
        return parent.empty() ? std::wstring() : parent + L"\\Pulse";
    }();
    return root;
}

// The Pulse data directory. Empty only when even the legacy location cannot
// be determined; callers already treat that as "storage unavailable".
inline std::wstring DataRootPath() { return Resolve(); }

// The legacy location, for checks that must cover both trees while an old
// one still exists on disk.
inline std::wstring LegacyDataRootPath() {
    wchar_t known[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, known))) return {};
    return std::wstring(known) + L"\\Pulse";
}

} // namespace pulse::storage
