# Changelog

Every entry lists what changed, why it changed, and what the previous behaviour was.
The rule for this tree: a note is written here whenever a change is made.

---

## Round 5 - 2026-09-26 (uncommitted at time of writing)

### 1. Menu crash: `WBP_OptionsMenu` was written through a stale header offset

**Symptom.** The host process died ~2 s after `TitleScreen -> FrontEnd`, i.e. while still in the
menu, right after the `AJBExecInternal OptionsMenu Toggle` console command. The client did not
die.

**Evidence.** In every crash the faulting read address was `0x43a48000`. That value is exactly
the IEEE-754 bit pattern of `329.0f`, and `329` is the `[InternalTickRate]` value the DLL had
just printed. The faulting instruction at RVA `0x82d52b` walks a container, and `Rdi - Rbx`
was `0x10` for a `Num` of 1, so the container is a `TArray` of 16-byte elements: a multicast
delegate's `InvocationList`. The container sat at `object + 0x240`, which the hand written
header calls `InternalTickRate`, but the shipped `OptionsMenuV2` asset has no member there.

**Root cause.** `PCPortSource/Dumper-7/CustomSDK/WBP_OptionsMenu_classes.hpp` lists
`WBP_MultiplayerMenu @ 0x0220`. The shipped asset does not contain that member at all - the
string `Multiplayer` does not appear anywhere in `WBP_OptionsMenu.uasset`/`.uexp` (verified by
scanning both, 0 occurrences), while `InternalTickRate`, `OnlineStatus`, `VersionInfo`,
`InputKeys` and `bIsOptionsMenuVisible` do. Every entry from `0x0220` onward is therefore
shifted by 8 bytes, so the real `OnInputKeyPressed` delegate `InvocationList` sits exactly on
`0x0240`. Writing a float through `0x0240` overwrote the low half of a heap pointer inside
that delegate; the next delegate bind dereferenced it and the process died.

This defect predates the listen server work: the same `0x82d52b` signature appears in crash
dumps from 2026-09-22 and 2026-09-23, produced by older DLL builds.

**Fix.** No member of `WBP_OptionsMenu` is poked through a hand written offset any more. All
access goes through Unreal's own reflection:

- `AJB::FindReflectedPropertyOffset(Class, Name, ExpectedSize)` walks `SuperStruct` -> `Children`
  -> `Next` (the same walk the SDK itself uses in `UClass::GetFunction`), matches by field name,
  requires the field to be a property, and refuses the property when `ElementSize` does not
  match the expected width or when `Offset + ElementSize` would escape `Class->Size`. On refusal
  it logs under `[Reflect]` and returns `-1` without writing anything.
- `AJB::SetOptionsMenuInternalTickRate(float)` writes `InternalTickRate` only after that
  validation, and caches the resolved offset per class pointer.
- `AJB::GetOptionsMenuInternalTickRate(float*)` reads it back the same way, so the console dump
  can no longer print a value from a bad offset. It prints `Unavailable` when the property is
  refused.
- `AJB::GetOptionsMenuIsVisible(bool*)` reads `bIsOptionsMenuVisible` through reflection too.
  That flag lives behind the same stale entry, so the old raw read was reading inside the
  `OnToggleMenu` delegate. It is now used by the `Browse` toggle and the console dump.

**Before:** `AJB::MOD_OptionsMenu->InternalTickRate = CurrentMaxFPS;` - a raw float store
through `0x0240` that corrupted a delegate and crashed the host in the menu.

**After:** `AJB::SetOptionsMenuInternalTickRate(...)`. If reflection refuses the property the
widget simply keeps its own blueprint default; nothing is written and nothing crashes.

Files: `PCPortSource/Aeyth8/Logic/AJB.h`, `PCPortSource/Aeyth8/Logic/AJB.cpp`,
`PCPortSource/Aeyth8/Tools/UFunctions.cpp`.

### 2. Infinite loading: a joiner could enter the battle map with `CharactorID = 0`

**Symptom.** `CheckForInfiniteLoadingScreen` reported `Errors found: BROKEN_CHARACTER_SPAWN`,
the game sat on the loading screen, and the state machine never reached `InGame.Gameplay`.

**Root cause.** `AJB::Server::CommitConnectionToMatchingPlayers` only set `Info.CharactorID`
when a selection had already been recorded in `ProfileCache.CharacterIds`. A first join has
none, because the game spawns players during the travel into `AJBStage01_P` before the
character select step can run. The entry was therefore committed with `CharactorID = 0`, and
`0` is not a valid character.

Confirmed by the 2026-09-25 17:47 log: the joiner's row reads `[CharactorID]: 0` in every
`MP-PostLogin-After` / `MP-CheckLoading` dump, and the harvest that feeds the cache reported
`[MP-CharacterHarvest] - [Harvested]: 0 | [Registry]: 0`.

**Fix.** When no selection is recorded, the permitted default character is written together
with a logged reason, so a committed entry can no longer carry `CharactorID = 0`. A real
selection still overwrites it later. This matches the behaviour the other two commit paths
(`EnsureLocalHostInSessionCache`, `RebuildMatchingPlayersFromSession`) already had.

Files: `PCPortSource/Aeyth8/Logic/ServerLogic.cpp`.

### Verification for this round

- `Proxy|x64` builds clean to `PCPortSource/x64/Proxy/dxgi.dll` (974848 bytes).
- `Injectable|x64` builds clean to `PCPortSource/x64/Injectable/dxgi.dll` (691712 bytes).
- Both binaries contain the new `Reflect` diagnostics and the character fallback message.
- Constraint check re-run and unchanged: 3527/3527 `Dumper-7` SDK files identical to
  `.baseline/sdk_hashes.txt`, 4/4 offset files identical, 24/24 inline `PB(0x...)` addresses
  unchanged with none added or removed, and all 56 baseline source files still present.

### Still open

The two fixes above are built but **not yet confirmed in game**. The dual-process run has to be
repeated with the new DLL; the log tags to watch are listed in the deployment notes.

