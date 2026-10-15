#pragma once
#include <windows.h>
#include <string>
namespace pulse::app {
// Stages live under <data root>\UpdateStaging; data_root is the resolved
// Pulse data directory (see common/data_root.h), not LocalAppData itself.
bool CreateUpdateStage(const std::wstring& data_root, std::wstring& directory,
                       std::wstring& file, HANDLE& lease);
bool RemoveUpdateStage(const std::wstring& directory);
void SweepUpdateStages(const std::wstring& data_root);
void CleanupAbandonedUpdateStagesAsync() noexcept;
}
