// SPDX-License-Identifier: GPL-3.0-or-later
// Included by Probe.cpp after the binary codec helpers.
std::uint32_t gNamedCampaign=0;
std::string gCampaignName;
bool gCampaignNameKnown=false;

std::filesystem::path CampaignNamePath(std::uint32_t gameID)
{
    wchar_t roaming[MAX_PATH]{};
    if (!gameID || !GetEnvironmentVariableW(L"APPDATA",roaming,MAX_PATH)) return {};
    return std::filesystem::path(roaming)/(IsProfile2()?L"SporeCoop2":L"Spore")/
        L"Games"/L"Game0"/(L"SporeCoop-name-"+std::to_wstring(gameID)+L".txt");
}

void ObserveCampaignName()
{
    if (Simulator::GetGameModeID()==GameModeIDs::kGGEMode) {
        gNamedCampaign=0; gCampaignName.clear(); gCampaignNameKnown=false;
        return;
    }
    if (!Simulator::IsCellGame()) return;
    auto game=Simulator::Cell::cCellGame::Get();
    auto data=game ? game->mpSerializableData.get() : nullptr;
    if (!data || !data->gameID || data->gameID==gNamedCampaign) return;
    gNamedCampaign=data->gameID; gCampaignName.clear(); gCampaignNameKnown=false;
    std::ifstream file(CampaignNamePath(gNamedCampaign),std::ios::binary);
    if (!file) return;
    std::string wire;
    for (char c; file.get(c);) { if (wire.size()>=1368) return; wire.push_back(c); }
    std::vector<unsigned char> bytes;
    if (!wire.empty() && !Base64Decode(wire,bytes)) return;
    if (bytes.size()%2 || bytes.size()>1024) return;
    gCampaignName=wire; gCampaignNameKnown=true;
}

std::string CampaignSpeciesName()
{
    ObserveCampaignName();
    return gCampaignName;
}

void SaveCampaignName()
{
    ObserveCampaignName();
    if (!gCampaignNameKnown || !gNamedCampaign) return;
    const auto path=CampaignNamePath(gNamedCampaign), temporary=std::filesystem::path(path.wstring()+L".tmp");
    { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
      if (!file || !file.write(gCampaignName.data(),gCampaignName.size())) return; }
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        WriteProbeLog("Campaign species name metadata could not be saved.");
}
