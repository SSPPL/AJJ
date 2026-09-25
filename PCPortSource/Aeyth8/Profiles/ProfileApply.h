#pragma once
#include "ProfileRecord.h"

/*

Written by Aeyth8

*/

// Game thread only. Everything in this header touches Unreal objects or SDK containers and
// must never be called from a worker thread.

namespace SDK
{
	class UBP_AJBGameInstance_C;
	struct FMatchingPlayerInfo;
	struct FCustomData;
}

namespace A8CL
{
namespace AJB
{

	// Converts a plain C++ profile into an SDK structure, writing every field individually.
	// The emote list is rebuilt through the engine allocator, no container is shared.
	// Returns false when the record is not usable.
	bool BuildMatchingPlayerInfoFromProfile(const ProfileRecord& Profile, SDK::FMatchingPlayerInfo* Out);

	// Fills the local PlayerLoginInfo from a validated profile: SessionID, UserDataID,
	// PlayerName, GameServerUserID, PlayerIconID, PlayerLevel, PlayerTitle and CustomData.
	// No value is ever set to a debug placeholder.
	bool ApplyProfileToPlayerLoginInfo(const ProfileRecord& Profile);

	// Builds a standalone FCustomData from the profile, used by the host when it pushes the
	// account cosmetics into the MatchingPlayers table through the game's own update call.
	// The emote array is allocated with the engine allocator and never shared with the record.
	bool BuildCustomDataFromProfile(const ProfileRecord& Profile, SDK::FCustomData* Out);

	// Applies the profile skins to the local character table so the selected character keeps
	// the account's skin choice.
	void ApplyProfileSkinsToInstance(const ProfileRecord& Profile);

}
}
