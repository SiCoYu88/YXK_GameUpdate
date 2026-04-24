# CODEBUDDY.md This file provides guidance to CodeBuddy when working with code in this repository.

## Project Overview

UE 5.7 热更新（OTA）插件项目，提供运行时 Pak/IoStore 动态下载挂载、增量更新、版本管理和编辑器打包工具。支持 Windows、Android、iOS 三平台。

## Build & Run Commands

### 生成项目文件

```bash
# 右键 GameUpdate.uproject → "Generate Visual Studio project files"
# 或使用命令行：
UnrealBuildTool -projectfiles -project=GameUpdate.uproject -game -engine
```

### 编译项目

```bash
# Development Editor（日常开发）
UnrealBuildTool GameUpdateEditor Win64 Development -project=GameUpdate.uproject

# Shipping（发布）
UnrealBuildTool GameUpdate Win64 Shipping -project=GameUpdate.uproject
```

### 构建基础包

```bash
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows
```

### 构建增量补丁

```bash
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=patch -version=1.0.1 -baseversion=1.0.0 -platform=Windows
```

### 最小包模式打包

```bash
UnrealEditor-Cmd GameUpdate -run=HotUpdate -mode=base -version=1.0.0 -platform=Windows -minimal -whitelist="/Game/UI;/Game/Startup"
```

### 部署热更文件

```bash
pip install paramiko
python upload_hotpatch.py
```

### 调试日志

在 `Config/DefaultGame.ini` 中添加：
```ini
[Core.Log]
LogHotUpdate=Verbose
LogHotUpdateEditor=Verbose
```

## Architecture

### 模块结构

项目包含 4 个 C++ 模块和 2 个 Build Target：

| 模块 | 类型 | 加载阶段 | 职责 |
|------|------|----------|------|
| **GameUpdate** | Runtime | Default | 游戏主模块，包含 UMG 热更 UI 和游戏变体（Strategy/TwinStick） |
| **HotUpdate** | Runtime | PreDefault | 运行时热更核心：版本检查、下载、Pak 挂载 |
| **HotUpdateEditor** | Editor | Default | 编辑器打包工具：基础包/补丁包构建、版本对比、Pak 查看 |
| **TestPlugin** | Runtime | Default | 最小化测试插件 |

**模块依赖关系**：`GameUpdate` → (private) `HotUpdate`；`HotUpdateEditor` → (public) `HotUpdate`。HotUpdate 依赖 PakFile、Json、HTTP、AssetRegistry；HotUpdateEditor 额外依赖 UnrealEd、Slate、ContentBrowser 等编辑器模块。

### 运行时核心（HotUpdate 模块）

**UHotUpdateManager**（GameInstanceSubsystem）是中枢调度器，管理完整热更流程：

```
Initialize() → 创建 PakManager + VersionStorage + Downloader
CheckForUpdate() → HTTP GET latest.json → 二次请求 manifest.json → CalculateIncrementalDownload()
StartDownload() → Downloader.AddContainerDownloadTasks() → 并发下载
ApplyUpdate() → PakManager.MountPak() → VersionStorage.SaveLocalVersion()
```

状态机：`Idle → CheckingVersion → UpdateAvailable → Downloading → Paused → Installing → Success/Failed`

**版本发现（两步协议）**：`ManifestUrl` 指向 `latest.json`（固定 URL），其中包含 `manifestUrl` 字段指向实际 `manifest.json`。服务端只需更新 `latest.json` 即可发布新版本。

**增量下载**：在容器级别（IoStore .utoc/.ucas 对或 .pak 文件）计算差异，按 ChunkId + SHA1 哈希比对，跳过未变更容器。不是单个资产文件级别。

**下载器体系（工厂模式 + 编译期平台选择）**：

```
UHotUpdateDownloaderBase::CreateDownloader()
├── #if PLATFORM_ANDROID → UHotUpdateAndroidDownloader（JNI DownloadManager，后台下载）
├── #if PLATFORM_IOS     → UHotUpdateIOSDownloader（NSURLSession background transfer）
└── #else                → UHotUpdateHttpDownloader（UE FHttpModule，并发 HTTP）
```

所有下载器共享任务队列模式（PendingTasks → ActiveTasks → CompletedTasks），基类提供 `AddContainerDownloadTasks` 默认实现（遍历调用 `AddDownloadTask`），子类只需重写单任务接口。

**其他运行时核心类**：
- `UHotUpdatePakManager`：Pak/IoStore 挂载/卸载/校验，加密密钥注册
- `UHotUpdateVersionStorage`：本地 version.json + manifest.json 持久化
- `UHotUpdateManifestParser`：Manifest JSON 序列化/反序列化（ManifestVersion=2）
- `UHotUpdateAssetManager`（继承 UAssetManager）：重写 Chunk 分配，读取 `MinimalPackageConfig.json` 配置
- `UHotUpdateSettings`（Config=Game）：服务器 URL、下载参数、存储路径、最小包配置
- `UHotUpdateFileUtils`：SHA1 计算、Hex 转换、引擎资产判断

### 编辑器工具（HotUpdateEditor 模块）

**构建器**：
- `UHotUpdateBaseVersionBuilder`：基础包构建（完整项目打包），生成 manifest.json + filemanifest.json，支持最小包模式
- `UHotUpdatePatchPackageBuilder`：差异补丁构建，支持链式 Patch、全量热更、增量 Cook
- `UHotUpdateCustomPackageBuilder`：自定义打包（独立于热更流程，只 Cook 指定资源）
- `UHotUpdateIoStoreBuilder`：调用 UnrealPak 创建 .utoc/.ucas 容器

**管理器**：
- `FHotUpdateChunkManager`：Chunk 分配策略（None / Size）
- `UHotUpdateVersionManager`：版本注册表 JSON 持久化
- `FHotUpdateAssetFilter`：白名单匹配 + 依赖收集（最小包）

**工具类**：
- `FHotUpdatePackageHelper`：编译、Cook、路径解析（静态）
- `FHotUpdatePackagingSettingsHelper`：读取 UProjectPackagingSettings
- `UHotUpdateDiffTool`：目录/Manifest/Pak 差异对比
- `UHotUpdateCommandlet`：CLI 打包入口（`-mode=base/patch`），CI/CD 集成

**编辑器 UI（Slate）**：`SHotUpdateMainWindow` 通过 FTabManager 管理 5 个子面板：BaseVersionPanel、PackagingPanel、CustomPackagingPanel、VersionDiffPanel、PakViewerPanel。

### UAT 自动化脚本

`Build/AutomationScripts/StripExtraPakChunks.Automation.cs` 实现 `CustomStagingHandler`，在 Staging 后将 pakchunk1+ 移至热更输出目录。通过 `-MinimalPackage` 和 `-HotUpdateOutputDir=<path>` 命令行参数控制。`.Automation.csproj` 文件必须存在才能被 UAT 编译加载。

### 数据流

```
[编辑器打包]
资源 → Chunk 分配(ChunkMapping) → Cook → IoStore/Pak 构建 → manifest.json 生成 → 版本注册

[运行时更新]
CheckForUpdate(HTTP) → 解析 latest.json → 获取 manifest.json → 增量计算(容器级) → 下载(并发) → SHA1 校验 → 挂载容器 → 保存版本
```

### 游戏模块（GameUpdate）

- `UHotUpdateWidget`：运行时 UMG 热更 UI，绑定 HotUpdateManager 事件，状态驱动的界面切换
- 游戏变体：`Variant_Strategy/`（策略）和 `Variant_TwinStick/`（双摇杆射击）包含各自的 GameMode、Character、AI、UI

## Key Configuration

| 文件 | 关键配置 |
|------|----------|
| `Config/DefaultGame.ini` | HotUpdate 运行时设置（ManifestUrl、ResourceBaseUrl、bAutoDownload）、打包设置（bGenerateChunks） |
| `Config/DefaultEngine.ini` | `AssetManagerClassName=/Script/HotUpdate.HotUpdateAssetManager`（分包必需） |
| `Config/DefaultInput.ini` | EnhancedInput 系统配置 |

## Manifest Format (Version 2)

容器信息包含双格式支持：IoStore（utocPath/ucasPath + Hash）和传统 Pak（pakPath + Hash），均有 ChunkId 和 ContainerType 字段。热更包 Manifest 只包含 patch 容器（chunkId != 0）。

## Chunk 策略

| 策略 | 说明 |
|------|------|
| `None` | 不分包，所有资源一个 Chunk |
| `Size` | 按 MaxChunkSizeMB 自动分割 |

最小包模式：Chunk 0 = 白名单资源及所有依赖（随安装包），Chunk 11 = 其余资源（热更下载）。
