#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "CoopNet.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr int kMaxFrame = 1024 * 1024;
    constexpr int kProtocol = 6;
    constexpr const char* kFingerprint =
        "3d81f0d5819a6b2f20260916c011a1b54a55a7a94c5cb596d56f1412cc17e220";

    std::atomic<bool> gStop{ false };
    std::atomic<bool> gStarted{ false };
    std::atomic<SOCKET> gSocket{ INVALID_SOCKET };
    HANDLE gThread = nullptr;
    std::mutex gMutex;
    CoopNet::Snapshot gSnapshot;
    std::deque<std::string> gOutgoing;
    std::string gRole;
    std::string gToken;
    std::string gAddress;
    unsigned short gPort = 5523;
    std::uint64_t gPositionSequence = 0;
    std::uint64_t gEventSequence = 0;
    std::uint64_t gSpeciesSequence = 1;
    std::uint64_t gNpcSequence = 0;
    std::uint64_t gWorldActionSequence = 0;
    std::vector<CoopNet::WorldAction> gWorldActions;

    std::string Environment(const char* name)
    {
        char buffer[512]{};
        const DWORD length = GetEnvironmentVariableA(name, buffer,
            static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer)) return {};
        return std::string(buffer, buffer + length);
    }

    std::string JsonEscape(const std::string& value)
    {
        std::string result;
        result.reserve(value.size() + 16);
        for (unsigned char c : value)
        {
            switch (c)
            {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char escaped[8]{};
                    sprintf_s(escaped, "\\u%04x", static_cast<unsigned>(c));
                    result += escaped;
                }
                else result.push_back(static_cast<char>(c));
                break;
            }
        }
        return result;
    }

    void Queue(std::string message)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        // Movement/population snapshots supersede older ones. Inventory and
        // invitation events must never be silently evicted by movement traffic.
        const char* realtime = message.rfind("{\"type\":\"position\"", 0) == 0 ? "{\"type\":\"position\"" :
            message.rfind("{\"type\":\"npcSnapshot\"", 0) == 0 ? "{\"type\":\"npcSnapshot\"" : nullptr;
        if (realtime)
            for (auto it = gOutgoing.begin(); it != gOutgoing.end();)
                if (it->rfind(realtime, 0) == 0) it = gOutgoing.erase(it); else ++it;
        if (gOutgoing.size() >= 256)
        {
            gSnapshot.lastError = "Outgoing reliable queue overflow; reconnect required";
            const SOCKET socket = gSocket.load();
            if (socket != INVALID_SOCKET) shutdown(socket, SD_BOTH);
            return;
        }
        gOutgoing.push_back(std::move(message));
    }

    bool SendAll(SOCKET socket, const char* data, int size)
    {
        int offset = 0;
        while (offset < size && !gStop.load())
        {
            const int sent = send(socket, data + offset, size - offset, 0);
            if (sent <= 0) return false;
            offset += sent;
        }
        return offset == size;
    }

    bool SendFrame(SOCKET socket, const std::string& json)
    {
        if (json.size() < 2 || json.size() > kMaxFrame) return false;
        const std::uint32_t length = static_cast<std::uint32_t>(json.size());
        unsigned char prefix[4]{
            static_cast<unsigned char>(length & 0xff),
            static_cast<unsigned char>((length >> 8) & 0xff),
            static_cast<unsigned char>((length >> 16) & 0xff),
            static_cast<unsigned char>((length >> 24) & 0xff)
        };
        return SendAll(socket, reinterpret_cast<const char*>(prefix), 4) &&
            SendAll(socket, json.data(), static_cast<int>(json.size()));
    }

    bool FindValueStart(const std::string& json, const char* key, size_t& position)
    {
        const std::string token = std::string("\"") + key + "\":";
        position = json.find(token);
        if (position == std::string::npos) return false;
        position += token.size();
        while (position < json.size() &&
            (json[position] == ' ' || json[position] == '\t' ||
                json[position] == '\r' || json[position] == '\n')) ++position;
        return position < json.size();
    }

    bool ReadString(const std::string& json, const char* key, std::string& value)
    {
        size_t position = 0;
        if (!FindValueStart(json, key, position) || json[position] != '"') return false;
        ++position;
        std::string result;
        while (position < json.size())
        {
            const char c = json[position++];
            if (c == '"') { value = std::move(result); return true; }
            if (c != '\\') { result.push_back(c); continue; }
            if (position >= json.size()) return false;
            const char escaped = json[position++];
            switch (escaped)
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: return false;
            }
        }
        return false;
    }

    bool ReadNumber(const std::string& json, const char* key, double& value)
    {
        size_t position = 0;
        if (!FindValueStart(json, key, position)) return false;
        char* end = nullptr;
        errno = 0;
        const double parsed = std::strtod(json.c_str() + position, &end);
        if (end == json.c_str() + position || errno == ERANGE || !std::isfinite(parsed))
            return false;
        value = parsed;
        return true;
    }

    bool ReadBool(const std::string& json, const char* key, bool& value)
    {
        size_t position = 0;
        if (!FindValueStart(json, key, position)) return false;
        if (json.compare(position, 4, "true") == 0) { value = true; return true; }
        if (json.compare(position, 5, "false") == 0) { value = false; return true; }
        return false;
    }

    bool ObjectHasKey(const std::string& json, const char* objectName,
        const char* key, bool& present)
    {
        size_t position = 0;
        if (!FindValueStart(json, objectName, position) || json[position] != '{')
            return false;
        const size_t objectStart = position;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (; position < json.size(); ++position)
        {
            const char c = json[position];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0)
            {
                const std::string token = std::string("\"") + key + "\":";
                const size_t found = json.find(token, objectStart + 1);
                present = found != std::string::npos && found < position;
                return true;
            }
        }
        return false;
    }

    void ClearRemotePeerStateLocked()
    {
        gSnapshot.remotePeerConnected = false;
        gSnapshot.hasRemotePosition = false;
        gSnapshot.remoteX = 0.0f;
        gSnapshot.remoteY = 0.0f;
        gSnapshot.remoteZ = 0.0f;
        gSnapshot.remotePositionSequence = 0;
        gSnapshot.remotePositionReceivedTick = 0;
        gSnapshot.hasRemoteAppearance = false;
        gSnapshot.remoteModelInstance = 0;
        gSnapshot.remoteModelType = 0;
        gSnapshot.remoteModelGroup = 0;
        gSnapshot.remoteCellResource = 0;
        gSnapshot.remoteScale = 1.0f;
        gSnapshot.remoteTargetSize = 1.0f;
        gSnapshot.remoteOpacity = 1.0f;
        gSnapshot.remotePose = CoopNet::CellPose{};
        gSnapshot.remoteAppearanceSequence = 0;
        gSnapshot.remoteAppearanceModelInstance = 0;
        gSnapshot.remoteAppearanceModelType = 0;
        gSnapshot.remoteAppearanceModelGroup = 0;
        gSnapshot.remoteAppearanceBlob.clear();
        gSnapshot.npcSequence = 0;
        gSnapshot.npcReceivedTick = 0;
        gSnapshot.remoteNpcs.clear();
        gSnapshot.worldActionAck = 0;
        gWorldActions.clear();
    }

    bool ReadNumberArray(const std::string& json, const char* key,
        std::vector<double>& values, size_t expected)
    {
        size_t position = 0;
        if (!FindValueStart(json, key, position) || json[position] != '[') return false;
        ++position;
        std::vector<double> result;
        while (position < json.size())
        {
            while (position < json.size() &&
                (json[position] == ' ' || json[position] == '\t' ||
                    json[position] == '\r' || json[position] == '\n')) ++position;
            if (position < json.size() && json[position] == ']')
            {
                if (expected && result.size() != expected) return false;
                values = std::move(result);
                return true;
            }
            char* end = nullptr;
            errno = 0;
            const double number = std::strtod(json.c_str() + position, &end);
            if (end == json.c_str() + position || errno == ERANGE || !std::isfinite(number))
                return false;
            result.push_back(number);
            if (result.size() > 4094 * 18) return false;
            position = static_cast<size_t>(end - json.c_str());
            while (position < json.size() && json[position] == ' ') ++position;
            if (position < json.size() && json[position] == ',') { ++position; continue; }
            if (position < json.size() && json[position] == ']') continue;
            return false;
        }
        return false;
    }

    void HandleMessage(const std::string& json)
    {
        std::string type;
        if (!ReadString(json, "type", type)) return;

        if (type == "sessionEnded")
        {
            std::string reason;
            if (!ReadString(json, "reason", reason) || reason != "host_left") return;
            std::lock_guard<std::mutex> lock(gMutex);
            gSnapshot.sessionEnded = true;
            gSnapshot.disconnectReason = reason;
            gSnapshot.connected = false;
            return;
        }

        if (type == "welcome")
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gSnapshot.connected = true;
            ++gSnapshot.connectionGeneration;
            gSnapshot.lastError.clear();
            ClearRemotePeerStateLocked();
            gSnapshot.invitePending = false;
            gSnapshot.inviteAccepted = false;
            gSnapshot.inviteFrom.clear();
            gSnapshot.worldGeneration = 0;
            gSnapshot.hostPaused = false;
            gSnapshot.progressInitialized = false;
            gSnapshot.progressAckSequence = 0;
            gSnapshot.revision = 0;
            gSnapshot.progress = CoopNet::CellProgress{};
            gSnapshot.editorOpen = false;
            gSnapshot.editorID = 0;
            gSnapshot.editorRole.clear();
            gSnapshot.speciesSequence = 0;
            gSnapshot.speciesAck = 0; gSnapshot.speciesConflict = false;
            gSnapshot.speciesBlob.clear();
            gSnapshot.editorBudget = -1;
            gSnapshot.speciesName.clear(); gSnapshot.editorFinished=false; gSnapshot.editorSession=0;
            return;
        }

        if (type == "error")
        {
            std::string message;
            if (!ReadString(json, "message", message)) return;
            std::lock_guard<std::mutex> lock(gMutex);
            gSnapshot.lastError = std::move(message);
            return;
        }

        if (type == "position")
        {
            std::string role;
            std::vector<double> position;
            double sequence = 0;
            if (!ReadString(json, "role", role) || role == gRole ||
                !ReadNumber(json, "sequence", sequence) ||
                sequence < 0 || sequence >= 9223372036854775808.0 || std::floor(sequence) != sequence ||
                !ReadNumberArray(json, "position", position, 3)) return;
            CoopNet::CellPose pose;
            pose.x = float(position[0]); pose.y = float(position[1]); pose.z = float(position[2]);
            double poseValue = 0;
            if (ReadNumber(json, "scale", poseValue)) pose.scale = float(poseValue);
            std::vector<double> renderPosition, orientation;
            size_t fieldStart = 0;
            if (FindValueStart(json, "renderPosition", fieldStart))
            {
                if (!ReadNumberArray(json, "renderPosition", renderPosition, 3)) return;
                for (const auto component : renderPosition) if (std::abs(component) > 1000000) return;
                pose.x = float(renderPosition[0]); pose.y = float(renderPosition[1]); pose.z = float(renderPosition[2]);
            }
            if (FindValueStart(json, "orientation", fieldStart))
            {
                if (!ReadNumberArray(json, "orientation", orientation, 4)) return;
                double norm = 0;
                for (const auto component : orientation) norm += component * component;
                if (norm < 0.99 || norm > 1.01) return;
                pose.qx = float(orientation[0]); pose.qy = float(orientation[1]);
                pose.qz = float(orientation[2]); pose.qw = float(orientation[3]);
            }
            if (FindValueStart(json, "renderScale", fieldStart))
            {
                if (!ReadNumber(json, "renderScale", poseValue) || poseValue < 0.001 || poseValue > 100000) return;
                pose.scale = float(poseValue);
            }
            if (FindValueStart(json, "animation", fieldStart))
            {
                if (!ReadNumber(json, "animation", poseValue) || poseValue < 0 || poseValue > UINT32_MAX ||
                    std::floor(poseValue) != poseValue) return;
                pose.animation = std::uint32_t(poseValue);
            }
            if (FindValueStart(json, "visible", fieldStart) && !ReadBool(json, "visible", pose.visible)) return;
            std::lock_guard<std::mutex> lock(gMutex);
            if (gSnapshot.hasRemotePosition && std::uint64_t(sequence) <= gSnapshot.remotePositionSequence) return;
            gSnapshot.hasRemotePosition = true;
            gSnapshot.remoteX = static_cast<float>(position[0]);
            gSnapshot.remoteY = static_cast<float>(position[1]);
            gSnapshot.remoteZ = static_cast<float>(position[2]);
            gSnapshot.remotePositionSequence = static_cast<std::uint64_t>(sequence);
            gSnapshot.remotePositionReceivedTick = GetTickCount64();
            double value = 0;
            if (ReadNumber(json, "modelInstance", value)) gSnapshot.remoteModelInstance = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "modelType", value)) gSnapshot.remoteModelType = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "modelGroup", value)) gSnapshot.remoteModelGroup = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "cellResource", value)) gSnapshot.remoteCellResource = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "scale", value)) gSnapshot.remoteScale = static_cast<float>(value);
            if (ReadNumber(json, "targetSize", value)) gSnapshot.remoteTargetSize = static_cast<float>(value);
            if (ReadNumber(json, "opacity", value)) gSnapshot.remoteOpacity = static_cast<float>(value);
            gSnapshot.remotePose = pose;
            return;
        }

        if (type == "appearance")
        {
            std::string role;
            std::string appearance;
            double sequence = 0;
            double value = 0;
            if (!ReadString(json, "role", role) || role == gRole ||
                !ReadString(json, "appearance", appearance) ||
                !ReadNumber(json, "sequence", sequence)) return;
            std::lock_guard<std::mutex> lock(gMutex);
            if (gSnapshot.hasRemoteAppearance &&
                static_cast<std::uint64_t>(sequence) <= gSnapshot.remoteAppearanceSequence)
                return;
            gSnapshot.remoteAppearanceSequence = static_cast<std::uint64_t>(sequence);
            gSnapshot.remoteAppearanceBlob = std::move(appearance);
            if (ReadNumber(json, "modelInstance", value))
                gSnapshot.remoteAppearanceModelInstance = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "modelType", value))
                gSnapshot.remoteAppearanceModelType = static_cast<std::uint32_t>(value);
            if (ReadNumber(json, "modelGroup", value))
                gSnapshot.remoteAppearanceModelGroup = static_cast<std::uint32_t>(value);
            gSnapshot.hasRemoteAppearance = gSnapshot.remoteAppearanceModelInstance != 0;
            return;
        }

        if (type == "npcSnapshot")
        {
            std::string role;
            double sequence = 0;
            std::vector<double> values;
            if (!ReadString(json, "role", role) || role == gRole ||
                !ReadNumber(json, "sequence", sequence) || sequence < 0 ||
                sequence >= 9223372036854775808.0 || std::floor(sequence) != sequence ||
                !ReadNumberArray(json, "npcs", values, 0) || values.size() % 18 != 0)
                return;
            std::vector<CoopNet::NpcState> npcs;
            npcs.reserve(values.size() / 18);
            std::vector<std::uint32_t> ids;
            for (size_t i = 0; i < values.size(); i += 18)
            {
                if (values[i] < 0 || values[i] > UINT32_MAX || std::floor(values[i]) != values[i] ||
                    values[i + 1] <= 0 || values[i + 1] > UINT32_MAX || std::floor(values[i + 1]) != values[i + 1] ||
                    std::abs(values[i + 2]) > 1000000 || std::abs(values[i + 3]) > 1000000 ||
                    std::abs(values[i + 4]) > 1000000 || values[i + 7] < 0.001 ||
                    values[i + 7] > 100000 || values[i + 8] < 0 ||
                    values[i + 8] > 100000 || values[i + 9] < 0 || values[i + 9] > 1 ||
                    values[i + 10] < -1 || values[i + 10] > 19 ||
                    std::floor(values[i + 10]) != values[i + 10]) return;
                const double orientationNorm = values[i + 5] * values[i + 5] +
                    values[i + 6] * values[i + 6];
                if (orientationNorm < 0.99 || orientationNorm > 1.01) return;
                for (size_t field = 11; field < 14; ++field)
                    if (values[i + field] < 0 || values[i + field] > UINT32_MAX ||
                        std::floor(values[i + field]) != values[i + field]) return;
                if (values[i+14] < 0 || values[i+14] > 1000000 || std::floor(values[i+14]) != values[i+14] ||
                    values[i+15] < 0 || values[i+15] > 125 || std::floor(values[i+15]) != values[i+15] ||
                    (values[i+16] != 0 && values[i+16] != 1) || std::abs(values[i+17]) > 1000000) return;
                const auto id = static_cast<std::uint32_t>(values[i]);
                if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
                ids.push_back(id);
                CoopNet::NpcState npc;
                npc.id = static_cast<std::uint32_t>(values[i]);
                npc.cellResource = static_cast<std::uint32_t>(values[i + 1]);
                npc.x = static_cast<float>(values[i + 2]);
                npc.y = static_cast<float>(values[i + 3]);
                npc.z = static_cast<float>(values[i + 4]);
                npc.qz = static_cast<float>(values[i + 5]);
                npc.qw = static_cast<float>(values[i + 6]);
                npc.scale = static_cast<float>(values[i + 7]);
                npc.targetSize = static_cast<float>(values[i + 8]);
                npc.opacity = static_cast<float>(values[i + 9]);
                npc.stageScale = static_cast<int>(values[i + 10]);
                npc.modelInstance = static_cast<std::uint32_t>(values[i + 11]);
                npc.modelType = static_cast<std::uint32_t>(values[i + 12]);
                npc.modelGroup = static_cast<std::uint32_t>(values[i + 13]);
                npc.health=int(values[i+14]); npc.animation=std::uint32_t(values[i+15]);
                npc.dead=values[i+16]!=0; npc.elevation=float(values[i+17]);
                npcs.push_back(npc);
            }
            std::lock_guard<std::mutex> lock(gMutex);
            if (static_cast<std::uint64_t>(sequence) <= gSnapshot.npcSequence &&
                gSnapshot.npcReceivedTick != 0) return;
            gSnapshot.npcSequence = static_cast<std::uint64_t>(sequence);
            gSnapshot.npcReceivedTick = GetTickCount64();
            gSnapshot.remoteNpcs = std::move(npcs);
            double ack=0; if (ReadNumber(json,"actionAck",ack)) gSnapshot.worldActionAck=std::uint64_t(ack);
            return;
        }

        if (type == "worldAction")
        {
            std::string role; double seq=0,id=0,resource=0,damage=0;
            CoopNet::WorldAction action;
            if (!ReadString(json,"role",role) || role==gRole ||
                !ReadNumber(json,"sequence",seq) || seq < 1 ||
                !ReadNumber(json,"id",id) || id < 0 || id > UINT32_MAX ||
                !ReadNumber(json,"resource",resource) || resource < 1 || resource > UINT32_MAX ||
                !ReadNumber(json,"damage",damage) || damage < 0 || damage > 1000000 ||
                !ReadBool(json,"removed",action.removed) || !ReadBool(json,"effects",action.effects)) return;
            action.sequence=std::uint64_t(seq); action.id=std::uint32_t(id);
            action.resource=std::uint32_t(resource); action.damage=int(damage);
            std::lock_guard<std::mutex> lock(gMutex);
            if (gWorldActions.size()<4096) gWorldActions.push_back(action);
            return;
        }

        if (type == "state")
        {
            double number = 0;
            bool initialized = false;
            CoopNet::CellProgress progress;
            std::vector<double> unlocks;
            ReadBool(json, "progressInitialized", initialized);
            std::uint64_t revision = 0;
            if (ReadNumber(json, "revision", number)) revision = static_cast<std::uint64_t>(number);
            std::uint64_t acknowledged = 0;
            if (ReadNumber(json, gRole == "host" ? "hostProgressSequence" : "guestProgressSequence", number))
                acknowledged = static_cast<std::uint64_t>(number);
            if (ReadNumber(json, "food", number)) progress.food = static_cast<int>(number);
            if (ReadNumber(json, "plantFood", number)) progress.plantFood = static_cast<int>(number);
            if (ReadNumber(json, "overPlantFood", number)) progress.overPlantFood = static_cast<int>(number);
            if (ReadNumber(json, "overAnimalFood", number)) progress.overAnimalFood = static_cast<int>(number);
            if (ReadNumber(json, "spent", number)) progress.spent = static_cast<int>(number);
            if (ReadNumberArray(json, "unlocks", unlocks, 13))
                for (size_t i = 0; i < 13; ++i) progress.unlocks[i] = static_cast<int>(unlocks[i]);
            std::vector<double> missions;
            if (ReadNumberArray(json, "missions", missions, 24))
                for (size_t i = 0; i < 24; ++i) progress.missions[i] = static_cast<int>(missions[i]);
            if (ReadNumber(json, "killCount", number)) progress.killCount = static_cast<int>(number);
            ReadBool(json, "playerHasMoved", progress.playerHasMoved);
            ReadBool(json, "playerHasEaten", progress.playerHasEaten);
            ReadBool(json, "partCinematicPlayed", progress.partCinematicPlayed);
            ReadBool(json, "showMateButton", progress.showMateButton);
            ReadBool(json, "firstEditorEntry", progress.firstEditorEntry);

            bool evolving = false;
            ReadBool(json, "evolving", evolving);
            bool invitePending = false;
            bool inviteAccepted = false;
            ReadBool(json, "invitePending", invitePending);
            ReadBool(json, "inviteAccepted", inviteAccepted);
            bool hostPaused = false;
            ReadBool(json, "hostPaused", hostPaused);
            std::string inviteFrom;
            ReadString(json, "inviteFrom", inviteFrom);
            std::uint64_t worldGeneration = 0;
            if (ReadNumber(json, "worldGeneration", number)) worldGeneration = static_cast<std::uint64_t>(number);
            std::string editorRole;
            ReadString(json, "editor", editorRole);
            std::uint32_t editorID = 0;
            if (ReadNumber(json, "editorId", number)) editorID = static_cast<std::uint32_t>(number);
            std::string species;
            ReadString(json, "species", species);
            std::uint64_t speciesSequence = 0;
            if (ReadNumber(json, "speciesSequence", number))
                speciesSequence = static_cast<std::uint64_t>(number);
            std::string speciesName; ReadString(json,"speciesName",speciesName);
            bool editorFinished=false; ReadBool(json,"editorFinished",editorFinished);
            double editorSession=0; ReadNumber(json,"editorSession",editorSession);
            int editorBudget = -1;
            if (ReadNumber(json,"editorBudget",number) && number >= 0 && number <= 100000000 && std::floor(number)==number)
                editorBudget = static_cast<int>(number);
            const char* remoteRole = gRole == "host" ? "guest" : "host";
            bool remotePeerPresent = false;
            const bool hasPlayers = ObjectHasKey(json, "players", remoteRole,
                remotePeerPresent);

            std::lock_guard<std::mutex> lock(gMutex);
            if (worldGeneration != gSnapshot.worldGeneration)
            {
                gSnapshot.remoteNpcs.clear(); gSnapshot.npcReceivedTick=0;
                gSnapshot.npcSequence=0; gSnapshot.worldActionAck=0; gWorldActions.clear();
                // The server resets species revisions on every accepted new
                // invitation. A previous campaign's larger revision must not
                // reject the new entry body or cause endless base conflicts.
                gSnapshot.speciesSequence = 0;
                gSnapshot.speciesBlob.clear();
                gSnapshot.editorBudget = -1;
                gSnapshot.speciesName.clear(); gSnapshot.editorFinished=false; gSnapshot.editorSession=0;
                gSnapshot.speciesAck = 0;
                gSnapshot.speciesConflict = false;
            }
            if (hasPlayers && !remotePeerPresent) ClearRemotePeerStateLocked();
            else if (hasPlayers)
            {
                if (!gSnapshot.remotePeerConnected) ++gSnapshot.remotePeerGeneration;
                gSnapshot.remotePeerConnected = true;
            }
            gSnapshot.progressInitialized = initialized;
            // Revision, acknowledgement and progress must become visible in
            // one mutex transaction. A mixed snapshot can lose local events.
            gSnapshot.revision = revision;
            gSnapshot.progressAckSequence = acknowledged;
            if (initialized) gSnapshot.progress = progress;
            gSnapshot.editorOpen = evolving;
            gSnapshot.invitePending = invitePending;
            gSnapshot.inviteAccepted = inviteAccepted;
            gSnapshot.inviteFrom = inviteFrom;
            gSnapshot.worldGeneration = worldGeneration;
            gSnapshot.hostPaused = hostPaused;
            gSnapshot.editorRole = editorRole;
            gSnapshot.editorID = editorID;
            if (speciesSequence >= gSnapshot.speciesSequence)
            {
                gSnapshot.speciesSequence = speciesSequence;
                gSnapshot.speciesBlob = std::move(species);
                gSnapshot.editorBudget = editorBudget;
                gSnapshot.speciesName=speciesName;
                gSnapshot.editorSession=static_cast<std::uint64_t>(editorSession);
                gSnapshot.editorFinished=editorFinished;
            }
            return;
        }

        if (type == "editorOpen")
        {
            std::string role;
            double editorID = 0;
            if (!ReadString(json, "role", role) || !ReadNumber(json, "editorId", editorID)) return;
            std::lock_guard<std::mutex> lock(gMutex);
            gSnapshot.editorOpen = true;
            gSnapshot.editorRole = role;
            gSnapshot.editorID = static_cast<std::uint32_t>(editorID);
            return;
        }

        if (type == "speciesLive" || type == "speciesConflict" || type == "editorClosed")
        {
            std::string role;
            std::string species;
            double sequence = 0;
            if (!ReadString(json, "role", role) ||
                !ReadString(json, "species", species) ||
                !ReadNumber(json, "sequence", sequence)) return;
            std::string name; ReadString(json,"speciesName",name);
            bool finished=false; ReadBool(json,"editorFinished",finished);
            double session=0; ReadNumber(json,"editorSession",session);
            double budget = -1;
            if (!ReadNumber(json,"editorBudget",budget) || budget < 0 || budget > 100000000 || std::floor(budget)!=budget) return;
            std::lock_guard<std::mutex> lock(gMutex);
            if (type=="editorClosed" && (static_cast<std::uint64_t>(sequence)<gSnapshot.speciesSequence ||
                static_cast<std::uint64_t>(session)!=gSnapshot.editorSession)) return;
            if (static_cast<std::uint64_t>(sequence) >= gSnapshot.speciesSequence)
            {
                gSnapshot.speciesSequence = static_cast<std::uint64_t>(sequence);
                gSnapshot.speciesBlob = std::move(species);
                gSnapshot.editorBudget = static_cast<int>(budget);
                gSnapshot.speciesName=name;
                gSnapshot.editorSession=static_cast<std::uint64_t>(session);
                gSnapshot.editorFinished=finished;
            }
            if (role == gRole && type != "editorClosed")
            {
                double ack=0; if(ReadNumber(json,"clientSequence",ack)) gSnapshot.speciesAck=std::uint64_t(ack);
                gSnapshot.speciesConflict=type=="speciesConflict";
            }
            if (type == "editorClosed") gSnapshot.editorOpen = false;
        }
    }

    bool DrainMessages(SOCKET socket)
    {
        std::deque<std::string> messages;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            if (!gSnapshot.connected) return true;
            messages.swap(gOutgoing);
        }
        for (const auto& message : messages)
            if (!SendFrame(socket, message)) return false;
        return true;
    }

    void MarkDisconnected()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gSnapshot.inviteAccepted && !gSnapshot.sessionEnded)
        {
            gSnapshot.sessionEnded = true;
            gSnapshot.disconnectReason = "connection_lost";
        }
        gSnapshot.connected = false;
        ClearRemotePeerStateLocked();
        gOutgoing.clear();
        gSnapshot.invitePending = false;
        gSnapshot.inviteAccepted = false;
        gSnapshot.inviteFrom.clear();
        gSnapshot.hostPaused = false;
        gSnapshot.editorOpen = false;
        gSnapshot.editorID = 0;
        gSnapshot.editorRole.clear();
    }

    bool ConnectedLoop(SOCKET socket)
    {
        int timeout = 1000;
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        const std::string hello = "{\"type\":\"hello\",\"protocol\":" +
            std::to_string(kProtocol) + ",\"role\":\"" +
            JsonEscape(gRole) + "\",\"token\":\"" + JsonEscape(gToken) +
            "\",\"fingerprint\":\"" + kFingerprint + "\"}";
        if (!SendFrame(socket, hello)) return false;

        std::vector<unsigned char> input;
        input.reserve(65536);
        ULONG64 lastHeartbeat = GetTickCount64();
        ULONG64 lastReceived = lastHeartbeat;
        while (!gStop.load())
        {
            { std::lock_guard<std::mutex> lock(gMutex); if (gSnapshot.sessionEnded) return false; }
            if (!DrainMessages(socket)) return false;
            // Menus, pauses and loading screens do not submit movement. Keep
            // the session alive independently of the game's update callback.
            const ULONG64 now = GetTickCount64();
            if (now - lastReceived > 15000) return false;
            if (now - lastHeartbeat >= 4000)
            {
                if (!SendFrame(socket, "{\"type\":\"ping\"}")) return false;
                lastHeartbeat = now;
            }

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);
            timeval wait{ 0, 25000 };
            const int selected = select(0, &readSet, nullptr, nullptr, &wait);
            if (selected == SOCKET_ERROR) return false;
            if (selected == 0) continue;

            unsigned char chunk[16384];
            const int received = recv(socket, reinterpret_cast<char*>(chunk),
                static_cast<int>(sizeof(chunk)), 0);
            if (received <= 0) return false;
            lastReceived = GetTickCount64();
            input.insert(input.end(), chunk, chunk + received);

            while (input.size() >= 4)
            {
                const std::uint32_t length = static_cast<std::uint32_t>(input[0]) |
                    (static_cast<std::uint32_t>(input[1]) << 8) |
                    (static_cast<std::uint32_t>(input[2]) << 16) |
                    (static_cast<std::uint32_t>(input[3]) << 24);
                if (length < 2 || length > kMaxFrame) return false;
                if (input.size() < static_cast<size_t>(length) + 4) break;
                HandleMessage(std::string(input.begin() + 4, input.begin() + 4 + length));
                input.erase(input.begin(), input.begin() + 4 + length);
                { std::lock_guard<std::mutex> lock(gMutex); if (gSnapshot.sessionEnded) return false; }
            }
        }
        return true;
    }

    DWORD WINAPI NetworkThread(LPVOID)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;

        while (!gStop.load())
        {
            bool ended;
            { std::lock_guard<std::mutex> lock(gMutex); ended = gSnapshot.sessionEnded; }
            if (ended) { Sleep(100); continue; }
            SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket == INVALID_SOCKET) break;

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(gPort);
            if (inet_pton(AF_INET, gAddress.c_str(), &address.sin_addr) != 1 ||
                connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
            {
                closesocket(socket);
                for (int i = 0; i < 10 && !gStop.load(); ++i) Sleep(100);
                continue;
            }

            gSocket.store(socket);
            ConnectedLoop(socket);
            gSocket.store(INVALID_SOCKET);
            shutdown(socket, SD_BOTH);
            closesocket(socket);
            MarkDisconnected();
            for (int i = 0; i < 10 && !gStop.load(); ++i) Sleep(100);
        }

        WSACleanup();
        return 0;
    }

    std::string UnlockArray(const std::array<int, 13>& values)
    {
        std::string json = "[";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) json += ',';
            json += std::to_string(std::max(0, values[i]));
        }
        json += ']';
        return json;
    }

    std::string MissionArray(const std::array<int, 24>& values)
    {
        std::string json = "[";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) json += ',';
            json += std::to_string(std::max(0, values[i]));
        }
        json += ']';
        return json;
    }

    std::string ProgressFields(const CoopNet::CellProgress& value,
        const std::array<int, 13>& unlocks, bool signedSpent)
    {
        return "\"food\":" + std::to_string(std::max(0, value.food)) +
            ",\"plantFood\":" + std::to_string(std::max(0, value.plantFood)) +
            ",\"overPlantFood\":" + std::to_string(std::max(0, value.overPlantFood)) +
            ",\"overAnimalFood\":" + std::to_string(std::max(0, value.overAnimalFood)) +
            ",\"spent\":" + std::to_string(signedSpent ? value.spent : std::max(0, value.spent)) +
            ",\"unlocks\":" + UnlockArray(unlocks) +
            ",\"missions\":" + MissionArray(value.missions) +
            ",\"killCount\":" + std::to_string(std::max(0, value.killCount)) +
            ",\"playerHasMoved\":" + (value.playerHasMoved ? "true" : "false") +
            ",\"playerHasEaten\":" + (value.playerHasEaten ? "true" : "false") +
            ",\"partCinematicPlayed\":" + (value.partCinematicPlayed ? "true" : "false") +
            ",\"showMateButton\":" + (value.showMateButton ? "true" : "false") +
            ",\"firstEditorEntry\":" + (value.firstEditorEntry ? "true" : "false");
    }

    std::string NextEventID(const char* prefix)
    {
        return gRole + "-" + prefix + "-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(++gEventSequence);
    }
}

namespace CoopNet
{
    void AcknowledgeSessionEnd()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        // Cleanup must complete before allowing another handshake.
        if (gSocket.load() != INVALID_SOCKET) return;
        gSnapshot.sessionEnded = false;
        gSnapshot.disconnectReason.clear();
    }

    bool StartFromEnvironment()
    {
        if (gStarted.exchange(true)) return gSnapshot.enabled;
        gRole = Environment("SPORE_COOP_ROLE");
        if (gRole != "host" && gRole != "guest")
        {
            gSnapshot.enabled = false;
            return false;
        }
        gAddress = Environment("SPORE_COOP_SERVER");
        if (gAddress.empty()) gAddress = "127.0.0.1";
        gToken = Environment("SPORE_COOP_TOKEN");
        if (gToken.size() < 24)
        {
            gSnapshot.enabled = false;
            return false;
        }
        const std::string port = Environment("SPORE_COOP_PORT");
        if (!port.empty())
        {
            const long parsed = std::strtol(port.c_str(), nullptr, 10);
            if (parsed > 0 && parsed <= 65535) gPort = static_cast<unsigned short>(parsed);
        }

        gSnapshot.enabled = true;
        gStop.store(false);
        gThread = CreateThread(nullptr, 0, NetworkThread, nullptr, 0, nullptr);
        if (!gThread)
        {
            gSnapshot.enabled = false;
            return false;
        }
        return true;
    }

    void Stop()
    {
        gStop.store(true);
        const SOCKET socket = gSocket.exchange(INVALID_SOCKET);
        if (socket != INVALID_SOCKET) shutdown(socket, SD_BOTH);
        if (gThread)
        {
            WaitForSingleObject(gThread, 1500);
            CloseHandle(gThread);
            gThread = nullptr;
        }
    }

    Snapshot GetSnapshot()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        return gSnapshot;
    }

    const char* GetRole()
    {
        return gRole.c_str();
    }

    void SubmitPosition(float x, float y, float z,
        std::uint32_t modelInstance, std::uint32_t modelType,
        std::uint32_t modelGroup, std::uint32_t cellResource,
        float scale, float targetSize, float opacity, const CellPose* pose)
    {
        char json[640]{};
        sprintf_s(json,
            "{\"type\":\"position\",\"sequence\":%llu,\"position\":[%.6f,%.6f,%.6f],"
            "\"modelInstance\":%u,\"modelType\":%u,\"modelGroup\":%u,\"cellResource\":%u,"
            "\"scale\":%.6f,\"targetSize\":%.6f,\"opacity\":%.6f}",
            static_cast<unsigned long long>(gPositionSequence++), x, y, z,
            modelInstance, modelType, modelGroup, cellResource, scale, targetSize, opacity);
        std::string packet(json);
        if (pose)
        {
            char render[512]{};
            sprintf_s(render, ",\"renderPosition\":[%.6f,%.6f,%.6f],\"orientation\":[%.6f,%.6f,%.6f,%.6f],\"renderScale\":%.6f,\"animation\":%u,\"visible\":%s}",
                pose->x, pose->y, pose->z, pose->qx, pose->qy, pose->qz, pose->qw,
                pose->scale, pose->animation, pose->visible ? "true" : "false");
            packet.pop_back(); packet += render;
        }
        Queue(std::move(packet));
    }

    void SubmitAppearance(std::uint32_t modelInstance, std::uint32_t modelType,
        std::uint32_t modelGroup, const std::string& appearanceBlob)
    {
        Queue("{\"type\":\"appearance\",\"sequence\":" +
            std::to_string(gSpeciesSequence++) +
            ",\"modelInstance\":" + std::to_string(modelInstance) +
            ",\"modelType\":" + std::to_string(modelType) +
            ",\"modelGroup\":" + std::to_string(modelGroup) +
            ",\"appearance\":\"" + JsonEscape(appearanceBlob) + "\"}");
    }

    void SubmitInvite()
    {
        Queue("{\"type\":\"invite\"}");
    }

    void SubmitInviteResponse(bool accepted)
    {
        Queue(std::string("{\"type\":\"inviteResponse\",\"accepted\":") +
            (accepted ? "true}" : "false}"));
    }

    void SubmitHostPause(bool paused)
    {
        Queue(std::string("{\"type\":\"hostPause\",\"paused\":") +
            (paused ? "true}" : "false}"));
    }

    void SubmitNpcSnapshot(const std::vector<NpcState>& npcs, std::uint64_t actionAck)
    {
        std::string packet = "{\"type\":\"npcSnapshot\",\"sequence\":" +
            std::to_string(gNpcSequence++) + ",\"actionAck\":" + std::to_string(actionAck) + ",\"npcs\":[";
        bool first = true;
        for (const auto& npc : npcs)
        {
            char item[384]{};
            sprintf_s(item, "%s%u,%u,%.6f,%.6f,%.6f,%.7f,%.7f,%.6f,%.6f,%.6f,%d,%u,%u,%u,%d,%u,%d,%.6f",
                first ? "" : ",", npc.id, npc.cellResource,
                npc.x, npc.y, npc.z, npc.qz, npc.qw, npc.scale,
                npc.targetSize, npc.opacity, npc.stageScale,
                npc.modelInstance, npc.modelType, npc.modelGroup, npc.health, npc.animation, npc.dead ? 1 : 0, npc.elevation);
            packet += item;
            first = false;
        }
        packet += "]}";
        Queue(std::move(packet));
    }

    std::uint64_t SubmitWorldAction(WorldAction action)
    {
        action.sequence=++gWorldActionSequence;
        const auto snapshot=GetSnapshot();
        Queue("{\"type\":\"worldAction\",\"worldGeneration\":" + std::to_string(snapshot.worldGeneration) +
            ",\"sequence\":" + std::to_string(action.sequence) + ",\"id\":" + std::to_string(action.id) +
            ",\"resource\":" + std::to_string(action.resource) + ",\"damage\":" + std::to_string(action.damage) +
            ",\"removed\":" + (action.removed ? "true" : "false") + ",\"effects\":" + (action.effects ? "true}" : "false}"));
        return action.sequence;
    }

    std::vector<WorldAction> TakeWorldActions()
    {
        std::lock_guard<std::mutex> lock(gMutex);
        std::vector<WorldAction> result; result.swap(gWorldActions); return result;
    }

    void SeedProgress(const CellProgress& progress)
    {
        Queue("{\"type\":\"seedProgress\"," + ProgressFields(progress, progress.unlocks, false) + "}");
    }

    void SubmitProgressDelta(std::uint64_t sequence, const CellProgress& delta,
        const std::array<int, 13>& absoluteUnlocks)
    {
        Queue("{\"type\":\"progressDelta\",\"sequence\":" + std::to_string(sequence) + ",\"eventId\":\"" +
            NextEventID("progress") + "\"," + ProgressFields(delta, absoluteUnlocks, true) + "}");
    }

    void SubmitEditorOpen(std::uint32_t editorID, const std::string& initialSpecies, int budget, const std::string& name)
    {
        Queue("{\"type\":\"editorOpen\",\"editorId\":" +
            std::to_string(static_cast<unsigned long long>(editorID)) + ",\"species\":\"" + JsonEscape(initialSpecies) + "\",\"editorBudget\":" + std::to_string(budget) + ",\"speciesName\":\"" + JsonEscape(name) + "\"}");
    }

    void SubmitEditorClose(const std::string& speciesBlob, int budget, const std::string& name, std::uint64_t session, bool finished)
    {
        Queue("{\"type\":\"editorClose\",\"species\":\"" +
            JsonEscape(speciesBlob) + "\",\"editorSession\":" + std::to_string(session) +
            ",\"editorFinished\":" + (finished ? "true" : "false") + ",\"editorBudget\":" + std::to_string(budget) + ",\"speciesName\":\"" + JsonEscape(name) + "\"}");
    }

    std::uint64_t SubmitSpecies(const std::string& speciesBlob, std::uint64_t baseSequence, int budget, const std::string& name)
    {
        const auto sequence=gSpeciesSequence++;
        Queue("{\"type\":\"speciesLive\",\"sequence\":" +
            std::to_string(sequence) + ",\"baseSequence\":" + std::to_string(baseSequence) + ",\"species\":\"" +
            JsonEscape(speciesBlob) + "\",\"editorBudget\":" + std::to_string(budget) + ",\"speciesName\":\"" + JsonEscape(name) + "\"}");
        return sequence;
    }
}
