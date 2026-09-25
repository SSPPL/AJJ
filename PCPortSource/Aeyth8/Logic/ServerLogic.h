#pragma once
#include <vector>
#include <map>
#include <string>

#include "../Profiles/ProfileRecord.h"
#include "../Profiles/ProfileProvider.h"

typedef signed char			int8;
typedef short				int16;
typedef int					int32;
typedef long long			int64;

typedef unsigned char		uint8;
typedef unsigned short		uint16;
typedef unsigned int		uint32;
typedef unsigned long long	uint64;


namespace UC
{
	class FString;
}
namespace SDK
{
	class AGameModeBase;
	class APlayerController;

	class UObject;
	class UFunction;
	class UPlayer;
	class UNetDriver;
	class UNetConnection;
	class UIpConnection;

	enum class ENetRole : unsigned char;

	class FName;
	struct FUniqueNetIdRepl;
}
namespace A8CL
{
namespace AJB
{
	enum EAJBNetConnectionFlags : uint8
	{
		bIsAdmin = 1 << 0
		// TBD
	};

	struct FAJBNetConnection
	{
		SDK::UIpConnection*		Connection;

		uint8					CharacterID;
		uint8					CharacterSkin;
		uint8					Flags;

		// Phase 3 profile data. Only plain C++ types live here, the connection table never
		// holds a borrowed pointer into an SDK::FString.
		std::string				ProfileToken;
		std::string				AccountId;
		bool					bProfileResolved;
		bool					bProfileFailed;
		ProfileRecord			Profile;
		uint8					AssignedPlayerID;

		// Phase 3/6: the provider runs on a worker thread, the join flow polls this handle
		// from the tick. No file or network IO ever happens on the game thread.
		AsyncProfileResolve		PendingResolve;

		inline bool GetFlag(EAJBNetConnectionFlags Flag) const				{ return (this->Flags & Flag);	}
		inline uint8 SetFlag(EAJBNetConnectionFlags Flag, bool bSetFlagTo)	{ return bSetFlagTo ? (this->Flags |= Flag) : (this->Flags &= ~Flag); }

		FAJBNetConnection(SDK::UIpConnection* Connection = nullptr, uint8 CharacterID = 1, uint8 CharacterSkin = 1, uint8 Flags = 0)
			: Connection(Connection), CharacterID(CharacterID), CharacterSkin(CharacterSkin), Flags(Flags),
			  bProfileResolved(false), bProfileFailed(false), AssignedPlayerID(0) {}
	};

	extern bool bIsDedicatedServer;
	extern bool bServerAllowsAdmins;
	extern bool bServerHasPassword;

	extern SDK::FName NAME_ServerPassword;
	extern SDK::FName NAME_AdminPassword;
	extern SDK::FName NAME_PreJoinParameters;				// Sent by the server to the client via RedirectURL and custom assembly.

	extern SDK::FName NAME_ClientJoinOptions;
	extern std::vector<FAJBNetConnection> ClientConnections;

	// Host authority over PlayerID assignment. account_id is the stable key, the array index
	// and the connection order are never used as identity.
	struct SessionPlayerRegistry
	{
		std::map<std::string, uint8> AccountToPlayerID;
		uint8 NextPlayerID{1};

		// Returns the PlayerID for an account, reserving the next free one when it is new.
		// Reconnecting with a known account always gets the same PlayerID back.
		uint8 GetOrAssignPlayerID(const std::string& AccountId);

		// PlayerIDs are held for the lifetime of the session so an active player is never
		// renumbered when somebody else leaves. Only session end clears this table.
		bool HasAccount(const std::string& AccountId) const;

		// Takes over a PlayerID the game itself already assigned to this account, and makes
		// sure the next automatic assignment can never hand the same number to somebody else.
		void AdoptPlayerID(const std::string& AccountId, uint8 PlayerID);

		void Reset();
		int32 Num() const { return static_cast<int32>(AccountToPlayerID.size()); }
	};

	extern SessionPlayerRegistry PlayerRegistry;

	// Phase 5 session cache. MatchingPlayers is rebuilt from this after every map switch, so
	// the battle map keeps the waiting room data. Entries are keyed by account id and cleared
	// only when the session itself ends.
	struct SessionProfileCache
	{
		std::map<std::string, ProfileRecord> Profiles;
		std::map<std::string, uint8> CharacterIds;

		// account_id -> the key the game itself used for that player's MatchingPlayers row.
		// The host's own row is created by the game under the local player name rather than the
		// account id, so the real key is remembered here and reused by the rebuild instead of
		// inventing a second row for one player.
		std::map<std::string, std::string> AccountToKey;

		void Store(const std::string& AccountId, const ProfileRecord& Profile) { Profiles[AccountId] = Profile; }
		bool Has(const std::string& AccountId) const { return Profiles.find(AccountId) != Profiles.end(); }
		void Clear() { Profiles.clear(); CharacterIds.clear(); AccountToKey.clear(); }
		int32 Num() const { return static_cast<int32>(Profiles.size()); }
	};

	extern SessionProfileCache ProfileCache;

	// Phase 3/5: this process's own validated profile, remembered at start up so a new room
	// session can seed the host's own entry into the session cache. Plain C++ only.
	extern bool bHasLocalProfileRecord;
	extern ProfileRecord LocalProfileRecord;
	void SetLocalProfileRecord(const ProfileRecord& Profile);

	// True when the account id belongs to this process itself.
	bool IsLocalAccount(const std::string& AccountId);

	// Records the character selection for an account. The profile itself never decides the
	// character, the local game flow does, this only remembers its result across the switch.
	void CacheCharacterSelection(const std::string& AccountId, uint8 CharacterId);

	// Session lifetime: called when a listen session starts and when it ends. A second call
	// while a session is already live is the map switch re-initialising the net driver, and it
	// deliberately keeps the PlayerID table and the profile cache.
	void BeginSession();
	void EndSession();

	// Makes sure the host's own account is present in the session cache, adopting the row the
	// game itself created for the local player when it exists. Safe to call repeatedly.
	void EnsureLocalHostInSessionCache();

	namespace Server
	{
		// Helpers

		FAJBNetConnection& GetConnection(SDK::UNetConnection* Connection);
		int32 FindConnectionIndex(SDK::UNetConnection* Connection);
		bool CheckAdmin(FAJBNetConnection& NetConnection);
		bool IsAdmin(SDK::APlayerController* Controller);
		bool IsAdmin(SDK::UNetConnection* Connection);

		void SetAdmin(FAJBNetConnection& Connection, bool bIsAdmin);

		// Returns the connection belonging to an account, or nullptr when nobody holds it.
		FAJBNetConnection* FindConnectionByAccount(const std::string& AccountId);

		// True when another live connection already claims this account, which is how a
		// duplicate join is detected instead of silently creating two players for one account.
		bool IsAccountAlreadyConnected(const std::string& AccountId, const SDK::UNetConnection* Ignored);

		// Reads ProfileToken= / ProfileId= out of the connection options.
		bool ParseProfileOptions(const std::string& Options, std::string& OutAccountId, std::string& OutProfileToken);

		// Phase 3/6: kicks off the asynchronous resolution for a connection.
		bool BeginConnectionProfileResolve(FAJBNetConnection& Connection);

		// Phase 3/6: called from the tick. Applies finished resolutions, commits the players
		// and drops a connection whose profile did not resolve within the bounded wait.
		void TickConnectionProfileResolves();

		// Builds the authoritative entry for a resolved connection and writes it into the
		// host GameInstance through AddMatchingPlayerInfo.
		bool CommitConnectionToMatchingPlayers(FAJBNetConnection& Connection);

		// Phase 4 broadcast hook, implemented in the profile sync module.
		void BroadcastMatchingPlayers(const char* Reason);

		// Console diagnostics for the session registry.
		void DumpSessionRegistry();

		// Phase 5: rebuild every MatchingPlayers entry from the session cache. Called after a
		// map switch, before InGame.Standby, so the battle map never sees placeholder data.
		void RebuildMatchingPlayersFromSession();

		// Phase 5: writes the character id and account cosmetics for every resolved player
		// through the game's own update call. Always runs before InGame.Standby, and never
		// leaves CharactorID at 0. When the flow produced no selection yet, the permitted
		// default character is written and the reason is logged.
		void ApplyCharacterSelectionsToSession();

		// Phase 5: copies the real CharactorID the game flow already wrote into the host table
		// into the session cache, so the post-map-switch rebuild keeps the real character.
		void HarvestCharacterSelectionsFromHostTable();

		// Phase 4/5: rebuilds the host table so it only holds players with a live connection.
		// Departed PlayerIDs stay in the session registry for a later reconnect.
		void PruneMatchingPlayersForLiveConnections();


		// Hook Wrappers

		void PreLogin(SDK::AGameModeBase* This, UC::FString* Options, UC::FString* Address, SDK::FUniqueNetIdRepl* UniqueId, UC::FString* ErrorMessage); // This is AJBPreLogin, which is customary to the AJB gamemodes ONLY
		void Login(SDK::AGameModeBase* This, SDK::UPlayer* NewPlayer, SDK::ENetRole InRemoteRole, UC::FString& Portal, UC::FString& Options, SDK::FUniqueNetIdRepl& UniqueId, UC::FString& ErrorMessage);
		void PostLogin(SDK::AGameModeBase* This, SDK::APlayerController* Player);

		bool Invoke(SDK::UFunction* This, SDK::UObject* Obj, void* FFrame_Stack, void* Result);
		void AddClientConnection(SDK::UNetDriver* This, SDK::UNetConnection* Connection);
		void CloseConnection(SDK::UNetConnection* This);

		// VFT Function Reimplementation

		void WorldWelcomePlayer(SDK::UWorld* This, SDK::UNetConnection* Connection);
		void ASM_GameWelcomePlayer(SDK::AGameModeBase* This, SDK::UNetConnection* Connection, UC::FString& RedirectURL);
		//void ASM_WelcomedByServer();
	}
	
}
}
