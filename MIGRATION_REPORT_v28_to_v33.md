# AJB-main Offset Migration Report — v28 → v33 (JJL10JPN-33)

**Scope:** Faithful mechanical migration of numeric offset literals only. Every code line, comment, type
signature, and whitespace is byte-identical to the original; **only numeric offset literals, byte-patch
targets, and struct field offsets changed.** VFT vtable indices were evaluated and left unchanged (see below).

**Target binary:** `JJL10JPN-33` / `AJB-Win64-Shipping.exe` (v33), imagebase-relative (RVA) offsets.
**Source baseline:** AJB-main written against game binary **v28**.

**Verification method:** Every offset was independently verified against the v33 binary — never by delta or
pattern alone. For each symbol: decompile AJB-main's v28 address → establish the true function identity →
locate the same function in v33 via unique string, AOB signature (wildcarding only volatile RIP-relative
displacements and rel-call targets), xref, vtable slot, or struct-type anchor → confirm structural identity
→ migrate. This caught several traps a naive delta would have missed (see "Findings requiring sign-off").

**Files changed (6 of 3490):**
- `PCPortSource/Aeyth8/Offsets.cpp` — 71 named offsets + informational comment references
- `PCPortSource/Aeyth8/Offsets.h` — 11 byte-patch constants (VFT indices unchanged)
- `PCPortSource/Aeyth8/Logic/AJB.cpp` — inline `PB()` offsets
- `PCPortSource/Aeyth8/Tools/UFunctions.cpp` — inline `PB()` offsets
- `PCPortSource/Aeyth8/Logic/ServerLogic.cpp` — inline `PB()` offsets
- `PCPortSource/Aeyth8/Logic/Callbacks/AJBCallbacks.cpp` — inline `PB()` offset + 2 UMG struct field offsets

---

## FINDINGS — RESOLUTION STATUS

All six flags are now resolved. Five were confirmed correct as migrated; one — `VFT_FindWidgetOfClass` —
turned out to have **shifted** and was corrected (`0xFE → 0x100`). Details per flag below.

### RESOLVED (a) — `NotifyControlMessage` targets `UPendingNetGame` (proven by source type)
AJB-main's v28 address points at `UPendingNetGame::NotifyControlMessage`; migrated to **`0x15C41F0`** (the
v33 UPendingNetGame overload, byte-identical to v28). This was flagged because `current` uses `0x17CCCC0`
(the **`UWorld`** overload — a different function). **The mod's own source settles it decisively:** the hook
is `void UFunctions::NotifyControlMessage(SDK::UPendingNetGame* This, ...)`, the `Decl` typedef
(`UFunctions.hpp:198`) is `__thiscall(SDK::UPendingNetGame*, ...)`, the log string is literally
`"[UPendingNetGame]: ..."`, and the body reads `This->URL` (a UPendingNetGame member). AJB-main categorically
hooks the UPendingNetGame overload; `current`'s value would be **wrong** for this mod. No sign-off needed.
The dependent expression `WelcomedByServer = NotifyControlMessage + 0xB8F` derives from the correct base.

The other 6 in-pack divergences from `current` are faithful-migration decisions (thunks / override wrappers /
real-workers vs. `current`'s slightly different target), each decompile-verified against what AJB-main's v28
address actually points at: `ClientTravelInternal 0x18CC260`, `HandleStartingNewPlayer 0x18326F0`,
`DestroyActor 0x148A3A0` (anchored on "UWorld::DestroyActor: World has no context!"), `Logout 0x13D8EE0`,
`StartLoadingDestination 0x17D52B0`, `ClientTeamMessage 0x18CC190`.

### RESOLVED (b) — `VFT_GetMaxFPS` is `0x51` (reference doc's `0xB9` disproven)
Direct vtable reads in **both** binaries: v33 UGameEngine slot[0x51] @ 0x25C1C20 -> 0x1770430 = GetMaxFPS,
byte-identical. In use (`UFunctions.cpp:800`). **Kept `0x51`.**

### RESOLVED (c) — `GSetString` naming quirk (informational)
Source label says "SetString"; the function is actually `FConfigCacheIni::GetString` (v33 `0x62C550`).
Migrated by true identity; source name unchanged (faithful). Non-issue.

### RESOLVED (f) — UMG widget offsets independently verified against v33 SDK reflection
Previously rested on `current`; now confirmed against the v33 Dumper-7 SDK reflection dump (independent of
`current`'s hand-tuned table). The offset chain is: `OffsetToClass` = the `WB_ModeSelect_Button_*` member
offset inside `UWB_ModeSelect_C`; `OffsetToRetainerBox` = the `WB_ModeSelect_Txt_*` member offset inside that
button subclass (non-subwidget rows) or the `RetainerBox_N` offset (subwidget rows). **All 10 rows, both
columns, match the SDK exactly.** The two changed values are correct:
- Button_PAIR RetainerBox **0x0378** = `UWB_ModeSelect_Button_PAIR_C::WB_ModeSelect_Txt_PAIR @ 0x0378`
- Button_SOLO RetainerBox **0x0378** = `UWB_ModeSelect_Button_SOLO_C::WB_ModeSelect_Txt_SOLO @ 0x0378`

Root cause of the +0x8 shift is now understood: v33 inserted one extra `UAJBTextBlock*` member before the Txt
widget in each of those two button blueprints (PAIR gained `Txt_Notice @ 0x0370`; SOLO gained
`Txt_InactivePreMes @ 0x0370`), pushing the Txt member from 0x0370 to 0x0378. All 8 `OffsetToClass` values
and the other 6 `OffsetToRetainerBox` values were verified unchanged.

### RESOLVED (d) — APlayerController VFT indices confirmed UNCHANGED by direct slot reads (both binaries)
Both indices were verified by locating the APlayerController vtable via its constructor (which writes
`*(this) = vtable`) and reading the slots directly in **both** binaries:
- v28 vtable base `0x2645240` (ctor `0x15FC300`); v33 vtable base `0x264DFE0` (ctor `0x1602510`). The two
  constructors are byte-identical in layout — strong class-stability evidence.
- **`VFT_LocalTravel 0x13F`**: v28 slot → `0x1617B50`, v33 slot → `0x161DD60` — both IDA-named
  `APlayerController::LocalTravel`. Unchanged. (commented out at `ServerLogic.cpp:240`.)
- **`VFT_ClientMainMenu 0x141`**: v28 slot → `0x18C5C90`, v33 slot → `0x18CBF20` — both are the
  `ClientReturnToMainMenuWithTextReason` client-RPC thunk (byte-identical: FText copy → `FindFunctionChecked`
  → ProcessEvent via `vtable+520`, single `const FText&` arg). Unchanged. (in active use at `ServerLogic.cpp:237`.)

### RESOLVED (e) — `VFT_FindWidgetOfClass` CHANGED `0xFE → 0x100` (value corrected in `Offsets.h`)
**This flag was not just a verification — it required a real fix.** `FindAJBWidgetOfClass` is a reflected
UFUNCTION on the game class `AAJBHUDBase` (unsymbolized, RTTI stripped), so the vtable couldn't be read from a
constructor. Instead the **engine's own generated exec-thunk** was used as ground truth: `execFindAJBWidgetOfClass`
steps the two parameters (`UClass*`, `UAJBUserWidget**`) off the VM frame and then dispatches to the real native
implementation via `(*(this) + N)(...)`, where `N = vtable_index × 8`. Both exec thunks were located via each
UClass's `{FName, function}` map:
- **v28** exec `0x54D1C0` → dispatches through `vtable + 2032` ⇒ `2032/8 = 254 = 0xFE` (matches the source).
- **v33** exec `0x5528A0` → dispatches through `vtable + 2048` ⇒ `2048/8 = 256 = 0x100` (**shifted +2**).

Both thunks step an identical two-parameter shape and are the sole `FindAJBWidgetOfClass` handler in their
respective function maps, so the identity is certain. `AAJBHUDBase` gained two virtual slots before this function
in v33. The hook is **in active use** at `AJB.cpp:1237` (full-map toggle), so keeping `0xFE` would have called
the wrong vtable entry. **`Offsets.h` updated: `VFT_FindWidgetOfClass 0xFE → 0x100`.** (Engine-class VFT indices
are unaffected — this is a game-class layout change; the other seven were each directly slot-read and confirmed.)

### Blocked-item cause (now cleared)
The IDA MCP bridge briefly went unresponsive during the second pass, which deferred (d)/(e); once it recovered,
both were resolved as above. All six flags are now closed.

---

## 1. Named offsets — `Offsets.cpp` (71)

| Symbol | v28 | v33 |
|---|---|---|
| Tick | 0x13E1E30 | 0x13E8040 |
| GEngine (DATA) | 0x32553B8 | 0x325F438 |
| GWorld (DATA) | 0x3257AF0 | 0x3261B70 |
| FMalloc | 0x5C75B0 | 0x5CD7D0 |
| FRealloc | 0x5C9790 | 0x5CF9B0 |
| FFree | 0x5BC5E0 | 0x5C2800 |
| FNameW | 0x681ED0 | 0x6880F0 |
| FNameA | 0x681E50 | 0x688070 |
| FNameTS | 0x692ED0 | 0x6990F0 |
| Logf *(commented)* | 0x64D570 | 0x653790 |
| OutputText | 0x17A8BF0 | 0x17AEE00 |
| ProcessEvent | 0x823B30 | 0x829D50 |
| Invoke | 0x70DBF0 | 0x713E10 |
| AppPreExit | 0x1E3380 | 0x1E3610 |
| SetClientTravel | 0x1781B80 | 0x1787D90 |
| ClientTravelInternal | 0x18C5FD0 | 0x18CC260 |
| StartLoadingDestination | 0x17CF0A0 | 0x17D52B0 |
| PreLogin | 0x13D7710 | 0x13DD920 |
| AJBPreLogin | 0x04A4060 | 0x4A4CB0 |
| Login | 0x13D2A20 | 0x13D8C30 |
| PostLogin | 0x13D6A70 | 0x13DCC80 |
| Logout | 0x13D2CD0 | 0x13D8EE0 |
| BeginPlay | 0x17BAEC0 | 0x17C10D0 |
| HandleStartingNewPlayer | 0x182C450 | 0x18326F0 |
| InitListen | 0x3FBE40 | 0x3FC0D0 |
| InitConnection *(commented)* | 0x14FBAF0 | 0x1501D00 |
| InitLocalConnection | 0x3FBFB0 | 0x3FC240 |
| NotifyControlMessage | 0x15BDFE0 | 0x15C41F0 |
| PeekNetworkFailureMessages | 0x140CB20 | 0x1412D30 |
| AddClientConnection | 0x14EEDF0 | 0x14F5000 |
| HandleClientPlayer | 0x14FB010 | 0x1501220 |
| Close | 0x14F30C0 | 0x14F92D0 |
| UConsole | 0x1796230 | 0x179C440 |
| ConsoleCommand | 0x16077D0 | 0x160D9E0 |
| Browse | 0x175C530 | 0x1762740 |
| IsTimeLimitedExceeded | 0x17C4BB0 | 0x17CADC0 |
| AddToWorld | 0x17BA220 | 0x17C0430 |
| RemoveFromWorld | 0x17CAEA0 | 0x17D10B0 |
| SpawnActor | 0x1494440 | 0x149A650 |
| DestroyActor | 0x1484190 | 0x148A3A0 |
| ProcessMulticastDelegate | 0x20C010 | 0x20C2A0 |
| ClientTeamMessage | 0x18C5F00 | 0x18CC190 |
| ClientTeamMessageImplementation | 0x1606D20 | 0x160CF30 |
| ActorDestroy | 0x11AC5E0 | 0x11B27F0 |
| CopyString | 0x1E0EE0 | 0x1E1170 |
| IsNonPakFileNameAllowed | 0x191F540 | 0x1925860 |
| FindFileInPakFiles | 0x191C430 | 0x1922750 |
| StaticLoadClass | 0x84B1A0 | 0x8513C0 |
| StaticFindObject | 0x84A3D0 | 0x8505F0 |
| StaticLoadObject | 0x84B620 | 0x851840 |
| CreateDefaultObject | 0x707A60 | 0x70DC80 |
| StaticConstructObject | 0x849630 | 0x84F850 |
| BroadcastDelegate | 0x1E3620 | 0x1E38B0 |
| ALevelScriptActorConstructor | 0x147E150 | 0x1484360 |
| ToFormattedString | 0x60F080 | 0x6152A0 |
| SetInputGameOnly | 0x10C41D0 | 0x10CA3E0 |
| SetInputMode_GameAndUIEx | 0x10C4060 | 0x10CA270 |
| ImageSetBrushFromMaterial | 0x10C1D10 | 0x10C7F20 |
| BorderSetBrushFromMaterial | 0x10C1C10 | 0x10C7E20 |
| MediaPlayer | 0x19813D0 | 0x19876F0 |
| OpenSource | 0x19877F0 | 0x198DB10 |
| PostEventAtLocation | 0x292F30 | 0x2931C0 |
| ChangeState | 0x21CF10 | 0x21D1A0 |
| TryGetMatchingMyPairInfo | 0x486FC0 | 0x487370 |
| TryGetMatchingPlayerInfo | 0x486A70 | 0x486E20 |
| GetUsername | 0x694650 | 0x69A870 |
| GetNationalMatchSchedule | 0x47E410 | 0x47E770 |
| AJBWindowWidget | 0x534690 | 0x539C90 |
| IsTenpoHost | 0x507210 | 0x50C7D0 |
| IsAJBOfflineMode | 0x49DE20 | 0x49EA70 |
| IsOfflineMode | 0x4ED5D0 | 0x4F2830 |

**Expression offsets (auto-derive from bases above; no literal edited, code unchanged):**
`WorldWelcomePlayer = WelcomePlayer6B − 0x113`; `WelcomePlayerStripped = WelcomePlayer6B − 0x5`;
`WelcomedByServer = NotifyControlMessage + 0xB8F`.

**Informational comment references also updated** (Offsets.cpp comment block): DestroyActor, AActor::Destroy,
FString::FString, FMemory::Malloc/Realloc/Free, and `FMemory::QuantizeSize 0x5C91A0 → 0x5CF3C0`.

## 2. Byte-patch constants — `Offsets.h` (11)

| Constant | v28 | v33 |
|---|---|---|
| HideCursorCaller | 0x04A04A0 | 0x04A10F0 |
| AJBGetMaxTickRate | 0x13CCB43 | 0x13D2D53 |
| AJBGetMaxTickRateCap | 0x13CCBD8 | 0x13D2DE8 |
| ResetPP | 0x04840B0 | 0x0484420 |
| StartConsumePP | 0x0522CE0 | 0x05282A0 |
| LogVerbosity (DATA) | 0x300D3C8 | 0x3017348 |
| NetDriverGetNetMode | 0x14F90F0 | 0x14FF300 |
| WorldInternalGetNetMode | 0x17C4820 | 0x17CAA30 |
| ActorInternalGetNetMode | 0x11BA5C0 | 0x11C07D0 |
| WelcomePlayer6B | 0x17D2EF3 | 0x17D9103 |
| FControlChannelOutBunch | 0x1357E90 | 0x135E0A0 |

## 3. VFT vtable indices — `Offsets.h` (8) — 7 UNCHANGED, 1 CHANGED

All 8 were slot-read/verified in **both** binaries. **Seven are unchanged** (all on engine classes with stable
layouts): `VFT_GameEngineTick 0x4F`, `VFT_GetMaxFPS 0x51` (doc's 0xB9 disproven — see (b)),
`VFT_HandleClientPlayer 0x55`, `VFT_GetMaterial 0x49`, `VFT_NetworkFailureMsg 0x62`, `VFT_ClientMainMenu 0x141`,
and `VFT_LocalTravel 0x13F` (the last two confirmed by direct APlayerController vtable slot reads — see (d)).

**One CHANGED:** `VFT_FindWidgetOfClass` **0xFE → 0x100** — the `AAJBHUDBase` game class gained two virtual
slots before `FindAJBWidgetOfClass` in v33, proven via the engine's `execFindAJBWidgetOfClass` dispatch offset
(`vtable+2032`=0xFE in v28 vs `vtable+2048`=0x100 in v33). See (e). This is the only VFT index requiring an edit.

## 4. Inline `PB()` offsets — 4 .cpp files (22 distinct, 27 occurrences)

**AJB.cpp:** 0x17A80B0→0x17AE2C0, 0x1908090→0x190E3B0 (UUserInterfaceSettings::GetPrivateStaticClass),
0x8066C0→0x80C8E0, 0x620550→0x626770 (FConfigCacheIni::Flush), 0x2233A0→0x223630 (NBAM read),
0x223610→0x2238A0 (FDrive; "f:\\%s\\*"), 0x20E680→0x20E910 / 0x20E8D0→0x20EB60 / 0x20EA10→0x20ECA0 /
0x20FB00→0x20FD90 (4 AMActivator wrappers), 0x522530→0x527AF0, 0x47C510→0x47C7A0 (ClearMatchingID),
0x6246F0→0x62A910, 0x6213C0→0x6275E0, 0x6331F0→0x639410 (FConfigCacheIni::SetString),
0x3051380→0x305B400 (GEngineIni FString global, ×3).
**UFunctions.cpp:** 0x3051520→0x305B5A0 (LogNetSerialization byte global), 0x5802E0→0x586500
(FArchive::operator<<, ×3), 0x6837F0→0x689A10 (FName::AppendString).
**ServerLogic.cpp:** 0x5F9600→0x5FF820 (FText::FromString, ×2), 0x18C6280→0x18CC510 (KickPlayer helper).
**AJBCallbacks.cpp:** 0x18D70A0→0x18DD3C0 (UPrimitiveComponent::execSetMaterial, comment-only).

## 5. Struct field offsets — `AJBCallbacks.cpp` `WidgetsToTranslate[]`

Only 2 values changed (both `OffsetToRetainerBox`, +0x8); all `OffsetToClass` and other `OffsetToRetainerBox`
values unchanged. See finding (f) for the verification caveat.

| Widget | Field | v28 | v33 |
|---|---|---|---|
| WB_ModeSelect_Button_PAIR | OffsetToRetainerBox | 0x0370 | 0x0378 |
| WB_ModeSelect_Button_SOLO | OffsetToRetainerBox | 0x0370 | 0x0378 |

`NTSurfer.hpp` PEB/TEB structures were intentionally **not** touched — they are OS/PE-version dependent, not
game-binary dependent.
