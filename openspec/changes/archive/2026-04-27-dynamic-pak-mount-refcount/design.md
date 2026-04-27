## Context

当前 `UHotUpdatePakManager` 提供 `MountPak` / `UnmountPak` 直接操作接口。`UHotUpdateManager::ApplyUpdate()` 在热更完成后一次性挂载所有下载的容器并持续保持挂载状态。这种"全量常驻"模式存在两个核心问题：

1. **内存浪费**：所有热更 Pak 持续占用文件句柄和索引内存，在移动平台上不可接受
2. **卸载不安全**：没有机制判断 Pak 是否正在被使用，直接 Unmount 可能导致资产加载崩溃

现有代码中 `MountedPaks` 是一个 `TArray<FHotUpdatePakMetadata>` 平坦列表，仅记录路径和元数据，无引用追踪。`IsPakMounted()` 仅做路径匹配，无使用频次信息。

## Goals / Non-Goals

**Goals:**
- 为每个 Pak/IoStore 容器提供精确的引用计数生命周期管理
- 首次请求时按需自动挂载，引用归零时自动卸载
- 提供 RAII 守卫防止引用泄漏
- 线程安全，支持游戏线程和异步加载线程并发访问
- 向后兼容：原有 `MountPak` / `UnmountPak` 保留为内部/遗留接口

**Non-Goals:**
- 不实现 LRU 缓存或定时自动卸载策略（可作为未来扩展）
- 不改变编辑器侧打包流程（仅运行时行为变更）
- 不处理 IoStore 容器的部分卸载（IoStore 仍以整容器为粒度）
- 不引入异步挂载（Mount 本身是同步操作，保持不变）

## Decisions

### Decision 1: 引用计数存储结构

**选择**：在 `UHotUpdatePakManager` 中使用 `TMap<FString, FPakMountRecord>` 替代 `TArray<FHotUpdatePakMetadata>`。

```cpp
struct FPakMountRecord
{
    FHotUpdatePakMetadata Metadata;  // 原有元数据
    int32 RefCount = 0;              // 引用计数
    bool bIsMounted = false;         // 实际挂载状态
    bool bIsRegistered = false;      // 是否已注册（可用但未挂载）
};
// Key = 规范化后的 PakPath（FPaths::ConvertRelativePathToFull）
TMap<FString, FPakMountRecord> PakRecords;
```

**理由**：TMap 按路径 O(1) 查找，比遍历 TArray 高效。路径规范化避免同一文件因路径格式不同产生多条记录。

**替代方案**：  
- 使用 `TArray` + 线性查找 — 简单但 O(n)，当容器数量增多时性能差  
- 使用 ChunkId 作为 Key — 不够通用，自定义 Pak 可能没有 ChunkId

### Decision 2: RequestMount / RequestUnmount 语义

**选择**：采用"请求式"接口，内部自动管理挂载/卸载时机。

```cpp
// 请求挂载：计数+1，首次时执行实际 Mount
bool RequestMount(const FString& PakPath, int32 PakOrder = INDEX_NONE,
                  const FString& EncryptionKey = TEXT(""));

// 请求卸载：计数-1，归零时执行实际 Unmount
bool RequestUnmount(const FString& PakPath);

// 直接增减引用（用于已挂载的 Pak）
void AddRef(const FString& PakPath);
void Release(const FString& PakPath);
```

- `RequestMount` 返回 true 表示 Pak 可用（无论是新挂载还是已挂载）
- `RequestUnmount` 返回 true 表示引用计数成功递减（不代表已卸载）
- `AddRef` / `Release` 是轻量操作，不触发实际挂载/卸载，仅调整计数
- 当 `Release` 使计数降为 0 时，自动调用内部 `UnmountPakInternal()`

**理由**：比暴露裸 Mount/Unmount + 独立 RefCount 更安全，不可能出现"已卸载但计数非零"的状态。

**替代方案**：
- 暴露 AddRef/Release 但不自动 Mount/Unmount — 调用方需自行管理时序，容易出错

### Decision 3: 线程安全策略

**选择**：使用 `FCriticalSection` 保护 `PakRecords` 的所有读写操作。

**理由**：  
- UE 的 `FPakPlatformFile::Mount/Unmount` 本身不是线程安全的，须在游戏线程执行  
- 但 AddRef/Release 可能从异步加载线程调用  
- 方案：引用计数操作用 `FCriticalSection` 保护；实际 Mount/Unmount 操作如果不在游戏线程则 marshal 到游戏线程执行

**替代方案**：
- `FRWLock` — 读多写少场景更优，但 Pak 操作不频繁，FCriticalSection 足够简单
- `TAtomic<int32>` — 仅保护计数本身，无法保护 Mount/Unmount 与计数的原子性

### Decision 4: FScopedPakRef RAII 守卫

**选择**：提供栈上 RAII 对象，生命周期内自动管理引用。

```cpp
struct FScopedPakRef
{
    FScopedPakRef(UHotUpdatePakManager* Manager, const FString& PakPath);
    ~FScopedPakRef();  // 析构时 Release
    
    FScopedPakRef(FScopedPakRef&& Other);             // 支持移动
    FScopedPakRef(const FScopedPakRef&) = delete;     // 禁止拷贝
    
    bool IsValid() const;  // Mount 是否成功
    
private:
    TWeakObjectPtr<UHotUpdatePakManager> Manager;
    FString PakPath;
    bool bIsValid;
};
```

**理由**：防止忘记 Release 导致 Pak 永不卸载的引用泄漏。使用 `TWeakObjectPtr` 避免 Manager 被销毁后悬空引用。

### Decision 5: ApplyUpdate 流程变更

**选择**：`ApplyUpdate()` 改为"注册可用容器"而非"直接挂载"。

```
ApplyUpdate():
    旧流程: 遍历容器 → MountPak() → 保存版本
    新流程: 遍历容器 → RegisterAvailablePak() → 保存版本 → 广播 OnPaksAvailable
```

新增 `RegisterAvailablePak(PakPath, Metadata)` 在 `PakRecords` 中创建 `bIsRegistered=true, RefCount=0, bIsMounted=false` 的记录。业务层通过 `RequestMount` 按需挂载。

提供 `MountAllRegistered()` 便捷方法，一次性挂载所有已注册容器（向后兼容简单场景）。

**理由**：将挂载决策权交给业务层，PakManager 仅负责执行。同时提供 `MountAllRegistered()` 保持原有一键挂载的便利性。

### Decision 6: 批量操作按 ChunkId

**选择**：提供 `RequestMountByChunkId(int32 ChunkId)` / `RequestUnmountByChunkId(int32 ChunkId)`。

**理由**：热更容器按 ChunkId 分组，业务场景通常是"加载某个关卡的所有资源"，对应一个或一组 ChunkId。比逐个 Pak 路径操作更方便。

## Risks / Trade-offs

- **[Risk] 引用泄漏导致 Pak 永不卸载** → 提供 `FScopedPakRef` RAII 守卫；提供 `GetRefCount()` + `GetAllMountedPaks()` 查询接口辅助调试；Debug 模式下引用计数长时间不归零时输出警告日志
- **[Risk] 游戏线程 Unmount 阻塞** → Mount/Unmount 是快速 IO 操作（文件索引操作），通常 < 1ms，可接受在游戏线程同步执行
- **[Risk] Release 后立即 Unmount 可能与正在进行的异步加载冲突** → 实际 Unmount 延迟到下一帧执行（`NextTick`），给异步加载一个缓冲窗口
- **[Trade-off] 向后兼容 vs 接口简洁** → 保留旧接口标记 `UE_DEPRECATED` 但不删除，一个版本周期后移除
- **[Trade-off] FCriticalSection 开销** → Pak 操作频率很低（秒级），锁竞争几乎不存在
