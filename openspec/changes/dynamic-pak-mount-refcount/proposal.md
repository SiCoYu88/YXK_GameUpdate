## Why

当前 `UHotUpdatePakManager` 在热更新完成后一次性挂载所有 Pak/IoStore 容器并持续保持挂载状态，无法按需动态加载和卸载。这导致内存常驻开销大，尤其在移动平台上资源紧张时不可接受。同时缺少安全的卸载判定机制——直接卸载正在使用中的 Pak 会导致崩溃。需要引入引用计数机制来精确追踪 Pak 的使用状态，只在引用归零时才执行卸载。

## What Changes

- **新增引用计数管理层**：在 `UHotUpdatePakManager` 中为每个已挂载的 Pak/IoStore 容器维护引用计数（`int32 RefCount`），提供 `AddRef` / `Release` 配对接口
- **新增按需挂载接口**：提供 `RequestMount(PakPath)` 方法，首次请求时自动挂载并设引用计数为 1，后续请求仅递增计数
- **新增安全卸载接口**：提供 `RequestUnmount(PakPath)` 方法，递减引用计数，当计数归零时自动执行实际卸载
- **新增 RAII 作用域守卫**：提供 `FScopedPakRef` 辅助结构，构造时 AddRef、析构时 Release，防止忘记释放
- **新增批量操作支持**：支持按 ChunkId 批量 Mount/Unmount，用于整组资源加载场景
- **新增查询接口**：获取指定 Pak 的当前引用计数、获取所有活跃挂载列表及其计数
- **修改现有 `ApplyUpdate()` 流程**：热更完成后不再自动挂载所有容器，改为注册可用容器列表，由业务层按需请求挂载
- **新增事件通知**：Pak 实际挂载/卸载时广播事件（区别于引用计数变化）

## Capabilities

### New Capabilities
- `pak-ref-counting`: 引用计数管理系统 — 为 Pak/IoStore 容器提供线程安全的引用计数追踪、自动挂载/卸载生命周期管理、RAII 作用域守卫、批量操作和查询接口

### Modified Capabilities
<!-- 无现有 spec 需要修改 -->

## Impact

- **核心文件变更**：
  - `HotUpdatePakManager.h/.cpp` — 新增引用计数数据结构和 RequestMount/RequestUnmount/AddRef/Release 接口
  - `HotUpdateManager.h/.cpp` — `ApplyUpdate()` 流程调整为注册可用容器而非直接挂载
  - `HotUpdateTypes.h` — 新增 `FHotUpdateMountedPakInfo` 结构体（含引用计数字段）
- **API 变更**：原有 `MountPak` / `UnmountPak` 保留但标记为内部使用，公开 API 切换为 `RequestMount` / `RequestUnmount`
- **线程安全**：引用计数操作需要使用 `FCriticalSection` 保护，支持多线程 AddRef/Release
- **向后兼容**：原有直接 Mount/Unmount 接口不删除，仅标记 deprecated，确保已有调用方平滑迁移
