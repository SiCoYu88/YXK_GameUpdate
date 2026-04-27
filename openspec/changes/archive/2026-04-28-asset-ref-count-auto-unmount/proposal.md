## Why

在上一个迭代（`asset-pak-manifest-auto-mount`）中，我们实现了 Asset-Pak Manifest 映射和自动按需 Mount 加载机制。但当前的资源释放仍然依赖**业务层主动调用** `ReleaseAutoMountAsset(AssetPath)` 来递减引用计数、触发 Pak 卸载。这种显式释放模式存在以下问题：

1. **容易遗忘释放**：业务层加载了资源后，如果忘记调用 `ReleaseAutoMountAsset`，Pak 的引用计数永远不归零，造成内存泄漏（Pak 永不卸载）。
2. **与 UE GC 脱节**：UE 引擎有自己的垃圾回收机制（GC），当没有强引用指向一个 UObject 时，GC 会自动回收它。但当前系统感知不到这一点——即使资源已经被 GC 回收，Pak 仍然挂载。
3. **生命周期管理割裂**：业务层需要同时管理两套生命周期——UE 引擎的 UObject 引用和热更系统的 Pak 引用，增加了使用复杂度和出错概率。

理想的模式是：**Pak 的生命周期自动跟随其内部加载出的资源的生命周期**。当所有从某个 Pak 中加载的资源都被释放/GC 回收后，系统自动卸载该 Pak，无需业务层显式调用释放方法。

## What Changes

- **新增 `FLoadedAsset` 结构体**：将从 Pak 中加载的每个主资源包装成 `FLoadedAsset`，内部持有资源的 `TWeakObjectPtr<UObject>` 弱引用，以及该资源所在的 Pak 路径。弱引用不会阻止 UE GC 回收资源，但可以检测资源是否仍然存活。
- **新增 `FPakLoadedAssetTracker` 结构体**：在 `UHotUpdatePakManager` 中为每个已挂载的 Pak 维护一个已加载资源跟踪列表（`TArray<FLoadedAsset>`），记录从该 Pak 中加载出了哪些资源。
- **新增定期扫描机制**：在 `UHotUpdateAutoMountLoader`（或 `UHotUpdatePakManager`）中使用 `FTSTicker` 定时器，每隔 N 秒（可配置，默认 5 秒）扫描所有 `FPakLoadedAssetTracker`，检查其中每个 `FLoadedAsset` 的弱指针是否为空。
- **自动卸载 Pak**：当扫描发现某个 Pak 的所有 `FLoadedAsset` 弱指针都已失效（资源全部被 GC 回收），自动调用 `RequestUnmount` 卸载该 Pak。利用已有的延迟卸载机制（下一帧执行）确保安全性。
- **保留显式释放接口**：`ReleaseAutoMountAsset` 仍然保留，作为业务层主动释放的快速路径（立即标记弱引用无效，不等待 GC 扫描周期）。

## Capabilities

### New Capabilities
- `asset-weak-ref-tracking`: 基于弱引用的资源加载跟踪——每次从 Pak 加载资源时自动注册弱引用跟踪，无需业务层额外操作
- `auto-unmount-on-gc`: GC 驱动的 Pak 自动卸载——定期扫描检测资源弱引用失效，当 Pak 中所有加载的资源都被 GC 回收后自动卸载

### Modified Capabilities
- `auto-mount-loading`: 修改 AutoMountLoader 的加载流程，在 Mount+加载后自动注册弱引用跟踪（无需手动 Release 也能实现自动卸载）
- `pak-ref-counting`: PakManager 引用计数机制增加按 Pak 维度的已加载资源跟踪能力

## Impact

- **运行时模块变更文件**：
  - `HotUpdatePakTypes.h` — 新增 `FLoadedAsset`、`FPakLoadedAssetTracker` 结构体
  - `HotUpdateAutoMountLoader.h/.cpp` — 加载完成后注册弱引用跟踪；新增定时扫描逻辑
  - `HotUpdatePakManager.h/.cpp` — 新增 `TMap<FString, FPakLoadedAssetTracker>` 成员，新增 `RegisterLoadedAsset`、`UnregisterLoadedAsset`、`ScanAndAutoUnmount` 方法
  - `HotUpdateSettings.h` — 新增扫描间隔配置项
  - `HotUpdateManager.h/.cpp` — 可能调整便捷方法注释/行为说明
- **无编辑器模块变更**
- **无数据文件格式变更**
- **向后兼容**：现有显式 Release 接口保持不变，新增的自动检测机制是增量能力
