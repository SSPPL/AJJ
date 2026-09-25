# 任务书：恢复 AJB PC Port 的「原版账号资料 → MatchingPlayers → 战斗地图」流程

> 本文件是交给其他 AI 执行的施工说明书。请逐条阅读，按 Phase 顺序施工，每个 Phase 完成后必须运行「验证」小节中的检查项，通过后才进入下一 Phase。
> 项目根目录：`D:\oldMAGUS\AJB-main`

---

## 0. 任务目标（一句话）

让 AJB PC Port（`PCPortSource` 内的 DLL 模组）在 **Listen Server（主机自建房间）+ 客机连接** 的模式下，
**恢复街机原版的账号资料流程**：

```text
账号资料(Name/IconID/Level/Title/CustomData)
  -> PlayerLoginInfo
  -> 全国对战等待列表(120 秒)
  -> 玩家确认 / 选择角色
  -> MatchingPlayers（主机权威）
  -> 选择落点
  -> 战斗地图(AJBStage01_P)中依然能读到完整玩家资料
  -> 正常进入 InGame.Standby -> InGame.Gameplay，不再无限加载
```

**当前缺陷**：进入战斗地图后，`MatchingPlayers` 变成占位数据：

```text
PlayerID = 1 / 2
GameServerUserID = 1 / 2
PlayerName = NO NAME
PlayerIconID = -1
PlayerLevel = 0
PlayerTitle = ""
CharactorID = 0
```

---

## 1. 已核实的事实（不要浪费时间重新论证）

请在动手前读一遍，以下结论均已在本仓库源码中逐行确认。

| 结论 | 依据 |
| --- | --- |
| 当前是 **Listen Server** 模式，不是 Dedicated Server | `AJB::IsServer()` 只在 `NetDriver->ServerConnection == nullptr && GetNetMode()==NM_ListenServer` 时为真（`PCPortSource/Aeyth8/Logic/AJB.cpp:988`） |
| 「中文电脑名导致编码失败」**已排除** | 电脑名现为 ASCII（`xaingzhexianglixainghuishou7891`），日志中的 `Name=` 只是 ComputerName 生成的连接名 |
| 皮肤广播链路正常、`MatchingPlayers` 链路不完整 | 在战斗地图能打印皮肤，但玩家资料为默认值 |
| 模组的自定义服务器→客户端消息系统**当前是关闭的** | `#define USING_CUSTOM_MESSAGING_SYSTEM 0`（`AJB.cpp:71`），因此 `NotifyControlMessage` / `WorldWelcomePlayer` 相关 Hook 未启用 |
| 玩家资料 Hooks 只复制了 4 个字段 | `TryGetMatchingPlayerInfoByPlayerIDPureFunction`（`AJB.cpp:294-335`）只写 `PlayerName / CharactorID / CustomData.charaSkinId / PlayerID` |
| 该 Hook 用 `AJB::Instance->MatchingPlayers`（全局）而非入参 `This` | 同上，第 318/320/322 行 |
| 该 Hook 把 `PlayerID` 直接当数组下标（`PlayerID-1`） | 同上，第 316 行 |
| 该 Hook 在索引无效时**仍然 `return true`** | 同上，第 331 行 |
| `TryFixInfiniteLoadingScreen()` 只调用 `DebugCharacterChange()`，完全不修 `MatchingPlayers` | `AJB.cpp:1070-1125` |
| `CheckForInfiniteLoadingScreen()` 在检测到 `CharactorID==0` 时调用上面的修复 | `AJB.cpp:1127-1182`，触发点在 `FlowUtilChangeState` 的 `InGame.Standby`（`AJB.cpp:1272-1276`） |
| `GetNetID` Hook 硬编码返回 `L"Aeyth8"` | `AJB.cpp:360-366`，注册于 `AJB.cpp:676` |
| 账号资料初始化代码**全部被注释掉**（原作者早期调试残留） | `AJB.cpp:814-872`（`Init_Vars` 内的 `PlayerLoginInfo` 块） |
| 主机侧 `PreLogin` 能读到连接参数（含 `Name=`） | `AJB.cpp:156` / `ServerLogic.cpp:97`；`UFunctions::PreLogin` 在 `UFunctions.cpp:1539` |
| 主机侧已有连接表可挂资料 | `AJB::ClientConnections`（`std::vector<FAJBNetConnection>`，`ServerLogic.h`），`AddClientConnection`/`CloseConnection` 已 Hook（`UFunctions.cpp:1738/1746`） |
| 客户端连接参数可被主机读取并可被客户端追加 | 客户端：`UFunctions::InitLocalConnection`（`UFunctions.cpp:1486-1530`）通过 `BP_GlobalPatcher_C::AppendToFStringArray` 往 `InURL.Op` 追加；主机：`PreLogin` 解析 `Options` |
| 「原版」关卡名 | 等待/落点地图 `/Game/AJB/Maps/SimpleStartLocationSelect_P`，战斗地图 `AJBStage01_P`，大厅 `AJBOutGame_ENV01`，角色选择 `AJBCharacterSelect`（见 `UFunctions.cpp:424-425,1337`） |
| 日志文件位置 | 可执行文件同级的 `Logs\Debug.log`（`PCPortSource/Aeyth8/Logger.hpp`）；`Global::LogA` 会 flush |
| 构建产物 | `JoJo.sln`，配置 `Proxy|x64`（dxgi 代理）与 `Injectable|x64`，`TargetName=dxgi`；已有产物 `x64/Proxy/dxgi.dll` |

---

## 2. 硬性约束（不要做这些）

1. **不要**修改 `PCPortSource/Dumper-7/SDK/**`（自动生成的 SDK）。所有改动只允许出现在 `PCPortSource/Aeyth8/**`、`PCPortSource/dllmain.cpp`、`Launcher/**`。
2. **不要**改动任何 offset 数值（`Offsets.cpp` / `Offsets.h` / 内联 `PB(...)`）。本仓库的 v33 偏移已在 `MIGRATION_REPORT_v28_to_v33.md` 中逐条验证。
3. **不要**删除或重排既有注释；风格上保持与仓库一致（Tab 缩进、`A8CL::`/`AJB::` 命名空间写法）。
4. **不要**新增版权头；**不要**提交 git；**不要**改文件名。
5. **不要**用 `memcpy` 整体拷贝 `FMatchingPlayerInfo` / `FCustomData`（内含 `FString`、`TArray`，直接 memcpy 会破坏引用计数并导致崩溃/泄漏）。必须逐字段使用游戏自身的 `FString` 拷贝函数或走 Blueprint 深拷贝接口。
6. **不要**把角色皮肤来自「电脑名 / 本地随机数」这类逻辑当作正解；皮肤属于 `CustomData`，必须随账号资料一起传递。
7. 每次只改一个关注点，改完立刻编译 + 跑一次双机测试，再继续。

---

## 3. 需要理解的三个数据对象（先读代码，再改）

```cpp
// PCPortSource/Dumper-7/SDK/AJB_structs.hpp:2527
struct FMatchingPlayerInfo {          // 0x0088
    uint8   PlayerID;                 // 0x00
    FString GameServerUserID;         // 0x08
    uint8   TeamID;                   // 0x18
    FString TeamHostUserID;           // 0x20
    FString PlayerName;               // 0x30
    int32   PlayerIconID;             // 0x40
    int32   PlayerLevel;              // 0x44
    FString PlayerTitle;              // 0x48
    uint8   CharactorID;              // 0x58
    FCustomData CustomData;           // 0x60
    EInGameProgressID InGameProgressID; // 0x80
    FStartLocation StartLocation;     // 0x81
    bool    bIsCameraMode;            // 0x83
    int32   Rate;                     // 0x84
};

// PCPortSource/Dumper-7/SDK/AJB_structs.hpp:2503
struct FCustomData {                  // 0x0020
    uint8 charaSkinId;                // 0x00
    uint8 standSkinId;                // 0x01
    TArray<FEmoteData> EmoteData;     // 0x08
    int32 KillCount;                  // 0x18
};

// PCPortSource/Dumper-7/SDK/AJB_structs.hpp:2577   —— 每进程本地账号资料
struct FPlayerLoginInfo {             // 0x0150
    FString SessionID;                // 0x00
    FString UserDataID;               // 0x10
    bool    bIsGuest;                 // 0x20
    FString AccessCode;               // 0x28
    bool    bIsBNCard;                // 0x38
    ...
    TArray<FCustomData> CustomData;   // 0x68
    FMatchingPlayerInfo MatchingPlayerInfo; // 0x78
    ...
};
```

GameInstance 上的两个容器（`PCPortSource/Dumper-7/SDK/AJB_classes.hpp`）：

```cpp
struct FPlayerLoginInfo  PlayerLoginInfo;   // 0x00D0  —— 本机账号资料
TMap<FString, FMatchingPlayerInfo> MatchingPlayers; // 0x0340 —— 全国对战资料表（应为主机权威）
TArray<FMatchingPlayerInfo> MachingNPC;     // 0x0390 —— NPC 资料
TMap<int32, FCustomData> OfflineDefaultCustomData;  // 0x0448 —— 离线默认皮肤
```

**关键设计原则（本任务书的核心）**：

```text
客户端：只负责「读出自己账号资料」+「把自己的资料告诉主机」+「读主机下发的结果」
主机  ：唯一的 MatchingPlayers 写入者，负责分配 PlayerID、生成完整 FMatchingPlayerInfo、复制给客户端
```

`MatchingPlayers` 的 Key 是 `FString`（原版用作 UserID/连接标识），Value 是完整资料。
**不要**再用「数组下标 == PlayerID - 1」这种假设。

---

## 4. 施工阶段

### Phase 0 — 先用日志定位「第一次变成默认值的位置」（必做，不要跳过）

**目的**：在不改行为的前提下，确定 `MatchingPlayers` 是在哪个阶段变成 `NO NAME / -1 / 0`。
**原因**：直接改 Hook 可能掩盖真实根因。本 Phase 只加日志。

**改动**：在 `PCPortSource/Aeyth8/Logic/AJB.cpp` 增加一个统一的资料打印辅助函数（放在 `AJB::PlayerInfoParser` 下方，约第 949 行后）：

```cpp
void AJB::DumpMatchingPlayers(const char* Tag)
{
    if (!AJB::Instance) return;

    const int32 Count = AJB::Instance->MatchingPlayers.Num();
    LogA(Tag, std::format("[Num]: {} | [IsServer]: {} | [IsInSession]: {}", Count, AJB::IsServer(), AJB::IsInSession()));

    for (int32 i{0}; i < Count; ++i)
    {
        auto& Entry = AJB::Instance->MatchingPlayers[i];
        LogA(Tag, std::format("[Index]: {} | [Key]: {} | [Info]: {} | [charaSkinId]: {} | [standSkinId]: {} | [EmoteNum]: {}",
            i,
            Entry.First.ToString(),
            AJB::PlayerInfoParser(Entry.Second),
            Entry.Second.CustomData.charaSkinId,
            Entry.Second.CustomData.standSkinId,
            Entry.Second.CustomData.EmoteData.Num()));
    }
}
```

在 `AJB.h` 的 Helper Functions 区（`PlayerInfoParser` 声明附近）加声明：

```cpp
void DumpMatchingPlayers(const char* Tag);
```

然后在这些位置各调用一次 `AJB::DumpMatchingPlayers("...")`：

| 位置 | 文件:行 | 建议 Tag |
| --- | --- | --- |
| 离开 OutGame 前的每帧/关键点：`FlowUtilChangeState` 进入 `OutGame.SelectStartLocation` 时 | `AJB.cpp:1255` 附近的 `SelectStartLocation` 分支 | `MP-OutGame-SelectStart` |
| 主机 `PostLogin`（`AJB::Server::PostLogin` 开头） | `ServerLogic.cpp:152` | `MP-PostLogin` |
| 客户端 `InitLocalConnection` 末尾 | `UFunctions.cpp:1530` 前 | `MP-ClientJoin` |
| 主机的 `TryGetMatchingPlayerInfoByPlayerIDPureFunction` 入口与出口 | `AJB.cpp:294` 与 `331` | `MP-Hook-In` / `MP-Hook-Out` |
| `InGame.Standby` 触发点（`CheckForInfiniteLoadingScreen` 之前） | `AJB.cpp:1272` | `MP-InGameStandby` |
| `CheckForInfiniteLoadingScreen` 内打印完之后 | `AJB.cpp:1181` | `MP-CheckLoading` |
| 战斗地图 `InGame.Gameplay` 进入时（`FlowUtilChangeState` 内） | `AJB.cpp:1202` 起 | `MP-Gameplay` |

**同时**在 Host 的 `AJB::Server::PreLogin`（`ServerLogic.cpp:97`）把完整 `Options` 拆开逐条打印，确认 `Name=`、以及将要新增的 `ProfileToken=` 是否能被读到：

```cpp
for (const SDK::FString& Op : Options->Split(L"?"))   // 若 Split 不可用，用 ToString() 手工按 '?' 切分
    LogA("PreLogin-Options", Op.ToString());
```

（`Options` 是 `UC::FString*`，可用 `Options->ToString()` 返回 `std::string` 后按 `?` 切分，最稳妥。）

**验证**：
1. 编译 `Proxy|x64`，替换 `x64/Proxy/dxgi.dll` 与游戏目录内的 `dxgi.dll`。
2. 主机用 `SimpleStartLocationSelect_P?listen` 开房，客机连接，走完 120 秒等待 → 选择落点 → 进战斗。
3. 抓 `Logs\Debug.log`，找出**第一次**出现 `PlayerName = NO NAME` 的那一行 Tag。

**完成判定**：日志中能明确指认「资料是在 A 阶段正常的、到 B 阶段变成默认值」，并写进 commit 说明/注释。若发现资料在 `MP-Hook-In` 之前就已经是完整值、而 `MP-Hook-Out` 变成默认值，则根因在 Hook（进入 Phase 1）。若在 `MP-PostLogin` 就是默认值，则根因在「资料从未进入 `MatchingPlayers`」（进入 Phase 2/3）。

---

### Phase 1 — 修正玩家资料查询 Hook

**目标**：让 `TryGetMatchingPlayerInfoByPlayerIDPureFunction` 不再产生残缺/默认资料。

**文件**：`PCPortSource/Aeyth8/Logic/AJB.cpp:294-335`

**改动要点（按优先级）**：

1. **先调用原版函数**，以原版结果为准：

```cpp
bool TryGetMatchingPlayerInfoByPlayerIDPureFunction(SDK::UAJBGameInstance* This, int32 PlayerID, SDK::FMatchingPlayerInfo* Out)
{
    if (!This || !Out) return false;

    // 1) 永远先走原版逻辑
    const bool bNativeResult = OFF::TryGetMatchingPlayerInfo
        .VerifyFC<bool(__thiscall*)(SDK::UAJBGameInstance*, int32, SDK::FMatchingPlayerInfo*)>()(This, PlayerID, Out);

    if (AJB::bDebugModeFromCMLA)
        LogA("TryGetMatchingPlayerInfo[Original]", std::format("[PlayerID]: {} | [Result]: {} | [Out]: {}", PlayerID, bNativeResult, AJB::PlayerInfoParser(*Out)));

    // 2) 原版没找到时才回退到本机 MatchingPlayers
    if (bNativeResult) return true;
    ...（回退逻辑见下）
}
```

2. **回退逻辑禁止使用 `PlayerID - 1`**。改为按 Key 或按 `Info.PlayerID` 查找：

```cpp
    // 回退：在 MatchingPlayers 中按 PlayerID 字段匹配，而不是按数组下标
    for (int32 i{0}; i < This->MatchingPlayers.Num(); ++i)
    {
        SDK::FMatchingPlayerInfo& Candidate = This->MatchingPlayers[i].Second;
        if (Candidate.PlayerID != static_cast<uint8>(PlayerID)) continue;

        // 逐字段深拷贝（FString 必须用游戏自身的拷贝函数）
        *Out = Candidate;                                   // POD 部分先整体赋值
        AJB::CopyString(&Out->PlayerName,      &Candidate.PlayerName);
        AJB::CopyString(&Out->PlayerTitle,     &Candidate.PlayerTitle);
        AJB::CopyString(&Out->GameServerUserID,&Candidate.GameServerUserID);
        AJB::CopyString(&Out->TeamHostUserID,  &Candidate.TeamHostUserID);
        Out->PlayerID = static_cast<uint8>(PlayerID);
        return true;
    }
    return false;   // 找不到就返回 false，绝不伪装成功
```

> 注意：`*Out = Candidate;` 会把 `FString`/`TArray` 的浅拷贝语义带进来（Dumper-7 的 `FString` 是 `TArray<wchar_t>` 包装）。如果编译后的行为不稳定（例如出现野指针），**改用**：先 `CopyString` 三个字符串字段、逐个赋值 POD 字段、`CustomData` 通过游戏接口（`UAJBGameInstance::TryUpdateCustomDataAndCharacterIDByPlayerID` 反向使用或 `UBP_AJBGameInstance_C::FindMatchingPlayerInfoFromPlayerID`）取得，避免自造深拷贝。

3. **`CustomData` 必须完整**，不能只拷 `charaSkinId`。若 `FEmoteData` 数组的深拷贝无法安全实现，优先走 Blueprint 接口：

```cpp
// UBP_AJBGameInstance_C::FindMatchingPlayerInfoFromPlayerID 内部是蓝图深拷贝（见 BP_AJBGameInstance_functions.cpp:552）
SDK::UBP_AJBGameInstance_C* BPInstance = static_cast<SDK::UBP_AJBGameInstance_C*>(This);
SDK::FMatchingPlayerInfo Value{};
if (BPInstance->FindMatchingPlayerInfoFromPlayerID(static_cast<uint8>(PlayerID), &Value))
{
    *Out = Value;   // 由游戏自己完成深拷贝
    return true;
}
```

> `FindMatchingPlayerInfoFromPlayerID` 声明见 `PCPortSource/Dumper-7/SDK/BP_AJBGameInstance_classes.hpp:144`。

4. **索引无效时不得 `return true`**。

**验证**：
- 编译后进战斗地图，日志中 `MatchingPlayers` 与 Hook 输出的 `PlayerIconID / PlayerLevel / PlayerTitle` 必须与原版一致（不再是 `-1 / 0 / 空`）。
- 客机侧 UI（战斗列表/结算）能显示玩家名与头像 ID。
- 若本 Phase 做完 UI 仍显示 `NO NAME`，说明 `MatchingPlayers` 本身就是默认值 → 回到 Phase 0 结论，进 Phase 2。

---

### Phase 2 — 让主机成为 `MatchingPlayers` 的唯一权威写入者

**目标**：主机在玩家连接时收到其账号资料，构造完整 `FMatchingPlayerInfo` 并写入 `MatchingPlayers`，随后由主机侧逻辑复制到客户端。

**2.1 建立「连接 → 资料」暂存**

在 `PCPortSource/Aeyth8/Logic/ServerLogic.h` 的 `FAJBNetConnection` 中增加资料字段（保持结构简单，避免 `FString` 生命周期问题可先存 `std::wstring`）：

```cpp
struct FAJBNetConnection
{
    SDK::UIpConnection* Connection;
    uint8  CharacterID;
    uint8  CharacterSkin;
    uint8  Flags;

    // 新增：本连接的账号资料（由 PreLogin 解析、PostLogin 落库）
    std::wstring PlayerName;
    std::wstring PlayerTitle;
    std::wstring ProfileToken;
    int32  PlayerIconID{-1};
    int32  PlayerLevel{0};
};
```

**2.2 客户端把资料/令牌塞进连接参数**

在 `UFunctions::InitLocalConnection`（`UFunctions.cpp:1486-1530`）里，已有的 `AppendToFStringArray(InURL.Op, ...)` 逻辑旁追加：

```cpp
if (bAppend && AJB::MOD_GlobalPatcher)
{
    // 已有：AppendToFStringArray(InURL.Op, AJB::DLLCommitVersion);
    // 新增：把本机账号资料/令牌带上（第一版用本地 profile，见 Phase 3）
    static SDK::FString ProfileToken{ AJB::LocalProfileToken.c_str() };
    if (!AJB::LocalProfileToken.empty())
        AJB::MOD_GlobalPatcher->AppendToFStringArray(InURL.Op, ProfileToken);
}
```

> 参考 `AppendToFStringArray` 声明：`PCPortSource/Dumper-7/CustomSDK/BP_GlobalPatcher_classes.hpp`。

**2.3 主机在 PreLogin/Login 解析并落库**

改 `AJB::Server::PreLogin`（`ServerLogic.cpp:97`）：

```cpp
// Options 形如 "Name=xxx-<hostname>?ProfileToken=ABCD"
// 用 Options->ToString() 切分 '?' 后逐项匹配 "ProfileToken="
```

在 `AJB::Server::Login`（`ServerLogic.cpp:135`）或 `PostLogin`（`ServerLogic.cpp:152`）中，把 token/资料写入对应的 `FAJBNetConnection`，然后**由主机**构造并写入：

```cpp
// 仅主机执行
SDK::FMatchingPlayerInfo Info{};
Info.PlayerID        = static_cast<uint8>(NewPlayerId);   // 主机分配，从 1 递增，不复用
Info.TeamID          = /* 与 PlayMode 对应 */;
AJB::CopyString(&Info.PlayerName,   &NameFString);
AJB::CopyString(&Info.PlayerTitle,  &TitleFString);
AJB::CopyString(&Info.GameServerUserID, &UserIdFString);
Info.PlayerIconID    = IconId;
Info.PlayerLevel     = Level;
Info.CharactorID     = CharacterId;             // 必须在进入 InGame.Standby 前有效，见 Phase 5
Info.CustomData.charaSkinId = SkinId;
Info.CustomData.standSkinId = StandSkinId;

// Key 用稳定标识（UserID / NetID / 连接名），不要用序号字符串
static SDK::FString Key{ /* UserId */ };
AJB::Instance->AddMatchingPlayerInfo(Key, Info);
```

> `AddMatchingPlayerInfo` 声明：`PCPortSource/Dumper-7/SDK/BP_AJBGameInstance_classes.hpp:149`，是 BlueprintCallable，内部做 `TMap::Add`（`BP_AJBGameInstance_functions.cpp:668`）。

**2.4 客户端不要自己写 `MatchingPlayers`**

客户端只做：读自己的 `PlayerLoginInfo` → 发 token → 之后一律读主机复制过来的结果。
如果发现客户端 UI 需要资料而主机尚未同步，**优先补齐主机→客户端的同步路径**，而不是让客户端本地猜。

**验证**：
- 主机日志 `MP-PostLogin` 打印出的 `MatchingPlayers` 每项必须含真实 `PlayerName / GameServerUserID / PlayerIconID / PlayerLevel / PlayerTitle`，且 `PlayerID` 连续从 1 开始且不重复。
- 客机日志（`MP-ClientJoin` 之后任意 Tag）读到的资料与主机一致。
- 玩家中途退出再重连，`PlayerID` 不出现「空洞导致主机删表」的旧 bug（对应 `AJB.cpp:1072-1074` 的注释描述）。回归测试：3 人房间，2 号退出，1/3 号继续，界面列表与战斗内资料正确。

---

### Phase 3 — 第一版资料源：本地 profile 文件（先不接网站）

**目标**：先用本地 JSON/INI 验证整条数据链，再换成 HTTP。

**做法**：
1. 在 `Logger::Init` 创建的 `Aeyth8\Configs` 目录（见 `PCPortSource/Aeyth8/A8CL/Logger/Logger.cpp:14`）旁，定义 profile 目录约定，例如：
   ```text
   <游戏目录>\Aeyth8\Configs\profiles\<AccountId>.ini
   ```
2. 内容键值：
   ```ini
   [Profile]
   UserId=PCPORT-SESSION-001
   Name=PlayerName
   IconId=1
   Level=50
   Title=PlayerTitle
   CharaSkinId=6
   StandSkinId=3
   ```
3. 客户端启动时（`AJB::Init_Vars`，`AJB.cpp:814`）读取本机 profile，写入：
   - `Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerName`
   - `Instance->PlayerLoginInfo.UserDataID` / `SessionID`
   - `Instance->PlayerLoginInfo.CustomData`（对应角色的 `charaSkinId/standSkinId`）
   - 并缓存 `AJB::LocalProfileToken`
4. 主机侧按 token 读取**同一份** profile 文件构造 `FMatchingPlayerInfo`（局域网测试时可约定共享目录，或让客户端把完整资料放在连接参数里，见下）。

> 参考：`Init_Vars` 中已有被注释掉的同类写法（`AJB.cpp:822-865`），可直接复用其中的 `CopyString(&Instance->PlayerLoginInfo.MatchingPlayerInfo.PlayerName, &Username)` 模式，但**不要**保留 `PlayerTitle = "1170"` 这类假值。

**验证**：使用 `profiles/Aeyth8.ini` 与 `profiles/Player2.ini`，两台机器分别指定不同 profile，战斗地图内双方 `PlayerName/IconID/Title/CustomData` 均正确。

---

### Phase 4 — 换成网站 / 本地 Web API

**目标**：把 Phase 3 的「读本地文件」替换为 HTTP 查询，架构不变。

**约定接口**：

```text
GET /profile?token=<ProfileToken>
200 OK
{
  "account_id": "PCPORT-SESSION-001",
  "name": "PlayerName",
  "icon_id": 1,
  "level": 50,
  "title": "PlayerTitle",
  "custom_data": { "chara_skin_id": 6, "stand_skin_id": 3, "emotes": [] }
}
```

**流程**（不要颠倒）：

```text
客户端 -> 网站  : 登录，取得短期 ProfileToken
客户端 -> 主机  : 连接参数带 ProfileToken（不要带完整资料，避免 URL 过长/转义问题）
主机   -> 网站  : 用 ProfileToken 换取可信资料
主机            : 构造 FMatchingPlayerInfo -> AddMatchingPlayerInfo
主机   -> 客户端: 下发 MatchingPlayers
```

**安全要求**：Token 有有效期、只能读公开资料、不含明文密码；主机必须重新查询网站而不是信任客户端直接传来的 `IconID/Title`。

> 注意：URL 里不要放 `Title`/`CustomData` 等长且可能含特殊字符的字段。连接名字段（`Name=`）本身已是 ComputerName 格式，不要依赖它承载账号资料。

---

### Phase 5 — 角色选择与 `CharactorID`

**目标**：`InGame.Standby` 检查（`CheckForInfiniteLoadingScreen`，`AJB.cpp:1127`）读取时，`MatchingPlayers[i].Second.CharactorID` 必须已经有效。

**做法**：
1. 角色选择由本地流程完成（原版在 OutGame/等待界面选人）。
2. 选择结果由主机写入 `MatchingPlayers`：使用 `UBP_AJBGameInstance_C::SetCharacterIDFromPlayerName`（`BP_AJBGameInstance_classes.hpp:151`）或 `UAJBGameInstance::TryUpdateCustomDataAndCharacterIDByPlayerID`（`AJB_classes.hpp:1916`）。
3. 确保写入时机**早于** `InGame.Standby`。若无法保证，则在进入战斗地图时由主机补写一次（在 `AJB::Server::PostLogin` 里，或切图后第一次可用 tick）。
4. 明确：`TryFixInfiniteLoadingScreen()`（`AJB.cpp:1070`）是**兜底**，不是正解。修复数据后，`CheckForInfiniteLoadingScreen` 应报告 `No errors detected.`（`AJB.cpp:1160` 附近）。允许保留该兜底，但必须在日志中可见「本帧没有触发修复」。

**验证**：
- 进战斗地图时 `BugsToFix == 0`，状态机能自行推进到 `InGame.Gameplay`。
- 客机与主机角色一致，没有出现「主机强行生成角色、客机不同步」现象。

---

### Phase 6 — 清理与加固

1. 修正 `GetNetID` Hook（`AJB.cpp:360`）：目前硬编码 `Aeyth8`。改为返回**本机真实 NetID / 账号 UserID**（若原版 `UAJBNetworkObserver::GetNetID` 已可用，优先直接调用原函数，仅在必要时覆盖）。
2. 处理 `TryGetMatchingMyPairInfo`（`AJB.cpp:282`）——同样存在「打印后直接转发」的临时性质，确认它在 Pair 模式下用的是完整资料。
3. 复查 `AJB::Server::PreLogin` 的客户端版本校验（`ServerLogic.cpp:118-126`，比对 `AJB::DLLCommitVersion`，当前为 `[v0.7.5]`，`AJB.cpp:129`）。改动连接参数时不要破坏该校验。
4. 复查 `AJB::ClientConnections` 的生命周期：`AddClientConnection`/`CloseConnection`（`UFunctions.cpp:1738/1746`）与新增字段的清理，避免断线后残留。
5. 移除 Phase 0 的临时日志，或收敛为 `bDebugModeFromCMLA` 控制（`AJB.cpp:687` 附近目前该开关的赋值被注释掉了，可顺手恢复为 `AJB::bDebugModeFromCMLA = CMLA::Debug.GetAsBool();`）。
6. 保持 `CheckForInfiniteLoadingScreen` 的日志，它在线上是重要的诊断信息。

---

## 5. 构建与测试步骤（供执行者照抄）

```text
构建：
  1) 用 Visual Studio 2022（v143 工具集）打开 D:\oldMAGUS\AJB-main\JoJo.sln
  2) 配置选 Proxy | x64（若要注入式构建则 Injectable | x64）
  3) 生成解决方案，产物为 dxgi.dll（TargetName=dxgi）

部署（Proxy 方式）：
  1) 把 x64\Proxy\dxgi.dll 复制到游戏目录 <GameDir>\AJB\Binaries\Win64\dxgi.dll
  2) 用 Launcher\ 里的 AJBLauncher 启动（它负责拼装 exe 路径与 -bDebugInputMode 参数）

测试（双机/双进程）：
  主机：AJB-Win64-Shipping.exe -debug -log  （然后进入模式选择，开 Listen 房间）
  客机：连接主机 IP
  路径：大厅 -> 全国对战(SimpleStartLocationSelect_P) -> 120 秒 -> 选落点 -> AJBStage01_P
  观察：<GameDir>\AJB\Binaries\Win64\Logs\Debug.log
  控制台命令（游戏内 ~ 键）：
    matchingplayers   —— 打印 MatchingPlayers（UFunctions.cpp:271）
    connections       —— 打印连接表（UFunctions.cpp:1206）
    netid             —— 打印 NetID（UFunctions.cpp:729）
    battle            —— 打印 BattleSettings（UFunctions.cpp:733）
```

---

## 6. 验收标准（黑盒）

| 编号 | 场景 | 期望 |
| --- | --- | --- |
| A1 | 双人 Listen，走完全流程 | 战斗地图 `MatchingPlayers` 含 2 项，`PlayerName` 为各自账号名，`PlayerIconID != -1`，`PlayerLevel > 0`，`PlayerTitle` 非空 |
| A2 | 同上，看 120 秒等待界面 | 列表显示双方 `Name/IconID/Title`（不是 NO NAME） |
| A3 | 同上，进战斗 | 无无限加载；`CheckForInfiniteLoadingScreen` 输出 `No errors detected.` |
| A4 | 客机中途退出再重连 | `PlayerID` 不空洞，主机表不丢项，UI 正常 |
| A5 | 不同 profile（不同 Icon/Title/皮肤） | 双方看到的对方资料与皮肤互相正确、不串号 |
| A6 | 单机（Offline）回归 | 原有离线流程不被破坏 |
| A7 | 编译 | `Proxy|x64` 与 `Injectable|x64` 均编译通过，无新增警告（`/W3`） |

---

## 7. 需要向提问者确认的开放问题（若无法从代码判定，请在开工前一次性问清）

1. 账号资料的权威来源最终是 **自建网站** 还是 **本地 profile 文件**？（Phase 3/4 二选一落地）
2. `PlayerTitle` / `PlayerIconID` / `PlayerLevel` 的取值范围与斗篷规则（原版由网站下发）,是否有既定表？
3. 是否需要保留 Dedicated Server 路径（当前代码里有大量 DSC 分支，例如 `AJB.cpp:1187`、`1261-1270`）？若不需要，后续可裁剪。
4. 是否允许改动 `Launcher`（例如增加 `-profile=` 参数）？

---

## 8. 参考索引

- 迁移与偏移验证：`MIGRATION_REPORT_v28_to_v33.md`、`OFFSET_REFERENCE_MASTER.md`
- 模组主逻辑：`PCPortSource/Aeyth8/Logic/AJB.cpp`（`Init_Hooks:562`、`Init_Engine:735`、`Init_Vars:814`、`FlowUtilChangeState:1202`）
- 服务器逻辑：`PCPortSource/Aeyth8/Logic/ServerLogic.cpp`（`PreLogin:97`、`Login:135`、`PostLogin:152`）
- UE 函数 Hook：`PCPortSource/Aeyth8/Tools/UFunctions.cpp`（`Browse:1313`、`InitListen:1409`、`InitLocalConnection:1486`、`PreLogin:1539`、`Login:1549`、`PostLogin:1683`）
- 原生函数偏移：`PCPortSource/Aeyth8/Offsets.cpp:112`（`TryGetMatchingPlayerInfo = 0x486E20`）
- SDK 结构定义：`PCPortSource/Dumper-7/SDK/AJB_structs.hpp:2503/2527/2577`、`PCPortSource/Dumper-7/SDK/AJB_classes.hpp:1737-1764`
- Blueprint 侧接口：`PCPortSource/Dumper-7/SDK/BP_AJBGameInstance_classes.hpp:143-178`、`PCPortSource/Dumper-7/SDK/BP_AJBOutGameProxy_classes.hpp:190`
