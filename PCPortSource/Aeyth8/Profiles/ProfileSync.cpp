#include "ProfileSync.h"
#include "ProfileRecord.h"
#include "ProfileApply.h"

#include <Windows.h>
#include <map>
#include <vector>
#include <format>
#include <chrono>
#include <random>

#include "../Global.hpp"
#include "../Logic/AJB.h"
#include "../Logic/ServerLogic.h"
#include "../Offsets.h"
#include "../Tools/Pointers.h"
#include "../Tools/UnrealTypes.h"

#include "../../Dumper-7/SDK/AJB_structs.hpp"
#include "../../Dumper-7/SDK/BP_AJBGameInstance_classes.hpp"
#include "../../Dumper-7/SDK/BP_AJBInGamePlayerController_classes.hpp"
#include "../../Dumper-7/SDK/Engine_classes.hpp"
#include "../ThirdParty/json.hpp"

using json = nlohmann::json;

using namespace A8CL;
using namespace Global;

/*

Written by Aeyth8

*/

namespace A8CL
{
namespace AJB
{

namespace
{
	constexpr const char* SyncPrefix{"AJB_MP_SYNC|"};
	constexpr const char* SyncVersion{"1"};
	constexpr int32 MaxChunkCount{64};
	constexpr size_t MaxChunkPayloadBytes{350};
	constexpr size_t MaxSnapshotBytes{64 * 1024};
	constexpr double ReassemblyTimeoutSeconds{5.0};

	constexpr const wchar_t* SyncTypeName{L"AJBProfileSync"};

	// Half-received snapshots are keyed by their snapshot id, which is never a PlayerID and is
	// never derived from the connection order.
	struct PartialSnapshot
	{
		int32 ChunkCount{0};
		double TimeSinceLastChunk{0.0};
		std::map<int32, std::string> Chunks;
	};

	std::map<std::string, PartialSnapshot> PartialSnapshots;
	uint64 SnapshotCounter{0};

	const std::string Base64UrlAlphabet{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};

	std::string Base64UrlEncode(const std::string& Input)
	{
		std::string Output;
		Output.reserve(((Input.size() + 2) / 3) * 4);

		int32 Value{0};
		int32 Bits{-6};

		for (const unsigned char Byte : Input)
		{
			Value = (Value << 8) + Byte;
			Bits += 8;

			while (Bits >= 0)
			{
				Output.push_back(Base64UrlAlphabet[(Value >> Bits) & 0x3F]);
				Bits -= 6;
			}
		}

		if (Bits > -6)
		{
			Output.push_back(Base64UrlAlphabet[((Value << 8) >> (Bits + 8)) & 0x3F]);
		}

		return Output;
	}

	bool Base64UrlDecode(const std::string& Input, std::string& Output)
	{
		Output.clear();

		std::vector<int32> Lookup(256, -1);
		for (size_t i{ 0 }; i < Base64UrlAlphabet.size(); ++i)
		{
			Lookup[static_cast<unsigned char>(Base64UrlAlphabet[i])] = static_cast<int32>(i);
		}

		int32 Value{0};
		int32 Bits{-8};

		for (const unsigned char Character : Input)
		{
			if (Lookup[Character] == -1) continue;

			Value = (Value << 6) + Lookup[Character];
			Bits += 6;

			if (Bits >= 0)
			{
				Output.push_back(static_cast<char>((Value >> Bits) & 0xFF));
				Bits -= 8;
			}
		}

		return true;
	}

	std::string MakeSnapshotId()
	{
		// Uniqueness per process is all that is needed, the host is the only producer.
		++SnapshotCounter;

		const auto Now = std::chrono::steady_clock::now().time_since_epoch().count();

		return std::format("{:X}-{}", static_cast<uint64>(Now), SnapshotCounter);
	}

	// Serialises every entry of the host table into the documented snapshot shape.
	std::string BuildSnapshotJson()
	{
		if (!AJB::Instance) return "[]";

		json Players = json::array();

		for (int32 i{ 0 }; i < AJB::Instance->MatchingPlayers.Num(); ++i)
		{
			SDK::FMatchingPlayerInfo& Info = AJB::Instance->MatchingPlayers[i].Second;

			json Entry{};
			Entry["account_id"] = SDK::FString(AJB::Instance->MatchingPlayers[i].First).ToString();
			Entry["player_id"] = Info.PlayerID;
			Entry["game_server_user_id"] = Info.GameServerUserID.ToString();
			Entry["team_id"] = Info.TeamID;
			Entry["team_host_user_id"] = Info.TeamHostUserID.ToString();
			Entry["name"] = Info.PlayerName.ToString();
			Entry["icon_id"] = Info.PlayerIconID;
			Entry["level"] = Info.PlayerLevel;
			Entry["title"] = Info.PlayerTitle.ToString();
			Entry["character_id"] = Info.CharactorID;
			Entry["chara_skin_id"] = Info.CustomData.charaSkinId;
			Entry["stand_skin_id"] = Info.CustomData.standSkinId;
			Entry["kill_count"] = Info.CustomData.KillCount;
			Entry["in_game_progress"] = static_cast<int>(Info.InGameProgressID);
			Entry["start_location_x"] = Info.StartLocation.X;
			Entry["start_location_y"] = Info.StartLocation.Y;
			Entry["b_is_camera_mode"] = Info.bIsCameraMode;
			Entry["rate"] = Info.Rate;

			json Emotes = json::array();
			for (int32 e{ 0 }; e < Info.CustomData.EmoteData.Num(); ++e)
			{
				json Emote{};
				Emote["emote_id"] = Info.CustomData.EmoteData[e].emoteId;
				Emote["voice_id"] = Info.CustomData.EmoteData[e].voiceId;
				Emote["emote_name"] = Info.CustomData.EmoteData[e].EmoteName.ToString();
				Emote["voice_name"] = Info.CustomData.EmoteData[e].VoiceName.ToString();

				Emotes.push_back(Emote);
			}
			Entry["emotes"] = Emotes;

			Players.push_back(Entry);
		}

		return Players.dump();
	}
}

const wchar_t* GetMatchingPlayersSyncTypeName()
{
	return SyncTypeName;
}

void InitMatchingPlayersSync()
{
	// Register the FName up front so the type comparison never allocates mid message.
	FName::NAME_FindOrAdd(SyncTypeName);

	LogA("ProfileSync", std::format("[Init]: Snapshot sync ready | [Version]: {} | [Type]: {}", SyncVersion, "AJBProfileSync"));
}

bool IsMatchingPlayersSyncMessage(const std::string& Message)
{
	return Message.rfind(SyncPrefix, 0) == 0;
}

void SyncMatchingPlayersToClients()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	SDK::UWorld* World = GWorld.GetPointer();
	if (!World || !World->NetDriver) return;

	const std::string SnapshotJson = BuildSnapshotJson();
	if (SnapshotJson.size() > MaxSnapshotBytes)
	{
		LogA("ProfileSync", std::format("[Error]: Snapshot is {} bytes which exceeds the {} byte limit.", SnapshotJson.size(), MaxSnapshotBytes));
		return;
	}

	const std::string Encoded = Base64UrlEncode(SnapshotJson);
	const std::string SnapshotId = MakeSnapshotId();

	const int32 ChunkCount = static_cast<int32>((Encoded.size() + MaxChunkPayloadBytes - 1) / MaxChunkPayloadBytes);
	if (ChunkCount <= 0 || ChunkCount > MaxChunkCount)
	{
		LogA("ProfileSync", std::format("[Error]: Snapshot would need {} chunks which exceeds the {} chunk limit.", ChunkCount, MaxChunkCount));
		return;
	}

	static SDK::FName SyncType{ FName::NAME_FindOrAdd(SyncTypeName) };

	int32 SentTotal{0};

	for (int32 ChunkIndex{ 0 }; ChunkIndex < ChunkCount; ++ChunkIndex)
	{
		const size_t Offset = static_cast<size_t>(ChunkIndex) * MaxChunkPayloadBytes;
		const std::string Payload = Encoded.substr(Offset, MaxChunkPayloadBytes);

		const std::string Message = std::format("{}{}|{}|{}/{}|{}", SyncPrefix, SyncVersion, SnapshotId, ChunkIndex, ChunkCount, Payload);
		// SDK::FString does not own its buffer, the wide text must outlive the call.
		const std::wstring MessageText(Message.begin(), Message.end());
		SDK::FString MessageString{ MessageText.c_str() };

		for (SDK::UNetConnection* Connection : World->NetDriver->ClientConnections)
		{
			if (!Connection || !Connection->PlayerController) continue;

			Connection->PlayerController->ClientTeamMessage(nullptr, MessageString, SyncType, 0.0f);
			++SentTotal;
		}
	}

	LogA("ProfileSync", std::format("[SnapshotId]: {} | [Chunks]: {} | [Payload]: {} bytes | [Sent]: {}", SnapshotId, ChunkCount, Encoded.size(), SentTotal));
}

bool HandleMatchingPlayersSyncMessage(const std::string& Message, const std::string& TypeName)
{
	if (!IsMatchingPlayersSyncMessage(Message)) return false;

	// Only the dedicated type is consumed. A prefixed message with any other type is not ours,
	// so it is handed back to the original implementation untouched.
	if (TypeName != "AJBProfileSync")
	{
		LogA("ProfileSync", std::format("[Warning]: Sync payload arrived with unexpected type [{}], forwarding it to the original handler.", TypeName));
		return false;
	}

	// AJB_MP_SYNC|1|<SnapshotId>|<ChunkIndex>/<ChunkCount>|<Payload>
	std::vector<std::string> Parts;
	size_t Start{0};

	while (Start <= Message.size())
	{
		const size_t Next = Message.find('|', Start);
		Parts.push_back(Message.substr(Start, Next == std::string::npos ? std::string::npos : Next - Start));

		if (Next == std::string::npos) break;
		Start = Next + 1;
	}

	if (Parts.size() < 5)
	{
		LogA("ProfileSync", std::format("[Warning]: Malformed sync message with {} fields.", Parts.size()));
		return true;
	}

	if (Parts[1] != SyncVersion)
	{
		LogA("ProfileSync", std::format("[Warning]: Unsupported sync version [{}].", Parts[1]));
		return true;
	}

	const std::string& SnapshotId = Parts[2];

	const size_t Slash = Parts[3].find('/');
	if (Slash == std::string::npos)
	{
		LogA("ProfileSync", "[Warning]: Missing chunk index/count.");
		return true;
	}

	int32 ChunkIndex{-1};
	int32 ChunkCount{-1};

	try
	{
		ChunkIndex = std::stoi(Parts[3].substr(0, Slash));
		ChunkCount = std::stoi(Parts[3].substr(Slash + 1));
	}
	catch (const std::exception&)
	{
		LogA("ProfileSync", "[Warning]: Chunk index/count is not numeric.");
		return true;
	}

	if (ChunkIndex < 0 || ChunkCount <= 0 || ChunkIndex >= ChunkCount || ChunkCount > MaxChunkCount)
	{
		LogA("ProfileSync", std::format("[Warning]: Chunk {}/{} is out of range.", ChunkIndex, ChunkCount));
		return true;
	}

	PartialSnapshot& Partial = PartialSnapshots[SnapshotId];
	Partial.ChunkCount = ChunkCount;
	Partial.TimeSinceLastChunk = 0.0;
	Partial.Chunks[ChunkIndex] = Parts[4];

	if (static_cast<int32>(Partial.Chunks.size()) < ChunkCount) return true;

	std::string Encoded;
	for (int32 i{ 0 }; i < ChunkCount; ++i)
	{
		Encoded += Partial.Chunks[i];
	}
	PartialSnapshots.erase(SnapshotId);

	std::string SnapshotJson;
	if (!Base64UrlDecode(Encoded, SnapshotJson))
	{
		LogA("ProfileSync", std::format("[Warning]: Snapshot {} could not be decoded.", SnapshotId));
		return true;
	}

	if (SnapshotJson.size() > MaxSnapshotBytes)
	{
		LogA("ProfileSync", std::format("[Warning]: Snapshot {} is {} bytes, over the limit.", SnapshotId, SnapshotJson.size()));
		return true;
	}

	LogA("ProfileSync", std::format("[SnapshotId]: {} | [Chunks]: {} | [Bytes]: {} | [State]: Reassembled", SnapshotId, ChunkCount, SnapshotJson.size()));

	ApplyMatchingPlayersSnapshot(SnapshotJson);

	return true;
}

bool ApplyMatchingPlayersSnapshot(const std::string& SnapshotJson)
{
	if (!AJB::Instance) return false;
	if (AJB::IsServer()) return false;

	json Players;
	try
	{
		Players = json::parse(SnapshotJson);
	}
	catch (const std::exception& Exception)
	{
		LogA("ProfileSync", std::format("[Error]: Snapshot JSON is malformed: {}", Exception.what()));
		return false;
	}

	if (!Players.is_array())
	{
		LogA("ProfileSync", "[Error]: Snapshot root is not an array.");
		return false;
	}

	// Clear only the player entries, the game keeps its own bookkeeping.
	AJB::Instance->ClearMatchingPlayerInfo(false);

	for (const json& Entry : Players)
	{
		if (!Entry.is_object()) continue;
		if (!Entry.contains("account_id") || !Entry.contains("player_id")) continue;

		SDK::FMatchingPlayerInfo Info{};
		Info.PlayerID = static_cast<uint8>(Entry.value("player_id", 0));
		Info.TeamID = static_cast<uint8>(Entry.value("team_id", 0));
		Info.PlayerIconID = Entry.value("icon_id", -1);
		Info.PlayerLevel = Entry.value("level", 0);
		Info.CharactorID = static_cast<uint8>(Entry.value("character_id", 0));
		Info.bIsCameraMode = Entry.value("b_is_camera_mode", false);
		Info.Rate = Entry.value("rate", 0);
		Info.StartLocation.X = static_cast<uint8>(Entry.value("start_location_x", 0));
		Info.StartLocation.Y = static_cast<uint8>(Entry.value("start_location_y", 0));
		Info.InGameProgressID = static_cast<SDK::EInGameProgressID>(Entry.value("in_game_progress", 0));

		Info.CustomData.charaSkinId = static_cast<uint8>(Entry.value("chara_skin_id", 0));
		Info.CustomData.standSkinId = static_cast<uint8>(Entry.value("stand_skin_id", 0));
		Info.CustomData.KillCount = Entry.value("kill_count", 0);

		const std::string AccountId = Entry.value("account_id", std::string());

		// Every wide text lives in a named local: SDK::FString only borrows the pointer and
		// the temporary of a range constructor would be destroyed before CopyString reads it.
		const std::string GameServerUserIDText = Entry.value("game_server_user_id", std::string());
		const std::string TeamHostUserIDText = Entry.value("team_host_user_id", std::string());
		const std::string PlayerNameText = Entry.value("name", std::string());
		const std::string PlayerTitleText = Entry.value("title", std::string());

		const std::wstring GameServerUserIDWide(GameServerUserIDText.begin(), GameServerUserIDText.end());
		const std::wstring TeamHostUserIDWide(TeamHostUserIDText.begin(), TeamHostUserIDText.end());
		const std::wstring PlayerNameWide(PlayerNameText.begin(), PlayerNameText.end());
		const std::wstring PlayerTitleWide(PlayerTitleText.begin(), PlayerTitleText.end());

		SDK::FString GameServerUserID{ GameServerUserIDWide.c_str() };
		SDK::FString TeamHostUserID{ TeamHostUserIDWide.c_str() };
		SDK::FString PlayerName{ PlayerNameWide.c_str() };
		SDK::FString PlayerTitle{ PlayerTitleWide.c_str() };

		AJB::CopyString(&Info.GameServerUserID, &GameServerUserID);
		AJB::CopyString(&Info.TeamHostUserID, &TeamHostUserID);
		AJB::CopyString(&Info.PlayerName, &PlayerName);
		AJB::CopyString(&Info.PlayerTitle, &PlayerTitle);

		// Emotes are rebuilt through the engine allocator, never shared with the JSON buffer.
		if (Entry.contains("emotes") && Entry["emotes"].is_array())
		{
			const json& Emotes = Entry["emotes"];

			if (Emotes.empty())
			{
				Info.CustomData.EmoteData = UC::TArray<SDK::FEmoteData>();
			}
			else
			{
				SDK::FEmoteData* NewData = static_cast<SDK::FEmoteData*>(FMemory::Malloc(sizeof(SDK::FEmoteData) * Emotes.size()));
				if (NewData)
				{
					Info.CustomData.EmoteData = UC::TArray<SDK::FEmoteData>(NewData, static_cast<int32>(Emotes.size()), static_cast<int32>(Emotes.size()));

					for (size_t e{ 0 }; e < Emotes.size(); ++e)
					{
						SDK::FEmoteData& Destination = Info.CustomData.EmoteData.Data[e];
						Destination = SDK::FEmoteData{};

						Destination.emoteId = static_cast<uint8>(Emotes[e].value("emote_id", 0));
						Destination.voiceId = static_cast<uint8>(Emotes[e].value("voice_id", 0));

						const std::string EmoteName = Emotes[e].value("emote_name", std::string());
						const std::string VoiceName = Emotes[e].value("voice_name", std::string());

						const std::wstring EmoteNameWide(EmoteName.begin(), EmoteName.end());
						const std::wstring VoiceNameWide(VoiceName.begin(), VoiceName.end());

						SDK::FString EmoteNameString{ EmoteNameWide.c_str() };
						SDK::FString VoiceNameString{ VoiceNameWide.c_str() };

						AJB::CopyString(&Destination.EmoteName, &EmoteNameString);
						AJB::CopyString(&Destination.VoiceName, &VoiceNameString);
					}
				}
			}
		}

		const std::wstring KeyText(AccountId.begin(), AccountId.end());
		SDK::FString Key{ KeyText.c_str() };

		AJB::Instance->AddMatchingPlayerInfo(Key, Info);
	}

	AJB::DumpMatchingPlayers("MP-Snapshot-Applied");

	return true;
}

void TickMatchingPlayersSync(float DeltaSeconds)
{
	for (auto It = PartialSnapshots.begin(); It != PartialSnapshots.end();)
	{
		It->second.TimeSinceLastChunk += DeltaSeconds;

		if (It->second.TimeSinceLastChunk >= ReassemblyTimeoutSeconds)
		{
			LogA("ProfileSync", std::format("[SnapshotId]: {} | [State]: Discarded after {}s without all chunks.", It->first, ReassemblyTimeoutSeconds));
			It = PartialSnapshots.erase(It);
		}
		else
		{
			++It;
		}
	}
}

}
}

// The host broadcast entry point lives here so the server logic does not need the sync header.
namespace A8CL
{
namespace AJB
{
namespace Server
{
	void BroadcastMatchingPlayers(const char* Reason)
	{
		if (!AJB::IsServer()) return;

		LogA("MP-Broadcast", std::format("[Reason]: {}", Reason));
		SyncMatchingPlayersToClients();
	}
}
}
}
