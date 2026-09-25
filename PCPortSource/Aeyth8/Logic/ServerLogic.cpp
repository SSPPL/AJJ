#include "AJB.h"
#include "ServerLogic.h"

#include "../Global.hpp"
#include "../Hooks/Hooks.hpp"
#include "../Offsets.h"
#include "../../Version/resource.h"

#include "../Tools/Pointers.h"
#include "../Tools/UFunctions.hpp"
#include "../Tools/UnrealTypes.h"
#include "../Tools/BytePatcher.h"

#include "../Tools/UnrealExternWrapper.h"

#include "../CmdArgs/CommandLineArgs.h"

#include "../../Dumper-7/CustomSDK/BP_Synchronizer_classes.hpp"				// Custom SDK header (NOT GAME NATIVE)
#include "../../Dumper-7/SDK/BP_AJBGameInstance_classes.hpp"

#include "../Profiles/ProfileApply.h"
#include "../Profiles/ProfileProvider.h"



using namespace A8CL; using namespace Global;

bool								AJB::bIsDedicatedServer{false};
bool								AJB::bServerAllowsAdmins{false};
bool								AJB::bServerHasPassword{false};
SDK::FName							AJB::NAME_ServerPassword{};
SDK::FName							AJB::NAME_AdminPassword{};
SDK::FName							AJB::NAME_PreJoinParameters{};

SDK::FName							AJB::NAME_ClientJoinOptions{};
std::vector<AJB::FAJBNetConnection>	AJB::ClientConnections{};

AJB::SessionPlayerRegistry			AJB::PlayerRegistry{};
AJB::SessionProfileCache			AJB::ProfileCache{};

bool								AJB::bHasLocalProfileRecord{false};
A8CL::AJB::ProfileRecord			AJB::LocalProfileRecord{};

namespace
{
	// True while a room session is live. A listen server re-initialises its net driver on every
	// map switch inside the session, so InitListen alone cannot tell a brand new room from the
	// travel into AJBStage01_P. Only EndSession (returning to the main menu) clears this, which
	// is what keeps PlayerIDs and the profile cache alive across the switch.
	bool gbSessionActive{false};

	// The game creates its own row for the host under its local player name, not under the
	// account id, and it is a placeholder (NO NAME / IconID -1). The real key and PlayerID are
	// adopted from the live table so the profile data can be written into the row the game
	// already knows about instead of creating a second row for one player.
	bool TryAdoptLocalHostRow();

	// Maps a MatchingPlayers row key back to the account that owns it. The host's row is keyed
	// by the local player name, so the game key and the account id are not interchangeable.
	std::string AccountIdForKey(const std::string& Key);
}

void AJB::SetLocalProfileRecord(const ProfileRecord& Profile)
{
	if (Profile.AccountId.empty() || Profile.PlayerName.empty()) return;

	AJB::LocalProfileRecord = Profile;
	AJB::bHasLocalProfileRecord = true;
}

bool AJB::IsLocalAccount(const std::string& AccountId)
{
	if (AccountId.empty()) return false;

	const std::string LocalAccount{ AJB::GetLocalAccountId() };
	return !LocalAccount.empty() && LocalAccount == AccountId;
}

namespace
{
	// The key the game itself created for an account's row, or the account id when the game
	// did not create one. Never the array index and never the connection order.
	std::string GetSessionKey(const std::string& AccountId)
	{
		const auto Existing = AJB::ProfileCache.AccountToKey.find(AccountId);
		if (Existing != AJB::ProfileCache.AccountToKey.end() && !Existing->second.empty())
		{
			return Existing->second;
		}

		return AccountId;
	}

	// True when the key belongs to a live remote connection's account. Such rows are managed
	// by the connection path, everything else can only be the host's own row.
	bool IsKeyOwnedByLiveConnection(const std::string& Key)
	{
		for (const AJB::FAJBNetConnection& Connection : AJB::ClientConnections)
		{
			if (Connection.AccountId == Key) return true;
			if (GetSessionKey(Connection.AccountId) == Key) return true;
		}

		return false;
	}

	bool TryAdoptLocalHostRow()
	{
		if (!AJB::Instance) return false;

		const std::string LocalAccount{ AJB::GetLocalAccountId() };
		if (LocalAccount.empty()) return false;

		// The game renumbers PlayerIDs itself as entries are inserted, so the row is located
		// every time and the current number is re-adopted instead of caching a stale one.
		const std::string KnownKey{ GetSessionKey(LocalAccount) };

		for (int32 i{ 0 }; i < AJB::Instance->MatchingPlayers.Num(); ++i)
		{
			const std::string Key{ AJB::Instance->MatchingPlayers[i].First.ToString() };
			if (Key.empty()) continue;

			// A previously adopted key is trusted even while a joiner is connected, every other
			// row that no live connection owns can only be the host's own row.
			if (Key != KnownKey && IsKeyOwnedByLiveConnection(Key)) continue;

			const SDK::FMatchingPlayerInfo& Info = AJB::Instance->MatchingPlayers[i].Second;

			AJB::ProfileCache.AccountToKey[LocalAccount] = Key;

			// Keep the PlayerID the game already handed to its own row so no lookup by id is
			// disturbed, and make sure no later assignment can reuse it.
			if (Info.PlayerID != 0)
			{
				AJB::PlayerRegistry.AdoptPlayerID(LocalAccount, Info.PlayerID);
			}

			LogA("Session", std::format("[AdoptLocalHostRow]: [AccountId]: {} | [Key]: {} | [PlayerID]: {}", LocalAccount, Key, Info.PlayerID));

			return true;
		}

		return false;
	}

	std::string AccountIdForKey(const std::string& Key)
	{
		if (Key.empty()) return std::string();

		for (const auto& Entry : AJB::ProfileCache.AccountToKey)
		{
			if (Entry.second == Key) return Entry.first;
		}

		// Remote players are keyed by their own account id, so the key is the account.
		return Key;
	}

}



AJB::FAJBNetConnection& AJB::Server::GetConnection(SDK::UNetConnection* Connection)
{
	int32 i = FindConnectionIndex(Connection);
	if (i != -1)
	{
		return ClientConnections[i];
	}
}

int32 AJB::Server::FindConnectionIndex(SDK::UNetConnection* Connection)
{
	if (Connection) 
	{
		for (int32 i{0}; i < ClientConnections.size(); ++i)
		{
			if (ClientConnections[i].Connection && ClientConnections[i].Connection == Connection)
			{
				return i;
			}
		}
	}

	return -1;
}

bool A8CL::AJB::Server::CheckAdmin(FAJBNetConnection& NetConnection)
{
	return NetConnection.Connection && NetConnection.GetFlag(bIsAdmin);
}

bool AJB::Server::IsAdmin(SDK::APlayerController* Controller)
{
	if (Controller)
	{
		int32 Index = FindConnectionIndex(Controller->NetConnection);
		if (Index != -1)
		{
			return CheckAdmin(ClientConnections[Index]);
		}
	}

	return false;
}

bool AJB::Server::IsAdmin(SDK::UNetConnection* Connection)
{
	if (Connection)
	{
		return IsAdmin(Connection->PlayerController);
	}

	return false;
}

void AJB::Server::SetAdmin(FAJBNetConnection& Connection, bool bIsAdmin)
{	
	if (bIsAdmin && !bServerAllowsAdmins)  bServerAllowsAdmins = true;
	Connection.SetFlag(EAJBNetConnectionFlags::bIsAdmin, bIsAdmin);
	
}

void AJB::Server::PreLogin(SDK::AGameModeBase* This, UC::FString* Options, UC::FString* Address, SDK::FUniqueNetIdRepl* UniqueId, UC::FString* ErrorMessage)
{
	LogA(OFF::PreLogin.GetName(), std::format("[AGameModeBase]: {} | [Options]: {} | [Address]: {} | [ErrorMessage]: {}", This->GetFullName(), Options->ToString(), Address->ToString(), ErrorMessage->ToString()));

	static SDK::FString CLIENTINCOMPATIBLE{L"OUTDATED CLIENT | INCOMPATIBLE"};
	static SDK::FString PASSWORDPROTECTED{L"PASSWORD PROTECTED | INVALID OR EMPTY PASSWORD"};

	if (Options)
	{
		bool bFailedPreLogin{false};
		SDK::FString* FailureMessage{nullptr};

		AJB::DumpMatchingPlayers("MP-PreLogin");

		// Phase 3/5: adopt the row the game already created for the host before any PlayerID is
		// handed to a joiner, otherwise the join could be assigned the number the host row owns.
		AJB::EnsureLocalHostInSessionCache();

		std::string OptionsStr = Options->ToString();

		// Phase 0 diagnostics: split the connection options on '?' so we can prove the version option
		// still survives and confirm a future ProfileToken=/ProfileId= would be readable from here.
		{
			size_t Start{0};
			while (Start <= OptionsStr.size())
			{
				const size_t Next = OptionsStr.find('?', Start);
				const std::string Entry = OptionsStr.substr(Start, Next == std::string::npos ? std::string::npos : Next - Start);

				if (!Entry.empty()) LogA("MP-PreLogin-Option", std::format("[Entry]: {}", Entry));

				if (Next == std::string::npos) break;
				Start = Next + 1;
			}
		}

		if (AJB::bServerHasPassword)
		{
			std::string ServerPasswordStr = AJB::NAME_ServerPassword.ToString();
			if (OptionsStr.find(ServerPasswordStr) == std::string::npos)
			{
				bFailedPreLogin = true;
				FailureMessage = &PASSWORDPROTECTED;
			}
		}

		if (Options->ToWString().find(AJB::DLLCommitVersion) == std::wstring::npos)
		{
			bFailedPreLogin = true;
			FailureMessage = &CLIENTINCOMPATIBLE;			
		}

		if (bFailedPreLogin)
		{
			AJB::CopyString(ErrorMessage, FailureMessage);
			LogA("PreLogin Failure", FailureMessage->ToString());
			return;
		}

		// Phase 3: the join must carry a readable account id. A missing or malformed identity
		// is rejected here rather than being allowed to create a placeholder player later.
		std::string AccountId;
		std::string ProfileToken;

		if (!AJB::Server::ParseProfileOptions(OptionsStr, AccountId, ProfileToken))
		{
			static SDK::FString NOPROFILE{L"NO ACCOUNT PROFILE | MISSING ProfileId"};
			AJB::CopyString(ErrorMessage, &NOPROFILE);
			LogA("MP-PreLogin-Reject", std::format("[Options]: {} | [Reason]: {}", OptionsStr, NOPROFILE.ToString()));
			return;
		}

		// A second connection claiming an account that is already in the room is refused so
		// two players can never exist for one account id.
		if (AJB::Server::IsAccountAlreadyConnected(AccountId, nullptr))
		{
			// Note: "DUPLICATE" is a Win32 macro, a distinct name is required here.
			static SDK::FString AlreadyConnected{L"ACCOUNT ALREADY CONNECTED"};
			AJB::CopyString(ErrorMessage, &AlreadyConnected);
			LogA("MP-PreLogin-Reject", std::format("[AccountId]: {} | [Reason]: {}", AccountId, AlreadyConnected.ToString()));
			return;
		}

		LogA("MP-PreLogin-Accept", std::format("[AccountId]: {} | [ProfileToken]: {}", AccountId, ProfileToken.empty() ? "None" : "Present"));
	}
}

void AJB::Server::Login(SDK::AGameModeBase* This, SDK::UPlayer* NewPlayer, SDK::ENetRole InRemoteRole, UC::FString& Portal, UC::FString& Options, SDK::FUniqueNetIdRepl& UniqueId, UC::FString& ErrorMessage)
{
	const std::string Op = Options.ToString();

	// Phase 3: stage the identity on the connection entry so PostLogin can resolve it without
	// touching the options string again.
	if (AJB::IsServer() && NewPlayer)
	{
		std::string AccountId;
		std::string ProfileToken;

		if (AJB::Server::ParseProfileOptions(Op, AccountId, ProfileToken))
		{
			const int32 Index = FindConnectionIndex(reinterpret_cast<SDK::UNetConnection*>(NewPlayer));
			if (Index != -1)
			{
				ClientConnections[Index].AccountId = AccountId;
				ClientConnections[Index].ProfileToken = ProfileToken;

				LogA("MP-Login-Staged", std::format("[AccountId]: {} | [ProfileToken]: {}", AccountId, ProfileToken.empty() ? "None" : "Present"));
			}
		}
	}

	if (AJB::bServerAllowsAdmins)
	{
		if (Op.find(AJB::NAME_AdminPassword.ToString()) != std::string::npos)
		{
			LogA("Admin", NewPlayer->GetFullName());

			const int32 Index = FindConnectionIndex((SDK::UNetConnection*)NewPlayer);
			ClientConnections[Index].SetFlag(bIsAdmin, true);
			LogA("Admin", ClientConnections[Index].Connection->GetFullName());
		}
	}
}

void AJB::Server::PostLogin(SDK::AGameModeBase* This, SDK::APlayerController* Player)
{
	AJB::DumpMatchingPlayers("MP-PostLogin-Before");

	if (!AJB::MOD_Global_Synchronizer)
	{
		SDK::UWorld* World = GWorld;
		if (World && World->NetDriver && World->NetDriver->ClientConnections.Num() > 0) {
			AJB::MOD_Global_Synchronizer = (SDK::ABP_Synchronizer_C*)Pointers::SpawnActorInternal(GWorld.GetPointer(), SDK::UClass::FindClass("BlueprintGeneratedClass BP_Synchronizer.BP_Synchronizer_C"), SDK::FVector{}, SDK::FRotator{}, Pointers::FActorSpawnParameters{ static_cast<unsigned char>(SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn) });
		}
	}
	if (AJB::MOD_Global_Synchronizer)
	{
		AJB::MOD_Global_Synchronizer->OnRep_ReplicateMovement();
		if (AJB::bDebugModeFromCMLA) LogA("GLOBAL SYNCHRONIZER", std::format("[Object]: {} | [Replicated PlayMode]: {}", AJB::MOD_Global_Synchronizer->GetFullName(), AJB::MOD_Global_Synchronizer->PlayMode));
	}



	/*	This actually reflects on both the server and client but it needs some adjusting and needs to run ONLY when it's the server.
	
	if (Player->IsA(SDK::AAJBPlayerControllerBase::StaticClass()))
	{
		const int PlayerCount = This->GameState->PlayerArray.Num();

		std::wstring Id = L"AJB-Player-";
		Id += std::to_wstring(PlayerCount);
		
		SDK::FString NewUniqueId{Id.c_str()};

		AJB::CopyString(&static_cast<SDK::AAJBPlayerControllerBase*>(Player)->GameServerUniqueID, &NewUniqueId);
	}*/

	AJB::DumpMatchingPlayers("MP-PostLogin-After");

	// Phase 3: the host is the only writer of MatchingPlayers. Resolve the joiner's profile off
	// the game thread, then commit the full snapshot entry under the stable account key.
	if (AJB::IsServer() && Player)
	{
		const int32 Index = FindConnectionIndex(Player->NetConnection);
		if (Index != -1)
		{
			FAJBNetConnection& Connection = ClientConnections[Index];

			if (!Connection.AccountId.empty())
			{
				// Phase 3/6: the provider is started here but never awaited here. The worker
				// thread does the file or network work, the tick applies the result and either
				// commits the player or drops the connection with a logged reason.
				AJB::Server::BeginConnectionProfileResolve(Connection);
			}
		}
	}
}

bool AJB::Server::Invoke(SDK::UFunction* This, SDK::UObject* Obj, void* FFrame_Stack, void* Result)
{
	constexpr const wchar_t* SDT_CheatNames[] = 
	{
		L"ROS_DebugCharaChange", L"ROS_DebugLastSurvivor", L"DebugAutoFullMP_On", L"ROS_DebugEnableAirJump", L"ROS_DebugChangeSuperJump", L"ROS_DebugSPMax",
		L"ROS_DebugSetNPCNum", L"ROS_DebugCPMax", L"ROS_DebugChangeCollisionEnable", L"ROS_DebugAPMax", L"ROS_DebugAddPassiveSkill", L"ROS_Debug_FinishMatching",
		L"ROS_Debug_BitesTheDustForceActive", L"ROS_Debug_ChangeDamageArea", L"DebugChangeForceFireSkillCore", L"DebugForceFireSkill_On",
	};
	constexpr int32 SIZE_CheatNames = sizeof(SDT_CheatNames) / sizeof(SDT_CheatNames[0]);
	static SDK::FName AdminOnlyCheats[SIZE_CheatNames]{};

	static bool bCheatsInitialized{false};
	if (!bCheatsInitialized)
	{
		bCheatsInitialized = true;
		for (int i{0}; i < SIZE_CheatNames; ++i)
		{
			AdminOnlyCheats[i] = FName::NAME_FindOrAdd(SDT_CheatNames[i]);
		}
	}


	for (int i{0}; i < SIZE_CheatNames; ++i)
	{
		if (This->Name == AdminOnlyCheats[i])
		{
			SDK::UNetConnection* NetConnection{nullptr};

			if (Obj->IsA(SDK::APlayerController::StaticClass()))
			{
				NetConnection = static_cast<SDK::APlayerController*>(Obj)->NetConnection;					
			}
			else if (Obj->IsA(SDK::ACharacter::StaticClass()))
			{
				if (SDK::APlayerController* Controller = static_cast<SDK::APlayerController*>(static_cast<SDK::ACharacter*>(Obj)->Owner))
				{
					if (Controller->IsA(SDK::APlayerController::StaticClass())) NetConnection = Controller->NetConnection;
				}
			}

			if (NetConnection)
			{
				bool bIsUnauthorized{true};

				if (AJB::bServerAllowsAdmins)
				{
					bIsUnauthorized = !IsAdmin(NetConnection);
				}

				if (bIsUnauthorized)
				{
					LogA("Kicked for cheating", Obj->GetFullName());
					
					OFFSET::VFTable<void(__thiscall*)(SDK::APlayerController*, const SDK::FText&)>(NetConnection->PlayerController)[OFF::VFT_ClientMainMenu](NetConnection->PlayerController, (Call<SDK::FText(*)(const SDK::FString&)>(PB(0x5FF820))(SDK::FString{L"CHEATING PIECE OF "}))); // Amazingly works first try first ever compilation, but is identical to me just closing it with UNetConnection::Close
					//Call<void(__thiscall*)(SDK::APlayerController*, const SDK::FText&)>(PB(0x18CC510))(NetConnection->PlayerController, (Call<SDK::FText(*)(const SDK::FString&)>(PB(0x5FF820))(SDK::FString{L"CHEATING PIECE OF "}))); //  Does nothing

					//OFFSET::VFTable<void(__thiscall*)(SDK::APlayerController*, const SDK::FString&)>(NetConnection->PlayerController)[OFF::VFT_LocalTravel](NetConnection->PlayerController, SDK::FString{L"/Game/Aeyth8/Maps/Purgatory/Purgatory"});
					//NetConnection->PlayerController->ClientTravel(L"/Game/Aeyth8/Maps/Purgatory/Purgatory", SDK::ETravelType::TRAVEL_Absolute, true, SDK::FGuid{});
					//UFunctions::CloseConnection(NetConnection);
					return false;
				}
			}			
		}	
	}

	return true;
}

void AJB::Server::AddClientConnection(SDK::UNetDriver* This, SDK::UNetConnection* Connection)
{
	FAJBNetConnection NewClient{(SDK::UIpConnection*)Connection};
	ClientConnections.push_back(NewClient);
}

void AJB::Server::CloseConnection(SDK::UNetConnection* This)
{
	int32 ConnectionIndex = FindConnectionIndex(This);
	if (ConnectionIndex != -1)
	{
		// PlayerID assignments are intentionally kept for the rest of the session so an active
		// player is never renumbered, only the per-connection profile staging is dropped.
		LogA("CloseConnection", std::format("[AccountId]: {} | [PlayerID]: {} | [Note]: assignment retained until session end",
			ClientConnections[ConnectionIndex].AccountId, ClientConnections[ConnectionIndex].AssignedPlayerID));

		ClientConnections.erase(ClientConnections.begin() + ConnectionIndex);

		// Phase 4: the remaining clients are refreshed so a leaving player is removed from
		// every snapshot while the survivors keep their entries and PlayerIDs.
		if (AJB::IsServer() && AJB::Instance)
		{
			AJB::Server::PruneMatchingPlayersForLiveConnections();
			AJB::Server::BroadcastMatchingPlayers("PlayerLeft");
		}
	}
}

// VFT Function Reimplementation

// Definition located in GameWelcomePlayer.asm
extern "C" void ASMGrabRedirectURL(qword Agony);
extern "C" qword RedirectURLAddress{0};
extern "C" qword PointerOfAgony{0};

struct PointerToStruct
{
	qword JumpTo;
	qword CopyStringCall;
	qword RedirectURL;
	qword RCX;
	qword RDX;
	qword RAX;
};

static PointerToStruct StructureOfHell{0};
static SDK::FString NewRedirectURL{L"KILLYOURSELFNOW"};

// Hookception :lemon_possessing:
// Basically this hook is the TRUE function, GameWelcomePlayer does not exist in the build, it was stripped.
// However there still remains the function call for it, and it's a blank return function, BUT at that specific instruction r8 contains RedirectURL.
// I redirected it to my own function hook and since I had to preserve the registers I had to whip something up in assembly that would properly modify the RedirectURL and preserve the flow.
// The ASM_GameWelcomePlayer hook is basically an inline function hook, WorldWelcomePlayer is the actual function hook which gets called before it is actually called.

void AJB::Server::WorldWelcomePlayer(SDK::UWorld* This, SDK::UNetConnection* Connection)
{
	std::string Feck(AJB::NAME_PreJoinParameters.ToString());
	std::wstring Frick(Feck.begin(), Feck.end());
	SDK::FString Frock(Frick.c_str());
	AJB::CopyString(&NewRedirectURL, &Frock);
	OFF::WorldWelcomePlayer.VerifyFC<void(__thiscall*)(SDK::UWorld*, SDK::UNetConnection*)>()(This, Connection);
}



extern "C" void InitHellscape()
{
	StructureOfHell.JumpTo = PB(OFF::WelcomePlayer6B);
	StructureOfHell.CopyStringCall = OFF::CopyString.PlusBase();
	StructureOfHell.RedirectURL = (qword)&NewRedirectURL;
	StructureOfHell.RCX = 0;
	StructureOfHell.RDX = 0;
	StructureOfHell.RAX = 0;

	PointerOfAgony = (qword)&StructureOfHell;
}



void AJB::Server::ASM_GameWelcomePlayer(SDK::AGameModeBase* This, SDK::UNetConnection* Connection, UC::FString& RedirectURL)
{
	ASMGrabRedirectURL(PointerOfAgony);
	
	
	//LogA("RedirectURL Address", HexToString(RedirectURLAddress));
	//AJB::CopyString(&RedirectURL, &KYS);
	//LogA("ASM_GameWelcomePlayer", std::format("[This]: {} | [RedirectURL]: {}", This->GetFullName(), RedirectURL.ToString()));
	//OFF::WelcomePlayerStripped.VerifyFC<void(__thiscall*)(SDK::AGameModeBase*, SDK::UNetConnection*, UC::FString&)>()(This, Connection, RedirectURL);
}

// ===========================================
// ##	PHASE 3 : HOST AUTHORITY			   ##
// ===========================================

A8CL::uint8 AJB::SessionPlayerRegistry::GetOrAssignPlayerID(const std::string& AccountId)
{
	if (AccountId.empty()) return 0;

	const auto Existing = AccountToPlayerID.find(AccountId);
	if (Existing != AccountToPlayerID.end())
	{
		// Reconnecting with a known account restores the original PlayerID.
		return Existing->second;
	}

	const uint8 Assigned = NextPlayerID++;
	AccountToPlayerID[AccountId] = Assigned;

	return Assigned;
}

bool AJB::SessionPlayerRegistry::HasAccount(const std::string& AccountId) const
{
	return AccountToPlayerID.find(AccountId) != AccountToPlayerID.end();
}

void AJB::SessionPlayerRegistry::AdoptPlayerID(const std::string& AccountId, uint8 PlayerID)
{
	if (AccountId.empty() || PlayerID == 0) return;

	AccountToPlayerID[AccountId] = PlayerID;

	// The game may hand its own row a number beyond our counter, never reuse it for a joiner.
	if (PlayerID >= NextPlayerID) NextPlayerID = static_cast<uint8>(PlayerID + 1);
}

void AJB::SessionPlayerRegistry::Reset()
{
	AccountToPlayerID.clear();
	NextPlayerID = 1;
}

void AJB::BeginSession()
{
	// Phase 3/5: a map switch re-runs InitListen, so a second call while a session is already
	// active must NOT wipe the registry. Clearing only on the first InitListen of a room is
	// what satisfies "map switches never clear the session registry".
	if (gbSessionActive)
	{
		LogA("Session", "[BeginSession]: A session is already active, the player registry and profile cache are kept for the map switch.");
		return;
	}

	gbSessionActive = true;

	// A new room session starts from a clean assignment table.
	AJB::PlayerRegistry.Reset();
	AJB::ProfileCache.Clear();
	LogA("Session", "[BeginSession]: Session player registry cleared.");

	// The host's own account is part of the room too. It is seeded here so the rebuild after
	// the map switch keeps the host's real profile instead of the game's placeholder row.
	AJB::EnsureLocalHostInSessionCache();
}

// Phase 3/5: the host's own row is created by the game under its local player name, and it is
// a placeholder (NO NAME / IconID -1 / CharactorID 0 or a leftover). The real profile is copied
// into that same row so the host is a first class player rather than a second entry.
void AJB::EnsureLocalHostInSessionCache()
{
	if (!AJB::bHasLocalProfileRecord) return;

	// Only a listen server owns MatchingPlayers. In offline mode the game manages its own
	// table and must not be touched, so every caller is funnelled through this guard.
	if (!AJB::IsServer()) return;

	const ProfileRecord& Profile = AJB::LocalProfileRecord;
	if (Profile.AccountId.empty()) return;

	AJB::ProfileCache.Store(Profile.AccountId, Profile);

	// Adopt the game's own row key and PlayerID when it exists, so the rebuild updates the row
	// the game already tracks instead of adding a duplicate host entry.
	if (!TryAdoptLocalHostRow())
	{
		LogA("Session", std::format("[EnsureLocalHostInSessionCache]: [AccountId]: {} | [State]: no local row yet, it will be seeded by the rebuild.", Profile.AccountId));
		return;
	}

	// The game's own host row is a placeholder (NO NAME / IconID -1). Writing the validated
	// profile back under the same key replaces it in place, so the waiting list shows the real
	// name, icon, level and title instead of the local player name the game seeded.
	const std::string Key{ GetSessionKey(Profile.AccountId) };

	const uint8 PlayerID = AJB::PlayerRegistry.GetOrAssignPlayerID(Profile.AccountId);
	if (PlayerID == 0) return;

	SDK::FMatchingPlayerInfo Info{};
	if (!AJB::BuildMatchingPlayerInfoFromProfile(Profile, &Info)) return;

	Info.PlayerID = PlayerID;

	// The character stays game local. A recorded selection wins, otherwise the permitted
	// default is used so the host row never carries CharactorID 0.
	const auto Cached = AJB::ProfileCache.CharacterIds.find(Profile.AccountId);
	Info.CharactorID = (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0)
		? Cached->second
		: static_cast<uint8>(A8CL::AJB::JOTARO);

	const std::wstring KeyText(Key.begin(), Key.end());
	SDK::FString KeyString{ KeyText.c_str() };

	AJB::Instance->AddMatchingPlayerInfo(KeyString, Info);

	LogA("Session", std::format("[EnsureLocalHostInSessionCache]: [AccountId]: {} | [Key]: {} | [PlayerID]: {} | [PlayerName]: {}", Profile.AccountId, Key, PlayerID, SDK::FString(Profile.PlayerName.c_str()).ToString()));
}

void AJB::EndSession()
{
	gbSessionActive = false;

	AJB::PlayerRegistry.Reset();
	AJB::ProfileCache.Clear();
	AJB::ClientConnections.clear();
	LogA("Session", "[EndSession]: Session player registry and connection table cleared.");
}

bool AJB::Server::ParseProfileOptions(const std::string& Options, std::string& OutAccountId, std::string& OutProfileToken)
{
	OutAccountId.clear();
	OutProfileToken.clear();

	// Connection options arrive as `?A?B?C`, each entry may be `Key=Value`.
	size_t Start{0};
	while (Start <= Options.size())
	{
		const size_t Next = Options.find('?', Start);
		const std::string Entry = Options.substr(Start, Next == std::string::npos ? std::string::npos : Next - Start);

		constexpr const char* ProfileIdPrefix{"ProfileId="};
		constexpr const char* ProfileTokenPrefix{"ProfileToken="};

		if (Entry.rfind(ProfileIdPrefix, 0) == 0)
		{
			OutAccountId = Entry.substr(std::char_traits<char>::length(ProfileIdPrefix));
		}
		else if (Entry.rfind(ProfileTokenPrefix, 0) == 0)
		{
			OutProfileToken = Entry.substr(std::char_traits<char>::length(ProfileTokenPrefix));
		}

		if (Next == std::string::npos) break;
		Start = Next + 1;
	}

	return !OutAccountId.empty();
}

AJB::FAJBNetConnection* AJB::Server::FindConnectionByAccount(const std::string& AccountId)
{
	if (AccountId.empty()) return nullptr;

	for (FAJBNetConnection& Entry : ClientConnections)
	{
		if (Entry.AccountId == AccountId) return &Entry;
	}

	return nullptr;
}

bool AJB::Server::IsAccountAlreadyConnected(const std::string& AccountId, const SDK::UNetConnection* Ignored)
{
	if (AccountId.empty()) return false;

	for (const FAJBNetConnection& Entry : ClientConnections)
	{
		if (Entry.Connection == Ignored) continue;
		if (Entry.AccountId != AccountId) continue;

		// Only a connection that still owns a live player controller counts as a duplicate.
		// A stale entry from a dropped link must not block the account from reconnecting.
		const bool bIsLive = Entry.Connection && Entry.Connection->PlayerController;

		if (bIsLive) return true;
	}

	return false;
}

bool AJB::Server::BeginConnectionProfileResolve(FAJBNetConnection& Connection)
{
	if (Connection.AccountId.empty())
	{
		Connection.bProfileFailed = true;
		LogA("BeginConnectionProfileResolve", "[Error]: The connection carries no ProfileId.");
		return false;
	}

	ProfileResolveRequest Request{};
	Request.AccountId = Connection.AccountId;
	Request.ProfileToken = Connection.ProfileToken;

	Connection.PendingResolve = StartProfileResolve(Request);

	if (!Connection.PendingResolve.bValid)
	{
		Connection.bProfileFailed = true;
		Connection.bProfileResolved = false;

		LogA("BeginConnectionProfileResolve", std::format("[AccountId]: {} | [Error]: the provider is disabled", Connection.AccountId));

		// The joiner can never get a profile, so it is dropped here with a logged reason
		// instead of being left in the room without an entry.
		if (Connection.Connection && Connection.Connection->PlayerController)
		{
			UFunctions::CloseConnection(Connection.Connection);
		}

		return false;
	}

	LogA("BeginConnectionProfileResolve", std::format("[AccountId]: {} | [Token]: {}", Connection.AccountId, Connection.ProfileToken.empty() ? "None" : "Present"));

	return true;
}

void AJB::Server::TickConnectionProfileResolves()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	for (size_t i{ 0 }; i < ClientConnections.size(); ++i)
	{
		FAJBNetConnection& Connection = ClientConnections[i];

		if (!Connection.PendingResolve.bValid) continue;
		if (Connection.bProfileResolved || Connection.bProfileFailed) continue;

		ProfileResolveResponse Response{};
		bool bTimedOut{false};

		if (!PollProfileResolve(Connection.PendingResolve, Response, bTimedOut))
		{
			if (!bTimedOut) continue;

			// The bounded wait expired. The joiner is dropped with a logged reason and never
			// receives a MatchingPlayers entry.
			Connection.bProfileFailed = true;

			LogA("MP-Resolve-Reject", std::format("[AccountId]: {} | [Reason]: the profile did not resolve within the bounded wait", Connection.AccountId));

			if (Connection.Connection && Connection.Connection->PlayerController)
			{
				UFunctions::CloseConnection(Connection.Connection);
				--i;
			}

			continue;
		}

		if (!Response.bSuccess)
		{
			Connection.bProfileFailed = true;
			Connection.bProfileResolved = false;

			LogA("MP-Resolve-Reject", std::format("[AccountId]: {} | [Reason]: {}", Connection.AccountId, Response.Error));

			if (Connection.Connection && Connection.Connection->PlayerController)
			{
				UFunctions::CloseConnection(Connection.Connection);
				--i;
			}

			continue;
		}

		Connection.Profile = Response.Record;
		Connection.Profile.AccountId = Connection.AccountId;
		Connection.Profile.ProfileToken = Connection.ProfileToken;
		Connection.bProfileResolved = true;
		Connection.bProfileFailed = false;

		AJB::ProfileCache.Store(Connection.AccountId, Connection.Profile);

		LogA("MP-Resolve-Accept", std::format("[AccountId]: {} | [PlayerName]: {} | [PlayerIconID]: {} | [PlayerLevel]: {} | [PlayerTitle]: {} | [Source]: {}",
			Connection.AccountId, SDK::FString(Connection.Profile.PlayerName.c_str()).ToString(), Connection.Profile.PlayerIconID,
			Connection.Profile.PlayerLevel, SDK::FString(Connection.Profile.PlayerTitle.c_str()).ToString(), static_cast<int>(Response.Source)));

		if (AJB::Server::CommitConnectionToMatchingPlayers(Connection))
		{
			// The post commit table is dumped under the documented tag so the real data can be
			// read from the same checkpoint the synchronous design used.
			AJB::DumpMatchingPlayers("MP-PostLogin-After");
			AJB::Server::BroadcastMatchingPlayers("ProfileResolved");
		}
	}
}

bool AJB::Server::CommitConnectionToMatchingPlayers(FAJBNetConnection& Connection)
{
	if (!AJB::IsServer()) return false;
	if (!AJB::Instance) return false;

	if (!Connection.bProfileResolved || Connection.bProfileFailed)
	{
		LogA("CommitConnectionToMatchingPlayers", std::format("[Error]: Refusing to commit an unresolved profile for [AccountId]: {}", Connection.AccountId));
		return false;
	}

	const uint8 PlayerID = AJB::PlayerRegistry.GetOrAssignPlayerID(Connection.AccountId);
	if (PlayerID == 0)
	{
		LogA("CommitConnectionToMatchingPlayers", "[Error]: The session registry refused to assign a PlayerID.");
		return false;
	}

	Connection.AssignedPlayerID = PlayerID;

	SDK::FMatchingPlayerInfo Info{};
	if (!AJB::BuildMatchingPlayerInfoFromProfile(Connection.Profile, &Info))
	{
		LogA("CommitConnectionToMatchingPlayers", std::format("[Error]: Failed to build player info for [AccountId]: {}", Connection.AccountId));
		return false;
	}

	Info.PlayerID = PlayerID;

	// A reconnect inside the battle map must not lose the character the flow already chose.
	// A first join has no recorded selection yet, and the game spawns the player during the
	// travel into AJBStage01_P, before the character select step can ever run on the host.
	// Leaving CharactorID at 0 is what makes the battle map hang on the loading screen with
	// BROKEN_CHARACTER_SPAWN, so the permitted default character is written here with the
	// reason logged. The real selection still overwrites this later if the flow records one.
	const auto Cached = AJB::ProfileCache.CharacterIds.find(Connection.AccountId);
	if (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0)
	{
		Info.CharactorID = Cached->second;
	}
	else
	{
		Info.CharactorID = static_cast<uint8>(A8CL::AJB::JOTARO);

		LogA("CommitConnectionToMatchingPlayers", std::format("[AccountId]: {} | [Reason]: no character selection is recorded yet, the permitted default character {} is written so the spawn is never left with CharactorID 0.", Connection.AccountId, Info.CharactorID));
	}

	// The map key is the stable account id, never the array index or the connection order.
	// SDK::FString does not own its buffer, so the wide text is held in a named local that
	// outlives the call instead of a temporary that dies at the end of this expression.
	const std::wstring KeyText(Connection.AccountId.begin(), Connection.AccountId.end());
	SDK::FString Key{ KeyText.c_str() };

	AJB::Instance->AddMatchingPlayerInfo(Key, Info);

	// Remember the actual game key for this account so every later rebuild and every
	// character write addresses the row the game really created.
	AJB::ProfileCache.AccountToKey[Connection.AccountId] = Connection.AccountId;

	LogA("CommitConnectionToMatchingPlayers", std::format("[AccountId]: {} | [PlayerID]: {} | [Key]: {}", Connection.AccountId, PlayerID, Key.ToString()));

	return true;
}

void AJB::Server::DumpSessionRegistry()
{
	LogA("SessionRegistry", std::format("[Accounts]: {} | [NextPlayerID]: {}", AJB::PlayerRegistry.Num(), AJB::PlayerRegistry.NextPlayerID));

	for (const auto& Entry : AJB::PlayerRegistry.AccountToPlayerID)
	{
		const FAJBNetConnection* Connection = AJB::Server::FindConnectionByAccount(Entry.first);

		LogA("SessionRegistry", std::format("[AccountId]: {} | [PlayerID]: {} | [Connected]: {} | [ProfileResolved]: {} | [ProfileFailed]: {}",
			Entry.first, Entry.second, Connection ? "Yes" : "No",
			Connection && Connection->bProfileResolved, Connection && Connection->bProfileFailed));
	}
}

// Phase 4/5: after someone leaves, the host table is rebuilt so it only holds players with a
// live connection. The session registry deliberately keeps the departed PlayerID, so the same
// account reconnecting later restores it while the table itself never grows a stale entry.
void AJB::Server::PruneMatchingPlayersForLiveConnections()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	int32 Kept{0};

	AJB::Instance->ClearMatchingPlayerInfo(false);

	// Phase 5: the host's own account is part of the room and has no remote connection, so it
	// is re-added from the session cache together with every live joiner.
	if (AJB::bHasLocalProfileRecord)
	{
		const std::string& LocalAccount = AJB::LocalProfileRecord.AccountId;

		if (AJB::ProfileCache.Has(LocalAccount))
		{
			const uint8 PlayerID = AJB::PlayerRegistry.GetOrAssignPlayerID(LocalAccount);

			SDK::FMatchingPlayerInfo Info{};
			if (PlayerID != 0 && AJB::BuildMatchingPlayerInfoFromProfile(AJB::ProfileCache.Profiles[LocalAccount], &Info))
			{
				Info.PlayerID = PlayerID;
				Info.CharactorID = static_cast<uint8>(A8CL::AJB::JOTARO);

				const auto Cached = AJB::ProfileCache.CharacterIds.find(LocalAccount);
				if (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0) Info.CharactorID = Cached->second;

				const std::string Key{ GetSessionKey(LocalAccount) };
				const std::wstring KeyText(Key.begin(), Key.end());
				SDK::FString KeyString{ KeyText.c_str() };

				AJB::Instance->AddMatchingPlayerInfo(KeyString, Info);
				++Kept;
			}
		}
	}

	for (const FAJBNetConnection& Connection : ClientConnections)
	{
		if (!Connection.bProfileResolved || Connection.bProfileFailed) continue;
		if (Connection.AccountId.empty()) continue;
		if (!Connection.Connection || !Connection.Connection->PlayerController) continue;

		const uint8 PlayerID = AJB::PlayerRegistry.GetOrAssignPlayerID(Connection.AccountId);
		if (PlayerID == 0) continue;

		SDK::FMatchingPlayerInfo Info{};
		if (!AJB::BuildMatchingPlayerInfoFromProfile(Connection.Profile, &Info))
		{
			LogA("PruneMatchingPlayersForLiveConnections", std::format("[Error]: Failed to rebuild [AccountId]: {}", Connection.AccountId));
			continue;
		}

		Info.PlayerID = PlayerID;
		Info.CharactorID = static_cast<uint8>(A8CL::AJB::JOTARO);

		const auto Cached = AJB::ProfileCache.CharacterIds.find(Connection.AccountId);
		if (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0)
		{
			Info.CharactorID = Cached->second;
		}

		const std::wstring KeyText(Connection.AccountId.begin(), Connection.AccountId.end());
		SDK::FString Key{ KeyText.c_str() };
		AJB::Instance->AddMatchingPlayerInfo(Key, Info);

		AJB::ProfileCache.AccountToKey[Connection.AccountId] = Connection.AccountId;

		++Kept;
	}

	LogA("MP-Prune", std::format("[Kept]: {} | [Connections]: {}", Kept, ClientConnections.size()));
}

void AJB::CacheCharacterSelection(const std::string& AccountId, uint8 CharacterId)
{
	if (AccountId.empty()) return;

	AJB::ProfileCache.CharacterIds[AccountId] = CharacterId;
}

void AJB::Server::RebuildMatchingPlayersFromSession()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	// Phase 5: the map switch recreates the GameInstance player table. Whenever anything is in
	// the session cache the table is rebuilt from it. A cache that happens to be empty must not
	// silently leave the battle map without the host's own entry, so the local profile is
	// seeded first.
	AJB::EnsureLocalHostInSessionCache();

	if (AJB::ProfileCache.Profiles.empty()) return;

	// The battle map recreates the GameInstance player table, so every entry is rebuilt from
	// the plain C++ session cache. Character ids are game data, never taken from the profile.
	int32 Rebuilt{0};

	for (const auto& Entry : AJB::ProfileCache.Profiles)
	{
		const std::string& AccountId = Entry.first;
		const ProfileRecord& Profile = Entry.second;

		const uint8 PlayerID = AJB::PlayerRegistry.GetOrAssignPlayerID(AccountId);
		if (PlayerID == 0) continue;

		SDK::FMatchingPlayerInfo Info{};
		if (!AJB::BuildMatchingPlayerInfoFromProfile(Profile, &Info))
		{
			LogA("RebuildMatchingPlayersFromSession", std::format("[Error]: Failed to rebuild [AccountId]: {}", AccountId));
			continue;
		}

		Info.PlayerID = PlayerID;

		// Phase 5: a character id is required before InGame.Standby. When the flow has not
		// produced one yet, the permitted default character is written with a logged reason.
		const auto Cached = AJB::ProfileCache.CharacterIds.find(AccountId);

		if (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0)
		{
			Info.CharactorID = Cached->second;
		}
		else
		{
			Info.CharactorID = static_cast<uint8>(A8CL::AJB::JOTARO);

			LogA("RebuildMatchingPlayersFromSession", std::format("[AccountId]: {} | [Reason]: no character selection was recorded, using the default character {}", AccountId, Info.CharactorID));
		}

		// The host's own row is keyed by the local player name the game created, everybody
		// else is keyed by their account id. GetSessionKey returns the real game key.
		const std::string Key{ GetSessionKey(AccountId) };
		const std::wstring KeyText(Key.begin(), Key.end());
		SDK::FString KeyString{ KeyText.c_str() };
		AJB::Instance->AddMatchingPlayerInfo(KeyString, Info);

		++Rebuilt;
	}

	LogA("MP-Session-Rebuild", std::format("[Rebuilt]: {} | [Registry]: {}", Rebuilt, AJB::PlayerRegistry.Num()));
}
// Phase 5: every resolved player gets its character id written through the game's own
// Phase 5: the game flow itself writes the real CharactorID into MatchingPlayers during
// character selection. Just before the battle map is entered that data is harvested into the
// session cache, so the rebuild after the map switch preserves it instead of defaulting.
void AJB::Server::HarvestCharacterSelectionsFromHostTable()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	int32 Harvested{0};

	for (int32 i{ 0 }; i < AJB::Instance->MatchingPlayers.Num(); ++i)
	{
		const SDK::FMatchingPlayerInfo& Info = AJB::Instance->MatchingPlayers[i].Second;

		if (Info.CharactorID == 0) continue;

		const std::string Key{ AJB::Instance->MatchingPlayers[i].First.ToString() };
		if (Key.empty()) continue;

		// The row key may be the local player name rather than the account id, so it is mapped
		// back before the selection is cached under the account.
		const std::string AccountId{ AccountIdForKey(Key) };

		AJB::ProfileCache.CharacterIds[AccountId] = Info.CharactorID;
		++Harvested;
	}

	LogA("MP-CharacterHarvest", std::format("[Harvested]: {} | [Registry]: {}", Harvested, AJB::ProfileCache.Num()));
}

// Blueprint update call before the loading checks run. The profile only supplies
// cosmetics; the character itself stays a game-local decision.
void AJB::Server::ApplyCharacterSelectionsToSession()
{
	if (!AJB::IsServer()) return;
	if (!AJB::Instance) return;

	// The host's own account must be covered here too, otherwise only the joiner would be
	// written a character id and the host would keep CharactorID 0.
	AJB::EnsureLocalHostInSessionCache();

	if (AJB::ProfileCache.Profiles.empty()) return;

	constexpr uint8 DefaultCharacter = static_cast<uint8>(A8CL::AJB::JOTARO);

	for (const auto& Entry : AJB::ProfileCache.Profiles)
	{
		const std::string& AccountId = Entry.first;
		const ProfileRecord& Profile = Entry.second;

		const auto Cached = AJB::ProfileCache.CharacterIds.find(AccountId);

		uint8 CharacterId{0};
		const char* Reason{"recorded selection"};

		if (Cached != AJB::ProfileCache.CharacterIds.end() && Cached->second != 0)
		{
			CharacterId = Cached->second;
		}
		else
		{
			CharacterId = DefaultCharacter;
			Reason = "no selection was recorded, the permitted default character is used";
		}

		// The account cosmetics travel through the game's own update path, which also applies
		// the character id to the MatchingPlayers entry for this user id.
		SDK::FCustomData CustomData{};
		if (!AJB::BuildCustomDataFromProfile(Profile, &CustomData))
		{
			LogA("ApplyCharacterSelectionsToSession", std::format("[AccountId]: {} | [Error]: the custom data could not be built, the character is still written", AccountId));
			CustomData = SDK::FCustomData{};
		}

		// The game's update call is addressed by the row key the game created for this player,
		// which for the host is the local player name rather than the account id.
		const std::string Key{ GetSessionKey(AccountId) };
		const std::wstring UserIdText(Key.begin(), Key.end());
		SDK::FString UserId{ UserIdText.c_str() };

		const bool bUpdated = AJB::Instance->TryUpdateCustomDataAndCharacterIDByUserID(UserId, CustomData, CharacterId);

		LogA("MP-CharacterWrite", std::format("[AccountId]: {} | [CharacterID]: {} | [Updated]: {} | [Reason]: {}", AccountId, CharacterId, bUpdated, Reason));

		// The Blueprint path may refuse an unknown user id, so the direct table write is the
		// guaranteed fallback and it never leaves CharactorID at 0.
		if (!bUpdated)
		{
			AJB::Instance->SetCharacterIDFromPlayerName(UserId, CharacterId);
		}

		AJB::ProfileCache.CharacterIds[AccountId] = CharacterId;
	}
}
