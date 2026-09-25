# AJB PC Port 原版账号资料流程恢复施工书

> 本文件交给其他 AI 或工程师执行。目标是恢复 AJB PC Port 在 **Listen Server（主机自建房间）+ 客机连接** 模式下的原版账号资料流程。
>
> 严格按 Phase 顺序推进；每个 Phase 完成后必须通过对应验证，再进入下一 Phase。每次只改变一个关注点，编译并完成一次双进程测试后再继续。
>
> 项目根目录：`D:\oldMAGUS\AJB-main`

---

## 0. 目标和最终数据流

最终流程必须满足：

```text
账号资料
  -> PlayerLoginInfo
  -> SimpleStartLocationSelect_P 等待列表
  -> 主机分配 PlayerID 并维护 MatchingPlayers
  -> 角色选择
  -> 主机向客户端同步完整玩家快照
  -> AJBStage01_P
  -> InGame.Standby
  -> InGame.Gameplay
```

有效玩家在战斗地图中必须仍然拥有：

```text
PlayerID / GameServerUserID / TeamID / TeamHostUserID
PlayerName / PlayerIconID / PlayerLevel / PlayerTitle
CharactorID / CustomData / StartLocation / InGameProgressID
```

不得出现由默认结构产生的：

```text
PlayerName = NO NAME
PlayerIconID = -1
PlayerLevel = 0
PlayerTitle = ""
CharactorID = 0
```

网站或本地 Web 服务只负责账号资料，不负责 UE4 联机、房间、地图切换或角色同步。Listen Server 仍然是房间和 `MatchingPlayers` 的权威源。

---

## 1. 已确认的源码事实

执行前先阅读这些事实，不要重新把问题归因到中文电脑名或 Dedicated Server：

| 事实 | 源码位置 |
| --- | --- |
| 当前模式是 Listen Server | `PCPortSource/Aeyth8/Logic/AJB.cpp` 中的 `AJB::IsServer()` |
| 当前不是 Dedicated Server | 主机日志在等待地图和战斗地图都出现 `InitListen` 与 `AJBLocalPlayer` |
| 中文电脑名不是当前根因 | 当前电脑名为 ASCII：`xaingzhexianglixainghuishou7891` |
| `MatchingPlayers` 属于 GameInstance | `PCPortSource/Dumper-7/SDK/AJB_classes.hpp` |
| `PlayerLoginInfo` 是每个进程的本地资料 | `AJB_classes.hpp` 中的 `FPlayerLoginInfo PlayerLoginInfo` |
| 自定义消息系统目前关闭 | `AJB.cpp` 中 `USING_CUSTOM_MESSAGING_SYSTEM 0` |
| 当前资料查询 Hook 只复制少数字段 | `AJB.cpp` 中的 `TryGetMatchingPlayerInfoByPlayerIDPureFunction` |
| 当前 Hook 使用全局 `AJB::Instance` | 同上，不使用函数入参 `This` |
| 当前 Hook 假设 `PlayerID - 1` 是数组下标 | 同上 |
| 当前 Hook 查找失败仍返回 `true` | 同上 |
| 当前 `GetNetID()` 硬编码返回 `Aeyth8` | `AJB.cpp` 中的 `GetNetID` |
| `PlayerLoginInfo` 初始化代码有被注释的旧调试块 | `AJB.cpp` 的 `Init_Vars` |
| 主机已有连接表 | `ServerLogic.h/.cpp` 的 `AJB::ClientConnections` |
| 主机 `PreLogin` 能读取连接参数 | `ServerLogic.cpp` 的 `AJB::Server::PreLogin` |
| 客户端可以追加 URL 选项 | `UFunctions.cpp` 的 `InitLocalConnection` 和 `AppendToFStringArray` |
| 客户端已有可拦截的消息入口 | `UFunctions.cpp` 的 `ClientTeamMessageImplementation` |
| `ClientTeamMessage` 是 SDK 可调用的客户端 RPC | `PCPortSource/Dumper-7/SDK/Engine_classes.hpp` |
| `AddMatchingPlayerInfoLocal` 只能传少数参数 | `BP_AJBOutGameProxy_classes.hpp` |
| 角色更新接口已经存在 | `TryUpdateCustomDataAndCharacterIDByUserID`、`SetCharacterIDFromPlayerName` |
| 日志输出位置 | 游戏目录同级 `Logs\Debug.log` |

---

## 2. 硬性约束

1. 不修改 `PCPortSource/Dumper-7/SDK/**`。SDK 只读，不能手工补字段或改生成代码。
2. 不修改任何 offset 数值、`Offsets.cpp`、`Offsets.h` 或内联 `PB(...)` 地址。
3. 不删除或重排既有注释，不新增版权头，不改已有文件名。
4. 允许新增 `PCPortSource/Aeyth8/**` 下的实现文件和 `ProfileService/**` 下的本地测试服务文件。
5. 不使用 `memcpy`、`*Out = Candidate` 或其他整体浅拷贝复制包含 `FString`/`TArray` 的 Unreal 结构。
6. `FString` 必须使用游戏自身的 `CopyString` 或 Blueprint 深拷贝接口；`FCustomData.EmoteData` 也必须安全构造。
7. 不把电脑名、本地随机数或连接顺序当作账号名、账号 ID 或皮肤来源。
8. 客户端不能把账号密码发送给 Listen Server；连接参数只携带短期 `ProfileToken`。
9. 网络请求不得阻塞 `PreLogin`、`PostLogin`、渲染线程或游戏 Tick；后台线程只处理普通 C++ 数据，UE 对象只能在游戏线程访问。
10. 每个 Phase 完成后先编译，再跑一次双进程 Listen Server 测试，记录日志结果。

---

## 3. 数据对象和所有权

### 3.1 SDK 结构

必须先阅读：

```text
PCPortSource/Dumper-7/SDK/AJB_structs.hpp
  FEmoteData / FCustomData / FMatchingPlayerInfo / FStartLocation

PCPortSource/Dumper-7/SDK/AJB_classes.hpp
  FPlayerLoginInfo / MatchingPlayers
  TryUpdateCustomDataAndCharacterIDByUserID

PCPortSource/Dumper-7/SDK/BP_AJBGameInstance_classes.hpp
  FindMatchingPlayerInfoFromPlayerID
  ClearMatchingPlayerInfo / AddMatchingPlayerInfo
  SetCharacterIDFromPlayerName
```

### 3.2 普通 C++ 资料缓存

新增 `ProfileRecord`，只保存普通 C++ 类型，不保存裸的 Unreal 容器：

```cpp
struct ProfileRecord
{
    std::string AccountId;
    std::string ProfileToken;
    std::wstring PlayerName;
    std::wstring GameServerUserID;
    std::wstring PlayerTitle;
    int32 PlayerIconID{-1};
    int32 PlayerLevel{0};
    uint8 CharaSkinId{0};
    uint8 StandSkinId{0};
    int32 KillCount{0};
    std::vector<ProfileEmote> Emotes;
};
```

`ProfileRecord` 只在游戏线程转换为 `FMatchingPlayerInfo`。转换函数必须逐字段写入，并使用游戏的字符串拷贝函数。

### 3.3 所有权规则

```text
客户端：读取自己的 profile，发送 ProfileToken，接收主机快照，展示主机结果
主机：验证 token，解析 profile，分配 PlayerID，唯一写入 MatchingPlayers，广播快照
网站：认证并返回公开资料，不参与 UE4 联机
```

客户端不得自行猜测、补写或覆盖其他玩家的 `MatchingPlayers`。

---

## 4. Phase 0：日志定位，不改变行为

### 4.1 新增快照日志

在 `PCPortSource/Aeyth8/Logic/AJB.h` 声明：

```cpp
void DumpMatchingPlayers(const char* Tag);
```

在 `PCPortSource/Aeyth8/Logic/AJB.cpp` 实现。每项至少打印：

```text
Tag / Map / IsServer / IsInSession / TMap 数量
数组索引 / Key / PlayerID / GameServerUserID
PlayerName / PlayerIconID / PlayerLevel / PlayerTitle
CharactorID / charaSkinId / standSkinId / EmoteData.Num()
```

不要以数组索引推断 PlayerID；这里只把索引作为诊断信息。

### 4.2 调用位置

在以下位置各打印一次：

| 位置 | Tag |
| --- | --- |
| `FlowUtilChangeState` 进入 `OutGame.SelectStartLocation` | `MP-OutGame-SelectStart` |
| `AJB::Server::PreLogin` 解析完 Options | `MP-PreLogin` |
| `AJB::Server::PostLogin` 开头和资料落库后 | `MP-PostLogin-Before` / `MP-PostLogin-After` |
| `UFunctions::InitLocalConnection` 追加选项后 | `MP-ClientJoin` |
| 玩家查询 Hook 入口和出口 | `MP-Hook-In` / `MP-Hook-Out` |
| 进入 `InGame.Standby` | `MP-InGameStandby` |
| `CheckForInfiniteLoadingScreen` 完成检查后 | `MP-CheckLoading` |
| 进入 `InGame.Gameplay` | `MP-Gameplay` |

主机 `PreLogin` 要把连接选项按 `?` 拆分逐条记录，确认现有版本选项仍然存在，并确认未来的 `ProfileToken=` 可以被读取。

### 4.3 验证

1. 用 `Proxy|x64` 编译。
2. 主机进入 `SimpleStartLocationSelect_P?listen`。
3. 客机连接，完成等待、落点选择和进入 `AJBStage01_P`。
4. 在 `Logs\Debug.log` 找到第一次出现默认资料的 Tag。

如果资料在 Hook 入口完整、出口丢失，进入 Phase 1；如果在 `PostLogin` 已经是默认值，进入 Phase 3；如果切图前完整、切图后丢失，重点进入 Phase 6。
如果资料在 Hook 入口完整、出口丢失，进入 Phase 1；如果在 `PostLogin` 已经是默认值，进入 Phase 3；如果切图前完整、切图后丢失，重点进入 Phase 6。

---

## 5. Phase 1：修复玩家资料查询 Hook

文件：`PCPortSource/Aeyth8/Logic/AJB.cpp`

### 5.1 首轮 A/B 测试

先暂时禁用：

```cpp
Hooks::CreateAndEnableHook(
    OFF::TryGetMatchingPlayerInfo,
    TryGetMatchingPlayerInfoByPlayerIDPureFunction
);
```

让原版函数读取完整资料。如果 UI 和战斗日志恢复正常，说明现有 Hook 是直接破坏源。

### 5.2 保留 Hook 时的规则

实现顺序固定为：

1. 检查 `This` 和 `Out`。
2. 优先调用原版函数。
3. 原版失败后，使用 `This->MatchingPlayers` 回退。
4. 回退按 `Candidate.PlayerID` 或稳定 Key 查找，禁止 `PlayerID - 1`。
5. 找不到时返回 `false`。
6. 复制完整字段，不只复制 `PlayerName`、`CharactorID` 和 `charaSkinId`。

优先使用：

```cpp
UBP_AJBGameInstance_C::FindMatchingPlayerInfoFromPlayerID
```

因为该 Blueprint 接口由游戏自身处理结构复制。若确实需要手动构造，使用 `CopyMatchingPlayerInfoSafely`：

```text
逐个复制 PlayerID、TeamID、IconID、Level、CharactorID、Progress、StartLocation、Rate
使用 CopyString 复制 GameServerUserID、TeamHostUserID、PlayerName、PlayerTitle
逐个创建 FCustomData 字段
逐个创建 FEmoteData 字段并 CopyString EmoteName/VoiceName
```

不得使用整体赋值、`memcpy` 或把 SDK 容器的内部指针复制到另一个结构。

---

## 6. Phase 2：本地 profile 和 PlayerLoginInfo

### 6.1 文件布局

创建：

```text
<游戏目录>\Aeyth8\Configs\profiles\PCPORT-SESSION-001.json
```

使用 UTF-8 JSON。推荐使用单独的 header-only JSON 解析器放在 `PCPortSource/Aeyth8/ThirdParty/`，或实现经过测试的严格对象解析器；不得用未处理转义、嵌套和 UTF-8 的脆弱字符串截取。

固定 schema：

```json
{
  "account_id": "PCPORT-SESSION-001",
  "name": "PlayerName",
  "icon_id": 1,
  "level": 50,
  "title": "PlayerTitle",
  "custom_data": {
    "chara_skin_id": 1,
    "stand_skin_id": 1,
    "kill_count": 0,
    "emotes": []
  }
}
```

解析失败、字段缺失、数值越界或 `account_id` 不一致时，profile 无效，不能生成半填充的玩家资料。

### 6.2 本地加载接口

新增：

```text
ProfileRecord LoadLocalProfile(account_id)
bool ApplyProfileToPlayerLoginInfo(const ProfileRecord& Profile)
```

初始化时填充：

```text
PlayerLoginInfo.SessionID
PlayerLoginInfo.UserDataID
PlayerLoginInfo.MatchingPlayerInfo.PlayerName
PlayerLoginInfo.MatchingPlayerInfo.GameServerUserID
PlayerLoginInfo.MatchingPlayerInfo.PlayerIconID
PlayerLoginInfo.MatchingPlayerInfo.PlayerLevel
PlayerLoginInfo.MatchingPlayerInfo.PlayerTitle
PlayerLoginInfo.CustomData
```

不要恢复旧代码中把所有值设成 `Aeyth8`、`ElSev` 或固定头像的调试行为。

### 6.3 验证

- 单机离线启动能读到 profile。
- 两个进程使用不同 JSON 后，本地日志中名称、头像、等级、称号和皮肤不同。
- profile 错误不会崩溃，也不会生成 `NO NAME` 作为成功结果。

---

## 7. Phase 3：主机权威 MatchingPlayers

### 7.1 连接资料

扩展 `FAJBNetConnection`，至少包含：

```text
Connection
ProfileToken
AccountId
ProfileResolved
ProfileFailed
ProfileRecord
AssignedPlayerID
CharacterID
CharacterSkin
```

普通字符串保存在 `std::string/std::wstring`，不要让连接表保存临时 `SDK::FString` 指针。

### 7.2 加入流程

客户端在 `UFunctions::InitLocalConnection` 追加：

```text
ProfileToken=<short-lived-token>
```

同时保留现有 DLL 版本选项和密码选项。

主机流程固定为：

```text
PreLogin：解析 token、检查格式、暂存连接资料
Login：保留现有版本和管理员校验
PostLogin/Tick：解析 profile，构造完整 FMatchingPlayerInfo
主机：AddMatchingPlayerInfo(stable_account_key, Info)
主机：广播完整快照
```

HTTP 或文件读取不能直接在 `PreLogin` 中执行。

### 7.3 PlayerID 规则

新增 `SessionPlayerRegistry`：

- `account_id -> PlayerID` 在一个房间会话内稳定。
- 相同账号重连恢复原 PlayerID。
- 新账号使用下一个未使用 ID。
- 断线玩家的 ID 保留到会话结束，避免正在游戏的玩家重排。
- 同一账号重复加入时拒绝新连接或明确替换旧连接，不能创建两个同 Key 玩家。
- 地图切换不清空会话注册表。

### 7.4 验证

- 主机 `MP-PostLogin-After` 中每项含完整真实资料。
- 客机最终快照与主机一致。
- 三人房间中 2 号退出，1/3 号继续；列表和战斗资料不丢失。
- 2 号以同一账号重连后恢复原 PlayerID。

---

## 8. Phase 4：主机到客户端快照同步

### 8.1 传输选择

不启用当前 RedirectURL 自定义消息系统作为主要资料同步方式。使用：

```cpp
APlayerController::ClientTeamMessage
```

客户端已有入口：

```cpp
UFunctions::ClientTeamMessageImplementation
```

### 8.2 固定消息格式

```text
AJB_MP_SYNC|1|<SnapshotId>|<ChunkIndex>/<ChunkCount>|<Base64UrlPayload>
```

约束：

- 版本固定为 `1`。
- Payload 是完整玩家数组的 UTF-8 JSON，再转 Base64URL。
- 单片 payload 不超过 350 字节。
- 最大 64 片、最大完整快照 64 KiB。
- 客户端按 SnapshotId 重组，5 秒超时后丢弃。
- SnapshotId 不是 PlayerID，也不能用连接顺序生成。
- 消息使用专用 `FName` 类型 `AJBProfileSync`，不得显示在聊天 UI。

### 8.3 快照字段

每个玩家至少包含：

```text
account_id
player_id
game_server_user_id
team_id
team_host_user_id
name
icon_id
level
title
character_id
chara_skin_id
stand_skin_id
kill_count
emotes
in_game_progress
start_location
b_is_camera_mode
rate
```

### 8.4 客户端应用

新增：

```text
SyncMatchingPlayersToClients()
HandleMatchingPlayersSyncMessage()
ApplyMatchingPlayersSnapshot()
```

快照完整且校验通过后：

1. 在游戏线程调用 `ClearMatchingPlayerInfo(false)`。
2. 逐项构造安全的 `FMatchingPlayerInfo`。
3. 用 `AddMatchingPlayerInfo` 写入本地 GameInstance。
4. 调用 `DumpMatchingPlayers` 验证。

只有以 `AJB_MP_SYNC|` 开头且类型为 `AJBProfileSync` 的消息才由自定义处理器消费；所有普通消息必须继续调用原版实现。

主机在以下时机广播：

```text
玩家资料解析完成
玩家加入或退出
角色选择完成
进入 OutGame.SelectStartLocation
进入战斗地图
进入 InGame.Standby 前
```

---

## 9. Phase 5：角色选择和地图切换

### 9.1 角色数据边界

网站 profile 只提供账号资料和 `CustomData`。以下内容由游戏本地流程决定：

```text
CharacterID
StartLocation
ReadyState
InGameProgressID
```

### 9.2 角色写入

优先使用：

```cpp
TryUpdateCustomDataAndCharacterIDByUserID
SetCharacterIDFromPlayerName
```

写入时机必须早于 `InGame.Standby` 检查。没有有效角色时，按照游戏允许的明确默认角色写入，并记录原因；不能保留 `CharactorID = 0`。

`TryFixInfiniteLoadingScreen()` 只能处理意外缺失的角色对象，不能通过 `DebugCharacterChange()` 掩盖资料表错误。

### 9.3 跨地图缓存

新增普通 C++ 会话缓存保存主机的 `SessionPlayerRegistry` 和完整 `ProfileRecord`。地图切换后：

```text
读取会话缓存
  -> 重新构造完整 FMatchingPlayerInfo
  -> AddMatchingPlayerInfo
  -> 广播快照
  -> 再进入 InGame.Standby
```

必须检查所有会清空或重建 `MatchingPlayers` 的路径，尤其是 `ClearMatchingPlayerInfo`、`Browse` 和战斗地图初始化。

### 9.4 验证

- `AJBStage01_P` 中主机和客机都拥有相同数量的玩家。
- 所有有效玩家的 `CharactorID` 不为 0。
- `CheckForInfiniteLoadingScreen` 输出 `No errors detected.`。
- 状态机自然进入 `InGame.Gameplay`，不依赖强制 Debug 修复。
- `CheckForInfiniteLoadingScreen` 输出 `No errors detected.`。
- 状态机自然进入 `InGame.Gameplay`，不依赖强制 Debug 修复。

---

## 10. Phase 6：HTTP Provider 和本地 Web 服务

### 10.1 本地服务目录

新增：

```text
ProfileService/server.py
ProfileService/accounts.json
ProfileService/profiles/*.json
ProfileService/README.md
```

只使用 Python 标准库，不引入第三方运行时依赖。

启动：

```powershell
py ProfileService\server.py --host 0.0.0.0 --port 8080
```

### 10.2 API 契约

`POST /login`

请求：

```json
{
  "account_id": "PCPORT-SESSION-001",
  "password": "test-password"
}
```

响应：

```json
{
  "token": "short-lived-token",
  "expires_at": "2026-01-01T00:00:00Z"
}
```

`GET /profile?token=<token>` 返回完整公开 profile JSON，不返回密码。

`POST /save-profile` 接受已验证 token 和 profile 更新，用于本地测试资料保存；非法字段必须拒绝。

服务必须处理：

```text
无效账号 / 无效密码 / 过期 token / 未知 token
不存在的 profile / 错误 JSON / 请求方法错误
```

### 10.3 C++ HTTP Provider

新增 `HttpProfileProvider`：

- 使用 WinHTTP。
- 默认地址 `http://127.0.0.1:8080`。
- 连接和读取超时必须有限制，建议 3 秒。
- 网络请求在工作线程执行。
- 工作线程只返回 `ProfileRecord` 或错误状态。
- 游戏线程负责把结果应用到 GameInstance。
- HTTP 失败时不生成伪造的默认账号资料；按明确策略返回 guest 或拒绝加入，并写日志。

在 `PCPortSource/JoJo.vcxproj` 的 `Proxy|x64` 和 `Injectable|x64` 配置加入 `winhttp.lib`，不改变其他链接库或 offset。

### 10.4 Provider 选择

```text
Phase 2/3：LocalJsonProfileProvider
Phase 6：HttpProfileProvider
```

两者返回同一个 `ProfileRecord`，后续 `MatchingPlayers` 逻辑不应区分资料来源。

---

## 11. Phase 7：清理和加固

1. `GetNetID()` 优先调用原版函数；必要时返回当前 profile 的稳定 `account_id`，移除硬编码 `Aeyth8`。
2. 复查 `AJB::ClientConnections` 的新增字段在断线、重连和会话结束时清理。
3. 保留关键 `MatchingPlayers` 和加载检查日志，并由 `bDebugModeFromCMLA` 控制高频日志。
4. 检查 `TryGetMatchingMyPairInfo` 是否仍返回完整资料。
5. 确认 `PlayerLoginInfo`、主机会话缓存和客户端快照不会互相覆盖。
6. 运行 `rg` 确认没有修改 SDK、offset 或禁用的原版函数地址。

---

## 12. 构建和运行

### 构建

Visual Studio 2022、v143、x64：

```powershell
msbuild D:\oldMAGUS\AJB-main\JoJo.sln /p:Configuration=Proxy /p:Platform=x64
msbuild D:\oldMAGUS\AJB-main\JoJo.sln /p:Configuration=Injectable /p:Platform=x64
```

Proxy 产物：

```text
x64\Proxy\dxgi.dll
```

### 部署

把 `x64\Proxy\dxgi.dll` 放入游戏：

```text
<GameDir>\AJB\Binaries\Win64\dxgi.dll
```

使用 `Launcher\AJBLauncher.exe` 或现有启动方式，保留版本校验和调试参数。

### 双进程测试

```text
主机：进入 SimpleStartLocationSelect_P?listen
客机：连接主机 IP
流程：大厅 -> 等待列表 -> 角色选择 -> 选择落点 -> AJBStage01_P
日志：<GameDir>\AJB\Binaries\Win64\Logs\Debug.log
```

可用控制台命令：

```text
matchingplayers
connections
netid
battle
```

---

## 13. 验收标准

| 编号 | 场景 | 通过条件 |
| --- | --- | --- |
| A1 | 双人 Listen Server 完整流程 | 战斗地图存在两项完整 `MatchingPlayers` |
| A2 | 等待列表 | 双方看到真实 Name、IconID、Level、Title |
| A3 | 战斗加载 | 无无限加载，检查结果为 `No errors detected.` |
| A4 | 角色同步 | 主机和客机的 CharacterID 一致且不为 0 |
| A5 | 不同 profile | 姓名、头像、称号、等级和皮肤不串号 |
| A6 | 地图切换 | `AJBStage01_P` 保留等待阶段资料 |
| A7 | 断线重连 | 原玩家恢复原 PlayerID，其他玩家不丢资料 |
| A8 | 新玩家加入 | 已有玩家资料不被覆盖或重排 |
| A9 | 离线流程 | 原有 Offline 流程不被破坏 |
| A10 | HTTP 异常 | 过期 token、超时和错误 JSON 有明确失败日志且不崩溃 |
| A11 | 本地服务 | `/login`、`/profile`、`/save-profile` 和错误状态可用 |
| A12 | 构建 | `Proxy|x64` 与 `Injectable|x64` 均编译通过 |
| A13 | 约束检查 | SDK、offset、原有文件名未被修改 |

---

## 14. 最终交付检查

执行 AI 完工时必须报告：

```text
修改了哪些文件
每个 Phase 的日志结论
Proxy 和 Injectable 的构建结果
本地 Web 服务启动和测试结果
双进程 Listen Server 测试结果
断线重连测试结果
是否仍出现 NO NAME / IconID=-1 / CharactorID=0
是否修改过 SDK 或 offset
```

未通过的验收项不得标记为完成，也不能用 `TryFixInfiniteLoadingScreen()` 或默认资料掩盖失败。
