#include "update_stage.h"
#include "../common/data_root.h"
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <thread>

namespace pulse::app {
namespace {
constexpr char marker[] = "Pulse owned update stage v1\n";
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
bool Directory(const std::wstring& path) {
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool StageName(const std::wstring& directory) {
    const auto slash = directory.find_last_of(L"\\/");
    const auto name = directory.substr(slash == std::wstring::npos ? 0 : slash + 1);
    return name.size() == 32 && name.find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
}
HANDLE Pin(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return handle;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { CloseHandle(handle); return INVALID_HANDLE_VALUE; }
    return handle;
}
bool DeleteOwnedFile(const std::wstring& path) {
    Handle file{CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
}
}
bool CreateUpdateStage(const std::wstring& data_root, std::wstring& directory, std::wstring& file, HANDLE& lease) {
    if (data_root.empty()) return false;
    const auto root = data_root + L"\\UpdateStaging";
    if (!Directory(root)) return false;
    Handle root_pin{Pin(root)};
    if (root_pin.value == INVALID_HANDLE_VALUE) return false;
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    std::wstring name;
    for (auto byte : random) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
    const auto candidate = root + L"\\" + name;
    if (!CreateDirectoryW(candidate.c_str(), nullptr)) return false;
    HANDLE acquired = CreateFileW((candidate + L"\\lease").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (acquired == INVALID_HANDLE_VALUE) { RemoveDirectoryW(candidate.c_str()); return false; }
    bool written = false;
    {
        Handle owner{CreateFileW((candidate + L"\\owner").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr)};
        DWORD bytes = 0;
        written = owner.value != INVALID_HANDLE_VALUE && WriteFile(owner.value, marker, sizeof(marker), &bytes, nullptr) && bytes == sizeof(marker);
    }
    if (!written) {
        CloseHandle(acquired);
        DeleteFileW((candidate + L"\\owner").c_str()); DeleteFileW((candidate + L"\\lease").c_str());
        RemoveDirectoryW(candidate.c_str()); return false;
    }
    directory = candidate; file = candidate + L"\\PulseSetup.exe"; lease = acquired;
    return true;
}
bool RemoveUpdateStage(const std::wstring& directory) {
    if (!StageName(directory)) return false;
    {
        Handle pin{Pin(directory)};
        if (pin.value == INVALID_HANDLE_VALUE) return false;
        Handle lease{CreateFileW((directory + L"\\lease").c_str(), GENERIC_READ | DELETE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (lease.value == INVALID_HANDLE_VALUE) return false;
        {
            Handle owner{CreateFileW((directory + L"\\owner").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            if (owner.value == INVALID_HANDLE_VALUE) return false;
            BY_HANDLE_FILE_INFORMATION info{};
            char content[sizeof(marker) + 1]{}; DWORD bytes = 0;
            if (!GetFileInformationByHandle(owner.value, &info) ||
                (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
                !ReadFile(owner.value, content, sizeof(content), &bytes, nullptr) || bytes != sizeof(marker) ||
                memcmp(content, marker, sizeof(marker))) return false;
        }
        WIN32_FIND_DATAW entry{};
        HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) return false;
        bool owned = true;
        do {
            const std::wstring name = entry.cFileName;
            if (name == L"." || name == L"..") continue;
            if ((name != L"owner" && name != L"lease" && name != L"PulseSetup.exe") ||
                (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { owned = false; break; }
        } while (FindNextFileW(find, &entry));
        const DWORD enumeration_error = GetLastError();
        FindClose(find);
        if (!owned || enumeration_error != ERROR_NO_MORE_FILES) return false;
        if (!DeleteOwnedFile(directory + L"\\PulseSetup.exe") || !DeleteOwnedFile(directory + L"\\owner")) return false;
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(lease.value, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
    }
    return RemoveDirectoryW(directory.c_str()) != FALSE;
}
void SweepUpdateStages(const std::wstring& data_root) {
    if (data_root.empty()) return;
    const auto root = data_root + L"\\UpdateStaging";
    Handle root_pin{Pin(root)};
    if (root_pin.value == INVALID_HANDLE_VALUE) return;
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return;
    int examined = 0;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            RemoveUpdateStage(root + L"\\" + entry.cFileName);
    } while (++examined < 128 && FindNextFileW(find, &entry));
    FindClose(find);
}
void CleanupAbandonedUpdateStagesAsync() noexcept {
    try {
        std::thread([] {
            try {
                const std::wstring root = storage::DataRootPath();
                if (!root.empty()) SweepUpdateStages(root);
                // Releases before the data root moved staged installers under
                // the legacy tree; sweep those leftovers once as well.
                const std::wstring legacy = storage::LegacyDataRootPath();
                if (!legacy.empty() && legacy != root) SweepUpdateStages(legacy);
            } catch (...) {
                OutputDebugStringW(L"Pulse update-stage cleanup failed; retained stages will be retried on next startup.\n");
            }
        }).detach();
    } catch (...) {
        OutputDebugStringW(L"Pulse update-stage cleanup could not start; startup will continue.\n");
    }
}
}
