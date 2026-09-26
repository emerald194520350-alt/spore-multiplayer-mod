// SPDX-License-Identifier: GPL-3.0-or-later
// The second profile contains launch-prepared transport copies. Keep those
// records available for LoadGame, but out of its galaxy menu's saved-world list.
using GalaxySavedWorldsFunction=void(__thiscall*)(void*);
GalaxySavedWorldsFunction gGalaxySavedWorldsOriginal=nullptr;
bool gBorrowedCampaignHook=false, gBorrowedCampaignsCaptured=false;
std::set<std::uint32_t> gBorrowedStarIds;
std::vector<cStarRecordPtr> gHiddenCampaigns;

bool HasPreparedCampaigns()
{
    if (!IsProfile2()) return false;
    wchar_t roaming[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA",roaming,MAX_PATH)) return false;
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(roaming)/L"SporeCoop2"/
        L"Games"/L"SporeCoop-prepared-copies.txt",error);
}

void RestoreBorrowedCampaigns()
{
    auto manager=Simulator::cStarManager::Get();
    if (!manager) return;
    auto& records=manager->mSavedGameStarRecords;
    for (const auto& hidden:gHiddenCampaigns) {
        const auto id=hidden->GetID().internalValue;
        const bool present=std::any_of(records.begin(),records.end(),[id](const auto& p) {
            return p && p->GetID().internalValue==id;
        });
        if (!present) records.push_back(hidden);
    }
    gHiddenCampaigns.clear();
}

void HideBorrowedCampaigns()
{
    if (!HasPreparedCampaigns()) return;
    auto manager=Simulator::cStarManager::Get();
    if (!manager) return;
    auto& records=manager->mSavedGameStarRecords;
    if (!gBorrowedCampaignsCaptured) {
        for (const auto& record:records) if (record) gBorrowedStarIds.insert(record->GetID().internalValue);
        gBorrowedCampaignsCaptured=true;
    }
    for (auto it=records.begin();it!=records.end();) {
        if (*it && gBorrowedStarIds.count((*it)->GetID().internalValue)) {
            gHiddenCampaigns.push_back(*it);
            it=records.erase(it);
        } else ++it;
    }
}

void __fastcall GalaxySavedWorldsHook(void* self,void*)
{
    // DF95A0 builds UI entries from the saved-record vector. Scope the filter
    // to this call: persistence, uniqueness checks and loading see all records.
    HideBorrowedCampaigns();
    struct Restore { ~Restore(){RestoreBorrowedCampaigns();} } restore;
    gGalaxySavedWorldsOriginal(self);
}
