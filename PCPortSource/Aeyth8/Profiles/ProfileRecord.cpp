#include "ProfileRecord.h"

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "../ThirdParty/json.hpp"

using json = nlohmann::json;

/*

Written by Aeyth8

*/

namespace A8CL
{
namespace AJB
{

namespace
{
	// UTF-8 is the wire format, the game wants UTF-16 internally.
	std::wstring Utf8ToWide(const std::string& In)
	{
		if (In.empty()) return std::wstring();

		const int Size = MultiByteToWideChar(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), nullptr, 0);
		if (Size <= 0) return std::wstring();

		std::wstring Out(static_cast<size_t>(Size), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), Out.data(), Size);

		return Out;
	}

	std::string WideToUtf8(const std::wstring& In)
	{
		if (In.empty()) return std::string();

		const int Size = WideCharToMultiByte(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), nullptr, 0, nullptr, nullptr);
		if (Size <= 0) return std::string();

		std::string Out(static_cast<size_t>(Size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, In.c_str(), static_cast<int>(In.size()), Out.data(), Size, nullptr, nullptr);

		return Out;
	}

	bool ReadTextFile(const std::wstring& Path, std::string& OutContents, std::string& OutError)
	{
		std::ifstream Stream(std::filesystem::path(Path), std::ios::binary);
		if (!Stream.is_open())
		{
			OutError = "profile file could not be opened";
			return false;
		}

		std::ostringstream Buffer;
		Buffer << Stream.rdbuf();
		OutContents = Buffer.str();

		return true;
	}
}

std::wstring GetProfileDirectory()
{
	wchar_t ExePath[MAX_PATH]{0};
	if (GetModuleFileNameW(nullptr, ExePath, MAX_PATH) == 0)
	{
		return L"Aeyth8\\Configs\\profiles";
	}

	std::filesystem::path Directory(ExePath);
	Directory.remove_filename();

	return (Directory / L"Aeyth8" / L"Configs" / L"profiles").wstring();
}

ProfileParseResult ParseProfileJson(const std::string& Json, const std::string& ExpectedAccountId)
{
	ProfileParseResult Result{};

	json Root;
	try
	{
		Root = json::parse(Json);
	}
	catch (const std::exception& Exception)
	{
		Result.Error = std::string("malformed JSON: ") + Exception.what();
		return Result;
	}

	if (!Root.is_object())
	{
		Result.Error = "profile root is not a JSON object";
		return Result;
	}

	// Strict field presence, a missing field can never silently become a default.
	const char* RequiredStrings[]{"account_id", "name", "title"};
	for (const char* Field : RequiredStrings)
	{
		if (!Root.contains(Field) || !Root[Field].is_string())
		{
			Result.Error = std::string("missing or non string field: ") + Field;
			return Result;
		}
	}

	const char* RequiredNumbers[]{"icon_id", "level"};
	for (const char* Field : RequiredNumbers)
	{
		if (!Root.contains(Field) || !Root[Field].is_number_integer())
		{
			Result.Error = std::string("missing or non integer field: ") + Field;
			return Result;
		}
	}

	if (!Root.contains("custom_data") || !Root["custom_data"].is_object())
	{
		Result.Error = "missing custom_data object";
		return Result;
	}

	const json& CustomData = Root["custom_data"];
	if (!CustomData.contains("chara_skin_id") || !CustomData["chara_skin_id"].is_number_integer())
	{
		Result.Error = "missing custom_data.chara_skin_id";
		return Result;
	}
	if (!CustomData.contains("stand_skin_id") || !CustomData["stand_skin_id"].is_number_integer())
	{
		Result.Error = "missing custom_data.stand_skin_id";
		return Result;
	}
	if (!CustomData.contains("emotes") || !CustomData["emotes"].is_array())
	{
		Result.Error = "missing custom_data.emotes array";
		return Result;
	}

	ProfileRecord Record{};
	Record.AccountId = Root["account_id"].get<std::string>();

	if (!ExpectedAccountId.empty() && Record.AccountId != ExpectedAccountId)
	{
		Result.Error = "account_id mismatch, expected '" + ExpectedAccountId + "' but the file declares '" + Record.AccountId + "'";
		return Result;
	}

	if (Record.AccountId.empty() || Record.AccountId.size() > ProfileLimits::MaxIdLength)
	{
		Result.Error = "account_id is empty or too long";
		return Result;
	}

	Record.PlayerName = Utf8ToWide(Root["name"].get<std::string>());
	Record.PlayerTitle = Utf8ToWide(Root["title"].get<std::string>());

	// Optional: the wire schema allows a distinct game server user id. When it is absent the
	// player name is used, which is what the build-from-profile helper already does.
	if (Root.contains("game_server_user_id"))
	{
		if (!Root["game_server_user_id"].is_string())
		{
			Result.Error = "game_server_user_id is not a string";
			return Result;
		}

		Record.GameServerUserID = Utf8ToWide(Root["game_server_user_id"].get<std::string>());
	}

	if (Record.PlayerName.empty() || Record.PlayerName.size() > ProfileLimits::MaxNameLength)
	{
		Result.Error = "name is empty or too long";
		return Result;
	}
	if (Record.PlayerTitle.size() > ProfileLimits::MaxTitleLength)
	{
		Result.Error = "title is too long";
		return Result;
	}

	Record.PlayerIconID = Root["icon_id"].get<PInt32>();
	Record.PlayerLevel = Root["level"].get<PInt32>();

	if (Record.PlayerIconID < ProfileLimits::IconIdMin || Record.PlayerIconID > ProfileLimits::IconIdMax)
	{
		Result.Error = "icon_id out of range";
		return Result;
	}
	if (Record.PlayerLevel < ProfileLimits::LevelMin || Record.PlayerLevel > ProfileLimits::LevelMax)
	{
		Result.Error = "level out of range";
		return Result;
	}

	const json& CharaSkin = CustomData["chara_skin_id"];
	const json& StandSkin = CustomData["stand_skin_id"];

	if (CharaSkin.get<PInt32>() < 0 || CharaSkin.get<PInt32>() > 255 || StandSkin.get<PInt32>() < 0 || StandSkin.get<PInt32>() > 255)
	{
		Result.Error = "skin id out of range";
		return Result;
	}

	Record.CharaSkinId = static_cast<PByte>(CharaSkin.get<PInt32>());
	Record.StandSkinId = static_cast<PByte>(StandSkin.get<PInt32>());

	if (CustomData.contains("kill_count"))
	{
		if (!CustomData["kill_count"].is_number_integer())
		{
			Result.Error = "custom_data.kill_count is not an integer";
			return Result;
		}

		Record.KillCount = CustomData["kill_count"].get<PInt32>();
		if (Record.KillCount < ProfileLimits::KillCountMin || Record.KillCount > ProfileLimits::KillCountMax)
		{
			Result.Error = "kill_count out of range";
			return Result;
		}
	}

	const json& Emotes = CustomData["emotes"];
	if (Emotes.size() > ProfileLimits::MaxEmotes)
	{
		Result.Error = "too many emotes";
		return Result;
	}

	for (const json& Emote : Emotes)
	{
		if (!Emote.is_object())
		{
			Result.Error = "emote entry is not an object";
			return Result;
		}
		if (!Emote.contains("emote_id") || !Emote["emote_id"].is_number_integer() ||
			!Emote.contains("voice_id") || !Emote["voice_id"].is_number_integer())
		{
			Result.Error = "emote entry is missing emote_id or voice_id";
			return Result;
		}

		const PInt32 EmoteId = Emote["emote_id"].get<PInt32>();
		const PInt32 VoiceId = Emote["voice_id"].get<PInt32>();

		if (EmoteId < 0 || EmoteId > 255 || VoiceId < 0 || VoiceId > 255)
		{
			Result.Error = "emote id out of range";
			return Result;
		}

		ProfileEmote NewEmote{};
		NewEmote.EmoteId = static_cast<PByte>(EmoteId);
		NewEmote.VoiceId = static_cast<PByte>(VoiceId);

		if (Emote.contains("emote_name") && Emote["emote_name"].is_string())
		{
			NewEmote.EmoteName = Utf8ToWide(Emote["emote_name"].get<std::string>());
		}
		if (Emote.contains("voice_name") && Emote["voice_name"].is_string())
		{
			NewEmote.VoiceName = Utf8ToWide(Emote["voice_name"].get<std::string>());
		}

		Record.Emotes.push_back(NewEmote);
	}

	Result.bSuccess = true;
	Result.Record = std::move(Record);

	return Result;
}

std::string SerializeProfileJson(const ProfileRecord& Profile)
{
	json Root{};
	Root["account_id"] = Profile.AccountId;
	Root["name"] = WideToUtf8(Profile.PlayerName);
	Root["icon_id"] = Profile.PlayerIconID;
	Root["level"] = Profile.PlayerLevel;
	Root["title"] = WideToUtf8(Profile.PlayerTitle);

	json CustomData{};
	CustomData["chara_skin_id"] = static_cast<int>(Profile.CharaSkinId);
	CustomData["stand_skin_id"] = static_cast<int>(Profile.StandSkinId);
	CustomData["kill_count"] = Profile.KillCount;

	json Emotes = json::array();
	for (const ProfileEmote& Emote : Profile.Emotes)
	{
		json Entry{};
		Entry["emote_id"] = static_cast<int>(Emote.EmoteId);
		Entry["voice_id"] = static_cast<int>(Emote.VoiceId);
		Entry["emote_name"] = WideToUtf8(Emote.EmoteName);
		Entry["voice_name"] = WideToUtf8(Emote.VoiceName);

		Emotes.push_back(Entry);
	}
	CustomData["emotes"] = Emotes;

	Root["custom_data"] = CustomData;

	return Root.dump(2);
}

ProfileParseResult LoadLocalProfile(const std::string& AccountId)
{
	ProfileParseResult Result{};

	if (AccountId.empty())
	{
		Result.Error = "no account id supplied";
		return Result;
	}

	const std::wstring Directory = GetProfileDirectory();
	const std::wstring Account = Utf8ToWide(AccountId);
	const std::wstring Path = Directory + L"\\" + Account + L".json";

	std::string Contents;
	if (!ReadTextFile(Path, Contents, Result.Error))
	{
		return Result;
	}

	return ParseProfileJson(Contents, AccountId);
}

}
}
