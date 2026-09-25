#include "ProfileApply.h"

#include "../Global.hpp"
#include "../Logic/AJB.h"
#include "../Tools/UnrealTypes.h"
#include <format>

#include "../../Dumper-7/SDK/AJB_structs.hpp"
#include "../../Dumper-7/SDK/AJB_classes.hpp"
#include "../../Dumper-7/SDK/BP_AJBGameInstance_classes.hpp"

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
	// Strings crossing into Unreal structures always go through the game's own copy routine,
	// the engine never receives a pointer into our std::wstring storage.
	void CopyWStringToFString(SDK::FString* Destination, const std::wstring& Source)
	{
		if (!Destination) return;

		SDK::FString Temporary{ Source.empty() ? L"" : Source.c_str() };
		AJB::CopyString(Destination, &Temporary);
	}

	// Rebuilds an emote array from plain C++ data using the engine allocator only.
	bool BuildEmoteArray(const ProfileRecord& Profile, UC::TArray<SDK::FEmoteData>* Out)
	{
		if (!Out) return false;

		*Out = UC::TArray<SDK::FEmoteData>();

		if (Profile.Emotes.empty()) return true;

		SDK::FEmoteData* NewData = static_cast<SDK::FEmoteData*>(A8CL::FMemory::Malloc(sizeof(SDK::FEmoteData) * Profile.Emotes.size()));
		if (!NewData)
		{
			LogA("BuildEmoteArray", std::format("[Error]: Failed to allocate {} emote entries", Profile.Emotes.size()));
			return false;
		}

		*Out = UC::TArray<SDK::FEmoteData>(NewData, static_cast<int32>(Profile.Emotes.size()), static_cast<int32>(Profile.Emotes.size()));

		for (size_t i{ 0 }; i < Profile.Emotes.size(); ++i)
		{
			SDK::FEmoteData& Destination = Out->Data[i];
			Destination = SDK::FEmoteData{};

			Destination.emoteId = Profile.Emotes[i].EmoteId;
			Destination.voiceId = Profile.Emotes[i].VoiceId;

			CopyWStringToFString(&Destination.EmoteName, Profile.Emotes[i].EmoteName);
			CopyWStringToFString(&Destination.VoiceName, Profile.Emotes[i].VoiceName);
		}

		return true;
	}
}

bool BuildMatchingPlayerInfoFromProfile(const ProfileRecord& Profile, SDK::FMatchingPlayerInfo* Out)
{
	if (!Out) return false;
	if (Profile.AccountId.empty() || Profile.PlayerName.empty()) return false;

	// Start from an empty structure, then write each field explicitly.
	*Out = SDK::FMatchingPlayerInfo{};

	Out->PlayerIconID = Profile.PlayerIconID;
	Out->PlayerLevel = Profile.PlayerLevel;

	CopyWStringToFString(&Out->GameServerUserID, Profile.GameServerUserID.empty() ? Profile.PlayerName : Profile.GameServerUserID);
	CopyWStringToFString(&Out->PlayerName, Profile.PlayerName);
	CopyWStringToFString(&Out->PlayerTitle, Profile.PlayerTitle);

	Out->CustomData.charaSkinId = Profile.CharaSkinId;
	Out->CustomData.standSkinId = Profile.StandSkinId;
	Out->CustomData.KillCount = Profile.KillCount;

	return BuildEmoteArray(Profile, &Out->CustomData.EmoteData);
}

bool BuildCustomDataFromProfile(const ProfileRecord& Profile, SDK::FCustomData* Out)
{
	if (!Out) return false;

	*Out = SDK::FCustomData{};

	Out->charaSkinId = Profile.CharaSkinId;
	Out->standSkinId = Profile.StandSkinId;
	Out->KillCount = Profile.KillCount;

	return BuildEmoteArray(Profile, &Out->EmoteData);
}

bool ApplyProfileToPlayerLoginInfo(const ProfileRecord& Profile)
{
	if (!AJB::Instance)
	{
		LogA("ApplyProfileToPlayerLoginInfo", "[Error]: No GameInstance is available yet.");
		return false;
	}

	if (Profile.AccountId.empty() || Profile.PlayerName.empty())
	{
		LogA("ApplyProfileToPlayerLoginInfo", "[Error]: Refusing to apply an incomplete profile.");
		return false;
	}

	SDK::FPlayerLoginInfo& LoginInfo = AJB::Instance->PlayerLoginInfo;

	const std::wstring AccountId{ std::wstring(Profile.AccountId.begin(), Profile.AccountId.end()) };

	// The account id doubles as the session and user data id, it is the stable identity the
	// host and client both agree on. It is never derived from the computer name.
	CopyWStringToFString(&LoginInfo.SessionID, AccountId);
	CopyWStringToFString(&LoginInfo.UserDataID, AccountId);
	CopyWStringToFString(&LoginInfo.MatchingID, AccountId);

	CopyWStringToFString(&LoginInfo.MatchingPlayerInfo.PlayerName, Profile.PlayerName);
	CopyWStringToFString(&LoginInfo.MatchingPlayerInfo.GameServerUserID, Profile.GameServerUserID.empty() ? Profile.PlayerName : Profile.GameServerUserID);
	CopyWStringToFString(&LoginInfo.MatchingPlayerInfo.PlayerTitle, Profile.PlayerTitle);

	LoginInfo.MatchingPlayerInfo.PlayerIconID = Profile.PlayerIconID;
	LoginInfo.MatchingPlayerInfo.PlayerLevel = Profile.PlayerLevel;

	LoginInfo.MatchingPlayerInfo.CustomData.charaSkinId = Profile.CharaSkinId;
	LoginInfo.MatchingPlayerInfo.CustomData.standSkinId = Profile.StandSkinId;
	LoginInfo.MatchingPlayerInfo.CustomData.KillCount = Profile.KillCount;

	// This account is a real account, not a guest and not a debug placeholder.
	LoginInfo.bIsGuest = false;

	LogA("ApplyProfileToPlayerLoginInfo", std::format("[AccountId]: {} | [PlayerName]: {} | [PlayerIconID]: {} | [PlayerLevel]: {} | [PlayerTitle]: {} | [charaSkinId]: {} | [standSkinId]: {}",
		Profile.AccountId, SDK::FString(Profile.PlayerName.c_str()).ToString(), Profile.PlayerIconID, Profile.PlayerLevel,
		SDK::FString(Profile.PlayerTitle.c_str()).ToString(), static_cast<int>(Profile.CharaSkinId), static_cast<int>(Profile.StandSkinId)));

	return true;
}

void ApplyProfileSkinsToInstance(const ProfileRecord& Profile)
{
	if (!AJB::Instance) return;

	// Skin tables are indexed by character id, the profile carries the account's choice.
	const int32 CharacterIndex{ AJB::Instance->CharacterNo };

	if (CharacterIndex > 0 && Profile.CharaSkinId > 0)
	{
		AJB::Instance->SetCharacterSkinId(CharacterIndex, Profile.CharaSkinId);
	}
	if (CharacterIndex > 0 && Profile.StandSkinId > 0)
	{
		AJB::Instance->SetStandSkinId(CharacterIndex, Profile.StandSkinId);
	}

	for (int32 i{ 0 }; i < AJB::Instance->OfflineDefaultCustomData.Num(); ++i)
	{
		if (AJB::Instance->OfflineDefaultCustomData[i].Second.charaSkinId == 0 && Profile.CharaSkinId > 0)
		{
			AJB::Instance->OfflineDefaultCustomData[i].Second.charaSkinId = Profile.CharaSkinId;
		}
		if (AJB::Instance->OfflineDefaultCustomData[i].Second.standSkinId == 0 && Profile.StandSkinId > 0)
		{
			AJB::Instance->OfflineDefaultCustomData[i].Second.standSkinId = Profile.StandSkinId;
		}
	}
}

}
}
