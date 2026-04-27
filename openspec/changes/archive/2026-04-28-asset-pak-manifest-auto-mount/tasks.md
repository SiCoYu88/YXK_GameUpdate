## 1. 数据结构与类型定义

- [x] 1.1 在 `HotUpdatePakTypes.h` 中新增 `FAssetPakInfo` 结构体（`FString PakPath`、`int32 ChunkId`），用于 Asset→Pak 映射查询结果
- [x] 1.2 在 `HotUpdatePakTypes.h` 中新增 `FAssetDependencyPakInfo` 结构体（`FString AssetPath`、`FString PakPath`、`int32 ChunkId`），用于依赖项的 Pak 信息
- [x] 1.3 在 `HotUpdatePakTypes.h` 中新增 `FAssetDependencyInfo` 结构体（`TArray<FString> RequiredPaks`、`TArray<FString> OptionalPaks`、`TArray<FAssetDependencyPakInfo> HardDeps`、`TArray<FAssetDependencyPakInfo> SoftDeps`）
- [x] 1.4 在 `HotUpdatePakTypes.h` 中新增 `FAutoMountAssetHandle` 结构体，包含 `TWeakObjectPtr<UHotUpdatePakManager>`、`TArray<FString> MountedPakPaths`、`TWeakObjectPtr<UObject> LoadedAsset`、`FString AssetPath`，实现移动语义和析构自动释放
- [x] 1.5 声明委托类型 `FOnAutoMountLoadComplete`（单个资源加载完成回调）和 `FOnAutoMountBatchComplete`（批量加载完成回调）

## 2. Asset-Pak Manifest 生成器（编辑器侧）

- [x] 2.1 新增 `HotUpdateAssetPakManifestGenerator.h/.cpp`（HotUpdateEditor 模块），声明 `FHotUpdateAssetPakManifestGenerator` 类，包含静态方法 `Generate(OutputDir, PakFiles, Version, Platform)` → `bool`
- [x] 2.2 实现 `Generate` 方法：遍历输出目录所有 .pak 文件，对每个 Pak 使用 `FPakFile` + `FFilenameIterator` 获取内部文件列表
- [x] 2.3 实现 Pak 内文件路径到 UE Asset 路径的转换逻辑（去除 Mount Point 前缀、去除 .uasset/.uexp/.ubulk 后缀、转换路径分隔符）
- [x] 2.4 构建 `paks` 数组（按 Pak 分组的 Asset 列表）和 `assetIndex`（Asset→Pak 平坦索引）
- [x] 2.5 将结果序列化为 `asset_pak_manifest.json`，写入 OutputDir

## 3. Asset 依赖信息收集器（编辑器侧）

- [x] 3.1 新增 `HotUpdateAssetDependencyCollector.h/.cpp`（HotUpdateEditor 模块），声明 `FHotUpdateAssetDependencyCollector` 类，包含静态方法 `Collect(OutputDir, AssetPakManifestPath, Version)` → `bool`
- [x] 3.2 实现 `Collect` 方法：加载 `asset_pak_manifest.json` 获取 Asset→Pak 映射，构建内存索引
- [x] 3.3 通过 AssetRegistry 对每个 Asset 查询 Hard 依赖（`GetDependencies` with `UE::AssetRegistry::EDependencyCategory::Package`）和 Soft 依赖
- [x] 3.4 对每个依赖 Asset，通过 Asset→Pak 映射关联其所在的 Pak 和 ChunkId
- [x] 3.5 预计算每个 Asset 的 `requiredPaks`（Hard 依赖关联的去重 Pak 列表）和 `optionalPaks`（Soft 依赖关联的去重 Pak 列表），排除 Asset 自身所在 Pak
- [x] 3.6 将结果序列化为 `asset_dependencies.json`，写入 OutputDir

## 4. 构建流程集成（编辑器侧）

- [x] 4.1 在 `UHotUpdateBaseVersionBuilder` 的构建流程中，在 `GenerateManifest()` 之后调用 `FHotUpdateAssetPakManifestGenerator::Generate()` 生成 Asset-Pak Manifest
- [x] 4.2 在 `UHotUpdateBaseVersionBuilder` 中，在 Asset-Pak Manifest 生成后调用 `FHotUpdateAssetDependencyCollector::Collect()` 生成依赖信息文件
- [x] 4.3 在 `UHotUpdatePatchPackageBuilder` 中同样追加 Manifest 和依赖信息生成步骤
- [x] 4.4 确保生成的 `asset_pak_manifest.json` 和 `asset_dependencies.json` 被包含在版本输出目录中，可随 manifest.json 一起部署

## 5. 运行时 Asset-Pak 映射查询（UHotUpdateAssetPakMapping）

- [x] 5.1 新增 `HotUpdateAssetPakMapping.h/.cpp`（HotUpdate 运行时模块），声明 `UHotUpdateAssetPakMapping` 类（UObject、BlueprintType）
- [x] 5.2 实现 `LoadManifest(ManifestDir)` — 读取 `asset_pak_manifest.json`，解析 JSON 构建 `TMap<FString, FAssetPakInfo> AssetToPakMap` 和 `TMap<FString, TArray<FString>> PakToAssetsMap`
- [x] 5.3 实现 `LoadDependencies(ManifestDir)` — 读取 `asset_dependencies.json`，构建 `TMap<FString, FAssetDependencyInfo> AssetDependencies`
- [x] 5.4 实现 `GetPakForAsset(AssetPath)` → `FString` — 从 AssetToPakMap 查找，返回 Pak 相对路径，未找到返回空串
- [x] 5.5 实现 `GetChunkIdForAsset(AssetPath)` → `int32` — 从 AssetToPakMap 查找 ChunkId，未找到返回 -1
- [x] 5.6 实现 `GetRequiredPaksForAsset(AssetPath)` → `TArray<FString>` — 从 AssetDependencies 获取 RequiredPaks，加上主资源自身的 Pak，去重返回
- [x] 5.7 实现 `GetOptionalPaksForAsset(AssetPath)` → `TArray<FString>` — 从 AssetDependencies 获取 OptionalPaks
- [x] 5.8 实现 `GetAssetsInPak(PakPath)` → `TArray<FString>` — 从 PakToAssetsMap 查找
- [x] 5.9 实现 `NormalizeAssetPath(AssetPath)` — 统一 Asset 路径格式（去除后缀、统一斜杠方向、大小写标准化）
- [x] 5.10 实现 `IsManifestLoaded()` → `bool`

## 6. 运行时自动挂载加载器（UHotUpdateAutoMountLoader）

- [x] 6.1 新增 `HotUpdateAutoMountLoader.h/.cpp`（HotUpdate 运行时模块），声明 `UHotUpdateAutoMountLoader` 类
- [x] 6.2 实现 `Initialize(PakManager, Mapping)` — 存储 PakManager 和 Mapping 引用
- [x] 6.3 实现 `AsyncLoadAsset(AssetPath, OnComplete)` 核心流程：
  - 调用 `Mapping->GetRequiredPaksForAsset(AssetPath)` 获取所有需要 Mount 的 Pak
  - 对每个 Pak 路径拼接本地完整路径后调用 `PakManager->RequestMount(FullPakPath)`
  - 记录成功 Mount 的 Pak 列表到 Handle
  - 使用 `FStreamableManager::RequestAsyncLoad` 执行异步加载
  - 加载完成回调中设置 Handle 的 LoadedAsset 并触发用户回调
- [x] 6.4 实现 `SyncLoadAsset(AssetPath)` — 同步版本：Mount 所有 Pak → `StaticLoadObject` → 返回 UObject*，内部创建引用追踪
- [x] 6.5 实现 `ReleaseAsset(AssetPath)` — 通过 AssetPath 查找关联的已 Mount Pak 列表，对每个调用 `PakManager->RequestUnmount`
- [x] 6.6 实现 `AsyncLoadAssets(AssetPaths, OnComplete)` 批量版本 — 收集所有 Asset 的 RequiredPaks 合并去重后批量 Mount，使用 `RequestAsyncLoad` 批量加载
- [x] 6.7 内部维护 `TMap<FString, FAutoMountTrackingInfo> ActiveLoads` 记录每个 AssetPath 对应的 Mounted Pak 列表，用于 ReleaseAsset 时知道要 Unmount 哪些 Pak

## 7. FAutoMountAssetHandle 实现

- [x] 7.1 实现 `FAutoMountAssetHandle` 构造函数（接受 PakManager、MountedPakPaths、LoadedAsset、AssetPath）
- [x] 7.2 实现移动构造/移动赋值 — 转移 Pak 引用所有权
- [x] 7.3 实现析构函数 — 检查 PakManager 有效性后对每个 MountedPakPaths 调用 `RequestUnmount`
- [x] 7.4 实现 `Release()` — 手动释放，Unmount 所有关联 Pak 并清空列表
- [x] 7.5 实现 `GetAsset()` → `UObject*` 和 `IsValid()` → `bool` 查询方法

## 8. HotUpdateManager 集成

- [x] 8.1 在 `UHotUpdateManager` 中新增 `UPROPERTY() UHotUpdateAssetPakMapping* AssetPakMapping` 和 `UPROPERTY() UHotUpdateAutoMountLoader* AutoMountLoader` 成员
- [x] 8.2 在 `UHotUpdateManager::Initialize()` 中创建 AssetPakMapping 和 AutoMountLoader 子对象
- [x] 8.3 在 `UHotUpdateManager::ApplyUpdate()` 成功后，调用 `AssetPakMapping->LoadManifest()` 和 `AssetPakMapping->LoadDependencies()` 加载 Manifest
- [x] 8.4 在 Manifest 加载成功后调用 `AutoMountLoader->Initialize(PakManager, AssetPakMapping)`
- [x] 8.5 新增面向业务层的便捷方法 `LoadAssetWithAutoMount(AssetPath, OnComplete)` — 转发到 AutoMountLoader
- [x] 8.6 新增 `ReleaseAutoMountAsset(AssetPath)` — 转发到 AutoMountLoader

## 9. Manifest 文件部署与下载集成

- [x] 9.1 在 `UHotUpdateManager::ApplyUpdate()` 或下载完成后，确保 `asset_pak_manifest.json` 和 `asset_dependencies.json` 也被下载到本地（可在 manifest.json 中添加对应条目，或作为固定名称文件从 CDN 下载）
- [x] 9.2 在 Manifest 文件格式中新增 `assetManifestUrl` 和 `assetDependenciesUrl` 字段（可选），用于指定 Manifest 文件的下载 URL
- [x] 9.3 如果 Manifest 文件不存在（旧版本包或未启用 Asset 级别映射），AutoMountLoader 退化为手动 Mount 模式，不影响现有流程

## 10. 验证与调试支持

- [x] 10.1 添加日志：Manifest 加载成功时输出 Asset 总数和 Pak 总数
- [x] 10.2 添加日志：AutoMount 时输出 `[AutoMount] Loading {AssetPath}, mounting {N} paks: {PakList}`
- [x] 10.3 添加日志：ReleaseAsset 时输出 `[AutoMount] Releasing {AssetPath}, unmounting {N} paks`
- [x] 10.4 编译验证：确保 HotUpdate 和 HotUpdateEditor 模块编译通过无错误
