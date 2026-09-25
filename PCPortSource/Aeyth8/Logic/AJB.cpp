#include "AJB.h"
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

#include "../Profiles/ProfileRecord.h"
#include "../Profiles/ProfileApply.h"
#include "../Profiles/ProfileSync.h"
#include "../Profiles/ProfileProvider.h"

#include "../../Dumper-7/SDK/BP_AJBGameInstance_classes.hpp"
#include "../../Dumper-7/SDK/EngineSettings_classes.hpp"
#include "../../Dumper-7/SDK/BP_AJBOutGameProxy_classes.hpp"
#include "../../Dumper-7/SDK/BP_AJBInGameCharacter_classes.hpp"
#include "../../Dumper-7/SDK/BP_AJBInGamePlayerController_classes.hpp"
#include "../../Dumper-7/SDK/FlowState_classes.hpp"
#include "../../Dumper-7/SDK/FlowState_structs.hpp"
#include "../../Dumper-7/SDK/BP_PPV_VSFilter_classes.hpp"
#include "../../Dumper-7/SDK/BPF_AJBGameInstance_classes.hpp"

#include "../../Dumper-7/CustomSDK/WBP_OptionsMenu_classes.hpp"				// Custom SDK header (NOT GAME NATIVE)
#include "../../Dumper-7/CustomSDK/BP_GlobalPatcher_classes.hpp"			// Custom SDK header (NOT GAME NATIVE)
#include "../../Dumper-7/CustomSDK/LemonHelper_classes.hpp"					// Custom SDK header (NOT GAME NATIVE)
#include "../../Dumper-7/CustomSDK/BP_Synchronizer_classes.hpp"				// Custom SDK header (NOT GAME NATIVE)

#include <intrin.h>
#include "../../Dumper-7/SDK/WB_Credit_classes.hpp"
#include "../../Dumper-7/SDK/WB_TimeLimitCountDown_classes.hpp"
#include "../../Dumper-7/SDK/WB_PpBuyWindow_classes.hpp"

#include "../../Dumper-7/SDK/AkAudio_classes.hpp"
//#include "../../Dumper-7/SDK/BP_SimpleStartLocationSelectGameMode_classes.hpp" // Removed in JJL10JPN-33

// Needed for ALevelScriptActor hook
#include "../../Dumper-7/SDK/AJBCreadit_classes.hpp"

// Needed for the FlowstateUtil hook
#include "../../Dumper-7/SDK/BP_AJBInGameHUD_classes.hpp"
#include "../../Dumper-7/SDK/WB_FullMap_classes.hpp"

// Needed for AJBSimpleMatch_P translation
#include "../../Dumper-7/SDK/WB_ModeSelect_classes.hpp"
#include "../../Dumper-7/SDK/WB_ModeSelectTextBase_classes.hpp"

// Needed for temporarily fixing the infinite loading screen.
#include "../../Dumper-7/SDK/BP_AJBBattleGameMode_classes.hpp"

// Needed for MediaPlayer constructor hook.
#include "../../Dumper-7/SDK/MediaAssets_classes.hpp"

// ......
#include "Server/Temporary/MRWT.h"

// New native and much more efficient callback timer using the native Engine Tick.
#include "../Tools/TickHook/TickHook.h"

// External callbacks
#include "Callbacks/AJBCallbacks.h"

#include "ServerLogic.h"

// UConsole Parser
#include "../UConsole/Core/UConsole.h"

// Macro because I want to remember where I placed this stuff so I can use it later
#define USING_CUSTOM_MESSAGING_SYSTEM 0

// PHASE 1 A/B TEST SWITCH
// Set to 0 to leave the original UAJBGameInstance::TryGetMatchingPlayerInfoByPlayerIDPureFunction
// completely untouched, which proves whether the mod hook is the thing corrupting player data.
// Set back to 1 to run the corrected hook implemented below.
#define USING_MATCHING_PLAYER_INFO_HOOK 1

/*

Written by Aeyth8

https://github.com/Aeyth8

*/

using namespace A8CL; using namespace Global; using namespace Pointers;

// -- UE Defaults -- 

SDK::UClass*						AJB::CoreUObject{nullptr};
SDK::UGameMapsSettings*				AJB::MapSettings{nullptr};

// -- AJB Specific -- 

SDK::UBP_AJBGameInstance_C*			AJB::Instance{nullptr};
SDK::UAJBAMSystemSettings*			AJB::Settings{nullptr};
SDK::UAJBAMSystemObject*			AJB::System{nullptr};
SDK::ABP_AJBOutGameProxy_C*			AJB::OutGameProxy{nullptr};
SDK::UAJBVersion*					AJB::Version{nullptr};
SDK::UAJBSettings*					AJB::AJBSettings{nullptr};

SDK::AAJBCreadit_C*					AJB::CreaditPointer{nullptr};
SDK::UWB_ModeSelect_C*				AJB::SimpleMatchHUD{nullptr};
SDK::FGameplayTag*					AJB::CurrentFlowstate{nullptr};

__int32*							AJB::PlayerPoints{nullptr};
bool*								AJB::bDebugInputMode{nullptr};

SDK::UMediaPlayer*					AJB::LemonPlayer{nullptr};
SDK::UMaterial*						AJB::M_LemonPossession{nullptr};
SDK::UMaterial*						AJB::AM_LemonPossession{nullptr};

int									AJB::TEMP_CachedCharacterID{1};
int									AJB::NUM_CPUCores{0};

bool								AJB::bDebugModeFromCMLA{false};
bool								AJB::bIsLemonPossessioned{false};
bool								AJB::bLocalProfileAvailable{false};
bool								AJB::bIsFrameRateUncapped{false};



// -- MOD --

SDK::UClass*						AJB::MOD_OptionsMenuClass{nullptr};
SDK::UWBP_OptionsMenu_C*			AJB::MOD_OptionsMenu{nullptr};

SDK::UClass*						AJB::MOD_GlobalPatcherClass{nullptr};
SDK::UBP_GlobalPatcher_C*			AJB::MOD_GlobalPatcher{nullptr};

SDK::UClass*						AJB::MOD_SynchronizerClass{nullptr};
SDK::ABP_Synchronizer_C*			AJB::MOD_PROXY_Synchronizer{nullptr};
SDK::ABP_Synchronizer_C*			AJB::MOD_Global_Synchronizer{nullptr};

const wchar_t*						AJB::DLLCommitVersion{L"[v0.7.5]"};
UC::FString*						AJB::StrDLLCommitVersion{nullptr};
UC::FString*						AJB::StrInGameUserName{nullptr};

// -- Windows External --

HMODULE								AJB::PCPortLib{nullptr};
HWND								AJB::PCPortWindow{nullptr};
bool								AJB::bKeepInitialThreadAlive{true};


// -- Assembly Opcodes -- 

constexpr BYTE MOV{0xB0}; // 8 bit to AL register
constexpr BYTE RETN{0xC3};
constexpr BYTE NOP{0x90};

void __fastcall GetNationalMatchSchedule(SDK::UAJBGameInstance*, bool*, bool*, SDK::FAJBMatchSchedule*, SDK::FAJBMatchScheduleDateTime*, SDK::FAJBMatchScheduleDateTime*);

std::vector<Hooks::HookStructure> StandaloneHooks =
{
	{OFF::UConsole,							UFunctions::UConsole},
	{OFF::ConsoleCommand,					UFunctions::ConsoleCommand},
	{OFF::OutputText,						UConsole::OutputText},
	{OFF::Browse,							UFunctions::Browse},
	{OFF::Login,							UFunctions::Login},
	{OFF::PreLogin,							UFunctions::PreLogin},
	{OFF::AJBPreLogin,						AJB::Server::PreLogin},
	{OFF::InitListen,						UFunctions::InitListen},

#if USING_CUSTOM_MESSAGING_SYSTEM
	{OFF::NotifyControlMessage,				UFunctions::NotifyControlMessage},
#endif

	{OFF::PeekNetworkFailureMessages,		UFunctions::PeekNetworkFailureMessages},
	{OFF::InitLocalConnection,				UFunctions::InitLocalConnection},
	{OFF::AppPreExit,						UFunctions::AppPreExit},
	{OFF::IsNonPakFileNameAllowed,			UFunctions::IsNonPakFilenameAllowed},
	{OFF::FindFileInPakFiles,				UFunctions::FindFileInPakFiles},

	{OFF::ClientTeamMessageImplementation,	UFunctions::ClientTeamMessageImplementation},
	//{OFF::ProcessMulticastDelegate,		UFunctions::ProcessMulticastDelegate}, // unsure if I'm keeping this
	//{OFF::BroadcastDelegate,				UFunctions::BroadcastDelegate},
	{OFF::Invoke,							UFunctions::Invoke},
	{OFF::HandleStartingNewPlayer,			UFunctions::HandleStartingNewPlayer},

	//{OFF::PrepareMapChange,				UFunctions::PrepareMapChange},
	{OFF::PostLogin,						UFunctions::PostLogin},
	//{OFF::Logout,							UFunctions::Logout}, // Doesn't get called for some STUPID reason
	{OFF::Close,							UFunctions::CloseConnection},
	//{OFF::BeginPlay,						UFunctions::BeginPlay},
	//{OFF::RequestLevel,					UFunctions::RequestLevel}, Temporarily disabled since it inevitably crashes and if it doesn't it will stall forever
	

	// Core gameplay hooks

	{OFF::ALevelScriptActorConstructor,		AJB::ALevelScriptActor},
	{OFF::AJBWindowWidget,					AJB::AJBWindowWidget},
	{OFF::MediaPlayer,						AJB::UMediaPlayer},
	{OFF::PostEventAtLocation,				AJB::PostEventAtLocation},
	{OFF::ChangeState,						AJB::FlowUtilChangeState},
	
	// Server logic hooks

	//{OFF::HandleClientPlayer,				UFunctions::HandleClientPlayer}, TRASH!
	{OFF::AddClientConnection, 				UFunctions::AddClientConnection},
	{OFF::IsTenpoHost,						AJB::IsServer},
	{OFF::IsAJBOfflineMode,					AJB::IsOfflineMode},
	{OFF::IsOfflineMode,					AJB::IsOfflineMode},
	//{OFF::SetClientTravel,				UFunctions::SetClientTravel},
	//{OFF::ClientTravelInternal,			UFunctions::ClientTravelInternal},
	{OFF::StartLoadingDestination,			UFunctions::StartLoadingDestination},


	{OFF::Tick,								TickHook::Tick},
};

A8CL::OFFSET NetID("UAJBNetworkObserver::GetNetID", 0x4F1EE0);
A8CL::OFFSET OpenCommand("AAJBOutGameProxy::GetOpenCommand", 0x50C390);
A8CL::OFFSET PostEventByName("UAkComponent::PostAkEventByName", 0x2918E0);
A8CL::OFFSET PostEvent("UAkComponent::PostAkEvent", 0x291620);
A8CL::OFFSET LoadBankByName("UAkGameplayStatics::LoadBankByName", 0x286AE0);

#define GenericConstructor(Type, Name, Offset, OutVar) \
	Type* Name(Type* This, void* Crap) \
	{ \
		Type* Pointer = Offset.VerifyFC<Type*(__thiscall*)(Type*, void*)>()(This, Crap); \
		std::string FullName = Pointer ? Pointer->GetFullName() : "None"; \
		LogA(Offset.GetName(), FullName); \
		OutVar = Pointer; \
		return Pointer; \
	}


#define M_TypeDef(Name, ReturnType, ...) \
	typedef ReturnType(__thiscall* Name##_T)(__VA_ARGS__);

//#define M_DebugHook(Offset, OffsetName, MacroTypeDef) \
//	ObjType* GenericHook()

/*
	* Offset			- qword
	* Name				- string
	* ReturnType		- type
	* ParamTypes		- Parenthesised arguments with names
	* ParamNames		- Parenthesised argument names only for function call
	* PreIfCondition	- Lamda if condition with any logic, runs before VerifyFC | If not used write (0);
	* PostIfCondition	- Lamda if condition with any logic, runs after VerifyFC | If not used write (0);

	Example: M_DebugHook(0x107F2D0, "UUserWidget::AddToViewport", MT_AddToViewport, SDK::UUserWidget*, (SDK::UUserWidget* This, int32 ZOrder), (This, ZOrder), (0); , (0); , "[This]: {} | [ZOrder]: {}", This->GetFullName(), ZOrder);
*/
#define M_DebugHook(Offset, OffsetName, Name, ReturnType, ParamTypes, ParamNames, PreIfCondition, PostIfCondition, ...) \
	typedef void*(__thiscall* Name##_T) ParamTypes; \
	namespace M_OFF { A8CL::OFFSET Name(OffsetName, Offset); } \
	ReturnType __fastcall Name ParamTypes \
	{	\
		if PreIfCondition \
		LogA(M_OFF::Name.GetName(), std::format(__VA_ARGS__)); \
		ReturnType* Returnal = (ReturnType*)M_OFF::Name##.VerifyFC<Name##_T>()ParamNames; \
		if PostIfCondition \
		return (ReturnType)Returnal; \
	}	\
	struct Name##LazyHookObject{ A8CL::OFFSET OS; Name##LazyHookObject(A8CL::OFFSET& OS) : OS(OS) { StandaloneHooks.push_back({M_OFF::Name, Name}); } }; \
	static Name##LazyHookObject Name##_LHO(M_OFF::Name);

#define M_DebugHookO(OffsetObj, Name, ReturnType, ParamTypes, ParamNames, PreIfCondition, PostIfCondition, ...) \
	M_DebugHook(OffsetObj.Offset, OffsetObj.GetName(), Name, ReturnType, ParamTypes, ParamNames, PreIfCondition, PostIfCondition, __VA_ARGS__);
/*template <class T>	struct Return		{ template <class Func, class... Arg> static T Call (Func Function, Arg... Args) {	return Function(Args...);	} }; \
	template <>			struct Return<void> { template <class Func, class... Arg> static T Call (Func Function, Arg... Args) {	Function(Args...);			} }; \
	\*/

//M_DebugHook(0x107F2D0, "UUserWidget::AddToViewport", MT_AddToViewport, SDK::UUserWidget*, (SDK::UUserWidget* This, int32 ZOrder), (This, ZOrder), (0); , (0); ,  "[This]: {} | [ZOrder]: {}", This ? This->GetFullName() : "NULL", ZOrder);
//M_DebugHook(0x1073FB0, "UUserWidget::UUserWidget", MT_UUserWidget, SDK::UUserWidget*, (SDK::UUserWidget* This, void* Initializer), (This, Initializer), (0);, (This && This->GetFullName() == "WB_CommonWIndow_S_C Transient.GameEngine_0.BP_AJBGameInstance_C_0.WB_CommonWIndow_S_C_0") { LogA("StupidFeckingIdiotIdiot", HexToString((qword)_ReturnAddress())); }, "[This]: {}", This ? This->GetFullName() : "NULL");
M_DebugHook(0x10CA270, "UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx",MT_SetInputMode_GameAndUIEx, void, (SDK::APlayerController* PlayerController, SDK::UWidget* InWidgetToFocus, SDK::EMouseLockMode InMouseLockMode, bool bHideCursorDuringCapture), (PlayerController, InWidgetToFocus, InMouseLockMode, bHideCursorDuringCapture), (0); , (1) { LogA("StupidFeckingIdiotIdiot", HexToString((qword)_ReturnAddress())); }, "[PlayerController]: {} | [InWidgetToFocus]: {} | [InMouseLockMode]: {} | [bHideCursorDuringCapture]: {} |", PlayerController->GetFullName(), InWidgetToFocus->GetFullName(), *((unsigned char*)&InMouseLockMode), bHideCursorDuringCapture);
//M_DebugHook(0x6CC2F0, "FWindowsCursor::Show", MT_ShowCursor, int, (void* This, bool bShow), (This, bShow), (0); , (0);, "[bShow]: {}", bShow);
M_DebugHookO(OFF::ClipboardCopy, MT_ClipboardCopy, int, (void* String), (String), constexpr (1) { const qword StrLen{wcslen((const wchar_t*)String)}; char Buffer[4096]{0}; int i{0}; int j{0}; const char* Str = (const char*)String; while ((Str[j] || Str[++j]) && i < 4096) { Buffer[i] = Str[j]; ++i; ++j; }  LogA(OFF::ClipboardCopy.GetName(), std::format("========= [CRASH] =========\n\n{}", Buffer)); return 0; }, (0);, "");
//M_DebugHook(0x96D0C0, "FSlateApplication::SetUserFocus", MT_SetUserFocus, bool, (void* This, dword Index, void* WidgetToFocus, byte ReasonFocusIsChanging), (This, Index, WidgetToFocus, ReasonFocusIsChanging), (0); , (0); , "[Caller]: {}", HexToString((qword)_ReturnAddress()));

void __fastcall GetNationalMatchSchedule(SDK::UAJBGameInstance* This, bool* OutCanPlaySoloMode, bool* OutCanPlayPairMode, SDK::FAJBMatchSchedule* OutMatchSchedule, SDK::FAJBMatchScheduleDateTime* OutSoloScheduleDateTime, SDK::FAJBMatchScheduleDateTime* OutPairScheduleDateTime)
{
	*OutCanPlaySoloMode = true;
	*OutCanPlayPairMode = true;

}

void FormatterHook(void* This, bool bInRebuildText, bool bInRebuildAsSource, SDK::FString& OutResult)
{
	OFF::ToFormattedString.VerifyFC<void(__thiscall*)(void*, bool, bool, SDK::FString&)>()(This, bInRebuildText, bInRebuildAsSource, OutResult);

	LogA("FormatterThing", OutResult.ToString());
}

// Server functionality only
void TryGetMatchingMyPairInfo(SDK::UAJBGameInstance* This, bool* bIsValid, bool* bIsRoomHost, SDK::FMatchingPlayerInfo* Out)
{
	if (AJB::bDebugModeFromCMLA) LogA(OFF::TryGetMatchingMyPairInfo.GetName(), std::format("[This]: {} [bIsValid]: {} [bIsRoomHost]: {} [Info]: {}", This->GetFullName(), *bIsValid, *bIsRoomHost, AJB::PlayerInfoParser(*Out)));
	//SDK::FString Lemon{L"Lemon Possession"};
	//AJB::GetBlueprintClass<SDK::UAJBPlayerInfoUtility>()->SetBotFMatchingPlayerInfo(Out, Lemon);
	//Call<UFunctions::Decl::CopyString>(OFF::CopyString.PlusBase())(&Out->PlayerName, &Lemon);
	//memcpy(Out, &AJB::Instance->MatchingPlayers[0].Second, sizeof(SDK::FMatchingPlayerInfo));
	//AJB::CopyString(&Out->PlayerName, &AJB::Instance->MatchingPlayers[0].Second.PlayerName);	

	return OFF::TryGetMatchingMyPairInfo.VerifyFC<void(__thiscall*)(SDK::UAJBGameInstance*, bool*,bool*, SDK::FMatchingPlayerInfo*)>()(This, bIsValid, bIsRoomHost, Out);
}

bool TryGetMatchingPlayerInfoByPlayerIDPureFunction(SDK::UAJBGameInstance* This, int32 PlayerID, SDK::FMatchingPlayerInfo* Out)
{
	// Phase 7: this hook fires on every player lookup, so its diagnostics are behind -debug.
	// The lifecycle tags (PreLogin, PostLogin, Standby, CheckLoading, Gameplay) stay on.
	if (AJB::bDebugModeFromCMLA) AJB::DumpMatchingPlayers("MP-Hook-In");

	// 1. Never dereference a null receiver or output buffer.
	if (!This || !Out)
	{
		LogA(OFF::TryGetMatchingPlayerInfo.GetName(), std::format("[Warning]: Refusing to run with [This]: {} | [Out]: {}", This ? "Valid" : "NULL", Out ? "Valid" : "NULL"));
		return false;
	}

	// 2. The original implementation is always tried first, it owns the correct lookup.
	const bool bOriginalResult = OFF::TryGetMatchingPlayerInfo.VerifyFC<bool(__thiscall*)(SDK::UAJBGameInstance*, int32, SDK::FMatchingPlayerInfo*)>()(This, PlayerID, Out);

	if (bOriginalResult && !AJB::IsPlaceholderPlayerInfo(*Out))
	{
		if (AJB::bDebugModeFromCMLA) LogA(OFF::TryGetMatchingPlayerInfo.GetName(), std::format("[PlayerID]: {} | [Source]: Original | [OUT FMatchingPlayerInfo]: {}", PlayerID, AJB::PlayerInfoParser(*Out)));

		if (AJB::bDebugModeFromCMLA) AJB::DumpMatchingPlayers("MP-Hook-Out");

		return true;
	}

	// 3. Fall back to the receiver's own table, never the global instance.
	// 4. Locate the entry by its real PlayerID, the array index is NOT the PlayerID.
	for (int32 i{ 0 }; i < This->MatchingPlayers.Num(); ++i)
	{
		SDK::FMatchingPlayerInfo& Candidate = This->MatchingPlayers[i].Second;

		if (Candidate.PlayerID != static_cast<uint8>(PlayerID)) continue;
		if (AJB::IsPlaceholderPlayerInfo(Candidate)) continue;

		if (!AJB::CopyMatchingPlayerInfoSafely(Out, Candidate))
		{
			LogA(OFF::TryGetMatchingPlayerInfo.GetName(), std::format("[Warning]: Safe copy failed for [PlayerID]: {}", PlayerID));
			return false;
		}

		Out->PlayerID = static_cast<uint8>(PlayerID);

		if (AJB::bDebugModeFromCMLA) LogA(OFF::TryGetMatchingPlayerInfo.GetName(), std::format("[PlayerID]: {} | [Source]: Fallback | [Index]: {} | [Key]: {} | [OUT FMatchingPlayerInfo]: {}", PlayerID, i, This->MatchingPlayers[i].First.ToString(), AJB::PlayerInfoParser(*Out)));

		if (AJB::bDebugModeFromCMLA) AJB::DumpMatchingPlayers("MP-Hook-Out");

		return true;
	}

	// 5. Nothing real was found, report the failure instead of handing back placeholders.
	LogA(OFF::TryGetMatchingPlayerInfo.GetName(), std::format("[Warning]: No real entry for [PlayerID]: {} | [Original Result]: {}", PlayerID, bOriginalResult));

	if (AJB::bDebugModeFromCMLA) AJB::DumpMatchingPlayers("MP-Hook-Out");

	return bOriginalResult;
}

const wchar_t* GetUsername()
{
	return CMLA::Username.GetArgumentAsString();
}

int32 LoadBankByNameHook(SDK::UAkGameplayStatics* This, SDK::FString& Name)
{
	LogA(LoadBankByName.GetName(), Name ? Name.ToString() : "Null");
	return PostEventByName.VerifyFC<int32(__thiscall*)(SDK::UAkGameplayStatics*, SDK::FString&)>()(This, Name);
}

int32 PostEventByNameHook(SDK::UAkComponent* This, SDK::FString& Name)
{
	LogA(PostEventByName.GetName(), Name ? Name.ToString() : "Null");
	return PostEventByName.VerifyFC<int32(__thiscall*)(SDK::UAkComponent*, SDK::FString&)>()(This, Name);
}

int32 PostEventHook(SDK::UAkComponent* This, SDK::UAkAudioEvent* Event, int32 CallbackMask, void* Delegate, SDK::FString& InEventName)
{
	LogA(PostEventByName.GetName(), InEventName ? InEventName.ToString() : "Null");
	return PostEvent.VerifyFC<int32(__thiscall*)(SDK::UAkComponent*, SDK::UAkAudioEvent*, int32, void*, SDK::FString&)>()(This, Event, CallbackMask, Delegate, InEventName);
}

SDK::FString* __fastcall GetNetID(SDK::UAJBNetworkObserver* This, SDK::FString* OutString)
{
	// The original implementation owns the lookup and is always tried first.
	SDK::FString* Result = NetID.VerifyFC<SDK::FString*(__fastcall*)(SDK::UAJBNetworkObserver*, SDK::FString*)>()(This, OutString);

	// An empty result means the game did not find an id. Never fall back to the hardcoded
	// "Aeyth8", the stable account id of the current profile is used instead.
	if (Result && !Result->IsValid())
	{
		const std::string AccountId{ AJB::GetLocalAccountId() };

		if (!AccountId.empty())
		{
			const std::wstring Wide(AccountId.begin(), AccountId.end());
			SDK::FString Fallback{ Wide.c_str() };

			AJB::CopyString(Result, &Fallback);

			LogA(NetID.GetName(), std::format("[Source]: AccountId fallback | [NetID]: {}", AccountId));
		}
		else
		{
			LogA(NetID.GetName(), "[Warning]: the original NetID lookup returned nothing and no account id is available.");
		}
	}

	return Result;
}

A8CL::OFFSET ExecCharacterNo("UAJBGameInstance::execSetSelectedCharacterNo", 0x54F180);

static void __fastcall execSetCharNo(SDK::UAJBGameInstance* This, int32 Num)
{
	uintptr_t Caller = (uintptr_t)_ReturnAddress();
	LogA(ExecCharacterNo.GetName(), std::format("[Num]: {} | [Caller Address]: {} / {} ", Num, HexToString(Caller), HexToString(Caller - GBA)));
	ExecCharacterNo.VerifyFC<void(__thiscall*)(SDK::UAJBGameInstance*, int32)>()(This, Num);
}

A8CL::OFFSET CharacterNo("UAJBGameInstance::SetSelectedCharacterNo", 0x486320);
static void SetCharNo(SDK::UAJBGameInstance* This, int32 Num)
{
	uintptr_t Caller = (uintptr_t)_ReturnAddress();
	LogA(CharacterNo.GetName(), std::format("[Num]: {} | [Caller Address]: {} / {} ", Num, HexToString(Caller), HexToString(Caller - GBA)));
	CharacterNo.VerifyFC<void(__thiscall*)(SDK::UAJBGameInstance*, int32)>()(This, Num);

	// Phase 5: remember the local player's real selection so the host can write it into the
	// session cache instead of falling back to the default character.
	if (AJB::IsServer() && Num > 0)
	{
		const std::string AccountId{ AJB::GetLocalAccountId() };
		if (!AccountId.empty()) AJB::CacheCharacterSelection(AccountId, static_cast<uint8>(Num));
	}
}


SDK::FString* __fastcall GetOpenCommand(SDK::AAJBOutGameProxy* This, SDK::FString* OutString)
{
	uintptr_t Addressee = (uintptr_t)_ReturnAddress();
	SDK::FString* Return = OpenCommand.VerifyFC<SDK::FString*(__fastcall*)(SDK::AAJBOutGameProxy*, SDK::FString*)>()(This, OutString);

	LogA(OpenCommand.GetName(), std::format("[This]: {} | [OutString]: {} | [Address Caller]: {} / {} ", This->GetFullName(), OutString->IsValid() ? OutString->ToString() : "Null", HexToString(Addressee - GBA), HexToString(Addressee)));

	return Return;
}

A8CL::OFFSET ObjBlueprint("UAJBUtilityFunctionLibrary::NewObjectFromBlueprint", 0x49FCD0);
SDK::UObject* NewObjectFromBlueprint(SDK::UObject* WorldContextObject, SDK::UClass* InClass)
{
	LogA("NewObjectFromBlueprint", std::format("[WorldContextObject]: {} | [InClass]: {} ", WorldContextObject->GetFullName(), InClass->GetFullName()));
	return ObjBlueprint.VerifyFC<SDK::UObject*(__fastcall*)(SDK::UObject*, SDK::UClass*)>()(WorldContextObject, InClass);
}

A8CL::OFFSET GetBaseMaterial("UMaterialInterface::GetBaseMaterial", 0x10A1A70);
SDK::UMaterial* HGetBaseMaterial(void* This)
{
	return SDK::UObject::FindObject<SDK::UMaterial>("Material M_LemonPossession.M_LemonPossession");

	return GetBaseMaterial.VerifyFC<SDK::UMaterial * (__thiscall*)(void*)>()(This);
}

A8CL::OFFSET GetMaterialInterface("FMaterialResource::GetMaterialInterface", 0x14D8E60);
SDK::UMaterialInterface* GetMaterial(void* This)
{
	static SDK::UMaterial* LemonEssence{nullptr};
	if (GWorld.GetPointer())
	{
		if (!LemonEssence) LemonEssence = SDK::UObject::FindObject<SDK::UMaterial>("Material M_LemonPossession.M_LemonPossession");
		else
		{
			return LemonEssence;
		}
	}
	
	return GetMaterialInterface.VerifyFC<SDK::UMaterialInterface * (__fastcall*)(void*)>()(This);
}

A8CL::OFFSET oGetDefaultMaterial("UMaterial::GetDefaultMaterial", 0x14B1C80);
SDK::UMaterial* GetDefaultMaterial(void* This)
{
	/*static SDK::UMaterial* Lemon{nullptr};
	if (!Lemon)
	{
		SDK::ALemonHelper_C* LemonHelper = Pointers::SpawnActor<SDK::ALemonHelper_C>();
		if (LemonHelper)
		{
			LemonHelper->PlayGrayscaleLemonPossession();
			Lemon = SDK::UObject::FindObject<SDK::UMaterial>("Material M_LemonPossession.M_LemonPossession");
		}		
	}
	if (Lemon)
	{
		return Lemon;
	}*/

	return oGetDefaultMaterial.VerifyFC<SDK::UMaterial*(__fastcall*)(void*)>()(This);
}

A8CL::OFFSET FTextConstructor("FText::FText", 0x5E2F40);
SDK::FText* FText(SDK::FText* This, SDK::FString* InString)
{
	SDK::FText* Return = FTextConstructor.VerifyFC<SDK::FText*(__fastcall*)(SDK::FText*, SDK::FString*)>()(This, InString);
	LogA(FTextConstructor.GetName(), std::format("{} | {}", This->ToString(), InString->ToString()));

	return Return;
}

A8CL::OFFSET oAddActionMapping("SDK::UPlayerInput::AddActionMapping", 0x1799E70);
void AddActionMapping(SDK::UPlayerInput* This, SDK::FInputActionKeyMapping& Mapping)
{
	LogA(oAddActionMapping.GetName(), std::format("[This]: {} | [Action]: {} | [Key]:{}", This->GetFullName(), Mapping.ActionName.ToString(), Mapping.Key.KeyName.ToString()));
	if (Mapping.ActionName.ToString() == "HDbg_DebugMenu")
	{
		Call<void(__thiscall*)(SDK::UPlayerInput*, SDK::FKey Key, float)>(PB(0x17AE2C0))(This, Mapping.Key, 1.0f);
	}
	return oAddActionMapping.VerifyFC<void(__thiscall*)(SDK::UPlayerInput*, SDK::FInputActionKeyMapping&)>()(This, Mapping);
}

A8CL::OFFSET oWinGetUsername("FWindowsPlatformProcess::UserName", 0x69FCC0);
const wchar_t* WinGetUsername()
{
	if (AJB::StrInGameUserName)
	{
		return AJB::StrInGameUserName->CStr();
	}

	return L"NAMELESS FECKER";
}

A8CL::OFFSET oFindRow("UDataTable::FindRow", 0x498CF0);
void* UFindRow(SDK::FName RowName, wchar_t* ContextString, bool bWarnIfMissing)
{
	LogA(oFindRow.GetName(), std::format("[RowName]: {} | [ContextString]: {} | [bWarnIfMissing]: {}", RowName.ToString(), SDK::FString(ContextString).ToString(), bWarnIfMissing));
	return oFindRow.VerifyFC<void*(__fastcall*)(SDK::FName, wchar_t*, bool)>()(RowName, ContextString, bWarnIfMissing);
}

static void* GConfigCache{nullptr};
static constexpr const wchar_t* StaticKey{L"SoftwareCursors"};
static const SDK::FString StaticValue{L"SoftwareCursors=((Default, /Game/Aeyth8/Blueprints/WBP_Cursor.WBP_Cursor_C))"};
static constexpr const wchar_t* HighDPI{L"bAllowHighDPIInGameMode"};

A8CL::OFFSET GSetString("FConfigCacheIni::SetString", 0x639410);

static bool __fastcall SetString(void* This, const wchar_t* Section, const wchar_t* Key, SDK::FString& Value, SDK::FString& Filename)
{
	if (!GConfigCache) GConfigCache = This;
	else if (GConfigCache != This) 
	{
		GConfigCache = This;
		LogA("New Cache Address", HexToString(*(uintptr_t*)(This) - GBA));
	}
	static int Count{0};
	if (wcscmp(Key, HighDPI) == 0)
	{
		Count++;

		if (Count == 1)
		{
			
			
			//Uncomment to unrestrict the GUI size, allowing for bigger screens to resize and feel more like a PC game.

			if (CMLA::Debug.GetAsBool())
			{
				SDK::UUserInterfaceSettings* Interface{nullptr};

				SDK::UClass* TheClass = Call<SDK::UClass * (__fastcall*)()>(PB(0x190E3B0))();
				Interface = Call<SDK::UUserInterfaceSettings*(__fastcall*)(SDK::UClass*)>(OFF::CreateDefaultObject.PlusBase())(TheClass);
				LogA("Interface", Interface->GetFullName());
				Call<void(__fastcall*)(SDK::UObject*, SDK::UClass*, const wchar_t*, uint32, SDK::UProperty*)>(PB(0x80C8E0))(Interface, TheClass, 0,0,0);
			}

			Key = StaticKey;
			Call<UFunctions::Decl::CopyString>(OFF::CopyString.PlusBase())(&Value, const_cast<SDK::FString*>(&StaticValue));
			Call<void(__fastcall*)(void*, bool, SDK::FString&)>(PB(0x626770))(This, true, Value); // FConfigCacheIni::Flush
			/*Key = StaticKey;
			wchar_t* Pointer = const_cast<wchar_t*>(Value.GetDataPtr());
			for (BYTE i{0}; i < 62; ++i)
			{
				if (Pointer)
				{
					*Pointer = StaticValue[i];
				}
			}*/
			// = StaticValue;
		}
		else if (Count == 2)
		{
			wchar_t* Pointer = const_cast<wchar_t*>(Value.GetDataPtr());
			static constexpr const wchar_t* One{L"1"};
			/* Value is already nullptr
			
			if (Pointer) *Pointer = *One;
			++Pointer;
			while (Pointer++ != nullptr)
			{
				*Pointer = 0;
			}*/
		}		
	}

	//LogA("SetString", std::format("[This] {} | [Section]: {} | [Key]: {} | [Value]: {} | [Filename]: {} ", HexToString(*(uintptr_t*)This - GBA), SDK::FString(Section).ToString(), SDK::FString(Key).ToString(), Value.ToString(), Filename.ToString()));
	return GSetString.VerifyFC<bool(__thiscall*)(void*, const wchar_t*, const wchar_t*, SDK::FString&, SDK::FString&)>()(This, Section, Key, Value, Filename);
}

A8CL::OFFSET oMainMenuImplementation("APlayerController::ClientReturnToMainMenuWithTextReason_Implementation", 0x160C880);
void ClientReturnToMainMenuWithTextReason_Implementation(SDK::APlayerController* This, SDK::FText& Reason)
{
	LogA(oMainMenuImplementation.GetName(), std::format("[This]: {} | [Reason]: {}", This->GetFullName(), Reason.ToString()));

	// Phase 3/5: returning to the main menu ends the room session, so the PlayerID table and
	// the profile cache are dropped. Map switches inside a session never reach this path.
	if (AJB::IsServer())
	{
		AJB::EndSession();
	}

	oMainMenuImplementation.VerifyFC<void(__thiscall*)(SDK::APlayerController* This, SDK::FText& Reason)>()(This, Reason);
}

void AJB::Init_Hooks()
{
	constexpr const BYTE Replacement[] = { RETN, NOP };
	constexpr const BYTE ReturnZero[] = {MOV, 00, RETN, NOP};

	if (GBA != 0)
	{
		/*
		
		
		#################### TO DO ####################

		Recreate this logic without any offsets by parsing the import directory and finding all DLLs requiring to be patched.
		Have something to find the end instruction to ensure that the patch can fit, also have something that ISN'T string parsing to determine the return value without too much accuracy just enough to make sure it works.
		
		*/

		/*
			Since each call manually unprotects and reprotects a 4kb page, and since the memory regions are so close I should be able to just one and done some of them to be more efficient 
		*/

		BytePatcher::ReplaceBytes(PB(0x223630), Replacement); // Retrieves the NBAM Save Data by calling externals from nbamsavdat.dll

		uintptr_t AMActivator_Destroy{PB(0x20E910)}; // Calls AMActivator_Destroy

		DWORD OldProtectionStatus = BytePatcher::GetProtectionStatus(AMActivator_Destroy);
		BytePatcher::SetProtectionStatus(AMActivator_Destroy, 0x3F0, BytePatcher::EXECUTE_READWRITE);

		BYTE Destroy[5]{MOV, 0x00, RETN, NOP, NOP};
		memcpy((void*)AMActivator_Destroy, Destroy, 5); 
		memcpy((void*)(AMActivator_Destroy + 0x60), Replacement, 2); // Calls AMActivator_Create and a bunch of other initialization functions for amactivator.dll

		uintptr_t RequestOneTimeKey{PB(0x20EB60)}; // Calls AMActivator_RequestOneTimeKey
		for (BYTE i{0}; i < 6; ++i)
		{	
			/*
				Patches:

				AMActivator_RequestOneTimeKey
				AMActivator_RequestSignature
				AMActivator_IsBusy
				AMActivator_GetOneTimeKeyLastStatus
				AMActivator_GetSignatureLastStatus
				AMActivator_GetOneTimeKey
			*/

			memcpy((void*)RequestOneTimeKey, ReturnZero, 4);
			RequestOneTimeKey += 0x20;
		}
		memcpy((void*)RequestOneTimeKey, Replacement, 2); // Calls AMActivator_GetOneTimeKeyExpiration

		uintptr_t GetSignatureGeneration{PB(0x20ECA0)};

		for (BYTE i{0}; i < 4; ++i)
		{
			/*
				Patches:

				AMActivator_GetSignatureGeneration
				AMActivator_Restore
				AMActivator_BitLockerLock
				AMActivator_BitLockerUnlock
			*/

			memcpy((void*)GetSignatureGeneration, ReturnZero, 4);
			GetSignatureGeneration += 0x20;
		}

		BytePatcher::SetProtectionStatus(AMActivator_Destroy, 0x3F0, OldProtectionStatus); // Just restoring the old protection after all patches are applied

		BytePatcher::ReplaceBytes(PB(0x2238A0), {MOV, 0, RETN, NOP, NOP}); // FDrive, I don't have a proper name but it creates a folder on your F:// drive if you have one, it saves data there.
		BytePatcher::ReplaceBytes(PB(0x20FD90), ReturnZero); // Calls AMActivator_Update

		BytePatcher::ReplaceBytes(PB(OFF::ResetPP), Replacement);		// UAJBGameInstance::ResetPP
		BytePatcher::ReplaceBytes(PB(OFF::StartConsumePP), Replacement); // UAJBGameInstance::StartConsumePP

		BytePatcher::ReplaceBytes(PB(OFF::HideCursorCaller), ReturnZero); // HideCursorCaller, I don't have a proper name but it spam-hides the cursor like 100 times a second
		
		//BytePatcher::ReplaceBytes(PB(0x57FBAC), {0x6A, 0x01, 0x58, NOP, NOP}); // push 1 ; pop rax
		//BytePatcher::ReplaceBytes(PB(0x49EB10), {MOV, 0x01, RETN, NOP}); // UAJBUtilityFunctionLibrary::IsEditorPreview


		//BytePatcher::ReplaceBytes(PB(OFF::ClipboardCopy), {RETN, NOP, NOP, NOP, NOP}); // This is a native Unreal Engine function AND I HATE IT SO MUCH

		//BytePatcher::ReplaceBytes(PB(0x527AF0), {MOV, 0, RETN, NOP, NOP, NOP, NOP}); // UAJBAMSystemObject::IsActiveAJBError

		LogA("BytePatcher", "Applied all patches successfully. (Failing would crash)");
	}

	if (Hooks::Init())
	{
		Hooks::CreateAndEnableHooks(StandaloneHooks);

		Hooks::CreateAndEnableHook(OFF::GetUsername, GetUsername);
		Hooks::CreateAndEnableHook(OFF::TryGetMatchingMyPairInfo, TryGetMatchingMyPairInfo);

		if (CMLA::HookAndLogProcessEvent.GetAsBool())
		{
			Hooks::CreateAndEnableHook(OFF::ProcessEvent, UFunctions::ProcessEvent);
		}
		/*if (CMLA::HookAndLogInvoke.GetAsBool())
		{
			Hooks::CreateAndEnableHook(OFF::Invoke, UFunctions::Invoke);
		}*/
		if (CMLA::HookAndLogSpawnActor.GetAsBool())
		{
			Hooks::CreateAndEnableHook(OFF::SpawnActor, UFunctions::SpawnActor);
		}
		if (CMLA::HookAndLogLoader.GetAsBool())
		{
			Hooks::CreateAndEnableHook(OFF::StaticLoadClass, UFunctions::StaticLoadClass);
			Hooks::CreateAndEnableHook(OFF::StaticLoadObject, UFunctions::StaticLoadObject);
		}

		Hooks::CreateAndEnableHook(NetID, GetNetID);
		Hooks::CreateAndEnableHook(CharacterNo, SetCharNo);

#if USING_MATCHING_PLAYER_INFO_HOOK
		Hooks::CreateAndEnableHook(OFF::TryGetMatchingPlayerInfo, TryGetMatchingPlayerInfoByPlayerIDPureFunction);
#else
		LogA("Init_Hooks", "Matching player info hook disabled for the Phase 1 A/B test, the original function will run untouched.");
#endif
		//Hooks::CreateAndEnableHook(oWinGetUsername, WinGetUsername);


		//Hooks::CreateAndEnableHook(oGetDefaultMaterial, GetDefaultMaterial);

		//Hooks::CreateAndEnableHook(ExecCharacterNo, execSetCharNo);
		
		// Phase 7: the high frequency diagnostics are controlled by -debug instead of always
		// running. The lifecycle MatchingPlayers tags and the loading check stay unconditional.
		AJB::bDebugModeFromCMLA = CMLA::Debug.GetAsBool();
		LogA("Init_Hooks", std::format("[bDebugModeFromCMLA]: {}", AJB::bDebugModeFromCMLA));
		{
			Hooks::CreateAndEnableHook(GSetString, SetString);			// Major problem that needs to be handled later.

			Hooks::CreateAndEnableHook(PostEventByName, PostEventByNameHook);
			Hooks::CreateAndEnableHook(oAddActionMapping, AddActionMapping);
			BytePatcher::ReplaceBytes(PB(0x57FBAC), {0x48, 0x31, 0xC0, NOP, NOP});	// UAJBUtilityFunctionLibrary::execIsShipping (Works and makes the debug menu spawn natively / might do other things / causes mouse to always appear ingame (probably because of their retardically designed HideCursorCaller)
		}

		AJB::bIsLemonPossessioned = CMLA::LemonPossession.GetAsBool();
		AJB::bIsDedicatedServer = CMLA::DedicatedServer.GetAsBool();
		AJB::bServerAllowsAdmins = CMLA::ServerAdminPassword.HasChanged();
		AJB::bServerHasPassword = CMLA::ServerPreLoginPassword.HasChanged();

		Hooks::CreateAndEnableHook(oMainMenuImplementation, ClientReturnToMainMenuWithTextReason_Implementation);

#if USING_CUSTOM_MESSAGING_SYSTEM
		BytePatcher::ReplaceByte(PB(OFF::WelcomePlayer6B), 0x90);
		Hooks::CreateAndEnableHook(OFF::WorldWelcomePlayer, AJB::Server::WorldWelcomePlayer);
		Hooks::CreateAndEnableHook(OFF::WelcomePlayerStripped, AJB::Server::ASM_GameWelcomePlayer);
#endif
		//Hooks::CreateAndEnableHook(oFindRow, UFindRow);
		//BytePatcher::ReplaceBytes(OFF::IsAJBOfflineMode.PlusBase(), {MOV, 0, RETN, NOP, NOP});
		//BytePatcher::ReplaceBytes(OFF::IsOfflineMode.PlusBase(), {MOV, 0, RETN, NOP, NOP});

		//Hooks::CreateAndEnableHook(OFF::StaticConstructObject, UFunctions::StaticConstructObject_Internal);
		//Hooks::CreateAndEnableHook(GetBaseMaterial, HGetBaseMaterial);

		/* If I remember correctly none of these hooks did anything.
		Hooks::CreateAndEnableHook(OpenCommand, GetOpenCommand);
		Hooks::CreateAndEnableHook(ObjBlueprint, NewObjectFromBlueprint);		
		Hooks::CreateAndEnableHook(PostEvent, PostEventHook);
		Hooks::CreateAndEnableHook(LoadBankByName, LoadBankByNameHook);*/

		

		//Hooks::CreateAndEnableHook(OFF::ALevelScriptActorConstructor, ALevelScriptActor);
		//Hooks::CreateAndEnableHook(OFF::ToFormattedString, FormatterHook);
		//Hooks::CreateAndEnableHook(FTextConstructor, FText);
		

		//BytePatcher::ReplaceBytes(PB(0x47C7A0),{RETN,NOP}); // UAJBGameInstance::ClearMatchingID

		
	}
	
}
extern "C" void InitHellscape();
void AJB::Init_Engine()
{	
	InitHellscape();
	while (!GEngine) Sleep(25);

	//MRWT::Activate();

	if (AJB::bIsDedicatedServer || AJB::bDebugModeFromCMLA) *reinterpret_cast<byte*>(PB(OFF::LogVerbosity)) = 6u;

	AJB::CoreUObject = SDK::UObject::FindClass("Class CoreUObject.Object");

	// Calls FConfigCacheIni::GetSectionPrivate
	//void* SectionPrivate = Call<void*(__fastcall*)(const wchar_t* Section, const bool Force, const bool Const, const SDK::FString* FileName)>(PB(0x62A910))(L"/Script/Engine.UserInterfaceSettings", false, true, reinterpret_cast<SDK::FString*>(PB(0x305B400)));

	// Calls FConfigFile::GenerateExportedPropertyLine
	/*SDK::FString TheKey = L"SoftwareCursors";
	SDK::FString TheValue = L"((Default, /Game/Aeyth8/Blueprints/WBP_Cursor.WBP_Cursor_C))";
	SDK::FString ExportedLine = Call<SDK::FString(__fastcall*)(SDK::FString& PropertyName, SDK::FString& PropertyValue)>(PB(0x6275E0))(TheKey, TheValue);

	LogA("Export", ExportedLine.ToString());
	LogA("GEngineIni", reinterpret_cast<SDK::FString*>(PB(0x305B400))->ToString());*/

	if (GConfigCache)
	{
		// Calls FConfigCacheIni::SetString
		Call<void(__fastcall*)(void* This, const wchar_t* Section, const wchar_t* Key, const wchar_t* Value, SDK::FString* Filename)>(PB(0x639410))(GConfigCache, L"/Script/Engine.UserInterfaceSettings", L"SoftwareCursors", L"((Default, /Game/Aeyth8/Blueprints/WBP_Cursor.WBP_Cursor_C))", reinterpret_cast<SDK::FString*>(PB(0x305B400)));
		LogA("GConfigCache", "Called");
	}

	if ((AJB::MapSettings = SDK::UGameMapsSettings::GetDefaultObj()) != nullptr)
	{
		FName::NAME_FindOrAdd(&MapSettings->GameDefaultMap.AssetPathName, CMLA::GameDefaultMap.GetArgumentAsString());
		FName::NAME_FindOrAdd(&MapSettings->TransitionMap.AssetPathName, CMLA::TransitionMap.GetArgumentAsString());
		FName::NAME_FindOrAdd(&MapSettings->GlobalDefaultGameMode.AssetPathName, CMLA::GlobalDefaultGameMode.GetArgumentAsString());
	}

	if (CMLA::ServerPreLoginPassword.HasChanged()) FName::NAME_FindOrAdd(&AJB::NAME_ServerPassword, CMLA::ServerPreLoginPassword.GetArgumentAsString());
	if (CMLA::ServerAdminPassword.HasChanged()) FName::NAME_FindOrAdd(&AJB::NAME_AdminPassword, CMLA::ServerAdminPassword.GetArgumentAsString());
	

	if (!IsNull(AJB::Version = SDK::UAJBVersion::GetDefaultObj()))
	{
		SDK::FString NewVersion{L"JJL128-1-NA-MPR0-F02-AEYTH8"};
		Call<UFunctions::Decl::CopyString>(OFF::CopyString.PlusBase())(&Version->BuildName, &NewVersion);
	}
	if (!AJB::StrDLLCommitVersion)
	{
		// Creates the global wide string literal into a dynamic FString. 
		static SDK::FString DLLCommitVersionSingleton{AJB::DLLCommitVersion};
		if (DLLCommitVersionSingleton.IsValid())
		{
			AJB::StrDLLCommitVersion = &DLLCommitVersionSingleton;
		}
	}

	while (AJB::PCPortWindow == nullptr)
	{
		AJB::PCPortWindow = FindWindowW(L"UnrealWindow", 0);
	}
	
	HICON AJBLogo = LoadIconA(AJB::PCPortLib, (char*)IDI_ICON1);
	if (AJBLogo)
	{
		SendMessageA(AJB::PCPortWindow, WM_SETICON, ICON_SMALL, (LPARAM)AJBLogo);
	}

	static wchar_t VersioningBuffer[30]{L"AJB PC Port V28 "};
	lstrcatW(VersioningBuffer, AJB::DLLCommitVersion);
	SetWindowTextW(AJB::PCPortWindow, VersioningBuffer);
	//SetConsoleTitleW(AJB::DLLCommitVersion);

	SYSTEM_INFO CPUInfo{};
	GetSystemInfo(&CPUInfo);

	AJB::NUM_CPUCores = CPUInfo.dwNumberOfProcessors;
}



void AJB::Init_Vars()
{
	Instance = static_cast<SDK::UBP_AJBGameInstance_C*>(GWorld->OwningGameInstance);
	Settings = static_cast<SDK::UAJBAMSystemSettings*>(Instance->AMSystemSettings);
	System = static_cast<SDK::UAJBAMSystemObject*>(Instance->AMSystemObject);
	PlayerPoints = (&System->PP);

	/*SDK::FString ElSev{L"1170"};
	SDK::FString Username{CMLA::Username.GetArgumentAsString()};
	CopyString(&Instance->PlayerLoginInfo.SessionID, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.AccessCode, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.UserDataID, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerName, &Username);
	CopyString(&Instance->PlayerLoginInfo.UserDataID, &ElSev);
	Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerIconID = 5;
	Instance->PlayerLoginInfo.bIsBNCard = true;
	Instance->PlayerLoginInfo.bIsGuest = false;*/
			
	/*for (SDK::FCustomData& Data : Instance->PlayerLoginInfo.CustomData)
	{
		LogA("charaSkinId", std::to_string(Data.charaSkinId));
		Data.charaSkinId = 2;
		for (SDK::FEmoteData& Emote : Data.EmoteData)
		{
			LogA("Emote", Emote.EmoteName.ToString());
			Emote.emoteId = 11;
		}
	}*/

	//Instance->TryCreateOfflinePlayerInfo();
	/*for (SDK::FCustomData& Data : Instance->PlayerLoginInfo.CustomData)
	{
		Data.charaSkinId = 2;

	}
	
	SDK::FString ElSev{L"1170"};
	CopyString(&Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerName, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.MatchingPlayerInfo.GameServerUserID, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerTitle, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.UserDataID, &ElSev);
	Instance->PlayerLoginInfo.bIsBNCard = true;
	Instance->PlayerLoginInfo.bIsGuest = false;
	CopyString(&Instance->PlayerLoginInfo.MatchingID, &ElSev);
	CopyString(&Instance->PlayerLoginInfo.SyncServerHostName, &ElSev);
	Instance->PlayerLoginInfo.SyncServerPort = wcstol(CMLA::ServerPort.GetArgumentAsString(), 0, 10);

	for (int i{0}; i < Instance->OfflineDefaultCustomData.Num(); ++i)
	{
		LogA("OfflineDefaultCustomData", std::format("[{}] {}", Instance->OfflineDefaultCustomData[i].First, Instance->OfflineDefaultCustomData[i].Second.charaSkinId));
		Instance->OfflineDefaultCustomData[i].First = 2;
	}

	/**(int*)&Instance->Pad_3A0[8] = 2; UAJBGameInstance::GetNationalMatchSchedule Modifies this*/

	/*for (SDK::FString& Text : AJB::Instance->InGameParamFileNames)
	{
		LogA("InGameCharacterParamFileNames", Text.ToString());
	}*/

	if (!IsNull(Settings = static_cast<SDK::UAJBAMSystemSettings*>(Instance->AMSystemSettings)))
	{
		bDebugInputMode = (&Settings->bDebugInputMode);

		Settings->CoinOptions.FreePlay = true;
		Settings->bUseDedicatedSeverStartSelect = true;
		//Settings->ShopEventSettings.bIsShopCompetition = true;
		//Settings->UpdateSettings.bIsServerMode = true;

		*bDebugInputMode = CMLA::bDebugInputMode.GetAsBool();
	}

	if (!IsNull(PlayerPoints = &System->PP))
	{
		*PlayerPoints = 1170;
	}
	if (!IsNull(AJBSettings = SDK::UAJBSettings::GetDefaultObj()))
	{
		AJBSettings->bAvailableAllCharacters = true;
		//AJBSettings->bAvailableAllStages = true;		I'm pretty sure everything is available and enabling this only allows you to open the PvE map on BR which puts you in an infinite loading screen. 
		AJBSettings->bEnableSkinCustomDebug = true;
		AJBSettings->bUseDebugClosedArcadeTimeSchedule = true;
		//LogA("AJBSettings AutoPlayTestMode", std::to_string(AJBSettings->AutoPlayTestMode));
	}

	SDK::UAJBArcadeTimeManager* TimeManager{nullptr};
	if (!IsNull(TimeManager = Instance->ArcadeTimeManager))
	{
		// Can 100% confirm that this somehow fixes being booted to the main menu when the store close time is up.
		// It also allows you to use the menu as if it wasn't closed.
		// I'm unaware of any negative sideeffects caused from doing this.
		Instance->ArcadeTimeManager = nullptr;

	}
	
	// Phase 2: the account profile is the only source of player identity. The old debug block
	// that forced every field to 'Aeyth8' / 'ElSev' / a fixed icon is deliberately gone.
	AJB::LoadAndApplyLocalProfile();

	// Phase 4: register the dedicated sync FName up front so no message handling path ever
	// has to allocate a name mid transfer.
	AJB::InitMatchingPlayersSync();

	// Phase 6: request the short lived login token on a worker thread so the HTTP provider is
	// ready by the time this process joins a room. Nothing here waits on the network.
	if (AJB::GetConfiguredProfileSource() == EProfileSource::Http)
	{
		AJB::BeginProfileTokenAcquisition(AJB::GetLocalAccountId());
	}

	//AJB::ThreadLoop(); // JMPs to the ThreadLoop until process exit or variable set for thread destruction.
}

std::string AJB::GetLocalAccountId()
{
	// The account id comes from the existing -Username= launch argument. It defaults to
	// "Aeyth8" only because that is the command line default, never because of the machine name.
	const std::wstring Username{ CMLA::Username.GetArgumentAsString() };
	if (Username.empty()) return std::string();

	// Narrowed per code unit on purpose: the id is ASCII and a non ASCII code unit must not
	// survive into the connection url, the filter below drops it.
	std::string Narrowed;
	Narrowed.reserve(Username.size());

	for (const wchar_t Character : Username)
	{
		Narrowed.push_back(Character <= 0x7F ? static_cast<char>(Character) : '?');
	}

	// Keep it ASCII safe, the id travels inside the connection URL.
	for (char& Character : Narrowed)
	{
		const bool bIsSafe = (Character >= '0' && Character <= '9')
			|| (Character >= 'A' && Character <= 'Z')
			|| (Character >= 'a' && Character <= 'z')
			|| Character == '-'
			|| Character == '_';

		if (!bIsSafe) Character = '_';
	}

	return Narrowed;
}

bool AJB::LoadAndApplyLocalProfile()
{
	const std::string AccountId{ AJB::GetLocalAccountId() };

	if (AccountId.empty())
	{
		LogA("LoadAndApplyLocalProfile", "[Error]: No account id was provided through -Username=.");
		return false;
	}

	const ProfileParseResult Result = A8CL::AJB::LoadLocalProfile(AccountId);

	// Remember the outcome so the join path never has to read the file again.
	bLocalProfileAvailable = Result.bSuccess;

	if (!Result.bSuccess)
	{
		// A broken profile is a hard failure, we never synthesize a placeholder identity.
		LogA("LoadAndApplyLocalProfile", std::format("[AccountId]: {} | [Error]: {}", AccountId, Result.Error));
		return false;
	}

	if (!AJB::ApplyProfileToPlayerLoginInfo(Result.Record))
	{
		LogA("LoadAndApplyLocalProfile", std::format("[AccountId]: {} | [Error]: Failed to apply the profile to PlayerLoginInfo.", AccountId));
		return false;
	}

	// Phase 3/5: remember this process's own validated profile so a new room session can seed
	// the host's own entry from it without reading the file again.
	AJB::SetLocalProfileRecord(Result.Record);

	LogA("LoadAndApplyLocalProfile", std::format("[AccountId]: {} | [Result]: Profile applied.", AccountId));

	return true;
}

bool AJB::IsLocalProfileAvailable()
{
	return AJB::bLocalProfileAvailable;
}

void AJB::ThreadLoop()
{
	while (AJB::bKeepInitialThreadAlive)
	{
		//Sleep(100);

		/*
			To be implemented later if needed....
		*/
	}
}

// -- Pointers

SDK::ABP_PPV_VSFilter_C* AJB::GetPostProcessFilter(const SDK::ABP_AJBInGamePlayerController_C* Player, const bool bCreateIfNull)
{
	if (Player)
	{
		SDK::ABP_PPV_VSFilter_C* Filter = Player->PPVVSFilter;

		return Filter ? Filter : bCreateIfNull ? const_cast<SDK::ABP_AJBInGamePlayerController_C*>(Player)->PPVVSFilter = SpawnActor<SDK::ABP_PPV_VSFilter_C>() : nullptr;		
	}

	return nullptr;
}

bool AJB::IsOfType(SDK::UObject* Object, SDK::UClass* Type)
{
	return Object->IsA(Type);	
}


// -- Helpers

std::string AJB::PlayerInfoParser(const SDK::FMatchingPlayerInfo& Info)
{
	return std::format("[PlayerID]: {} | [GameServerUserID]: {} | [TeamID]: {} | [TeamHostUserID]: {} | [PlayerName]: {} | [PlayerIconID]: {} | [PlayerLevel]: {} | [PlayerTitle]: {} | [CharactorID]: {} | [bIsCameraMode]: {} | [Rate]: {}", Info.PlayerID, Info.GameServerUserID.ToString(), Info.TeamID, Info.TeamHostUserID.ToString(), Info.PlayerName.ToString(), Info.PlayerIconID, Info.PlayerLevel, Info.PlayerTitle.ToString(), Info.CharactorID, Info.bIsCameraMode, Info.Rate);
}

void AJB::DumpMatchingPlayers(const char* Tag)
{
	SDK::UWorld* CurrentWorld = GWorld.GetPointer();
	const std::string MapName{ CurrentWorld ? CurrentWorld->GetName() : "NoWorld" };
	const int32 NumPlayers{ Instance ? Instance->MatchingPlayers.Num() : -1 };

	LogA("MatchingPlayers-Dump", std::format("[Tag]: {} | [Map]: {} | [IsServer]: {} | [IsInSession]: {} | [TMap Count]: {} | [Instance]: {}", Tag, MapName, AJB::IsServer(), AJB::IsInSession(), NumPlayers, Instance ? "Valid" : "NULL"));

	if (!Instance) return;

	for (int32 i{0}; i < Instance->MatchingPlayers.Num(); ++i)
	{
		// The index is purely diagnostic, PlayerID must be read from the entry itself.
		SDK::FMatchingPlayerInfo& Info = Instance->MatchingPlayers[i].Second;
		SDK::FString& Key = Instance->MatchingPlayers[i].First;
		SDK::FCustomData& CustomData = Info.CustomData;

			LogA("MatchingPlayers-Dump", std::format("[Tag]: {} | [Index]: {} | [Key]: {} | [PlayerID]: {} | [GameServerUserID]: {} | [PlayerName]: {} | [PlayerIconID]: {} | [PlayerLevel]: {} | [PlayerTitle]: {} | [CharactorID]: {} | [charaSkinId]: {} | [standSkinId]: {} | [EmoteData.Num()]: {}", Tag, i, Key.ToString(), Info.PlayerID, Info.GameServerUserID.ToString(), Info.PlayerName.ToString(), Info.PlayerIconID, Info.PlayerLevel, Info.PlayerTitle.ToString(), Info.CharactorID, CustomData.charaSkinId, CustomData.standSkinId, CustomData.EmoteData.Num()));
	}
}

AJB::ESelectedCharacter AJB::GetSelectedCharacter()
{
	return Instance ? static_cast<ESelectedCharacter>(Instance->GetSelectedCharacterNo()) : INVALID;
}

bool AJB::IsPlaceholderPlayerInfo(const SDK::FMatchingPlayerInfo& Info)
{
	const std::wstring PlayerName{ Info.PlayerName.ToWString() };
	const std::wstring PlayerTitle{ Info.PlayerTitle.ToWString() };

	return Info.PlayerIconID == -1
		&& Info.PlayerLevel == 0
		&& Info.CharactorID == 0
		&& PlayerTitle.empty()
		&& (PlayerName.empty() || PlayerName == L"NO NAME");
}

bool AJB::CopyMatchingPlayerInfoSafely(SDK::FMatchingPlayerInfo* Out, const SDK::FMatchingPlayerInfo& Source)
{
	if (!Out) return false;

	// Scalars are plain old data, they are copied by value only.
	Out->PlayerID = Source.PlayerID;
	Out->TeamID = Source.TeamID;
	Out->PlayerIconID = Source.PlayerIconID;
	Out->PlayerLevel = Source.PlayerLevel;
	Out->CharactorID = Source.CharactorID;
	Out->InGameProgressID = Source.InGameProgressID;
	Out->StartLocation = Source.StartLocation;
	Out->bIsCameraMode = Source.bIsCameraMode;
	Out->Rate = Source.Rate;

	// Every FString goes through the game's own copy routine, never a raw pointer copy.
	AJB::CopyString(&Out->GameServerUserID, &const_cast<SDK::FString&>(Source.GameServerUserID));
	AJB::CopyString(&Out->TeamHostUserID, &const_cast<SDK::FString&>(Source.TeamHostUserID));
	AJB::CopyString(&Out->PlayerName, &const_cast<SDK::FString&>(Source.PlayerName));
	AJB::CopyString(&Out->PlayerTitle, &const_cast<SDK::FString&>(Source.PlayerTitle));

	// FCustomData is rebuilt field-by-field so the destination never shares the source allocation.
	Out->CustomData.charaSkinId = Source.CustomData.charaSkinId;
	Out->CustomData.standSkinId = Source.CustomData.standSkinId;
	Out->CustomData.KillCount = Source.CustomData.KillCount;

	return AJB::CopyEmoteDataSafely(&Out->CustomData.EmoteData, Source.CustomData.EmoteData);
}

bool AJB::CopyEmoteDataSafely(UC::TArray<SDK::FEmoteData>* Out, const UC::TArray<SDK::FEmoteData>& Source)
{
	if (!Out) return false;

	// The destination is a caller owned temporary, we only ever replace it with storage we
	// allocated ourselves so we never free or reinterpret a pointer we do not own.
	*Out = UC::TArray<SDK::FEmoteData>();

	const int32 Count{ Source.Num() };
	if (Count <= 0) return true;

	SDK::FEmoteData* NewData = static_cast<SDK::FEmoteData*>(FMemory::Malloc(sizeof(SDK::FEmoteData) * Count));
	if (!NewData)
	{
		LogA("CopyEmoteDataSafely", std::format("[Error]: Failed to allocate {} emote entries", Count));
		return false;
	}

	*Out = UC::TArray<SDK::FEmoteData>(NewData, Count, Count);

	for (int32 i{ 0 }; i < Count; ++i)
	{
		SDK::FEmoteData& Destination = Out->Data[i];
		SDK::FEmoteData& SourceEmote = const_cast<SDK::FEmoteData&>(Source[i]);

		// Start from a zeroed entry so no stale pointer from the source is ever retained.
		Destination = SDK::FEmoteData{};

		Destination.emoteId = SourceEmote.emoteId;
		Destination.voiceId = SourceEmote.voiceId;

		AJB::CopyString(&Destination.EmoteName, &SourceEmote.EmoteName);
		AJB::CopyString(&Destination.VoiceName, &SourceEmote.VoiceName);
	}

	return true;
}

unsigned char AJB::GetSelectedSkin()
{
	return Instance ? Instance->GetCharacterSkinId(AJB::GetSelectedCharacter()) : 0;
}

unsigned char AJB::GetSelectedStandSkin()
{
	return Instance ? Instance->GetStandSkinId(AJB::GetSelectedCharacter()) : 0;
}

bool AJB::SetSelectedCharacter(const ESelectedCharacter CharacterIndex, const unsigned char SkinIndex, const unsigned char StandSkinIndex)
{
	return AJB::SetSelectedCharacter(CharacterIndex, SkinIndex) ? (Instance->SetStandSkinId(CharacterIndex, StandSkinIndex), static_cast<int32>(AJB::GetSelectedStandSkin() == StandSkinIndex)) : false;
}

bool AJB::SetSelectedCharacter(const ESelectedCharacter CharacterIndex, const unsigned char SkinIndex)
{
	return AJB::SetSelectedCharacter(CharacterIndex) ? (Instance->SetCharacterSkinId(CharacterIndex, SkinIndex), static_cast<int32>(AJB::GetSelectedSkin() == SkinIndex)) : false;
}

bool AJB::SetSelectedCharacter(const ESelectedCharacter CharacterIndex)
{
	return Instance ? (SetCharNo(Instance, CharacterIndex), Instance->CharacterNo == static_cast<int32>(CharacterIndex)) : false;
}

void AJB::CopyString(UC::FString* StringToModify, UC::FString* StringToCopy)
{
	Call<UFunctions::Decl::CopyString>(OFF::CopyString.PlusBase())(StringToModify, StringToCopy);
}



bool AJB::IsServer()
{
	SDK::UWorld* CurrentWorld = GWorld.GetPointer();
	if (CurrentWorld && CurrentWorld->NetDriver)
	{
		/*LogA("GWorld", CurrentWorld->GetFullName());
		LogA("NetDriver", CurrentWorld->NetDriver->GetFullName());*/
		// Only clients have a valid ServerConnection pointer.
		return CurrentWorld->NetDriver->ServerConnection == nullptr && GetNetMode(CurrentWorld->NetDriver) == Enums::ENetMode::NM_ListenServer;
	}

	return false;
}

bool AJB::IsInSession()
{
	SDK::UWorld* CurrentWorld = GWorld.GetPointer();
	return CurrentWorld && CurrentWorld->NetDriver && CurrentWorld->NetDriver->ServerConnection;
}

bool AJB::IsOfflineMode()
{
	return !IsInSession();
}

unsigned char AJB::SetPlayMode(unsigned char NewPlayMode)
{
	// Thanks to bad game design, it is ABSOLUTELY ESSENTIAL to set bIsLocalSessionMode, or else everything just breaks
	// I don't even remember where I found it from, I think it was from decompiling the developer debug menu blueprint and reading the hosting logic (which is how I figured out how to even host on this game)
	// After a lot of debugging with server->client custom messaging via the RedirectURL, I actually realized that this has to be set or it will still desync... oh no... don't tell me that's why the Synchronizer isn't able to fix it in the first place...
	// (it is) I added the switch logic to my Synchronizer blueprint and now I don't need any of the custom messaging logic.. it will probably be useful in the future

	switch (NewPlayMode)
	{
	case 3:
	case 4:
	case 7:
	case 8:
		AJB::Instance->bIsLocalSessionMode = true;
		break;

	default:
		AJB::Instance->bIsLocalSessionMode = false;
	}

	AJB::Instance->PlayMode = (SDK::EPlayMode)NewPlayMode;

	return NewPlayMode;
}

void AJB::CreateCallbackTimer(void* FunctionCallback, float fTimer, unsigned nLoopFor, bool bInfinite)
{
	TickHook::FTimerHandlerEntry Entry{(ull)FunctionCallback, fTimer, 0, 0, bInfinite};	
	TickHook::CallbackTimers.push_back(Entry);

	/*if (AJB::MOD_CallbackTimer)
	{
		uint64 Function = (uint64)FunctionCallback;

		uint32 Lower = static_cast<uint32>(Function & 0xFFFFFFFF);
		uint32 Upper = static_cast<uint32>((Function >> 32) & 0xFFFFFFFF);

		AJB::MOD_CallbackTimer->SetCallbackTimer(fTimer, Upper, Lower);
	}*/
}

void AJB::SetFrameRateCap(bool bEnabled)
{
	bIsFrameRateUncapped = bEnabled;

	if (bEnabled)
	{
		BytePatcher::ReplaceBytes(PB(OFF::AJBGetMaxTickRate), {0x4D, 0x3B, 0xCB, 0x7C, 0xE9});
		BytePatcher::ReplaceBytes(PB(OFF::AJBGetMaxTickRateCap), {0x75, 0x03});
	}
	else
	{
		BytePatcher::ReplaceBytes(PB(OFF::AJBGetMaxTickRate), {NOP, NOP, NOP, NOP, NOP}); 
		BytePatcher::ReplaceBytes(PB(OFF::AJBGetMaxTickRateCap), {NOP, NOP});		
	}
}

void AJB::TryFixInfiniteLoadingScreen()
{
	// If a player leaves and rejoins the stupid PlayerID increments even though the count is wrong, it goes from 1 and upwards but due to the bug you will have missing slots.
	// So basically if there is [PlayerID 1], [PlayerID 2] and someone leaves, or rejoins, it becomes [PlayerID 1], [PlayerID 3], now there's a gap, and it also breaks the host for some STUPID reason and deletes the entry for that.
	// And you would assume huh okay so then the disconnecting logic must be broken or missing something, ITS JUST THIS STUPID NUMBER AND SOME OTHER NUMBER THAT HAS NO SYNCHRONIZATION WITH THE REST!

	/*if (BugsToFix & BROKEN_CHARACTER_SPAWN)
	{
		SDK::ABP_AJBInGamePlayerController_C* Player = Pointers::Player<SDK::ABP_AJBInGamePlayerController_C>();
		if (Player) 
		{
			Player->ROS_DebugCharaChange(AJB::TEMP_CachedCharacterID);
		}
	}
	if (BugsToFix & BROKEN_ENTRY)
	{
		for (int i{0}; i < Instance->MatchingPlayers.Num(); ++i)
		{
			auto& Value = Instance->MatchingPlayers[i].Value();

			Value.PlayerID = (i + 1);
		}
	}
	if (BugsToFix & BROKEN_PLAYERID)
	{

	}*/

	LogA("TryFixInfiniteLoadingScreen", "Attempting...");

	/*for (SDK::ULocalPlayer* Player : AJB::Instance->LocalPlayers)*/
	for (SDK::APlayerController* Player : Pointers::FindObjects<SDK::APlayerController>())
	{
		if (Player && Player->IsA(SDK::ABP_AJBInGamePlayerController_C::StaticClass()))
		{
			SDK::ABP_AJBInGamePlayerController_C* Controller = static_cast<SDK::ABP_AJBInGamePlayerController_C*>(Player);
			if (!Controller->Character)
			{
				const bool bIsHost = Pointers::Player() == Controller;
				if (bIsHost)
				{
					Controller->DebugCharacterChange(AJB::TEMP_CachedCharacterID);
				}
				else Controller->DebugCharacterChange(Controller->CharacterNo > 0 ? Controller->CharacterNo : 1);
			}
		}
	}

	/*SDK::ABP_AJBBattleGameMode_C* CurrentGameMode = AJB::GetGameMode<SDK::ABP_AJBBattleGameMode_C>();
	if (CurrentGameMode)
	{
		CurrentGameMode->ResetGame();
	}*/

	//AJB::CreateCallbackTimer(CheckForInfiniteLoadingScreen, 30.0f);
}

void AJB::CheckForInfiniteLoadingScreen()
{	
	constexpr const char* LogHeader{"CheckForInfiniteLoadingScreen"};
	constexpr const char* SDT_BUG[4]{"FUNCTIONAL | ", "BROKEN_PLAYERID | ", "BROKEN_ENTRY | ", "BROKEN_CHARACTER_SPAWN | "};

	byte Bug{0};

	SDK::UWorld* CurrentWorld = GWorld.GetPointer();
	if (CurrentWorld && CurrentWorld->NetDriver && CurrentWorld->NetDriver->ClientConnections.IsValid())
	{
		// Phase 5: a listen server's own local player has no client connection, so the host's
		// entry is legitimately present without one. Comparing the table size to the raw
		// connection count would flag every healthy listen server as BROKEN_ENTRY.
		const int32 ExpectedPlayers{ CurrentWorld->NetDriver->ClientConnections.Num() + (AJB::bIsDedicatedServer ? 0 : 1) };

		if (Instance->MatchingPlayers.Num() > ExpectedPlayers)
		{
			Bug |= BROKEN_ENTRY;
		}

		for (int i{0}; i < Instance->MatchingPlayers.Num(); ++i)
		{
			SDK::FMatchingPlayerInfo& Info = Instance->MatchingPlayers[i].Second;

			if (Info.PlayerID == 0)
			{
				Bug |= BROKEN_PLAYERID;
			}
			if (Info.CharactorID == 0)
			{
				Bug |= BROKEN_CHARACTER_SPAWN;
			}

			LogA(LogHeader, AJB::PlayerInfoParser(Info));
		}

		std::string BugResult{""};

		if (Bug == 0)
		{
			BugResult = "No errors detected.";
		}
		else
		{
			BugResult += "Errors found: ";

			byte e{1};
			while (e < 4)
			{
				if (Bug & (1 << e))
				{
					BugResult += SDT_BUG[e];
				}
				++e;
			}

			// Phase 5: a profile-table error is a data bug, not a missing character object. It is
			// reported as-is and never masked by the last-resort loader fix, which is what made
			// an earlier build look healthy while every entry was still placeholder data.
			if (Bug & (BROKEN_ENTRY | BROKEN_PLAYERID | BROKEN_CHARACTER_SPAWN))
			{
				LogA(LogHeader, "Profile table errors are reported without running TryFixInfiniteLoadingScreen, they must be fixed at the source.");
			}
		}

		LogA(LogHeader, BugResult);

		AJB::DumpMatchingPlayers("MP-CheckLoading");
	}
}

#pragma warning(disable: 4996)  // SHUTUP!

void A8CL::AJB::DedicatedServerLoop()
{
	//static SDK::FString ServerTravel{L"servertravel /Game/AJB/Maps/SimpleStartLocationSelect_P"};
	//constexpr const wchar_t* ServerTravelBase{L"servertravel /Game/Aeyth8/Maps/DedicatedServer/ReconnectLoop"};
	//const int NumPlayers = GWorld.GetPointer()->NetDriver->ClientConnections.Num();
	const int NumPlayers = GWorld.GetPointer()->AuthorityGameMode->GetNumPlayers();
	wchar_t ServerTravelBuffer[260]{L"servertravel /Game/Aeyth8/Maps/DedicatedServer/DedicatedServerRestart"};
	_ltow(NumPlayers, &ServerTravelBuffer[72], 10);

	SDK::FString ServerTravel{ServerTravelBuffer};
	UFunctions::UConsole(GEngine->GameViewport->ViewportConsole, ServerTravel);


}

bool __fastcall AJB::FlowUtilChangeState(SDK::FFlowStateHandler* StateHandler, SDK::FGameplayTag NextStateTag)
{
	LogA("UFlowStateUtil", std::format("New FlowState: {}", NextStateTag.TagName.ToString()));
	
	AJB::CurrentFlowstate = &NextStateTag;
	
	// The mouse will not lock into the viewport on its own (making KBM compatibility unplayable unless you enjoy constantly holding down middle click to move your camera)
	constexpr const static wchar_t* SDT_MouseLockFlowstates[]
	{
		L"InGame.Gameplay",
		L"InGame.Victory",
		L"InGame.VictoryResult",
		L"InGame.VictoryShot.Posing",
		L"InGame.VictoryShot.Shot",
		L"InGame.VictoryShot.Finish"
	};

	constexpr uint32 SDT_Size = sizeof(SDT_MouseLockFlowstates) / sizeof(SDT_MouseLockFlowstates[0]);

	static SDK::FName MouseLockFlowstates[SDT_Size]{};

	static bool bOne{0};
	if (!bOne)
	{
		bOne = 1;

		uint32 i{0};
		while (i < SDT_Size)
		{
			MouseLockFlowstates[i] = FName::NAME_FindOrAdd(SDT_MouseLockFlowstates[i]);
			++i;
		}
	}

	if (AJB::bIsLemonPossessioned)
	{
		AJB::CreateCallbackTimer(AJB::Callbacks::LemonPossession, 0.7f);
	}

	if (SDK::APlayerController* PC = Player(); PC != nullptr)
	{
		for (SDK::FName& Flowstate : MouseLockFlowstates)
		{
			if (NextStateTag.TagName == Flowstate)
			{
				OFF::SetInputGameOnly.Call<decltype(&SDK::UWidgetBlueprintLibrary::SetInputMode_GameOnly)>()(PC);
				PC->bShowMouseCursor = false;
				break;
			}
		}

		static SDK::FName InGameStandby = FName::NAME_FindOrAdd(L"InGame.Standby");
		static SDK::FName InGameResult = FName::NAME_FindOrAdd(L"InGame.Result");
		static SDK::FName SelectStartLocation = FName::NAME_FindOrAdd(L"OutGame.SelectStartLocation");
		static SDK::FName InGameGameplay = FName::NAME_FindOrAdd(L"InGame.Gameplay");

		// Phase 0 diagnostics: these tags mark exactly where player data is first lost.
		if (NextStateTag.TagName == SelectStartLocation)
		{
			// Phase 5: the game's own host row exists by now. Adopting its key and PlayerID here
			// is what lets the post-map-switch rebuild write the host back into the same row
			// instead of creating a second entry for the same player.
			AJB::EnsureLocalHostInSessionCache();

			AJB::DumpMatchingPlayers("MP-OutGame-SelectStart");

			// Phase 5: the real character selections are written by the game flow during the
			// outgame step. They are harvested before the travel so the battle map rebuild keeps
			// them instead of having to fall back to the permitted default.
			AJB::Server::HarvestCharacterSelectionsFromHostTable();

			// Phase 4: refresh every client right before the start location step so the
			// selection screen shows the authoritative table.
			AJB::Server::BroadcastMatchingPlayers("OutGame.SelectStartLocation");
		}
		else if (NextStateTag.TagName == InGameStandby)
		{
			AJB::DumpMatchingPlayers("MP-InGameStandby");

			// Phase 5: harvest the real character ids the game flow already wrote, then push the
			// cosmetics through the game's own update path. The harvest runs first so the apply
			// step can never overwrite a real selection with the default.
			AJB::Server::HarvestCharacterSelectionsFromHostTable();
			AJB::Server::ApplyCharacterSelectionsToSession();

			// Phase 5: rebuild the full table from the session cache before the loading checks
			// run, so a map switch can never leave the battle map with placeholder entries.
			AJB::Server::RebuildMatchingPlayersFromSession();
			AJB::Server::BroadcastMatchingPlayers("InGame.Standby");
		}
		else if (NextStateTag.TagName == InGameGameplay)
		{
			AJB::DumpMatchingPlayers("MP-Gameplay");
		}

		SDK::UWorld* CurrentWorld = GWorld.GetPointer();

        if (AJB::IsServer())
        {
            if (AJB::bIsDedicatedServer)
            {
                if (NextStateTag.TagName == SelectStartLocation && CurrentWorld && CurrentWorld->NetDriver && CurrentWorld->NetDriver->ClientConnections.Num() < 1)
                {
					LogA(OFF::ChangeState.GetName(), "No players are connected to the dedicated server, redirecting to DedicatedServerRestart...");

                    static SDK::FString Restart = L"open /Game/Aeyth8/Maps/DedicatedServer/DedicatedServerRestart";
                    UFunctions::UConsole(GEngine->GameViewport->ViewportConsole, Restart);
                }
            }

			if (NextStateTag.TagName == InGameStandby)
			{
				static const float WaitFor = AJB::NUM_CPUCores >= 4 ? (16.0f / AJB::NUM_CPUCores) * 10.0f : 60.0f;
				AJB::CreateCallbackTimer(AJB::CheckForInfiniteLoadingScreen, WaitFor);
			}
			else if (NextStateTag.TagName == InGameResult)
			{
				AJB::CreateCallbackTimer(AJB::DedicatedServerLoop, 7.5f);
			}
		}

		if (NextStateTag.TagName == MouseLockFlowstates[5])
		{			
			CreateCallbackTimer(AJB::Callbacks::Screenshot, 0.0f);			
		}
	}
	
	return OFF::ChangeState.VerifyFC<bool(__fastcall*)(SDK::FFlowStateHandler* StateHandler, SDK::FGameplayTag NextStateTag)>()(StateHandler, NextStateTag);
}

void __fastcall AJB::OnToggleFullMapVisibility(SDK::UObject* Object)
{
	static bool bToggled{false};
	SDK::UWB_FullMap_C* MapCache{nullptr};

	bToggled = !bToggled;

	LogA("OnToggleFullMapVisibility", Object->GetFullName());

	if (Object->IsA(SDK::ABP_AJBInGameHUD_C::StaticClass()))
	{
		SDK::ABP_AJBInGameHUD_C* HUD = reinterpret_cast<SDK::ABP_AJBInGameHUD_C*>(Object);
		HUD->PlayerOwner->bShowMouseCursor = bToggled;

		OFFSET::VFTable<void(__thiscall*)(SDK::AAJBHUDBase*, SDK::UClass*, SDK::UAJBUserWidget**)>(HUD)[OFF::VFT_FindWidgetOfClass](HUD, SDK::UWB_FullMap_C::StaticClass(), (SDK::UAJBUserWidget**)&MapCache); // AAJBHUDBase::FindAJBWidgetOfClass

		if (MapCache)
		{
			if (bToggled)
			{
				//LogA("FullMapVisibility", MapCache->GetFullName());
				OFF::SetInputMode_GameAndUIEx.Call<decltype(&SDK::UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx)>()(HUD->PlayerOwner, nullptr, SDK::EMouseLockMode::LockAlways, false);
			}
			else
			{
				OFF::SetInputGameOnly.Call<decltype(&SDK::UWidgetBlueprintLibrary::SetInputMode_GameOnly)>()(HUD->PlayerOwner);
			}
		}
	}
}

int __fastcall AJB::PostEventAtLocation(SDK::UAkAudioEvent* AkEvent, SDK::FVector& Location, SDK::FRotator& Orientation, SDK::FString& EventName, SDK::UObject* WorldContextObject)
{
	if (AJB::bDebugModeFromCMLA) LogA(OFF::PostEventAtLocation.GetName(), EventName.ToString());

	const SDK::UKismetStringLibrary* Kismet = Pointers::GetBlueprintClass<SDK::UKismetStringLibrary>();
	
	LogA(OFF::PostEventAtLocation.GetName(), std::format("[AkEvent]: {} | [Location]: {} | [Orientation]: {} | [EventName]: {}", AkEvent->GetFullName(), Kismet->Conv_VectorToString(Location).ToString(), Kismet->Conv_RotatorToString(Orientation).ToString(), EventName.ToString()));

	// Play_BGM03_Menu2 is the song played for the stupid "GameOver" sequence whenever you run out of time in AJBSimpleMatch_P (which I patched long ago) but also when you click to "exit" the game.
	// Normally doing so would play the annoying and pointlessly delayed song and then eventually try to go to AJBStartUp_P.
	// Since my browse hook already redirects it to my Titlescreen this hook will simply end the stupid delayed sequence early and immediately head back to the Titlescreen, I'M DONE WAITING.

	if (EventName.ToString() == "Play_BGM03_Menu2")
	{
		constexpr const wchar_t* Titlescreen = L"open /Game/Aeyth8/Maps/TitleScreen/AJBTitleScreen";
		SDK::FString ImmediateExit(Titlescreen);

		UFunctions::UConsole(GEngine->GameViewport->ViewportConsole, ImmediateExit);
	}

	return OFF::PostEventAtLocation.VerifyFC<int32(__fastcall*)(SDK::UAkAudioEvent*, SDK::FVector&, SDK::FRotator&, SDK::FString&, SDK::UObject*)>()(AkEvent, Location, Orientation, EventName, WorldContextObject);
}

void __fastcall AJB::OnVictoryShot(SDK::UObject* Object)
{
	LogA("OnVictoryShot", "Screenshotting game...");
	Pointers::GetBlueprintClass<SDK::UAJBUtilityFunctionLibrary>()->Screenshot(L"Screenshot", true);
}

SDK::UAJBWindowWidget* __fastcall AJB::AJBWindowWidget(SDK::UAJBWindowWidget* This)
{
	SDK::UAJBWindowWidget* Result = OFF::AJBWindowWidget.VerifyFC<SDK::UAJBWindowWidget*(__fastcall*)(SDK::UAJBWindowWidget*)>()(This);

	if (!Result->IsDefaultObject() && !(Result->Flags & SDK::EObjectFlags::ArchetypeObject))
	{
		if (Result->IsA(SDK::UWB_ModeSelect_C::StaticClass()))
		{
			//LogA("ModeSelect", Result->GetFullName());

			AJB::SimpleMatchHUD = (SDK::UWB_ModeSelect_C*)This;

			AJB::CreateCallbackTimer(AJB::Callbacks::TranslateSimpleMatch, 0.0f);
		}
		/*else if (Result->IsA(SDK::UWB_GameOver_C::StaticClass()))
		{
			LogA("Stupid", "it may be stupid BUT ITS ALSO DUMB");
		}*/		
	}

	return Result;
}

SDK::ALevelScriptActor* __fastcall AJB::ALevelScriptActor(SDK::AActor* This, void* ObjectInitializer)
{
	if (This->IsA(SDK::AAJBCreadit_C::StaticClass())) {
		AJB::CreaditPointer = static_cast<SDK::AAJBCreadit_C*>(This);
	}
	return OFF::ALevelScriptActorConstructor.VerifyFC<SDK::ALevelScriptActor* (__fastcall*)(SDK::AActor*, void*)>()(This, ObjectInitializer);
}

SDK::UMediaPlayer* __fastcall AJB::UMediaPlayer(SDK::UMediaPlayer* This, void* ObjectInitializer)
{
	if (!AJB::LemonPlayer && This)
	{
		static SDK::FName ElNameOh = FName::NAME_FindOrAdd("LemonPlayer");
		if (This->Name == ElNameOh)
		{
			//LogA("LemonPlayer", This->GetFullName());
			AJB::LemonPlayer = This;
		}
		//LogA(OFF::MediaPlayer.GetName(), This->GetFullName());
	}
	return OFF::MediaPlayer.VerifyFC<SDK::UMediaPlayer*(__fastcall*)(SDK::UMediaPlayer*, void*)>()(This, ObjectInitializer);
}
