## 1. 数据结构与类型定义

- [ ] 1.1 在 `HotUpdateTypes.h` 中新增 `FPakMountRecord` 结构体（Metadata、RefCount、bIsMounted、bIsRegistered 字段）
- [ ] 1.2 在 `HotUpdateTypes.h` 中新增 `FPakMountInfo` 结构体（供查询接口返回：PakPath、RefCount、ChunkId、bIsMounted）
- [ ] 1.3 在 `HotUpdatePakManager.h` 中将 `TArray<FHotUpdatePakMetadata> MountedPaks` 替换为 `TMap<FString, FPakMountRecord> PakRecords`，添加 `FCriticalSection PakRecordsMutex`

## 2. 核心引用计数接口

- [ ] 2.1 在 `HotUpdatePakManager.h` 中声明 `RequestMount`、`RequestUnmount`、`AddRef`、`Release` 四个公开方法
- [ ] 2.2 实现 `RequestMount` — 路径规范化、加锁、首次挂载(RefCount 0→1 执行 MountPakInternal)或递增计数、广播 OnPakMounted 事件
- [ ] 2.3 实现 `RequestUnmount` — 加锁、递减计数、计数归零时注册延迟卸载（下一帧执行）
- [ ] 2.4 实现 `AddRef` — 加锁、校验 Pak 已挂载(RefCount>=1)、递增计数，未挂载时 log error
- [ ] 2.5 实现 `Release` — 加锁、递减计数、归零时注册延迟卸载，防止计数低于 0

## 3. 延迟卸载机制

- [ ] 3.1 新增 `TSet<FString> PendingUnmounts` 记录待卸载路径列表
- [ ] 3.2 实现 `ProcessPendingUnmounts` — 遍历 PendingUnmounts，对 RefCount 仍为 0 的执行实际 Unmount，RefCount 已恢复的取消卸载
- [ ] 3.3 在 `RequestUnmount` / `Release` 中，RefCount 归零时将路径加入 PendingUnmounts 并注册 NextTick 回调（`FTSTicker::GetCoreTicker().AddTicker` 或 `AsyncTask(GameThread)`)
- [ ] 3.4 在 `RequestMount` 中检查 PendingUnmounts，如果目标 Pak 在待卸载列表中则取消卸载并直接递增计数

## 4. FScopedPakRef RAII 守卫

- [ ] 4.1 在 `HotUpdatePakManager.h` 中声明 `FScopedPakRef` 结构体（TWeakObjectPtr<UHotUpdatePakManager>、FString PakPath、bool bIsValid）
- [ ] 4.2 实现构造函数 — 调用 `Manager->RequestMount`，设置 bIsValid
- [ ] 4.3 实现析构函数 — 检查 TWeakObjectPtr 有效性后调用 `Manager->RequestUnmount`
- [ ] 4.4 实现移动构造/移动赋值（转移所有权），禁用拷贝构造/拷贝赋值
- [ ] 4.5 实现 `IsValid()` 查询方法

## 5. 容器注册与批量操作

- [ ] 5.1 实现 `RegisterAvailablePak(PakPath, Metadata)` — 创建 bIsRegistered=true、RefCount=0 的记录
- [ ] 5.2 实现 `MountAllRegistered()` — 遍历 PakRecords，对所有 bIsRegistered && RefCount==0 的调用 RequestMount
- [ ] 5.3 实现 `RequestMountByChunkId(ChunkId)` — 遍历 PakRecords 按 ChunkId 过滤并逐一 RequestMount，返回成功数
- [ ] 5.4 实现 `RequestUnmountByChunkId(ChunkId)` — 遍历 PakRecords 按 ChunkId 过滤并逐一 RequestUnmount

## 6. 查询接口

- [ ] 6.1 实现 `GetRefCount(PakPath)` — 返回引用计数，未找到返回 -1
- [ ] 6.2 实现 `GetAllMountedPaks()` — 返回所有 bIsMounted==true 的 FPakMountInfo 数组
- [ ] 6.3 实现 `GetAllRegisteredPaks()` — 返回所有 bIsRegistered==true 的 FPakMountInfo 数组
- [ ] 6.4 实现 `IsRegistered(PakPath)` — 路径是否已注册

## 7. 线程安全

- [ ] 7.1 在 RequestMount / RequestUnmount / AddRef / Release / 查询接口中统一使用 `FScopeLock Lock(&PakRecordsMutex)` 保护
- [ ] 7.2 实际 Mount/Unmount 操作确保在游戏线程执行：非游戏线程调用时通过 `AsyncTask(ENamedThreads::GameThread, ...)` 调度
- [ ] 7.3 路径规范化工具方法 `NormalizePakPath(PakPath)` — 使用 `FPaths::ConvertRelativePathToFull` + `FPaths::NormalizeFilename`

## 8. 修改 HotUpdateManager 集成

- [ ] 8.1 修改 `UHotUpdateManager::ApplyUpdate()` — 替换直接 MountPak 调用为 `PakManager->RegisterAvailablePak` + `PakManager->MountAllRegistered()`
- [ ] 8.2 新增 `OnPaksAvailable` 事件委托，在 RegisterAvailablePak 完成后广播
- [ ] 8.3 更新 `IsPakMounted` 相关调用点使用新的查询接口

## 9. 向后兼容与废弃标记

- [ ] 9.1 将原有 `MountPak(PakPath, PakOrder, EncryptionKey)` 标记 `UE_DEPRECATED`，内部转发到 `RequestMount`
- [ ] 9.2 将原有 `UnmountPak(PakPath)` 标记 `UE_DEPRECATED`，内部强制 RefCount=0 并执行立即卸载
- [ ] 9.3 将原有 `IsPakMounted(PakPath)` 实现改为查询 `PakRecords` 中 `bIsMounted` 字段
- [ ] 9.4 提取原有挂载逻辑为 `MountPakInternal` / `UnmountPakInternal` 私有方法，供新旧接口共用

## 10. 验证与调试支持

- [ ] 10.1 添加 Debug 日志：RefCount 变化时输出 `[PakPath] RefCount: old → new`
- [ ] 10.2 添加 Debug 警告：RefCount 持续 > 0 超过可配置时长（默认 300 秒）时输出泄漏警告
- [ ] 10.3 编译验证：确保整个 HotUpdate 模块编译通过无错误
- [ ] 10.4 验证 `FScopedPakRef` 移动语义和 Manager 销毁安全性
