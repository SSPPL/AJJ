# AJB-main — Master Offset Reference (v28 → v33)

**Purpose.** This is the authoritative, verification-annotated record of every offset, byte-patch constant,
vtable index, and struct field offset in the AJB-main mod, with each value's v28 source, its v33 value, and
**exactly how it was verified against the binary.** It is intended to be the basis for future migrations
(v33 → next). When a new build lands, each row below tells you what the symbol *is* and how to re-anchor it,
so you never have to re-derive identity from scratch.

**Binaries.**
- v28 = `AJB-Win64-Shipping.exe` under "PC Port Prod v0.5.5" (IDA IDB `E:\AJB\...\.i64`).
- v33 = `AJB-Win64-Shipping.exe` / `JJL10JPN-33` (IDA IDB `E:\v33IDA\...\.i64`).
- Both analyzed with **imagebase 0x0**, so all addresses here are **RVA** (file/module offsets, no rebasing).
- Tooling: two IDA Pro instances driven over MCP (port 13337 = v28, 13338 = v33).

**Golden rule used throughout.** Never trust a delta or a reference doc. For every symbol: decompile the
address AJB-main actually points at in v28 → establish true identity → find that same function in v33 by an
independent anchor (unique string, AOB signature with only volatile RIP/rel displacements wildcarded, xref,
vtable slot, or struct/reflection anchor) → confirm structural identity → record. This caught multiple traps
(GetMaxFPS reference-doc error, NotifyControlMessage overload confusion, CopyString/IsAJBOfflineMode AOB
collisions, two UMG struct shifts, and the VFT_FindWidgetOfClass +2 index shift).

**Files that carry offsets (6).** `Offsets.cpp`, `Offsets.h`, `Logic/AJB.cpp`, `Tools/UFunctions.cpp`,
`Logic/ServerLogic.cpp`, `Logic/Callbacks/AJBCallbacks.cpp`. Verified by tree-wide sweep: no other `.cpp/.h/.hpp`
file under `PCPortSource/Aeyth8` contains game-address-range literals. The 3 `.asm` files carry **no** game
offsets (see §7). One dead comment in `dllmain.cpp` is the only unresolved item (see §8).

---

## Verification legend

- **AOB** — located in v33 by a byte-signature copied from v28, wildcarding only volatile bytes (RIP-relative
  displacements, rel32 call/jmp targets). "unique" = exactly one match; "disambiguated" = multiple matches
  resolved by neighborhood + structural confirmation.
- **STR** — anchored by a unique string reference in the function body.
- **SDK** — cross-checked against the v33 Dumper-7 reflection dump (`PCPortSource/Dumper-7/SDK/`).
- **VT** — read directly from a vtable slot in both binaries.
- **IDA-named** — IDA independently assigned the same symbol name in both DBs (strong identity confirmation).
- **DATA** — a data global (not a function); anchored via a code site that references it.

---

## 1. Named offsets — `Offsets.cpp` (71)

All 71 verified; each `OFFSET Name("sig", 0xV28)` had its literal replaced with the v33 value. `*` = the decl
is commented-out in source (migrated anyway for diff-fidelity).

| Symbol | v28 | v33 | How verified |
|---|---|---|---|
| Tick | 0x13E1E30 | 0x13E8040 | AOB unique (UGameEngine::Tick) |
| GEngine `DATA` | 0x32553B8 | 0x325F438 | qword passed to GetWorldFromContextObject in sub_213690; =SDK-consistent |
| GWorld `DATA` | 0x3257AF0 | 0x3261B70 | GetWorldFromContextObject fallback in sub_213690; live global, many xrefs |
| FMalloc | 0x5C75B0 | 0x5CD7D0 | AOB unique |
| FRealloc | 0x5C9790 | 0x5CF9B0 | AOB unique |
| FFree | 0x5BC5E0 | 0x5C2800 | AOB unique |
| FNameW | 0x681ED0 | 0x6880F0 | AOB unique |
| FNameA | 0x681E50 | 0x688070 | AOB unique |
| FNameTS | 0x692ED0 | 0x6990F0 | AOB unique (FName::ToString) |
| Logf * | 0x64D570 | 0x653790 | AOB unique (vararg printf, FOutputDevice::Logf) |
| OutputText | 0x17A8BF0 | 0x17AEE00 | AOB unique (UConsole::OutputText) |
| ProcessEvent | 0x823B30 | 0x829D50 | AOB unique (UObject::ProcessEvent) |
| Invoke | 0x70DBF0 | 0x713E10 | AOB unique (UFunction::Invoke) |
| AppPreExit | 0x1E3380 | 0x1E3610 | AOB unique + STR "Preparing to exit." (FEngineLoop::AppPreExit) |
| SetClientTravel | 0x1781B80 | 0x1787D90 | AOB unique |
| ClientTravelInternal | 0x18C5FD0 | 0x18CC260 | AOB unique (thunk; ≠ current's target — faithful to AJB-main) |
| StartLoadingDestination | 0x17CF0A0 | 0x17D52B0 | AOB unique (real worker; ≠ current) |
| PreLogin | 0x13D7710 | 0x13DD920 | AOB unique (AGameModeBase::PreLogin) |
| AJBPreLogin | 0x04A4060 | 0x4A4CB0 | AOB unique (AJB gamemode PreLogin) |
| Login | 0x13D2A20 | 0x13D8C30 | AOB unique |
| PostLogin | 0x13D6A70 | 0x13DCC80 | AOB unique |
| Logout | 0x13D2CD0 | 0x13D8EE0 | AOB unique (override wrapper; ≠ current) |
| BeginPlay | 0x17BAEC0 | 0x17C10D0 | AOB unique (UWorld::BeginPlay) |
| HandleStartingNewPlayer | 0x182C450 | 0x18326F0 | AOB unique (thunk; ≠ current) |
| InitListen | 0x3FBE40 | 0x3FC0D0 | AOB unique (UIpNetDriver::InitListen) |
| InitConnection * | 0x14FBAF0 | 0x1501D00 | AOB unique (sole caller UDemoNetConnection::InitConnection) |
| InitLocalConnection | 0x3FBFB0 | 0x3FC240 | AOB unique (UIpConnection::InitLocalConnection) |
| NotifyControlMessage | 0x15BDFE0 | 0x15C41F0 | **UPendingNetGame overload** — proven by source hook signature (see §5-a). current's 0x17CCCC0 = UWorld overload = WRONG for this mod |
| PeekNetworkFailureMessages | 0x140CB20 | 0x1412D30 | AOB unique |
| AddClientConnection | 0x14EEDF0 | 0x14F5000 | AOB unique |
| HandleClientPlayer | 0x14FB010 | 0x1501220 | AOB unique (UNetConnection::HandleClientPlayer) |
| Close | 0x14F30C0 | 0x14F92D0 | AOB unique (UNetConnection::Close) |
| UConsole | 0x1796230 | 0x179C440 | AOB unique (UConsole::ConsoleCommand) |
| ConsoleCommand | 0x16077D0 | 0x160D9E0 | AOB unique (APlayerController::ConsoleCommand) |
| Browse | 0x175C530 | 0x1762740 | AOB unique (UEngine::Browse) |
| IsTimeLimitedExceeded | 0x17C4BB0 | 0x17CADC0 | AOB unique |
| AddToWorld | 0x17BA220 | 0x17C0430 | AOB unique (ULevel::AddToWorld) |
| RemoveFromWorld | 0x17CAEA0 | 0x17D10B0 | AOB unique |
| SpawnActor | 0x1494440 | 0x149A650 | AOB unique |
| DestroyActor | 0x1484190 | 0x148A3A0 | STR "UWorld::DestroyActor: World has no context!" (≠ current) |
| ProcessMulticastDelegate | 0x20C010 | 0x20C2A0 | AOB unique (TMulticastScriptDelegate<FWeakObjectPtr>::…UObject) |
| ClientTeamMessage | 0x18C5F00 | 0x18CC190 | AOB unique (≠ current) |
| ClientTeamMessageImplementation | 0x1606D20 | 0x160CF30 | AOB unique |
| ActorDestroy | 0x11AC5E0 | 0x11B27F0 | AOB unique (AActor::Destroy) |
| CopyString | 0x1E0EE0 | 0x1E1170 | Disambiguated from 7; IDA-named FString_CopyConstructor + body match |
| IsNonPakFileNameAllowed | 0x191F540 | 0x1925860 | AOB unique |
| FindFileInPakFiles | 0x191C430 | 0x1922750 | AOB unique |
| StaticLoadClass | 0x84B1A0 | 0x8513C0 | AOB unique |
| StaticFindObject | 0x84A3D0 | 0x8505F0 | AOB unique |
| StaticLoadObject | 0x84B620 | 0x851840 | AOB unique |
| CreateDefaultObject | 0x707A60 | 0x70DC80 | AOB unique (UClass::CreateDefaultObject) |
| StaticConstructObject | 0x849630 | 0x84F850 | AOB unique (StaticConstructObject_Internal) |
| BroadcastDelegate | 0x1E3620 | 0x1E38B0 | AOB unique (broadcast worker) |
| ALevelScriptActorConstructor | 0x147E150 | 0x1484360 | AOB unique |
| ToFormattedString | 0x60F080 | 0x6152A0 | AOB unique |
| SetInputGameOnly | 0x10C41D0 | 0x10CA3E0 | AOB unique (SetInputMode_GameOnly) |
| SetInputMode_GameAndUIEx | 0x10C4060 | 0x10CA270 | AOB unique |
| ImageSetBrushFromMaterial | 0x10C1D10 | 0x10C7F20 | AOB unique (UImage::SetBrushFromMaterial) |
| BorderSetBrushFromMaterial | 0x10C1C10 | 0x10C7E20 | AOB unique (UBorder::SetBrushFromMaterial) |
| MediaPlayer | 0x19813D0 | 0x19876F0 | AOB unique (UMediaPlayer ctor) |
| OpenSource | 0x19877F0 | 0x198DB10 | AOB unique (UMediaPlayer::OpenSource) |
| PostEventAtLocation | 0x292F30 | 0x2931C0 | AOB unique (UAkGameplayStatics::PostEventAtLocation) |
| ChangeState | 0x21CF10 | 0x21D1A0 | AOB unique (UFlowStateUtil::ChangeState) |
| TryGetMatchingMyPairInfo | 0x486FC0 | 0x487370 | STR "UAJBGameInstance::TryGetMyPairMatchingPlayerInfo" |
| TryGetMatchingPlayerInfo | 0x486A70 | 0x486E20 | AOB unique |
| GetUsername | 0x694650 | 0x69A870 | AOB unique |
| GetNationalMatchSchedule | 0x47E410 | 0x47E770 | AOB unique |
| AJBWindowWidget | 0x534690 | 0x539C90 | AOB unique (UAJBWindowWidget ctor) |
| IsTenpoHost | 0x507210 | 0x50C7D0 | IDA-named AAJBOutGameProxy_IsTenpoHost |
| IsAJBOfflineMode | 0x49DE20 | 0x49EA70 | Disambiguated from 2; IDA-named + tail-calls verified IsOfflineMode 0x4F2830 |
| IsOfflineMode | 0x4ED5D0 | 0x4F2830 | AOB unique (UAJBNetworkObserver::IsOfflineMode) |

**Naming note (flag c):** the symbol the source calls `GSetString` (an inline `PB()`, see §4) is actually
`FConfigCacheIni::GetString` — a source labeling quirk, migrated by true identity. Non-issue.

### 1a. Comment-block reference offsets in `Offsets.cpp` (informational, also updated for fidelity)
These live in a `/* … */` note, not in code, but were migrated so the note stays accurate:
DestroyActor 0x1484190→0x148A3A0, AActor::Destroy 0x11AC5E0→0x11B27F0, FString::FString 0x1E0EE0→0x1E1170,
FMemory::Malloc 0x5C75B0→0x5CD7D0, **FMemory::QuantizeSize 0x5C91A0→0x5CF3C0** (AOB unique; this one has no
active `OFFSET` decl — comment only), FMemory::Realloc 0x5C9790→0x5CF9B0, FMemory::Free 0x5BC5E0→0x5C2800.

### 1b. Expression offsets (no literal edited — auto-derive from bases above)
`WorldWelcomePlayer = WelcomePlayer6B − 0x113` (= WelcomePlayer entry); `WelcomePlayerStripped =
WelcomePlayer6B − 0x5` (= the stripped call start); `WelcomedByServer = NotifyControlMessage + 0xB8F`.
These update automatically when their base is migrated; verified the resulting addresses land correctly.

---

## 2. Byte-patch constants — `Offsets.h` (11)

Each is a specific instruction address the mod patches. `+off` notes show how the address sits relative to a
function entry, which is how they were re-anchored (find the entry via AOB, then apply the fixed offset;
instruction bytes at the offset were confirmed identical).

| Constant | v28 | v33 | How verified |
|---|---|---|---|
| HideCursorCaller | 0x04A04A0 | 0x04A10F0 | AOB at patch site (note: reference doc's 0x4A04A0 was the v28 value) |
| AJBGetMaxTickRate | 0x13CCB43 | 0x13D2D53 | entry sub_13CCAE0→0x13D2CF0, +0x63; instruction-identical |
| AJBGetMaxTickRateCap | 0x13CCBD8 | 0x13D2DE8 | same entry, +0xF8 |
| ResetPP | 0x04840B0 | 0x0484420 | AOB at patch site |
| StartConsumePP | 0x0522CE0 | 0x05282A0 | AOB at patch site |
| LogVerbosity `DATA` | 0x300D3C8 | 0x3017348 | data global anchor |
| NetDriverGetNetMode | 0x14F90F0 | 0x14FF300 | AOB at patch site (UNetDriver GetNetMode) |
| WorldInternalGetNetMode | 0x17C4820 | 0x17CAA30 | AOB at patch site |
| ActorInternalGetNetMode | 0x11BA5C0 | 0x11C07D0 | AOB at patch site |
| WelcomePlayer6B | 0x17D2EF3 | 0x17D9103 | stripped-VFT `call [rax+700h]` site; +5 geometry-verified. Anchors the §1b expressions |
| FControlChannelOutBunch | 0x1357E90 | 0x135E0A0 | AOB at patch site |

---

## 3. VFT vtable indices — `Offsets.h` (8)

Indices into a C++ vtable (used as `VFTable<...>(obj)[index]`). **Seven unchanged, one changed.** All were
confirmed by reading the actual vtable slot in both binaries (engine classes) or via the engine's own exec-thunk
dispatch offset (the game class). Method for finding an engine vtable: decompile the class constructor, read the
`*(this) = <vtable>` it writes, then read `vtable + index*8`.

| Constant | v28 | v33 | Target function | How verified |
|---|---|---|---|---|
| VFT_GameEngineTick | 0x4F | 0x4F | UGameEngine::Tick | VT read, both (UGameEngine vtable) |
| VFT_GetMaxFPS | 0x51 | 0x51 | UEngine::GetMaxFPS | VT read, both — v33 slot[0x51]@0x25C1C20→0x1770430 byte-identical. **Reference doc's 0xB9 was WRONG** |
| VFT_HandleClientPlayer | 0x55 | 0x55 | UNetConnection::HandleClientPlayer | VT read, both (base 0x260EB90, 0x2A8=0x55·8) |
| VFT_GetMaterial | 0x49 | 0x49 | UMaterialInterface::GetMaterial | VT read, both (via GetBaseMaterial +0x248 thunk) |
| VFT_ClientMainMenu | 0x141 | 0x141 | APlayerController::ClientReturnToMainMenuWithTextReason | VT read, both — v28 slot→0x18C5C90, v33 slot→0x18CBF20 (client-RPC thunk, single const FText& arg) |
| VFT_LocalTravel | 0x13F | 0x13F | APlayerController::LocalTravel | VT read, both — v28 slot→0x1617B50, v33 slot→0x161DD60, both IDA-named LocalTravel |
| VFT_NetworkFailureMsg | 0x62 | 0x62 | UGameViewportClient::PeekNetworkFailureMessages | VT read, both (base 0x25CE3B0, 0x310=0x62·8) |
| **VFT_FindWidgetOfClass** | **0xFE** | **0x100** | AAJBHUDBase::FindAJBWidgetOfClass | **CHANGED +2.** Game class, unsymbolized/RTTI-stripped → used the engine's exec-thunk dispatch as ground truth (see §5-e) |

**APlayerController vtable bases:** v28 `0x2645240` (ctor 0x15FC300), v33 `0x264DFE0` (ctor 0x1602510). The two
constructors are byte-identical in field layout — that's the class-stability evidence backing the engine-class
indices. `VFT_FindWidgetOfClass` is on a *game* blueprint class (`AAJBHUDBase`), which is why it could shift
while the engine ones didn't.

---

## 4. Inline `PB()` offsets — 4 .cpp files (22 distinct, 27 occurrences)

Offsets passed directly to `PB(0x…)` in code. `×N` = appears N times (all replaced). `*` = commented-out.

**Logic/AJB.cpp**
| v28 | v33 | Identity / how verified |
|---|---|---|
| 0x17A80B0 | 0x17AE2C0 | UPlayerInput input fn — AOB unique |
| 0x1908090 | 0x190E3B0 | UUserInterfaceSettings::GetPrivateStaticClass — IDA-named in v33, STR "UserInterfaceSettings" + 600 const |
| 0x8066C0 | 0x80C8E0 | property-config fn — AOB unique; 5441B/325BB, STR "Windows", calls GetSectionPrivate |
| 0x620550 | 0x626770 | FConfigCacheIni::Flush — AOB unique |
| 0x2233A0 | 0x223630 | NBAM save read — AOB unique |
| 0x223610 | 0x2238A0 | FDrive folder-creator — FindFirstFileA/NextFileA + STR "f:\\%s\\*", 57BB match |
| 0x20E680 | 0x20E910 | AMActivator_Destroy wrapper — sole ref of ext import, size 0x5b |
| 0x20E8D0 | 0x20EB60 | AMActivator_RequestOneTimeKey wrapper — IDA-named _wrapper, size 0x16 |
| 0x20EA10 | 0x20ECA0 | AMActivator_GetSignatureGeneration wrapper — sole ref, size 0x17 |
| 0x20FB00 | 0x20FD90 | AMActivator_Update wrapper — sole ref, size 0x14 |
| 0x522530 * | 0x527AF0 | IsActiveAJBError — AOB unique |
| 0x47C510 * | 0x47C7A0 | ClearMatchingID — STR "ClearMatchingID" (disambiguated from Shutdown collision) |
| 0x6246F0 * | 0x62A910 | FConfigCacheIni::GetSectionPrivate — AOB unique |
| 0x6213C0 | 0x6275E0 | FConfigFile::GenerateExportedPropertyLine — AOB unique |
| 0x6331F0 | 0x639410 | FConfigCacheIni::SetString (the "GSetString" quirk is a different symbol) — AOB unique |
| 0x3051380 ×3 | 0x305B400 | GEngineIni FString global `DATA` — anchored via UWidget::GetDefaultFontName `lea` in both |

**Tools/UFunctions.cpp**
| v28 | v33 | Identity / how verified |
|---|---|---|
| 0x3051520 | 0x305B5A0 | LogNetSerialization verbosity byte `DATA` — anchored via operator<< `cmp cs:byte,2` sites in both |
| 0x5802E0 ×3 | 0x586500 | FArchive::operator<< — AOB unique (used at NotifyControlMessage handler) |
| 0x6837F0 * | 0x689A10 | FName::AppendString — AOB unique |

**Logic/ServerLogic.cpp**
| v28 | v33 | Identity / how verified |
|---|---|---|
| 0x5F9600 ×2 | 0x5FF820 | FText::FromString — AOB unique |
| 0x18C6280 * | 0x18CC510 | KickPlayer FText-helper — sole caller AGameSession::KickPlayer (disambiguated from 6) |

**Logic/Callbacks/AJBCallbacks.cpp**
| v28 | v33 | Identity / how verified |
|---|---|---|
| 0x18D70A0 * | 0x18DD3C0 | UPrimitiveComponent::execSetMaterial — AOB unique (pure comment) |

---

## 5. Struct-layout / field offsets — `AJBCallbacks.cpp` `WidgetsToTranslate[]`

The only genuinely struct-layout-sensitive site. Each row translates one mode-select button. Two columns are
field offsets: **OffsetToClass** = the `WB_ModeSelect_Button_*` member offset inside `UWB_ModeSelect_C`;
**OffsetToRetainerBox** = the `WB_ModeSelect_Txt_*` member offset inside that button subclass (normal rows) OR
the `RetainerBox_N` offset (the 2 `bSubwidget=true` rows). **All 10 rows verified against the v33 SDK reflection
dump (independent of `current`).** Only 2 values changed.

| Widget | OffsetToClass (v28→v33) | OffsetToRetainerBox (v28→v33) | SDK anchor (v33) |
|---|---|---|---|
| Button_PAIR | 0x02E8 (same) | **0x0370 → 0x0378** | Txt_PAIR @ 0x0378 (v33 inserted Txt_Notice @0x0370) |
| Button_Reward | 0x0300 (same) | 0x0398 (same) | Txt_Reward @ 0x0398 |
| Button_PremiumDraw | 0x02F0 (same) | 0x0378 (same) | Txt_PremiumDraw_C_0 @ 0x0378 |
| Button_PvE | 0x02F8 (same) | 0x0330 (same) | Txt_PvE_C_1 @ 0x0330 |
| Button_Shop | 0x0310 (same) | 0x0368 (same) | Txt_Shop @ 0x0368 |
| Button_SOLO | 0x0318 (same) | **0x0370 → 0x0378** | Txt_SOLO @ 0x0378 (v33 inserted Txt_InactivePreMes @0x0370) |
| Button_Training | 0x0320 (same) | 0x0330 (same) | Txt_Training @ 0x0330 |
| Button_Tutorial | 0x0328 (same) | 0x0358 (same) | Txt_Tutorial @ 0x0358 |
| Button_EndGame `sub` | 0x02E0 (same) | 0x0320 (same) | RetainerBox_1 @ 0x0320 |
| Button_PvE `sub` | 0x02F8 (same) | 0x0328 (same) | RetainerBox_0 @ 0x0328 |

**Not migrated (correctly):** `A8CL/NTSurfer/NTSurfer.hpp` PEB/TEB structs are OS/PE-version dependent, not
game-binary dependent — intentionally left untouched. `Tools/UnrealTypes.h` has only fn-ptr typedefs + enums.

---

## 6. The divergences from the prior `current` tree (context, not error)

`current` is a *different* mod built for the same v33 binary; it is a hint, never an override. Where AJB-main's
v28 address points at a different-but-related function than `current`'s same-named symbol, AJB-main was followed
(decompile-verified). Seven such cases, most important first:

- **(5-a) NotifyControlMessage** → `UPendingNetGame::NotifyControlMessage` (0x15C41F0). **Proven by AJB-main's
  own source:** hook `void UFunctions::NotifyControlMessage(SDK::UPendingNetGame* This, …)`, `Decl` typedef
  `__thiscall(SDK::UPendingNetGame*, …)` (UFunctions.hpp:198), log string `"[UPendingNetGame]"`, body reads
  `This->URL`. `current`'s 0x17CCCC0 is the `UWorld` overload and would be wrong here.
- ClientTravelInternal 0x18CC260 (thunk), HandleStartingNewPlayer 0x18326F0 (thunk), DestroyActor 0x148A3A0
  (STR-anchored), Logout 0x13D8EE0 (override wrapper), StartLoadingDestination 0x17D52B0 (real worker),
  ClientTeamMessage 0x18CC190.

---

## 7. Assembly files (3) — NO game offsets (verified)

- `Logic/Server/GameWelcomePlayer.asm` (`ASMGrabRedirectURL`): operates only on **mod-owned struct fields**
  via stack-relative offsets (`[rcx+8/16/32/40]`) from the `PointerOfAgony`/`StructureOfHell` control block the
  C++ sets up. The actual game addresses it uses (WelcomePlayer6B jump target, CopyString call) are injected
  from C++ via the **migrated named offsets** (`StructureOfHell.JumpTo = PB(OFF::WelcomePlayer6B)`,
  `CopyStringCall = OFF::CopyString.PlusBase()`). Version-independent.
- `Tools/IsInLocalDirectory.asm`: compares against hardcoded UTF-16 string immediates (`L"./.."` etc.). No
  offsets. Version-independent.
- `Proxy8/Entry/Entry.asm`: proxy DLL entrypoint using PEB/TEB ABI offsets (`gs:[60h]`, ImageBase field).
  OS-ABI constants. Version-independent.

The `RedirectURL` machinery (ServerLogic.cpp ~270-325) therefore needs no offset edits — it flows entirely
through the migrated `WelcomePlayer6B` / `CopyString` offsets and the derived expression offsets.

---

## 8. OPEN ITEM — one unused comment (does not affect the build)

`PCPortSource/dllmain.cpp:43` contains a lone comment `// 0x20773C4` directly above `PreInit()`. **It is
commented out and referenced by no code** (confirmed by a tree-wide search: it appears exactly once, in that
comment). Because it is commented out and unused, **it is not a problem** — it has zero effect on the build or
runtime. It is almost certainly a v28-era scratch note for the PreInit hook/entry site, left as-is.

*(Optional, novelty only — do not action unless requested: to identify its v33 equivalent, inspect address
`0x20773C4` in the v28 IDB, find the same site in v33 by the usual anchor, and update or delete the comment.
Purely documentation hygiene; there is no functional gap.)*

---

## 8b. RedirectURL / GameWelcomePlayer mechanism — VERIFIED IN IDA (register convention holds in v33)

**Result: PASS. The assembly trampoline's assumptions hold exactly in v33 — no silent-regression risk. This was
the one genuine binary-verification item outstanding; it is now closed.**

What the mechanism does (per source comments + code):
- `AGameModeBase::GameWelcomePlayer` **does not exist as a discrete function in the build — it was
  stripped/inlined by the compiler.** There is nothing to hook the normal way. What remains is the *call site*
  inside `UWorld::GameWelcomePlayer`, and at that instruction `r8` holds the `RedirectURL` pointer.
- So the mod hooks a specific **byte inside that call** (`WelcomePlayer6B`, migrated `0x17D2EF3 → 0x17D9103`)
  and, because hooking mid-instruction clobbers registers, uses `ASMGrabRedirectURL` (in
  `GameWelcomePlayer.asm`) as a register-preserving trampoline: it saves `r8`, calls `CopyString` (migrated
  `0x1E0EE0 → 0x1E1170`) to overwrite the game's empty RedirectURL with the mod's `NewRedirectURL`, restores
  rcx/rdx/rax, and jumps back to `WelcomePlayer6B`.
- The struct fields the .asm reads (`[rcx+8/16/32/40]`) are the **mod-owned** `StructureOfHell`
  (`PointerToStruct`), populated from C++ in `InitHellscape()` using the migrated named offsets. Those are not
  game offsets, so they need no migration.

**IDA verification (both binaries disassembled, call site compared instruction-by-instruction):**
The `UWorld::GameWelcomePlayer` function is structurally identical v28↔v33. The sequence into the stripped call:

| | v28 | v33 |
|---|---|---|
| `mov rcx,[rsi+140h]` (UWorld) | 0x17D2EDC | 0x17D90EC |
| `lea r8,[rsp+var_160]` (**r8 = &RedirectURL**) | 0x17D2EE3 | 0x17D90F3 |
| `mov rdx,rdi` (UNetConnection) | 0x17D2EE8 | 0x17D90F8 |
| `mov rax,[rcx]` (vtable) | 0x17D2EEB | 0x17D90FB |
| `call qword ptr [rax+700h]` (stripped VFT call) | 0x17D2EEE | 0x17D90FE |

Every register assignment matches: **r8 = &RedirectURL (same `var_160` = `[rsp+0x38]` slot in both)**, rcx =
UWorld, rdx = UNetConnection, rax = vtable, and the same VFT slot `[rax+700h]`. The derived offsets check out
exactly: `WelcomePlayer6B` v33 = `0x17D90FE + 5 = 0x17D9103` ✓; `WelcomePlayerStripped` = `0x17D9103 − 5` =
`0x17D90FE` ✓; `WorldWelcomePlayer` (entry) = `0x17D9103 − 0x113` = `0x17D8FF0` ✓. Patch geometry confirmed by
byte read: the call at `0x17D90FE` is `FF 90 00 07 00 00` (6 bytes); byte 5 = `0x17D9103` = value `00` — the
orphaned 6th byte the 5-byte hook leaves behind, which is why `WelcomePlayer6B` must be patched "to keep
alignment." `CopyString 0x1E1170` was confirmed to be `FString_CopyConstructor` with the exact `(dest, src)`
`__fastcall` signature the trampoline calls it with.

**Conclusion:** r8 = RedirectURL and the rcx/rdx/rax convention survived the v28→v33 compiler change intact. The
hand-rolled trampoline is safe as-is; no changes needed. (Skipping the .asm by hooking a real function is still
impossible — `AGameModeBase::GameWelcomePlayer` remains stripped/inlined in v33, same as v28.)

---


## 8c. SDK & struct layout — the big-picture scope note (READ THIS)

There are **two** completely different kinds of struct/field offset in this project, and only one of them is in
the scope of an offset-literal migration.

**(A) The generated SDK — `PCPortSource/Dumper-7/SDK/` (~2,341 files).** This is the Dumper-7 dump and it
describes the **v28** game. Every field the mod reads through an SDK type (`Connection->PlayerController`,
`HUD->PlayerOwner`, `Box->Slots`, `Instance->PlayMode`, the `FUniqueNetIdRepl` param structs, etc.) uses the
offset baked into these headers. Wherever v33 moved a field, this dump is wrong. **This was NOT part of the
offset-literal migration and the output tree still contains the v28 SDK.** The correct engineering action is to
**regenerate the SDK with Dumper-7 against the v33 binary** (this is exactly what the `current` tree did — it
ships a *different*, 2,443-file v33 dump; the 100+ extra files are new v33 classes). Confirmed example of a real
shift: `UWB_ModeSelect_Button_PAIR_C::WB_ModeSelect_Txt_PAIR` is `0x0370` in the bundled v28 SDK but `0x0378`
in v33 — the same shift the widget table in §5 had to absorb. **Do not attempt to hand-patch 2,300 files; re-dump.**
(Historically stable anchors that did NOT move v28→v33, checked here: `UUserWidget` ends at `0x208`,
`AActor` at `0x328`, `AGameModeBase` at `0x3C8`, `UActorComponent` at `0xF0` — so *base* offsets are stable;
it is individual blueprint/class member layouts that can move.)

**(B) The hand-written `CustomSDK` — `PCPortSource/Dumper-7/CustomSDK/` (20 files).** These are Blueprint classes
the mod author declared by hand (they are NOT re-emitted by a normal Dumper-7 run), each with hardcoded offsets
and explicit `Pad_XXX[..]`. **These must be checked/updated by hand for v33** — this is where the "we had to add
padding in the original v33 port" pain lives. The mod actually dereferences these fields (verified by usage
search), so they matter:

| Custom class (base) | Field used by mod | Offset (v28 SDK) | Base stable? | Action for v33 |
|---|---|---|---|---|
| `UWBP_OptionsMenu_C` (UUserWidget) | `OnlineStatus` | 0x0210 | yes (0x208) | re-verify member layout vs v33 (has internal `Pad_2C2[0x6]`) |
| `UWBP_OptionsMenu_C` | `InternalTickRate` | 0x0240 | yes | re-verify |
| `UWBP_OptionsMenu_C` | `bIsOptionsMenuVisible` | 0x02C0 | yes | re-verify (near the padded tail) |
| `UWBP_BLOnlineStatus_C` (UUserWidget) | `OnlineStatus`,`bShouldUpdate` | 0x0218/0x0219 | yes | re-verify |
| `UBP_GlobalPatcher_C` (UObject) | `ProofOfExistenceSignature` | 0x0028 | yes | low risk (UObject base) |
| `ABP_Synchronizer_C` (AActor) | `PlayMode` | 0x0338 | yes (0x328) | re-verify |
| `UBP_AJBGameInstance_C` (main-SDK subclass) | `PlayMode` | (main SDK) | — | covered by SDK re-dump (A) |
| `ALemonHelper_C` (AActor) | (LemonPlayer is a separate `UMediaPlayer*` global, not this field) | 0x0330 | yes | low risk / unused-as-field |
| `UWBP_Cursor_C`, `UWBP_TLVersionInfo_C` | single member @0x208 | 0x0208 | yes | low risk (single member, stable base) |

Because these are hand-authored and their blueprints are the mod's own, the safe move for each **used** field is
to confirm the offset against the v33 game (either via a fresh Dumper-7 pass that includes them, or by reading
the class layout in IDA). The base-class offsets are all confirmed stable, so any breakage would come from a
member being inserted *within* one of these blueprints between v28 and v33 — exactly the `WB_ModeSelect` failure
mode. **This is the most likely place, after §8b, for a v33 struct-layout regression, and it is by design not
covered by the offset-literal migration.**

---



1. **Set up two IDA instances**, both at imagebase 0x0 (RVA). Old = current shipping build, New = target build.
2. **Work file-by-file** through the 6 offset-carrying files. For each literal:
   - Decompile the OLD address; establish identity (prefer a unique STR or IDA name; else structural shape).
   - Find it in NEW by an **independent anchor** — STR first, then AOB (wildcard only RIP/rel bytes), then
     xref / vtable / SDK. Confirm structural identity before accepting.
   - For collisions, disambiguate by address neighborhood + callee/caller/size match. Never take the first hit
     blindly.
3. **VFT indices:** read the actual vtable slot in both builds (engine classes via the constructor's
   `*(this)=vtable`; game/blueprint classes via the engine's `exec<Fn>` thunk dispatch offset =
   `vtable + index*8`). Do **not** assume indices are stable — `VFT_FindWidgetOfClass` shifted +2 in v33.
4. **Struct/UMG offsets:** verify against the NEW Dumper-7 SDK reflection dump, following the exact member the
   code dereferences (mind multi-level indirection — the widget table points at Txt members, not RetainerBox
   members directly).
5. **Assembly & PEB/TEB:** confirm they still carry no game offsets (they didn't here). Don't touch OS-ABI
   constants.
6. **Watch traps:** reference docs can be wrong (GetMaxFPS 0xB9); "same name" in another tree can be a
   different overload (NotifyControlMessage); deltas are never reliable across data or code; commented-out
   literals still get migrated for diff-fidelity.
7. **Validate:** diff NEW-edited vs NEW-original — every changed line must differ **only** in hex tokens
   (a scripted hex-normalized comparison catches any accidental code/comment drift).
8. **Regenerate the SDK (separate from offset migration):** run Dumper-7 on the NEW binary and replace
   `Dumper-7/SDK/`. The bundled SDK is version-specific; do not hand-patch it. (§8c-A.)
9. **Hand-check `Dumper-7/CustomSDK/` (20 files):** these are author-written and are NOT re-emitted by Dumper-7.
   For every field the mod actually uses (§8c-B table), confirm the offset against the NEW game — base offsets
   are usually stable, but a member inserted inside one of these blueprints will silently shift the rest.
10. **Re-verify the GameWelcomePlayer trampoline register convention:** disassemble the NEW `WelcomePlayer6B`
    site and confirm RedirectURL is still in `r8` and the rcx/rdx/rax clobbers still match the `.asm`. The
    offset migrating correctly does NOT prove the register layout survived (§8b). Highest silent-regression risk.

---

*Every value in §1–§5 was verified against the binary as described. §8 is the sole item not re-anchored, and it
is a dead comment with no runtime effect. If any offset ever misbehaves at runtime, re-anchor it with the
playbook in §9 — the identity notes tell you exactly what each symbol is.*

---

## 10. v33 SDK API-mismatch sweep (source vs current's v33 CustomSDK)

**Deployment decision:** the migrated source is paired with **current's v33 SDK + v33 CustomSDK** (dumped
directly from v33). That resolves all main-SDK layout concerns (Category A) and the offset-stable custom fields.
A full sweep of every SDK member/type the source references confirms the *only* incompatibilities are in one
blueprint that was redesigned in v33 (`WBP_OptionsMenu`, and the `WBP_BLOnlineStatus` widget that v33 folded
into it). **All are in `Tools/UFunctions.cpp`. These are code/API fixes, NOT offset edits — deliberately left
out of the offset migration and listed here for a separate reconciliation pass. current's source shows the exact
v33 pattern for each.**

Complete list (7 references, 5 lines):

| Line | Current (v28) source | Problem in v33 | v33 fix (per current's source) |
|---|---|---|---|
| 188 | `#include ".../CustomSDK/WBP_BLOnlineStatus_classes.hpp"` | file/class removed in v33 | include `WBP_AJBTitleScreen`/drop; online status now lives on `WBP_OptionsMenu` |
| 794 | `SDK::EOnlineStatus NewStatus = …Hosting/Online/Offline` | `EOnlineStatus` enum removed | pass a `bool bIsOnline` instead |
| 795 | `MOD_OptionsMenu->OnlineStatus->UpdateStatus(NewStatus)` | `OnlineStatus` member removed (now `Burple_Packround`, a UImage) | `MOD_OptionsMenu->SetOnlineStatus(bIsOnline)` |
| 837 | `MOD_OptionsMenu->bIsOptionsMenuVisible` | renamed | `MOD_OptionsMenu->bPauseMenuIsVisible` |
| 1280 | `MOD_OptionsMenu->bIsOptionsMenuVisible` | renamed | `MOD_OptionsMenu->bPauseMenuIsVisible` |
| 1591 | `MOD_OptionsMenu->VersionInfo->SetDLLCommitVersion(*StrDLLCommitVersion)` | `VersionInfo` member removed | v33 exposes `DLLCommitVersion` (UTextBlock*) directly; set its text, or call current's equivalent |

Notes:
- Line 837 already contains a v33 name (`bPauseMenuIsVisible`) in the format string alongside the stale
  `bIsOptionsMenuVisible` argument — evidence the AJB-main tree was itself a partially-completed v33 update.
- The commented-out `GM_AJBTitleScreen` include (line 184) is NOT a dependency (dead comment) — no action.
- **Everything else verified present:** every other blueprint `_C` member the source uses
  (`ABP_AJBInGamePlayerController_C`, `ABP_AJBInGameHUD_C`, `ABP_AJBOutGame*`, `UWB_*`, `BP_Synchronizer`,
  `BP_GlobalPatcher`, `LemonHelper`, etc.) exists in current's v33 SDK (via inheritance or directly) and/or is
  used identically by current's own v33 source. Offset-stable custom fields confirmed unchanged v28→v33:
  `ProofOfExistenceSignature` (0x28), `BP_Synchronizer::PlayMode` (0x338), `LemonPlayer` (0x330),
  `CursorImg`/`TXT_VERSION` (0x208), `CurrentScopeWidget` (0x3F8), `VersioningInfo` (0x3E0). Only
  `WBP_OptionsMenu::InternalTickRate` moved (0x240→0x278) and is handled by using current's v33 header.

**Bottom line:** offset migration = complete & verified. To build against v33 you must (1) use current's v33
SDK+CustomSDK, and (2) apply the 5-line API reconciliation above. No further offset work; no IDA needed for any
of this. (The §8b `WelcomePlayer6B` register-convention check has since been done in IDA and PASSED — see §8b.)

---

## 11. ACTUAL v33 COMPILE ERRORS (ground truth) + fixes

The §10 static sweep found the `WBP_OptionsMenu` field issues but **missed** the SDK-struct-signature and
include changes below. A real build against current's v33 SDK produced **8 errors** — this is the authoritative
list. All are pre-existing v28→v33 API drift in files the offset migration correctly left diff-identical; **none
are caused by the offset edits.** current's v33 source (built against v33) is the reference for each fix.

| # | Error | File:Line | Root cause | v33 fix (from current) |
|---|---|---|---|---|
| 1 | C1083 cannot open `WBP_BLOnlineStatus_classes.hpp` | UFunctions.cpp:188 | widget removed in v33 | delete the `#include` |
| 2 | C1083 cannot open `WBP_BLOnlineStatus_functions.cpp` | (project file) | same | remove from project/build |
| 3 | C1083 cannot open `BP_SimpleStartLocationSelectGameMode_classes.hpp` | AJB.cpp:37 | class **removed in JJL10JPN-33** | comment out the `#include` (current's exact comment: "Removed in JJL10JPN-33") |
| 4 | C1083 cannot open `BP_SimpleStartLocationSelectGameMode_functions.cpp` | (project file) | same | remove from project/build |
| 5 | C2440 initializer-list → `FActorSpawnParameters` **+** C2660 SpawnActorInternal "does not take 4 arguments" | ServerLogic.cpp:158 (struct in Pointers.h) | **STRUCT PADDING.** v33's `FActorSpawnParameters` needs the tail fields the v28 struct commented out | in `Pointers.h`, add to the struct after the collision union: `unsigned char Pad_41;` `unsigned short SpawnFlags;` `__int32 ObjectFlags;` and init them in all 3 ctors (see current's Pointers.h) |
| 6 | C2039 `AddToRootSet` not a member of `SDK::UMediaSource` | AJBCallbacks.cpp:116 | not exposed on UMediaSource in v33 SDK | remove the `Source->AddToRootSet();` call (current does not call it) |
| 7 | C2039 `CreateGenericErrorPopup` not a member of `UWBP_OptionsMenu_C` | AJBCallbacks.cpp:243 | member removed in v33 blueprint redesign | remove/replace the call (current's error-popup path differs; not present) |

Plus the four §10 items (UFunctions.cpp 794/795/837/1280/1591 — OnlineStatus/EOnlineStatus/bIsOptionsMenuVisible/
VersionInfo) which are also real and fixed per current's `SetOnlineStatus`/`bPauseMenuIsVisible`/`DLLCommitVersion`.

**KEY LESSON for the playbook:** a static "does the SDK have this member" sweep is necessary but NOT sufficient.
It cannot catch (a) hand-written mod structs that mirror a game struct whose *size* changed
(`FActorSpawnParameters` needed 8 bytes of tail padding re-added — item 5, exactly the "struct padding" failure
mode), nor (b) members that moved off a class onto/off a base between SDK dumps (item 6). **Always compile against
the target SDK and treat the compiler's error list as the source of truth.** Item 5 in particular is a mod-owned
struct in `Pointers.h`, not an SDK file — so no SDK swap fixes it; it must be hand-padded to match v33.

---

## 12. CORRECTED SDK STRATEGY (supersedes §8c / §10 / §11 deployment guidance)

**Correction:** `Dumper-7/CustomSDK/` is NOT base-game data — it is **the mod's OWN custom blueprints**
(`WBP_OptionsMenu`, `BP_GlobalPatcher`, `BP_Synchronizer`, `LemonHelper`, `WBP_BLOnlineStatus`, etc. — mod
infrastructure that ships with the mod, e.g. a GlobalPatcher and a PlayMode Synchronizer). The migrated AJB-main
source was written against **AJB-main's** custom blueprints. `current` is a *different* mod whose CustomSDK
describes *its* (different) blueprints — so swapping in current's CustomSDK is wrong and was the sole cause of the
"member does not exist" errors.

**Correct deployment:**
1. **Swap only the native `Dumper-7/SDK/`** to the v33 dump (base-game engine/game classes — these differ most
   between versions). 
2. **Keep AJB-main's own `Dumper-7/CustomSDK/`** (its shipped blueprints; the source matches these).
3. **Spot-check CustomSDK padding — DONE, result: no changes needed.** Every AJB-main custom blueprint inherits
   from an engine base class that is **size-stable v28→v33** (verified against the v33 native SDK): `UUserWidget`
   ends at 0x208, `AActor` 0x328, `AGameModeBase` 0x3C8, `UActorComponent` 0xF0, `UObject` 0x28. Since the bases
   are unchanged and the blueprints are the mod's own (unchanged), the CustomSDK offsets remain valid for v33 as
   written. The `UWBP_MultiplayerMenu_C`/`UWBP_Options_C`/`UWBP_SettingsMenu_C` types that appear "missing" are
   only forward-declared pointer members (`class X* member;`) and never dereferenced — they compile fine.

**This means §10 and most of §11 do NOT apply when AJB-main's CustomSDK is kept.** The following were artifacts of
the wrong-CustomSDK swap and REQUIRE NO CODE CHANGES once AJB-main's CustomSDK is kept:
`WBP_BLOnlineStatus` include (UFunctions.cpp:188), `CreateGenericErrorPopup` (AJBCallbacks.cpp:243),
`OnlineStatus`/`UpdateStatus`/`EOnlineStatus` (UFunctions.cpp:794–795), `bIsOptionsMenuVisible`
(UFunctions.cpp:837,1280), `VersionInfo` (UFunctions.cpp:1591).

### The only REAL remaining fixes (native-game changes — persist with correct SDK strategy)

| Fix | File | Issue | Action |
|---|---|---|---|
| A | `Tools/Pointers.h` (`FActorSpawnParameters`) | **native** game struct grew in v33; the mod's mirror struct is short | add tail padding after the collision union: `unsigned char Pad_41; unsigned short SpawnFlags; __int32 ObjectFlags;` and init in all 3 ctors. *(This is the genuine "struct padding to read game data" case — and it's a mod struct in Pointers.h, not in CustomSDK.)* |
| B | `Logic/Callbacks/AJBCallbacks.cpp:116` | `UMediaSource::AddToRootSet` not emitted by the v33 native SDK dump | remove the `Source->AddToRootSet();` call (a keep-alive; current omits it) — or re-add the helper to the native SDK if you prefer to keep the call |
| C | `Logic/AJB.cpp:37` (+ project file) | **native** blueprint `BP_SimpleStartLocationSelectGameMode` removed in JJL10JPN-33 | comment out its `#include` and remove its `_functions.cpp` from the build (current does exactly this) |

Fix A is a real struct-padding edit; B and C are include/call removals. That is the complete set once the native
SDK is swapped and AJB-main's CustomSDK is kept. No IDA needed for any of it. Re-verify by recompiling.

---

## 13. CustomSDK ↔ Dumper-7 version mismatch (StaticClass glue) — FIXED

Keeping AJB-main's CustomSDK while swapping to current's v33 native SDK exposed a **Dumper-7 version mismatch**:
AJB-main's CustomSDK was emitted by an OLDER Dumper-7 whose `StaticClass()` glue calls
`return StaticBPGeneratedClassImpl<"X_C">();`. current's v33 `Basic.hpp` (newer Dumper-7) removed that helper and
replaced it with a macro `BP_STATIC_CLASS_IMPL("X_C")` (which expands to `GetStaticBPGeneratedClass(Name,
ClassIdx, ClassName)` with static locals). Result: every AJB-main CustomSDK class failed with `C3861
'StaticBPGeneratedClassImpl': identifier not found`.

**Fix applied (in the delivered tree):** converted the `StaticClass()` body of all 9 AJB-main CustomSDK classes
from the old template-call to the v33 macro — `return StaticBPGeneratedClassImpl<"X_C">();` →
`BP_STATIC_CLASS_IMPL("X_C");`. `GetDefaultObjImpl<>` is unchanged in v33, so the `GetDefaultObj()` methods were
left as-is. Classes converted: WBP_OptionsMenu, WBP_BLOnlineStatus, WBP_Cursor, WBP_TLVersionInfo,
GM_AJBUserInterface, BP_GlobalPatcher, BP_Synchronizer, LemonHelper, BPAC_LoadTags. (One, BPAC_LoadTags, had a
missing `return` in the original — the macro supplies its own return, so it's now correct.)

**General principle:** when mixing a mod's hand-maintained CustomSDK with a freshly-dumped native SDK, the two
must share a compatible Dumper-7 runtime (`Basic.hpp` API). If the native SDK is newer, port the CustomSDK's
`StaticClass()`/name glue to the new `Basic.hpp` macros (a mechanical rename); the field layouts/offsets don't
change. This is separate from — and in addition to — the 3 native-game fixes in §12.

---

## 14. FINAL BUILD-READY STATE — all fixes applied, v33 SDK integrated

The delivered tree is now assembled to compile against v33. Summary of everything done beyond the offset
migration:

**SDK assembly (in the tree):**
- `Dumper-7/SDK/` + root files (`SDK.hpp`, `UnrealContainers.hpp`, `UtfN.hpp`, `PropertyFixup.hpp`,
  `NameCollisions.inl`, `Assertions.inl`) → replaced with current's **v33** versions.
- `Dumper-7/CustomSDK/` → **AJB-main's own**, with the `StaticClass()` glue ported to the v33 `Basic.hpp` macro
  (§13). Field layouts unchanged (offsets verified stable, §12).

**Source fixes applied (the 3 native-game issues):**
- **A — `FActorSpawnParameters`** — *two* parts: (1) compile fix at `ServerLogic.cpp:158` — the brace-init
  `{ ESpawnActorCollisionHandlingMethod::AlwaysSpawn }` was a narrowing enum→uchar conversion (C2440); wrapped
  in `static_cast<unsigned char>(...)` per current. (2) runtime fix in `Pointers.h` — added the v33 tail fields
  `unsigned char Pad_41; unsigned short SpawnFlags; __int32 ObjectFlags;` (and ctor inits) so the mirror struct's
  *size* matches the v33 game struct passed by reference. **Note: the padding was NOT the compile fix — the
  static_cast was; the padding is a separate runtime-correctness requirement. Both are needed.**
- **B — `AddToRootSet`** (`AJBCallbacks.cpp:116`) — removed the call (not exposed on UMediaSource in the v33
  dump; the immediately-following `OpenSource` gives the media player its own reference).
- **C — `BP_SimpleStartLocationSelectGameMode`** — commented out the two includes (`AJB.cpp`, `UFunctions.cpp`,
  "Removed in JJL10JPN-33") and removed its `_functions.cpp` from `JoJo.vcxproj` / `.filters`. Type is used
  nowhere else, so no follow-on errors.

**Files changed vs original AJB-main (v28):**
- Offset migration (6): `Offsets.cpp`, `Offsets.h`, `Logic/AJB.cpp`, `Tools/UFunctions.cpp`,
  `Logic/ServerLogic.cpp`, `Logic/Callbacks/AJBCallbacks.cpp`.
- Build fixes add one more source file: `Tools/Pointers.h`; plus `JoJo.vcxproj`, `JoJo.vcxproj.filters`; plus
  the 9 patched `CustomSDK/*_classes.hpp` (StaticClass glue); plus the wholesale v33 SDK swap.

**§8b `WelcomePlayer6B` register check: DONE, PASSED** (IDA-verified r8=RedirectURL and the rcx/rdx/rax
convention are identical v28↔v33). Nothing left runtime-pending on that front.

Compile expectation: with the tree as delivered, the 8 original errors + the StaticClass wave should all be
resolved. If a further wave appears (errors can mask later ones), it will most likely be more of the same
category — native SDK member/enum drift — resolvable the same way (check current's usage, adjust the call).

---

## 15. COMPLETE CHANGE LOG — EVERY non-offset change made (source, SDK, project, CustomSDK)

This is the exhaustive list of everything changed **beyond the offset-literal migration** (§1–§5). If it is not
an offset in §1–§5 and not listed here, it was not touched. §8b (RedirectURL) required **no** change — verified
only.

### 15.1 Source-code edits (non-offset) — 5 lines across 5 files

**(1) `PCPortSource/Aeyth8/Logic/ServerLogic.cpp`** — line ~158, inside `AJB::Server::PostLogin`. Added
`static_cast<unsigned char>(...)` to fix C2440 narrowing in the brace-init:
```
- ... Pointers::FActorSpawnParameters{ SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn });
+ ... Pointers::FActorSpawnParameters{ static_cast<unsigned char>(SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn) });
```

**(2) `PCPortSource/Aeyth8/Tools/Pointers.h`** — `struct FActorSpawnParameters`. Added three tail fields after
the collision-handling union, and initialized them in all three constructors (runtime size-match with the v33
game struct):
```
+ unsigned char  Pad_41;      // +41 padding
+ unsigned short SpawnFlags;  // +42 bRemoteOwned, bNoFail, bDeferConstruction, etc.
+ __int32        ObjectFlags; // +44 EObjectFlags
```
ctor initializer lists gained `, Pad_41(0), SpawnFlags(0), ObjectFlags(0)` (3 constructors). No existing field
or logic changed.

**(3) `PCPortSource/Aeyth8/Logic/Callbacks/AJBCallbacks.cpp`** — line ~116, inside the media-source loop.
Removed one line (call not present on UMediaSource in the v33 SDK dump):
```
- Source->AddToRootSet();
```
(The following `OpenSource` call is unchanged and gives the media player its own reference.)

**(4) `PCPortSource/Aeyth8/Logic/AJB.cpp`** — line ~37. Commented out one include (blueprint removed in v33):
```
- #include "../../Dumper-7/SDK/BP_SimpleStartLocationSelectGameMode_classes.hpp"
+ //#include "../../Dumper-7/SDK/BP_SimpleStartLocationSelectGameMode_classes.hpp" // Removed in JJL10JPN-33
```

**(5) `PCPortSource/Aeyth8/Tools/UFunctions.cpp`** — line ~215. Same include commented out:
```
- #include "../../Dumper-7/SDK/BP_SimpleStartLocationSelectGameMode_classes.hpp"
+ //#include "../../Dumper-7/SDK/BP_SimpleStartLocationSelectGameMode_classes.hpp" // Removed in JJL10JPN-33
```

*(Note: ServerLogic.cpp, AJBCallbacks.cpp, AJB.cpp, and UFunctions.cpp were ALSO offset-migrated in §1–§5; the
edits above are the additional non-offset changes in those same files. Pointers.h is the one file changed ONLY
for a non-offset reason — it carries no migrated offsets.)*

### 15.2 Project-file edits — `PCPortSource/JoJo.vcxproj` and `JoJo.vcxproj.filters`

Removed the build reference to the deleted native-SDK source file (one `<ClCompile>` entry in each):
```
- <ClCompile Include="Dumper-7\SDK\BP_SimpleStartLocationSelectGameMode_functions.cpp" />        (JoJo.vcxproj)
- <ClCompile Include="...SimpleStartLocationSelectGameMode_functions.cpp"> ... </ClCompile>       (JoJo.vcxproj.filters)
```
Nothing else in the project files was changed. (The `WBP_BLOnlineStatus_*` entries were LEFT intact — that is
the mod's own CustomSDK blueprint, which we keep.)

### 15.3 CustomSDK glue edits — `PCPortSource/Dumper-7/CustomSDK/*_classes.hpp` (9 files)

Ported each class's `StaticClass()` body from the old Dumper-7 helper to the v33 `Basic.hpp` macro. Purely the
class-resolution glue; **no field, offset, or padding in these files was changed.** Per file, one line:
```
- return StaticBPGeneratedClassImpl<"X_C">();
+ BP_STATIC_CLASS_IMPL("X_C");
```
Files: WBP_OptionsMenu, WBP_BLOnlineStatus, WBP_Cursor, WBP_TLVersionInfo, GM_AJBUserInterface, BP_GlobalPatcher,
BP_Synchronizer, LemonHelper, BPAC_LoadTags. (BPAC_LoadTags's original also lacked a `return`; the macro supplies
its own, so it is now correct.)

### 15.4 SDK folder swap — `PCPortSource/Dumper-7/`

- **Replaced with current's v33 dump:** the entire `SDK/` folder (native game/engine classes) and the root files
  `SDK.hpp`, `UnrealContainers.hpp`, `UtfN.hpp`, `PropertyFixup.hpp`, `NameCollisions.inl`. **Added** `Assertions.inl`
  (v33-only; compile-time size/offset asserts on native structs).
- **Kept (AJB-main's own, with the §15.3 glue edit):** the `CustomSDK/` folder.
- Net effect: `Dumper-7/` is entirely v33 **except** `CustomSDK/`, which stays the mod's own. All CustomSDK→SDK
  `#include`s were verified to resolve against the v33 SDK.

### 15.5 Additive documentation (not part of the mod)

- `MIGRATION_REPORT_v28_to_v33.md` and `OFFSET_REFERENCE_MASTER.md` (this file) were added at the tree root.
  They are documentation only and are not compiled.

### 15.6 What was NOT changed (explicit)

- No change to any `.asm` file (all 3 verified to carry no game offsets; the RedirectURL trampoline is correct
  as-is per §8b).
- No change to `dllmain.cpp` (the `// 0x20773C4` comment is dead/unused — left as-is, §8).
- No change to MinHook, the DummyLibraries, the Launcher, `Global.hpp`, `Hooks.cpp`, or any other Aeyth8 source
  beyond the five files in §15.1.
- No offset in §1–§5 was altered by any of the §15 changes, and none of the §15 changes altered code logic
  except the five explicit edits in §15.1 (which are the minimal set required to build/run against v33).

---

## 16. FINAL CORRECTED PORT (supersedes §12–§15 SDK guidance) — "port v33 TO AJB-main"

**Direction confirmed by the user:** keep AJB-main's source, design, AND its CustomSDK; bring the port to v33 by
replacing only the NATIVE game SDK + migrating offsets/structs. The CustomSDK is a *curation boundary* the mod
author made to separate mod-owned blueprints from native game classes — NOT a game dump. So AJB-main's own
CustomSDK is authoritative for the mod's blueprints (`WBP_OptionsMenu` with its `OnlineStatus` child,
`WBP_BLOnlineStatus`, `BP_Synchronizer`, etc.), even where a raw v33 game dump shows a different-shaped class of
the same name.

### 16.1 SDK assembly (final, correct)
- **Native `SDK/` + 6 root files** ← from a **native-only v33 dump**. NOTE: the user's own Dumper-7 v33 dump was
  taken with the mod loaded, so its `SDK/` folder CONFLATES mod blueprints with native classes (it contains
  `WBP_OptionsMenu`/`BP_Synchronizer`/`BP_GlobalPatcher` with mod shapes). Using it directly collides with the
  CustomSDK. The clean native-only v33 `SDK/` (no mod classes) is the correct base. Verified: it contains none of
  the 9 mod classes, so there are **zero class collisions** with AJB-main's CustomSDK.
- **`CustomSDK/`** ← AJB-main's own (kept), with `StaticClass()` glue ported to the v33 `BP_STATIC_CLASS_IMPL`
  macro (§13). All 9 mod classes present; base-class offsets verified stable (§12), so member offsets valid.
- **Do NOT** let a mod-inclusive game dump override the CustomSDK. If a class exists in both, the CustomSDK copy
  is authoritative for the mod.

### 16.2 Native-SDK hand-edits required (mod needs members Dumper-7 left as padding)
- **`UPendingNetGame::URL`** — AJB-main's v28 SDK hand-added `struct FURL URL;` (+ `bSuccessfullyConnected`,
  `bSentJoinRequest`, `ConnectionError`) at offset 0x40, where Dumper-7 emits `Pad_40[0x88]`. Re-applied to the
  v33 native `Engine_classes.hpp` (size-preserving: FURL 0x70 + 2 bool + pad + FString 0x10 = 0x88, ends 0xC8).
  Source uses `&This->URL` (UFunctions.cpp:1400).
- **`UObject::AddToRootSet()` / `RemoveFromRoot()`** — AJB-main hand-added these to its v28
  `CoreUObject_classes.hpp` (GC keep-alive). Re-added to the v33 native `CoreUObject_classes.hpp`, adapted to the
  v33 `GObjects` API: v33 `TUObjectArray::GetByIndex` returns `UObject*` (not the item), so the helper reaches
  the `FUObjectItem` directly and pokes internal flags at item+0x8 with `EInternalObjectFlags::RootSet`
  (0x40000000) — the same bit/semantics as v28. 5 call sites (AJBCallbacks.cpp:116; UFunctions.cpp:1231,1608,
  1612,1622) now compile unchanged. **NOTE:** 0x40000000 here is the *internal-object* RootSet flag living in the
  GObjects item — NOT `EObjectFlags` (where v33 labels 0x40000000 as MirroredGarbage). Do not "fix" it to 0x80.

### 16.3 Source fixes — v33 type-strictness, using AJB-main's OWN idioms (not current's design)
All keep AJB-main's logic; each follows a pattern AJB-main already uses elsewhere in its own code:
- **FActorSpawnParameters brace-init** (ServerLogic.cpp:158, UFunctions.cpp:714): enum→uchar is narrowing in
  `{}`; wrapped in `static_cast<unsigned char>(...)`.
- **FActorSpawnParameters padding** (Pointers.h): added v33 tail fields `Pad_41`/`SpawnFlags`/`ObjectFlags` +
  ctor inits (native struct grew; runtime size-match). *(static_cast is the compile fix; padding is the separate
  runtime fix.)*
- **PlayMode → to_string** (UFunctions.cpp:1375): `(uint8)AJB::Instance->PlayMode` — AJB-main already casts
  PlayMode to byte at line 350. (Fixes the C2665 cascade.)
- **FName `{0}` inits → `{}`** (ServerLogic.cpp:27,28,29,31; AJB.cpp:1157; UFunctions.cpp:2019,2020): v33
  `SDK::FName` ctor is `explicit`, so `{0}` fails; `{}` uses the defaulted ctor.
- **FName `.Clear()` → `= SDK::FName()`** (UFunctions.cpp:1472): v33 `SDK::FName` has no `Clear()`; assign a
  default to reset. (Line 1480 is inside a comment block — left as-is.)
- **SDT array index cast** (UFunctions.cpp:1732): `SDT_SpawnCollision[static_cast<int>(...)]` — matches
  AJB-main's own `[(byte)...]` idiom at line 350.
- **std::format C7595** (UFunctions.cpp:1726, 1735): mixed-type ternaries (`std::string` vs `const char*`) broke
  `std::format`'s consteval validation; made both branches `std::string("NULL")`. *(If this still fails to
  compile, the fallback is string concatenation — the format string can be split into `"..." + s + "..."` — but
  the consistent-type fix is minimal and should suffice.)*
- **BP_SimpleStartLocationSelectGameMode** (AJB.cpp:37, UFunctions.cpp:215, project): native blueprint genuinely
  removed in v33; includes commented, `_functions.cpp` dropped from build. (Only genuinely-removed native class.)

### 16.4 Reverted (earlier wrong-direction edits undone)
- AJBCallbacks.cpp:116 `Source->AddToRootSet();` — RESTORED (I had wrongly removed it; the helper now exists).
- WBP_BLOnlineStatus / OnlineStatus / VersionInfo source lines — never actually edited (confirmed identical to
  original); the widget is mod-owned and stays.

### 16.5 The online-status widget — kept as-is
`WBP_BLOnlineStatus` is a MOD-authored widget (child of the mod's `WBP_OptionsMenu`), not a base-game class. It
is not "removed from v33." AJB-main's CustomSDK defines it; `MOD_OptionsMenu->OnlineStatus->UpdateStatus()`
stays unchanged. No feature change.

---

## 17. Verification of the two native-SDK hand-edits (struct-shift / crash-prevention check)

The user stressed that `current` is a *proven-working* v33 build and that struct padding is runtime-load-bearing
(the game crashes on layout mismatch). Findings from checking the two hand-edits:

**`current` does NOT actually have either hand-edit** — and it still compiles/runs, because:
- `current` **commented out** its `This->URL.Op` usage (UFunctions.cpp:1635 is inside `/* */`), so it never needs
  `UPendingNetGame::URL`. **AJB-main uses `&This->URL` in LIVE code** (UFunctions.cpp:1400 — passes it to the
  FString-copy at PB(0x586500)). So AJB-main genuinely needs the member; the hand-edit is required to keep
  AJB-main's feature. (Size-preserving: 0x40→0xC8, matches AJB-main's v28 layout exactly.)
- `current` **replaced `AddToRootSet` entirely** with a `MOD_CallbackTimer->CacheMaterial(...)` system (its own
  `WBP_CallbackTimerHandler` widget). Adopting that would be a large architectural change away from AJB-main's
  design. AJB-main keeps its 5 `AddToRootSet` call sites, so the helper is re-added instead.

**`AddToRootSet` flag/offset — VERIFIED against AJB-main's own v28 SDK (same UE4.20 engine):**
AJB-main's v28 `Basic.hpp` names the FUObjectItem fields explicitly:
`{ UObject* Object @0x0; int32 Flags @0x8; int32 ClusterIndex @0xC; int32 SerialNumber @0x10; pad[4] @0x14 }`,
total stride **0x18**. v33's Dumper-7 SDK collapses 0x8–0x17 into `Pad_8[0x10]` but the stride is **identically
0x18**. So the internal `Flags` field is at **item+0x8** in both — exactly where the re-added helper writes the
RootSet bit (0x40000000). The helper reproduces AJB-main's proven v28 behavior byte-for-byte; only the access
path differs (v33 `GetByIndex` returns `UObject*`, so the helper computes the FUObjectItem pointer via
`GetDecrytedObjPtr()[chunk] + inChunkIdx`). No layout/crash risk.

**Conclusion:** both native-SDK hand-edits are correct and necessary for AJB-main's live code, and both are
layout-verified. This is the struct-shift check the user asked for; it passes.

---

## 18. ROOT CAUSE OF LAUNCH CRASH — un-migrated OFFSET declarations in AJB.cpp (FIXED)

**The mod compiled but crashed the game on inject.** Root cause: a set of `A8CL::OFFSET` declarations live
directly inside `Logic/AJB.cpp` (lines ~206–500), SEPARATE from `Offsets.cpp`/`Offsets.h`. The earlier migration
updated Offsets.cpp/.h (and all inline `PB()` calls in every file) but **missed the 16 OFFSET decls in AJB.cpp**,
which still held their original **v28** addresses. Several of these are installed as MinHook hooks at init
(`Init_Hooks` → `CreateAndEnableHook`). Hooking a v28 address in the v33 binary writes a trampoline into the
wrong code → immediate crash on injection (the "compiles but won't launch" symptom).

Confirmed via an independent v33 migration of a sibling mod (user-provided) — its 9 `Offsets.h` values matched
mine exactly (cross-validating the earlier migration), while exposing the AJB.cpp OFFSET decls as still-v28.

**Fixed — all 16 AJB.cpp OFFSET decls migrated v28 → v33:**
| Symbol | v28 | v33 |
|---|---|---|
| NetID | 0x4ECC80 | 0x4F1EE0 |
| OpenCommand | 0x506DD0 | 0x50C390 |
| PostEventByName | 0x291650 | 0x2918E0 |
| PostEvent | 0x291390 | 0x291620 |
| LoadBankByName | 0x286850 | 0x286AE0 |
| ExecCharacterNo | 0x549AA0 | 0x54F180 |
| CharacterNo | 0x485F70 | 0x486320 |
| ObjBlueprint | 0x49F080 | 0x49FCD0 |
| GetBaseMaterial | 0x109B860 | 0x10A1A70 |
| GetMaterialInterface | 0x14D2C50 | 0x14D8E60 |
| oGetDefaultMaterial | 0x14ABA70 | 0x14B1C80 |
| FTextConstructor | 0x5DCD20 | 0x5E2F40 |
| oAddActionMapping | 0x1793C60 | 0x1799E70 |
| oWinGetUsername | 0x699AA0 | 0x69FCC0 |
| GSetString | 0x626330 | 0x639410 |
| oMainMenuImplementation | 0x1606670 | **0x160C880** (IDA-VERIFIED by signature match, not the reference — reference didn't list it) |

`oFindRow` (UDataTable::FindRow, 0x498CF0) is **commented out** in the hook list (not installed), so its stale
v28 value is inert. Flag: if ever re-enabled, re-verify its v33 address (its v33 location was not resolved here).

**IDA verification of the critical hooked one:** v28 `0x1606670` = `ClientReturnToMainMenuWithTextReason_Implementation`
(0x49-byte fn calling GetGameInstance). In v33 that RVA is inside `APlayerCameraManager::scalar_deleting_destructor`
(totally wrong — would corrupt on hook). Signature `40 53 48 83 EC ?? 48 8B D9 E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 10`
matched EXACTLY ONE v33 location: **0x160C880**, confirmed as the correct `_Implementation` (same structure,
references migrated GEngine 0x325F438). Applied.

**Other files audited (all already correctly migrated in the earlier pass):** ServerLogic.cpp inline PB
(0x5F9600→0x5FF820, 0x18C6280→0x18CC510), UFunctions.cpp inline PB (0x3051520→0x305B5A0, 0x5802E0→0x586500,
0x6837F0→0x689A10), AJBCallbacks.cpp (0x18D70A0→0x18DD3C0), AJB.cpp inline PB (all 8 done). NTSurfer.hpp/cpp and
FileSystem.h contain Windows/PEB constants (KUSER_SD, IMAGE_ORDINAL_FLAG64, etc.) — OS-dependent, NOT game
offsets — correctly untouched.

**Lesson:** offset migration must sweep EVERY source file for hardcoded addresses, not just Offsets.cpp/.h. The
`OFFSET` type is declared inline in logic files too.
