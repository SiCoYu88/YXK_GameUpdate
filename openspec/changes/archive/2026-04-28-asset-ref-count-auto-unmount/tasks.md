## 1. 数据结构定义

- [x] 1.1 在 `HotUpdatePakTypes.h` 中新增 `FLoadedAsset` 结构体（`TWeakObjectPtr<UObject> Asset`、`FString AssetPath`、`FString PakPath`、`double RegisterTime`），实现 `IsAssetAlive()` 检测方法和带参构造函数
- [x] 1.2 在 `HotUpdatePakTypes.h` 中新增 `FPakLoadedAssetTracker` 结构体，包含 `TArray<FLoadedAsset> LoadedAssets` 和 `bHasEverTracked` 标记
- [x] 1.3 实现 `FPakLoadedAssetTracker::RegisterAsset(UObject*, AssetPath, PakPath)` — 创建 FLoadedAsset 并添加到列表
- [x] 1.4 实现 `FPakLoadedAssetTracker::UnregisterAsset(AssetPath)` — 按 AssetPath 查找并移除对应的 FLoadedAsset
- [x] 1.5 实现 `FPakLoadedAssetTracker::CleanupStaleEntries()` — 遍历移除 `!IsAssetAlive()` 的条目，返回清理数量
- [x] 1.6 实现 `FPakLoadedAssetTracker::AreAllAssetsReleased()` — 检查列表是否为空或所有弱引用都已失效
- [x] 1.7 实现 `FPakLoadedAssetTracker::GetAliveAssetCount()` — 统计仍然存活的资源数量

## 2. PakManager 资源跟踪扩展

- [x] 2.1 在 `UHotUpdatePakManager` 中新增 `TMap<FString, FPakLoadedAssetTracker> PakAssetTrackers` 成员（HotUpdatePakManager.h）
- [x] 2.2 实现 `RegisterLoadedAsset(PakPath, Asset, AssetPath)` — 规范化 PakPath 后在对应 Tracker 中注册弱引用
- [x] 2.3 实现 `UnregisterLoadedAsset(PakPath, AssetPath)` — 从对应 Tracker 中移除指定资源的跟踪
- [x] 2.4 实现 `ScanAndAutoUnmount()` — 遍历 PakAssetTrackers，对每个 Tracker 执行 CleanupStaleEntries，检测 AreAllAssetsReleased 后收集需要卸载的 Pak 列表，在锁外批量调用 RequestUnmount
- [x] 2.5 实现 `GetTrackedAssetCount(PakPath)` 和 `GetTrackedAssetPaths(PakPath)` 查询方法（调试用）
- [x] 2.6 在 ScanAndAutoUnmount 中处理 AutoMountLoader 的 ActiveLoads 联动清理：扫描发现 Pak 所有资源释放后，同步清理 ActiveLoads 中引用了该 Pak 的条目

## 3. AutoMountLoader 弱引用注册集成

- [x] 3.1 修改 `AsyncLoadAsset` 的加载完成回调：资源加载成功后，对该资源关联的每个 MountedPak 调用 `PakManager->RegisterLoadedAsset(PakPath, LoadedAsset, AssetPath)`
- [x] 3.2 修改 `SyncLoadAsset`：加载成功后同样为每个 MountedPak 注册弱引用跟踪
- [x] 3.3 修改 `AsyncLoadAssets`（批量）的加载完成回调：对每个成功加载的资源注册弱引用到对应的 Pak Tracker
- [x] 3.4 修改 `ReleaseAsset`：在递减 RefCount 之前，对每个 MountedPak 调用 `PakManager->UnregisterLoadedAsset(PakPath, AssetPath)`，保留现有的 RefCount 归零 → RequestUnmount 逻辑

## 4. 定时扫描机制

- [x] 4.1 在 `UHotUpdateAutoMountLoader` 中新增 `FTSTicker::FDelegateHandle ScanTickerHandle` 成员
- [x] 4.2 实现 `StartAssetScan()` — 读取 `UHotUpdateSettings::AssetScanInterval`，使用 `FTSTicker::GetCoreTicker().AddTicker()` 注册定期扫描回调
- [x] 4.3 实现 `StopAssetScan()` — 调用 `FTSTicker::GetCoreTicker().RemoveTicker(ScanTickerHandle)` 停止扫描
- [x] 4.4 实现 `OnScanTick(float DeltaTime)` — 调用 `PakManager->ScanAndAutoUnmount()`，返回 true 保持 Ticker 持续运行
- [x] 4.5 在 `UHotUpdateAutoMountLoader::Initialize` 末尾调用 `StartAssetScan()` 自动启动扫描
- [x] 4.6 提供 `BeginDestroy` 或析构保护：在 AutoMountLoader 被销毁时确保 StopAssetScan 被调用

## 5. 配置项

- [x] 5.1 在 `UHotUpdateSettings` 中新增 `float AssetScanInterval = 5.0f` 配置项（Config=Game），注释说明含义和设为 0 禁用扫描
- [x] 5.2 在 `UHotUpdateSettings` 中新增 `bool bEnableAutoUnmountOnGC = true` 配置项（Config=Game），控制是否启用 GC 驱动的自动卸载

## 6. HotUpdateManager 集成

- [x] 6.1 在 `ApplyUpdate()` 中 AutoMountLoader 初始化成功后，自动调用 `AutoMountLoader->StartAssetScan()`（如果 `bEnableAutoUnmountOnGC` 为 true）
- [x] 6.2 在 `Deinitialize()` 中调用 `AutoMountLoader->StopAssetScan()`
- [x] 6.3 更新 `ReleaseAutoMountAsset` 的文档注释，说明即使不调用此方法也会在 GC 后自动释放

## 7. 日志与调试

- [x] 7.1 在 `RegisterLoadedAsset` 中添加日志：`[AssetTracker] Registered {AssetPath} -> {PakPath} (tracked: N)`
- [x] 7.2 在 `UnregisterLoadedAsset` 中添加日志：`[AssetTracker] Unregistered {AssetPath} from {PakPath}`
- [x] 7.3 在 `ScanAndAutoUnmount` 中添加日志：`[AssetTracker] Scan: {PakPath} all assets released, auto-unmounting`
- [x] 7.4 在 `CleanupStaleEntries` 中添加日志：清理掉的已 GC 资源数量
- [x] 7.5 在 `OnScanTick` 中添加 Verbose 日志：扫描了多少个 Pak，存活资源总数
