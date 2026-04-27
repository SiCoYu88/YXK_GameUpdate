## Why

在上一个迭代（`2026-04-27-dynamic-pak-mount-refcount`）中，我们为 `UHotUpdatePakManager` 引入了引用计数机制，实现了 Pak 容器的按需挂载/卸载生命周期管理。但当前系统仍存在以下不足：

1. **缺少 Asset → Pak 的映射关系**：运行时无法知道某个资源在哪个 Pak 容器内，必须提前手动挂载所有可能需要的 Pak，无法做到真正的"按需加载"。
2. **缺少 Asset 依赖信息**：加载一个主资源时，其依赖的子资源可能分散在多个 Pak 中，缺少依赖拓扑信息意味着无法自动挂载依赖资源所在的 Pak，导致加载失败或需要预挂载全部 Pak。
3. **无法实现真正的按需 Mount**：理想的流程是——加载某个 Asset 时自动发现它在哪个 Pak 中、它依赖哪些 Asset 以及这些 Asset 在哪些 Pak 中，然后动态 Mount 所有相关 Pak，加载完成后通过引用计数自动管理卸载。

这些问题导致当前即使有了引用计数，业务层仍需手动维护"哪些 Pak 需要挂载"的知识，本次变更旨在从工具链到运行时实现完整的自动化按需 Mount Pak 能力。

## What Changes

- **新增 Asset-Pak Manifest 文件生成（编辑器侧）**：在基础包/补丁包构建流程中，扫描每个 Pak 内的 Cook 后 Asset，生成 `asset_pak_manifest.json` 文件，记录每个 Asset 所在的 Pak 路径和 ChunkId
- **新增 Asset 依赖信息文件生成（编辑器侧）**：利用 AssetRegistry 收集每个 Asset 的依赖关系，并关联依赖 Asset 所在的 Pak 信息，生成 `asset_dependencies.json` 文件
- **新增运行时 Manifest 加载与查询系统**：在 HotUpdate 运行时模块新增 `UHotUpdateAssetPakMapping` 类，负责加载和查询 `asset_pak_manifest.json` 与 `asset_dependencies.json`，提供 `GetPakForAsset(AssetPath)` 和 `GetDependencyPaks(AssetPath)` 接口
- **新增自动按需 Mount 机制**：提供 `LoadAssetWithAutoMount(AssetPath)` 高层接口，加载主资源时自动查询 Manifest → 发现主资源及其全部依赖所在的 Pak → 调用 `RequestMount` 逐一挂载 → 执行异步资源加载 → 加载完成后持有引用计数
- **集成引用计数 UnMount**：资源释放时自动 `RequestUnmount` 对应 Pak，引用归零时延迟卸载（复用上一迭代的引用计数基础设施）

## Capabilities

### New Capabilities
- `asset-pak-mapping`: Asset-Pak Manifest 映射系统 — 编辑器生成 + 运行时查询，提供 Asset 到 Pak 的双向映射和 Asset 依赖图的 Pak 关联信息
- `auto-mount-loading`: 自动按需挂载加载 — 根据 Asset 依赖图自动 Mount 所需 Pak，加载完成后通过引用计数自动管理卸载

### Modified Capabilities
- `pak-ref-counting`: 在已有引用计数系统上构建，新增 Asset 级别的 Mount/Unmount 协调能力

## Impact

- **编辑器模块新增文件**：
  - `HotUpdateAssetPakManifestGenerator.h/.cpp` — 扫描 Pak 内容生成 `asset_pak_manifest.json`
  - `HotUpdateAssetDependencyCollector.h/.cpp` — 收集 Asset 依赖关系并关联 Pak 信息，生成 `asset_dependencies.json`
- **运行时模块新增文件**：
  - `HotUpdateAssetPakMapping.h/.cpp` — 运行时加载和查询 Asset-Pak 映射与依赖信息
  - `HotUpdateAutoMountLoader.h/.cpp` — 自动按需 Mount + 异步加载 + 引用计数管理的高层封装
- **现有文件变更**：
  - `UHotUpdateBaseVersionBuilder` / `UHotUpdatePatchPackageBuilder` — 构建完成后调用 Manifest 生成器
  - `UHotUpdateManager` — 集成 AutoMountLoader，提供面向业务层的便捷接口
  - `UHotUpdatePakManager` — 可能新增按 AssetPath 查询挂载状态的便捷方法
- **数据文件格式**：新增两个 JSON 文件，随 Pak 一起部署到 CDN/设备
- **向后兼容**：现有手动 Mount 流程不受影响，新增的自动 Mount 机制为增量能力
