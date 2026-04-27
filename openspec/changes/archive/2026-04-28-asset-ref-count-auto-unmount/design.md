## Overview

本设计在现有 AutoMount + 引用计数基础设施之上，引入**弱引用跟踪 + 定期 GC 扫描**机制，实现 Pak 文件随其内部加载资源的生命周期自动卸载。核心思路：用 `TWeakObjectPtr` 跟踪从 Pak 中加载的每个 UObject，定时检测弱指针是否失效（被 UE GC 回收），当某 Pak 的所有关联资源都被回收时自动触发 `RequestUnmount`。

## Data Structures

### FLoadedAsset（新增，HotUpdatePakTypes.h）

```cpp
/**
 * 从 Pak 中加载的单个资源弱引用记录
 *
 * 持有资源的 TWeakObjectPtr，不会阻止 UE GC 回收资源。
 * 当弱指针失效时表示资源已被 GC 回收。
 */
struct HOTUPDATE_API FLoadedAsset
{
    /// 加载的资源弱引用
    TWeakObjectPtr<UObject> Asset;

    /// 资源路径（用于日志和调试）
    FString AssetPath;

    /// 所属 Pak 路径（规范化后的完整路径）
    FString PakPath;

    /// 注册时间戳（用于调试和统计）
    double RegisterTime = 0.0;

    FLoadedAsset() = default;
    FLoadedAsset(UObject* InAsset, const FString& InAssetPath, const FString& InPakPath);

    /// 检查资源是否仍然存活
    bool IsAssetAlive() const { return Asset.IsValid(); }
};
```

### FPakLoadedAssetTracker（新增，HotUpdatePakTypes.h）

```cpp
/**
 * 单个 Pak 的已加载资源跟踪器
 *
 * 维护从该 Pak 中加载出的所有资源的弱引用列表。
 * 当列表中所有弱引用都失效时，该 Pak 可以被安全卸载。
 */
struct HOTUPDATE_API FPakLoadedAssetTracker
{
    /// 已加载的资源列表
    TArray<FLoadedAsset> LoadedAssets;

    /// 注册一个新加载的资源
    void RegisterAsset(UObject* Asset, const FString& AssetPath, const FString& PakPath);

    /// 移除指定资源的跟踪（用于显式 Release）
    void UnregisterAsset(const FString& AssetPath);

    /// 清理已失效的弱引用（资源已被 GC）
    /// @return 清理掉的数量
    int32 CleanupStaleEntries();

    /// 检查是否所有资源都已被释放（弱引用全部失效）
    bool AreAllAssetsReleased() const;

    /// 获取仍存活的资源数
    int32 GetAliveAssetCount() const;

    /// 获取已注册的总资源数（含已失效）
    int32 GetTotalAssetCount() const { return LoadedAssets.Num(); }

    /// 是否有任何资源被注册过
    bool HasEverTrackedAssets() const { return bHasEverTracked; }

private:
    bool bHasEverTracked = false;
};
```

## Component Changes

### UHotUpdatePakManager（修改）

在 `HotUpdatePakManager.h` 中新增以下成员和方法：

```
新增成员:
  TMap<FString, FPakLoadedAssetTracker> PakAssetTrackers;
    — Key = 规范化 PakPath，Value = 该 Pak 的资源跟踪器

新增方法:
  void RegisterLoadedAsset(const FString& PakPath, UObject* Asset, const FString& AssetPath);
    — 在指定 Pak 的 Tracker 中注册一个新加载的资源弱引用

  void UnregisterLoadedAsset(const FString& PakPath, const FString& AssetPath);
    — 从指定 Pak 的 Tracker 中移除某资源的跟踪（显式 Release 用）

  void ScanAndAutoUnmount();
    — 遍历所有 PakAssetTrackers：
      1. 调用 CleanupStaleEntries() 清理已 GC 的弱引用
      2. 检查 AreAllAssetsReleased()
      3. 如果全部释放且 Pak 仍挂载，调用 RequestUnmount

  int32 GetTrackedAssetCount(const FString& PakPath) const;
    — 查询指定 Pak 跟踪的存活资源数

  TArray<FString> GetTrackedAssetPaths(const FString& PakPath) const;
    — 获取指定 Pak 中仍存活的资源路径列表（调试用）
```

**线程安全**：`PakAssetTrackers` 与 `PakRecords` 共享 `PakRecordsMutex` 保护（它们的访问总是伴随引用计数操作）。`ScanAndAutoUnmount` 在游戏线程 Ticker 中调用，收集需要 Unmount 的路径后在锁外执行 `RequestUnmount`（与 `ProcessPendingUnmounts` 模式一致）。

### UHotUpdateAutoMountLoader（修改）

修改加载完成的回调逻辑，在资源加载成功后自动注册弱引用跟踪：

```
修改 AsyncLoadAsset():
  加载完成回调中：
    if (LoadedAsset != nullptr)
      对该资源关联的每个 MountedPak 调用:
        PakManager->RegisterLoadedAsset(PakPath, LoadedAsset, AssetPath)

修改 SyncLoadAsset():
  加载成功后：
    对每个 MountedPak 调用:
      PakManager->RegisterLoadedAsset(PakPath, LoadedAsset, AssetPath)

修改 AsyncLoadAssets() (批量):
  加载完成回调中：
    对每个成功加载的资源，注册弱引用到对应 Pak

修改 ReleaseAsset():
  在递减 RefCount 前：
    对每个 MountedPak 调用:
      PakManager->UnregisterLoadedAsset(PakPath, AssetPath)
  保留现有 RefCount 归零 → RequestUnmount 逻辑（作为立即释放路径）

新增成员:
  FTSTicker::FDelegateHandle ScanTickerHandle;
    — 定时扫描的 Ticker 句柄

新增方法:
  void StartAssetScan();
    — 在 Initialize 后启动定时扫描 Ticker

  void StopAssetScan();
    — 停止定时扫描

  bool OnScanTick(float DeltaTime);
    — Ticker 回调：调用 PakManager->ScanAndAutoUnmount()
    — 返回 true 保持 Ticker 持续运行
```

### UHotUpdateSettings（修改）

```
新增配置:
  UPROPERTY(Config, EditAnywhere)
  float AssetScanInterval = 5.0f;
    — 弱引用扫描间隔（秒），默认 5 秒
    — 设为 0 禁用自动扫描（仅依赖显式 Release）

  UPROPERTY(Config, EditAnywhere)
  bool bEnableAutoUnmountOnGC = true;
    — 是否启用 GC 驱动的自动卸载
    — 禁用后回退到纯手动 Release 模式
```

### HotUpdateManager（微调）

- 在 `ApplyUpdate()` 初始化 AutoMountLoader 之后，调用 `AutoMountLoader->StartAssetScan()` 启动定时扫描
- 在 `Deinitialize()` 中调用 `AutoMountLoader->StopAssetScan()` 停止扫描
- 更新 `ReleaseAutoMountAsset` 的文档注释，说明即使不调用此方法，资源也会在 GC 后自动释放

## Flow Diagrams

### 加载流程（带弱引用注册）

```
业务层调用 LoadAssetWithAutoMount("/Game/MyAsset")
  └→ AutoMountLoader::AsyncLoadAsset()
      ├→ Mapping::GetRequiredPaksForAsset → [Pak1, Pak2]
      ├→ PakManager::RequestMount(Pak1) → RefCount 0→1, Mount
      ├→ PakManager::RequestMount(Pak2) → RefCount 0→1, Mount
      ├→ StreamableManager::RequestAsyncLoad → 异步加载...
      └→ 加载完成回调:
           ├→ 更新 ActiveLoads 跟踪信息
           ├→ PakManager::RegisterLoadedAsset(Pak1, Asset, AssetPath) ← 新增
           ├→ PakManager::RegisterLoadedAsset(Pak2, Asset, AssetPath) ← 新增
           └→ OnComplete 回调给业务层
```

### GC 驱动的自动卸载流程

```
定时 Ticker (每 5 秒)
  └→ AutoMountLoader::OnScanTick()
      └→ PakManager::ScanAndAutoUnmount()
           ├→ 遍历 PakAssetTrackers:
           │   ├→ Pak1: CleanupStaleEntries() → 清理已 GC 的弱引用
           │   │   ├→ GetAliveAssetCount() → 2（仍有活跃资源）
           │   │   └→ 跳过
           │   └→ Pak2: CleanupStaleEntries() → 清理已 GC 的弱引用
           │       ├→ AreAllAssetsReleased() → true（全部被 GC 回收）
           │       ├→ 从 ActiveLoads 中清理对应记录 ← 联动
           │       └→ 加入 UnmountList
           └→ 对 UnmountList 中每个 Pak:
               └→ RequestUnmount(Pak2) → RefCount→0 → 延迟卸载
```

### 显式释放流程（保留）

```
业务层调用 ReleaseAutoMountAsset("/Game/MyAsset")
  └→ AutoMountLoader::ReleaseAsset()
      ├→ PakManager::UnregisterLoadedAsset(Pak1, AssetPath) ← 新增
      ├→ PakManager::UnregisterLoadedAsset(Pak2, AssetPath) ← 新增
      ├→ RefCount-- → 如果归零:
      │   └→ RequestUnmount(Pak1/Pak2) → 延迟卸载
      └→ 从 ActiveLoads 移除
```

## Key Design Decisions

### 1. 弱引用跟踪放在 PakManager 层而非 AutoMountLoader 层

**理由**：
- PakManager 是 Pak 生命周期的唯一权威管理者，在这里维护"每个 Pak 关联了哪些资源"是最自然的
- 跟踪数据与 PakRecords（引用计数、挂载状态）紧密关联，放在同一个类中减少跨组件耦合
- 如果有其他加载路径（非 AutoMountLoader 的手动 Mount + Load），也可以使用 `RegisterLoadedAsset`

### 2. 使用 FTSTicker 定时扫描而非 GC 回调

**理由**：
- UE 的 GC 回调（`FCoreUObjectDelegates::PostGarbageCollect`）虽然更精确，但 GC 频率不可控且回调中操作受限
- Ticker 定时扫描简单可靠，间隔可配置（5 秒足够平衡响应性和性能）
- 扫描逻辑轻量（只是遍历弱指针检查 `IsValid()`），5 秒一次不会有性能问题
- 即使在 GC 不频繁的场景下，定时扫描也能确保资源最终被释放

### 3. 保留显式 Release 作为快速路径

**理由**：
- 有些场景业务层明确知道不再需要某资源，直接 Release 比等待下一次扫描更及时
- 显式 Release 会同时 UnregisterLoadedAsset（从 Tracker 移除），避免扫描时重复处理
- 两种释放路径互不冲突：显式 Release 先到就立即卸载，否则等扫描周期自动卸载

### 4. 单个资源可关联多个 Pak

由于依赖关系，加载一个主资源可能 Mount 了多个 Pak。弱引用跟踪是**按 Pak 维度**存储的（PakAssetTrackers[PakPath]），所以同一个 UObject 可能被注册到多个 Pak 的 Tracker 中。当该 UObject 被 GC 时，所有关联 Pak 的 Tracker 都会在扫描时发现弱引用失效。

### 5. ScanAndAutoUnmount 的 RefCount 协调

扫描发现 Pak 的所有资源都已释放后，需要确保 RefCount 也能归零。策略：
- 注册弱引用跟踪时不额外增加 RefCount（Mount 时已经 +1）
- 扫描检测到全部释放时，计算该 Pak 在 ActiveLoads 中还有多少 AssetPath 引用了它，对每个调用 `RequestUnmount` 使 RefCount 归零
- 这样即使业务层没有调用 `ReleaseAsset`，扫描也能替代完成释放

## Edge Cases

1. **资源被 GC 但又被重新加载**：弱引用失效 → 扫描卸载 Pak → 业务层再次加载同一资源 → 触发重新 Mount（与首次加载流程一致，无问题）
2. **扫描周期内资源被释放又加载**：新的加载会注册新的弱引用到 Tracker，扫描时会发现仍有存活资源，不会错误卸载
3. **多线程安全**：Ticker 回调在游戏线程，`RegisterLoadedAsset`/`UnregisterLoadedAsset` 在游戏线程（异步加载回调在游戏线程），PakRecordsMutex 保护数据一致性
4. **Deinitialize 时的清理**：停止 Ticker，清空 PakAssetTrackers，不执行额外 Unmount（由 PakManager 的 Deinitialize 统一处理）
