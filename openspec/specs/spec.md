# YXK_GameUpdate — 技术规格方案（Spec）

> **文档版本**：1.0.0 | **引擎**：UE 5.7 | **平台**：Windows / Android / iOS | **日期**：2026-04-24

---

## 1. 系统概述

### 1.1 产品定位

UE 5.7 运行时热更新（OTA）插件，提供：运行时 Pak/IoStore 动态下载挂载、容器级增量更新、版本管理与链式补丁、编辑器打包工具链、跨平台下载器、最小包模式、CI/CD 集成。

### 1.2 核心设计原则

| 原则 | 描述 |
|------|------|
| 容器级粒度 | 增量计算在 IoStore/Pak 容器级别，ChunkId + SHA1 比对 |
| 两步版本发现 | `latest.json`（固定 URL）→ `manifest.json`（动态 URL） |
| 平台编译期选择 | `#if PLATFORM_XXX` 宏选择下载器实现 |
| 状态机驱动 | 6 状态 + 2 终态管理更新流程 |
| GameInstanceSubsystem | 核心管理器生命周期与游戏实例绑定 |
| 异步构建管道 | `Async(EAsyncExecution::Thread)` + `TWeakObjectPtr` 安全回调 |

### 1.3 系统架构图

```
CDN/文件服务器 (latest.json, manifest.json, *.utoc/*.ucas/*.pak)
        │ HTTP
        ▼
UHotUpdateManager (GameInstanceSubsystem)
├── VersionStorage (本地 version.json + manifest.json)
├── ManifestParser (JSON 解析/序列化)
├── PakManager (Pak/IoStore 挂载/卸载/校验)
└── Downloader (平台特定：HTTP / Android JNI / iOS NSURLSession)
        │
        ▼
UE 5.7 Runtime (FPakPlatformFile / FIoStoreReader / AssetRegistry)
```

---

## 2. 模块架构

| 模块 | 类型 | 加载阶段 | 源文件 | 职责 |
|------|------|----------|--------|------|
| **HotUpdate** | Runtime | PreDefault | 25 | 版本检查、下载、Pak 挂载 |
| **HotUpdateEditor** | Editor | Default | 39+ | 基础包/补丁包构建、版本对比 |
| **GameUpdate** | Runtime | Default | 49 | 热更 UI、游戏变体 |
| **TestPlugin** | Runtime | Default | 2 | 测试插件 |

**依赖**：`GameUpdate →(private) HotUpdate`；`HotUpdateEditor →(public) HotUpdate`

---

## 3. HotUpdate 运行时模块

### 3.1 UHotUpdateManager — 中枢调度器

**文件**：`Core/HotUpdateManager.h/.cpp` (791行) | **父类**：`UGameInstanceSubsystem`

**公开接口**：

| 方法 | 描述 |
|------|------|
| `Initialize(FSubsystemCollectionBase&)` | 创建 PakManager/VersionStorage/Downloader |
| `CheckForUpdate()` | 两步版本发现协议 |
| `StartDownload()` | 开始下载增量容器 |
| `PauseDownload() / ResumeDownload() / CancelDownload()` | 下载控制 |
| `ApplyUpdate()` | 挂载 Pak/IoStore 容器 |
| `RetryUpdate()` | 从失败状态重试 |
| `GetCurrentState()` → `EHotUpdateState` | 获取状态 |
| `GetCurrentProgress()` → `FHotUpdateProgress` | 获取进度 |
| `CleanupOldVersions()` | 清理旧版本 |

**6 个事件委托**：`OnStateChanged`, `OnProgress`, `OnError`, `OnVersionFound`, `OnCompleted`, `OnNoUpdate`

**核心流程**：

```
Initialize → 创建子对象 → SetState(Idle)
CheckForUpdate → HTTP GET latest.json → HTTP GET manifest.json
    → ParseManifest → CalculateIncrementalDownload → SetState(UpdateAvailable)
StartDownload → Downloader.AddContainerDownloadTasks → 并发下载
    → SHA1校验 → SetState(Installing)
ApplyUpdate → PakManager.Mount → VersionStorage.Save → SetState(Success)
```

**增量计算**：遍历远程 Manifest 容器，按 ChunkId 匹配本地容器，Hash 不同则加入下载列表。

### 3.2 UHotUpdatePakManager — Pak 挂载管理器

| 方法 | 描述 |
|------|------|
| `MountPak(PakPath, PakOrder)` | 挂载 .pak |
| `MountIoStore(UtocPath, UcasPath)` | 挂载 IoStore |
| `UnmountPak/UnmountIoStore` | 卸载 |
| `VerifyPakFile(PakPath, ExpectedHash)` | SHA1 校验 |
| `RegisterEncryptionKey(Guid, Key)` | 注册加密密钥 |

**PakOrder**：`BasePakOrder(100) + ChunkId * PakOrderStep(1)`，确保热更 Pak 优先级高于基础包。

### 3.3 UHotUpdateVersionStorage — 版本持久化

存储路径：`{ProjectSavedDir}/HotUpdate/`，管理 `version.json` 和 `manifest.json`。

### 3.4 UHotUpdateManifestParser — Manifest 解析

解析/序列化 Manifest JSON。兼容 `"containers"` 和 `"chunks"` 字段名，支持 ManifestVersion 2-4。

### 3.5 下载器体系

**工厂方法**（编译期平台选择）：
```
CreateDownloader() →
  PLATFORM_ANDROID → UHotUpdateAndroidDownloader (JNI DownloadManager, 后台下载)
  PLATFORM_IOS     → UHotUpdateIOSDownloader (NSURLSession background)
  默认              → UHotUpdateHttpDownloader (UE FHttpModule, 并发HTTP)
```

**基类 UHotUpdateDownloaderBase** 提供：`AddDownloadTask`(纯虚), `AddContainerDownloadTasks`(默认实现), `StartDownload/Pause/Resume/Cancel`(纯虚), `UpdateProgressCalculation`(共享)

**UHotUpdateHttpDownloader** (466行)：并发下载 + Range Header 断点续传 + SHA1 校验 + 临时文件原子重命名 + 指数退避重试(Timer)

**UHotUpdateAndroidDownloader** (725行)：JNI 调用 Android DownloadManager → `enqueue()`入队 → Timer 0.5s 轮询 `query()` Cursor 查状态 → Range Header 续传 → 系统通知栏

**UHotUpdateIOSDownloader**：NSURLSession background transfer，系统自动管理生命周期。

### 3.6 UHotUpdateAssetManager — 自定义资产管理器

重写 `GetPackageChunkIds()`：白名单资产 → Chunk 0（随安装包），其余 → Chunk 11（热更下载）。通过 `MinimalPackageConfig.json` 配置白名单，带缓存。

### 3.7 UHotUpdateSettings — 运行时配置

**父类**：`UDeveloperSettings` | **Config**：Game

| 分类 | 关键配置 |
|------|---------|
| 服务器 | `ManifestUrl`, `ResourceBaseUrl`, `bValidateUrlDomain`, `AllowedDomains` |
| 下载 | `MaxConcurrentDownloads(3)`, `MaxRetryCount(3)`, `RetryBaseDelay(2.0)`, `bEnableResumeDownload(true)`, `DownloadTimeoutSeconds(30)`, `bAutoDownload(false)` |
| 存储 | `StoragePath`, `bCleanupOldVersions(true)`, `MaxVersionsToKeep(2)` |
| 最小包 | `bMinimalPackage`, `MinimalPackageConfigPath` |

### 3.8 HotUpdateFileUtils — 文件工具

`CalculateSHA1`(流式1MB分块), `BytesToHex`, `HexToBytes`, `EnsureDirectoryExists`, `IsEngineAsset`

---

## 4. HotUpdateEditor 编辑器模块

### 4.1 UHotUpdateBaseVersionBuilder — 基础包构建器 (41KB)

```
ValidateConfig → PrepareOutputDirectory → CookAssets(UnrealEditor-Cmd)
→ AnalyzeChunks(ChunkManager) → BuildContainers(IoStoreBuilder)
→ GenerateManifest(manifest.json + filemanifest.json) → RegisterVersion
```

**最小包模式**：Chunk 0 = 白名单 + 递归依赖，Chunk 11 = 其余资源。

### 4.2 UHotUpdatePatchPackageBuilder — 补丁包构建器 (1822行)

**配置**：`FHotUpdatePatchPackageConfig` — Version, BaseVersion, Platform, bChainPatch, bFullHotUpdate, bIncrementalCook

```
ValidateConfig → CollectAssets(PackagingSettings + Cooked目录)
→ LoadBaseManifest(优先filemanifest.json)
→ ComputeDiff(Added/Modified/Deleted/Unchanged)
→ [可选] IncrementalCook(仅Cook变更资产)
→ [可选] FullHotUpdate(包含基础容器)
→ [可选] ChainPatch(基于前一版本补丁)
→ BuildContainers → GenerateManifest(含DiffSummary) → RegisterVersion
```

**Diff 算法**：遍历当前资产按 Hash 对比基础 Manifest，分类为 Added/Modified/Deleted/Unchanged。

### 4.3 UHotUpdateCustomPackageBuilder (467行)

独立于热更流程的自定义打包，支持 uasset + 非资产文件，用于快速测试。

### 4.4 UHotUpdateIoStoreBuilder (25KB)

调用 UnrealPak 创建容器：生成 ResponseFile → 构造命令行参数(`-IoStore -compress -compressionformat=Oodle`) → 执行 → 验证输出。

### 4.5 FHotUpdateChunkManager (316行)

| 策略 | 描述 |
|------|------|
| `None` | 所有资产 → Chunk 0 |
| `Size` | 按 `MaxChunkSizeMB` 自动分割，资产排序保证确定性 |

### 4.6 UHotUpdateDiffTool (602行)

三种对比模式：`CompareDirectories`(文件级), `CompareManifests`(容器/资产级), `ComparePakFiles`(Pak内部)。输出 `FHotUpdateDiffReport`(Added/Modified/Deleted/Unchanged 列表 + 统计)。

### 4.7 FHotUpdatePackageHelper (380行)

静态工具：`CompileProject`(UBT), `CookAssets`(UnrealEditor-Cmd), `GetAssetDiskPath`(项目/引擎/插件路径统一解析), `ConvertAssetPathToFileName`/`FileNameToAssetPath`

### 4.8 FHotUpdatePackagingSettingsHelper (350行)

解析 `UProjectPackagingSettings`：MapsToCook, DirectoriesToAlwaysCook, NeverCook 过滤, 依赖递归收集。

### 4.9 UHotUpdateVersionManager (425行)

版本注册表 JSON 持久化：RegisterVersion, GetVersionChain, CompareVersions(SemVer)。

### 4.10 FHotUpdateAssetFilter (368行)

白名单路径前缀匹配 + 递归依赖收集(Hard/Soft/All 策略，通过 AssetRegistry)。

### 4.11 UHotUpdateCommandlet (19.7KB)

CI/CD 命令行入口：`-mode=base/patch`, `-version`, `-platform`, `-baseversion`, `-minimal`, `-whitelist`, `-chunkstrategy`, `-incrementalcook`, `-chainpatch`, `-fullhotupdate`

### 4.12 编辑器 UI — SHotUpdateMainWindow

5 标签页(FTabManager)：基础包构建 / 补丁包构建 / 自定义打包 / 版本对比 / Pak 查看器。通过 NomadTabSpawner 注册，集成到 Tools 菜单。

---

## 5. GameUpdate 游戏模块

### 5.1 UHotUpdateWidget — 热更 UI

绑定 `UHotUpdateManager` 全部 6 个事件委托，状态驱动界面切换：Idle→隐藏, CheckingVersion→加载中, UpdateAvailable→下载按钮, Downloading→进度条+速度, Paused→继续按钮, Installing→安装中, Success→完成, Failed→错误+重试。

### 5.2 Strategy 变体 — RTS 控制器 (818行)

`AStrategyPlayerController`：鼠标/触屏双模式、射线单击选择、拖动框选、单位移动/攻击指令、NavigationSystem 寻路、相机边缘滚动/拖拽/缩放。

### 5.3 TwinStick 变体 — 双摇杆射击

`ATwinStickCharacter`：左摇杆移动 + 右摇杆瞄准(推动自动射击) + 冲刺 + AoE 技能。
`ATwinStickGameMode`：得分×连击倍率、NPC 上限管理、波次生成、StateTree AI。

---

## 6. 数据结构与协议

### 6.1 枚举

```
EHotUpdateState: Idle | CheckingVersion | UpdateAvailable | Downloading | Paused | Installing | Success | Failed
EHotUpdateError: None | NetworkError | ServerError | ParseError | DownloadError | VerificationFailed | MountFailed | InsufficientStorage | InvalidVersion | Unknown
EHotUpdateContainerType: IoStore | Pak
EHotUpdatePackageKind: Base | Patch | Custom
EHotUpdatePlatform: Windows | Android | iOS
EHotUpdateChunkStrategy: None | Size
EHotUpdateDependencyStrategy: Hard | Soft | All
```

### 6.2 核心结构体

**FHotUpdateVersionInfo**：Version, BuildTime, Description, TotalSize, ContainerCount

**FHotUpdateProgress**：TotalBytes, DownloadedBytes, Percentage(0-100), DownloadSpeed(B/s), RemainingTime(s), CompletedTasks, TotalTasks

**FHotUpdateContainerInfo**：ChunkId, ContainerType, Hash(SHA1), CompressedSize, UncompressedSize, UtocPath/UcasPath/UtocSize/UcasSize(IoStore), PakPath/PakSize(Pak)

**FHotUpdateManifest**：ManifestVersion(2-4), Version, Platform, BuildTime, Description, PackageKind, BaseVersion, Containers[], DiffSummary, TotalSize

**FHotUpdateVersionCheckResult**：bUpdateAvailable, CurrentVersion, LatestVersion, VersionInfo, TotalDownloadSize, ContainersToDownload

**FHotUpdateDiffReport**：Added[], Modified[], Deleted[], Unchanged[] (每项为 FHotUpdateAssetDiff: Path, OldHash, NewHash, OldSize, NewSize, DiffType)

### 6.3 网络协议

**latest.json**：
```json
{ "version": "1.0.1", "manifestUrl": "https://cdn/1.0.1/manifest.json",
  "timestamp": "...", "description": "...", "forceUpdate": false, "minVersion": "1.0.0" }
```

**manifest.json** (v4)：
```json
{ "manifestVersion": 4, "version": "1.0.1", "platform": "Windows",
  "packageKind": "patch", "baseVersion": "1.0.0",
  "containers": [
    { "chunkId": 1, "containerType": "IoStore", "hash": "sha1...",
      "utocPath": "pakchunk1-Windows.utoc", "ucasPath": "pakchunk1-Windows.ucas", ... }
  ],
  "diffSummary": { "added": 15, "modified": 32, "deleted": 3 } }
```

**filemanifest.json**（文件级）：
```json
{ "version": "1.0.1", "files": [
    { "path": "/Game/Maps/Level01.uasset", "hash": "...", "size": 1048576, "chunkId": 1 } ] }
```

---

## 7. 状态机

```
Idle ──CheckForUpdate──▶ CheckingVersion
    CheckingVersion ──无更新──▶ Idle
    CheckingVersion ──有更新──▶ UpdateAvailable
    CheckingVersion ──错误──▶ Failed
UpdateAvailable ──StartDownload──▶ Downloading
    Downloading ──Pause──▶ Paused ──Resume──▶ Downloading
    Downloading ──完成+校验──▶ Installing
    Downloading ──失败──▶ Failed
    Paused ──Cancel──▶ Idle
Installing ──成功──▶ Success
Installing ──失败──▶ Failed
Failed ──Retry──▶ Idle
```

---

## 8. 平台差异

| 特性 | Windows (HTTP) | Android (JNI) | iOS (NSURLSession) |
|------|---------------|---------------|---------------------|
| 下载引擎 | UE FHttpModule | Android DownloadManager | NSURLSession background |
| 后台下载 | ❌ | ✅ | ✅ |
| 断点续传 | Range Header | Range Header | 系统自动 |
| 并发控制 | MaxConcurrentDownloads | 系统管理 | 系统管理 |
| 重试 | 指数退避 Timer | 系统级 | 系统级 |
| 进度查询 | 实时回调 | Cursor 轮询(0.5s) | 委托回调 |

---

## 9. 配置规格

**DefaultEngine.ini**：`AssetManagerClassName=/Script/HotUpdate.HotUpdateAssetManager`

**DefaultGame.ini**：ManifestUrl, ResourceBaseUrl, MaxConcurrentDownloads(3), bGenerateChunks=True, Oodle Kraken 压缩

**MinimalPackageConfig.json**：`{ "whitelist": [...], "defaultChunkId": 11, "whitelistChunkId": 0 }`

---

## 10. CI/CD 集成

**Commandlet**：`UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=base/patch ...`

**UAT 自动化**：`StripExtraPakChunks.Automation.cs` — CustomStagingHandler，Staging 后将 pakchunk1+ 移至热更输出目录。参数：`-MinimalPackage`, `-HotUpdateOutputDir=<path>`

---

## 11. 安全机制

| 机制 | 描述 |
|------|------|
| URL 域名白名单 | `bValidateUrlDomain` + `AllowedDomains` 配置 |
| SHA1 文件校验 | 下载后 + 挂载前双重校验 |
| Pak 加密 | `RegisterEncryptionKey(Guid, Key)` |
| 临时文件安全 | `.tmp` 写入 → SHA1 校验 → 原子重命名 |
| Manifest 校验 | 解析时验证必需字段和格式 |

---

## 12. 扩展点与设计模式

| 模式 | 应用 | 扩展方式 |
|------|------|---------|
| 工厂模式 | 下载器创建 | 新增 `#elif PLATFORM_XXX` + 子类 |
| 策略模式 | Chunk 分配 | 新增枚举值 + 实现分配逻辑 |
| 观察者模式 | Manager 事件委托 | 绑定 6 个 Multicast Delegate |
| 模板方法 | 构建器基类 | 子类重写特定步骤 |
| 子系统模式 | GameInstanceSubsystem | UE 原生生命周期管理 |
| 异步任务 | Async + TWeakObjectPtr | 线程安全回调到 GameThread |
