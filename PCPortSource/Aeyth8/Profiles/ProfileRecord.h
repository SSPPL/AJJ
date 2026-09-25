#pragma once
#include <string>
#include <vector>
#include <cstdint>

/*

Written by Aeyth8

*/

// Plain C++ account profile cache. It deliberately holds no Unreal containers so it can be
// built, moved and compared on worker threads. Conversion into SDK structures only ever
// happens on the game thread.

namespace A8CL
{
namespace AJB
{

	typedef uint8_t  PByte;
	typedef int32_t  PInt32;

	struct ProfileEmote
	{
		PByte EmoteId{0};
		PByte VoiceId{0};
		std::wstring EmoteName;
		std::wstring VoiceName;
	};

	struct ProfileRecord
	{
		std::string AccountId;
		std::string ProfileToken;
		std::wstring PlayerName;
		std::wstring GameServerUserID;
		std::wstring PlayerTitle;
		PInt32 PlayerIconID{-1};
		PInt32 PlayerLevel{0};
		PByte CharaSkinId{0};
		PByte StandSkinId{0};
		PInt32 KillCount{0};
		std::vector<ProfileEmote> Emotes;
	};

	enum class EProfileSource : PByte
	{
		Disabled	= 0,
		LocalJson	= 1,
		Http		= 2,
	};

	// Lowest and highest accepted values. Anything outside is treated as an invalid profile,
	// a half populated record is never produced.
	struct ProfileLimits
	{
		static constexpr PInt32 IconIdMin{0};
		static constexpr PInt32 IconIdMax{999};
		static constexpr PInt32 LevelMin{1};
		static constexpr PInt32 LevelMax{9999};
		static constexpr PInt32 KillCountMin{0};
		static constexpr PInt32 KillCountMax{999999};
		static constexpr size_t MaxEmotes{64};
		static constexpr size_t MaxNameLength{64};
		static constexpr size_t MaxTitleLength{64};
		static constexpr size_t MaxIdLength{64};
	};

	struct ProfileParseResult
	{
		bool bSuccess{false};
		ProfileRecord Record{};
		std::string Error;
	};

	// Parses the fixed profile schema. Returns an error string when the payload is missing
	// fields, has out of range values, or when the embedded account_id disagrees with the
	// expected one. Never returns a partially filled record.
	ProfileParseResult ParseProfileJson(const std::string& Json, const std::string& ExpectedAccountId);

	// Renders a profile back into the canonical schema (used by /save-profile and debugging).
	std::string SerializeProfileJson(const ProfileRecord& Profile);

	// Directory holding the local JSON profiles, <GameDir>\Aeyth8\Configs\profiles.
	std::wstring GetProfileDirectory();

	// Loads and validates <GameDir>\Aeyth8\Configs\profiles\<AccountId>.json.
	ProfileParseResult LoadLocalProfile(const std::string& AccountId);

}
}