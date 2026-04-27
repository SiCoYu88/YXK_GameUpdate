// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "Core/HotUpdatePakTypes.h"
#include "HAL/CriticalSection.h"
#include "HotUpdatePakManager.generated.h"

class UHotUpdatePakManager;

/**
 * RAII 作用域守卫
 *
 * 构造时调用 RequestMount，析构时调用 RequestUnmount，
 * 防止忘记释放引用导致 Pak 永不卸载。
 * 使用 TWeakObjectPtr 避免 Manager 被销毁后悬空引用。
 */

struct HOTUPDATE_API FScopedPakRef
{
	/** 构造 — 调用 RequestMount 获取引用 */
	FScopedPakRef(UHotUpdatePakManager* InManager, const FString& InPakPath,
		int32 InPakOrder = 0, const FString& InEncryptionKey = TEXT(""));

	/** 析构 — 安全释放引用 */
	~FScopedPakRef();

	/** 移动构造 — 转移所有权 */
	FScopedPakRef(FScopedPakRef&& Other) noexcept;

	/** 移动赋值 — 释放旧引用并转移所有权 */
	FScopedPakRef& operator=(FScopedPakRef&& Other) noexcept;

	/** 禁止拷贝 */
	FScopedPakRef(const FScopedPakRef&) = delete;
	FScopedPakRef& operator=(const FScopedPakRef&) = delete;

	/** 查询 Mount 是否成功 */
	bool IsValid() const { return bIsValid; }

private:
	/** 释放当前持有的引用 */
	void ReleaseRef();

	TWeakObjectPtr<UHotUpdatePakManager> Manager;
	FString PakPath;
	bool bIsValid;
};

/**
 * Pak 管理器
 *
 * 负责 Pak 文件的挂载、卸载、验证
 * 提供引用计数管理，支持按需挂载/卸载
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdatePakManager : public UObject
{
	GENERATED_BODY()

public:
	UHotUpdatePakManager();

	/// 初始化
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void Initialize(const FString& InPakDirectory);

	// ============================================================
	// 引用计数挂载接口（推荐使用）
	// ============================================================

	/**
	 * 请求挂载 Pak 文件
	 *
	 * 首次请求时执行实际 Mount（RefCount 0→1），后续请求仅递增计数。
	 * @param PakPath         Pak 文件路径
	 * @param PakOrder        挂载优先级（仅首次 Mount 时生效）
	 * @param EncryptionKey   加密密钥（仅首次 Mount 时生效）
	 * @return true 表示 Pak 可用（无论是新挂载还是已挂载）
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	bool RequestMount(const FString& PakPath, int32 PakOrder = 0, const FString& EncryptionKey = TEXT(""));

	/**
	 * 请求卸载 Pak 文件
	 *
	 * 递减引用计数，归零时注册延迟卸载（下一帧执行）。
	 * @param PakPath  Pak 文件路径
	 * @return true 表示引用计数成功递减
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	bool RequestUnmount(const FString& PakPath);

	/**
	 * 直接增加引用计数（仅用于已挂载的 Pak）
	 *
	 * 不触发实际 Mount 操作。如果 Pak 未挂载会 log error。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void AddRef(const FString& PakPath);

	/**
	 * 直接减少引用计数
	 *
	 * 归零时注册延迟卸载。不会使计数低于 0。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void Release(const FString& PakPath);

	// ============================================================
	// 容器注册与批量操作
	// ============================================================

	/**
	 * 注册可用 Pak（注册但不挂载）
	 *
	 * 在 PakRecords 中创建 bIsRegistered=true, RefCount=0, bIsMounted=false 的记录。
	 * 如果 Pak 已挂载，则只更新元数据不影响挂载状态。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void RegisterAvailablePak(const FString& PakPath, const FHotUpdatePakMetadata& Metadata);

	/**
	 * 挂载所有已注册但未挂载的容器
	 *
	 * 对每个 bIsRegistered && RefCount==0 的记录调用 RequestMount。
	 * @return 成功挂载的容器数量
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	int32 MountAllRegistered();

	/**
	 * 按 ChunkId 批量请求挂载
	 *
	 * @return 成功挂载的容器数量
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	int32 RequestMountByChunkId(int32 ChunkId);

	/**
	 * 按 ChunkId 批量请求卸载
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	void RequestUnmountByChunkId(int32 ChunkId);

	// ============================================================
	// 查询接口
	// ============================================================

	/**
	 * 获取指定 Pak 的引用计数
	 * @return 引用计数；未找到返回 -1
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Pak")
	int32 GetRefCount(const FString& PakPath) const;

	/**
	 * 获取所有已挂载的 Pak 信息
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	TArray<FPakMountInfo> GetAllMountedPaks() const;

	/**
	 * 获取所有已注册的 Pak 信息
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	TArray<FPakMountInfo> GetAllRegisteredPaks() const;

	/**
	 * 检查 Pak 是否已注册
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Pak")
	bool IsRegistered(const FString& PakPath) const;

	// ============================================================
	// 向后兼容接口（已废弃，内部转发到引用计数系统）
	// ============================================================

	/**
	 * 挂载 Pak 文件 [已废弃 — 请使用 RequestMount]
	 *
	 * 内部转发到 RequestMount。
	 */
	UE_DEPRECATED(5.7, "Use RequestMount instead.")
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestMount instead."))
	bool MountPak(const FString& PakPath, int32 PakOrder = 0, const FString& EncryptionKey = TEXT(""));

	/**
	 * 卸载 Pak 文件 [已废弃 — 请使用 RequestUnmount]
	 *
	 * 内部强制 RefCount=0 并执行立即卸载（不走延迟路径），保持向后兼容行为。
	 */
	UE_DEPRECATED(5.7, "Use RequestUnmount instead.")
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak", meta = (DeprecatedFunction, DeprecationMessage = "Use RequestUnmount instead."))
	bool UnmountPak(const FString& PakPath);

	/**
	 * 检查 Pak 是否已挂载 — 查询 PakRecords 中 bIsMounted 字段
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|Pak")
	bool IsPakMounted(const FString& PakPath) const;

	/// 获取 Pak 文件条目详细信息
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|Pak")
	TArray<FHotUpdatePakEntry> GetPakEntries(const FString& PakPath);

	// == 事件委托 ==

	/// Pak 实际挂载完成事件（仅在 RefCount 0→1 时广播）
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPakMounted, const FString&, PakPath, bool, bSuccess);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnPakMounted OnPakMounted;

	/// Pak 实际卸载完成事件（仅在延迟卸载执行时广播）
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPakUnmounted, const FString&, PakPath);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnPakUnmounted OnPakUnmounted;

	/// Pak 容器可用事件（RegisterAvailablePak 后广播）
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPaksAvailable, const FString&, PakPath);
	UPROPERTY(BlueprintAssignable, Category = "HotUpdate|Events")
	FOnPaksAvailable OnPaksAvailable;

	// ============================================================
	// 资源弱引用跟踪接口
	// ============================================================

	/**
	 * 注册已加载资源的弱引用跟踪
	 *
	 * 在指定 Pak 的 Tracker 中注册一个资源弱引用。
	 * 当所有弱引用失效（被 GC 回收）时，Pak 可被自动卸载。
	 * @param PakPath    Pak 文件路径（将被规范化）
	 * @param Asset      加载的资源指针
	 * @param AssetPath  资源路径（用于索引和日志）
	 */
	void RegisterLoadedAsset(const FString& PakPath, UObject* Asset, const FString& AssetPath);

	/**
	 * 移除指定资源的弱引用跟踪（显式 Release 时调用）
	 *
	 * @param PakPath    Pak 文件路径
	 * @param AssetPath  资源路径
	 */
	void UnregisterLoadedAsset(const FString& PakPath, const FString& AssetPath);

	/**
	 * 扫描所有 Pak 的资源跟踪器，自动卸载全部资源已释放的 Pak
	 *
	 * 遍历 PakAssetTrackers：
	 *  1. CleanupStaleEntries() 清理已 GC 的弱引用
	 *  2. AreAllAssetsReleased() 检测是否全部释放
	 *  3. 收集待卸载列表，在锁外批量 RequestUnmount
	 */
	void ScanAndAutoUnmount();

	/**
	 * 获取指定 Pak 中仍存活的跟踪资源数
	 * @return 存活资源数；Pak 无跟踪记录返回 0
	 */
	int32 GetTrackedAssetCount(const FString& PakPath) const;

	/**
	 * 获取指定 Pak 中仍存活的资源路径列表（调试用）
	 */
	TArray<FString> GetTrackedAssetPaths(const FString& PakPath) const;

	/// 解析 Pak 元数据
	FHotUpdatePakMetadata ParsePakMetadata(const FString& PakPath);

	/// 生成 Pak 挂载顺序
	int32 CalculatePakOrder(const FHotUpdateVersionInfo& Version);

private:
	// ============================================================
	// 内部方法
	// ============================================================

	/** 实际执行 Pak 挂载（必须在游戏线程调用） */
	bool MountPakInternal(const FString& NormalizedPath, int32 PakOrder, const FString& EncryptionKey);

	/** 实际执行 Pak 卸载（必须在游戏线程调用） */
	bool UnmountPakInternal(const FString& NormalizedPath);

	/** 路径规范化 */
	static FString NormalizePakPath(const FString& PakPath);

	/** 处理待卸载队列 — 对 RefCount 仍为 0 的执行实际 Unmount */
	void ProcessPendingUnmounts();

	/** 从 FPakMountRecord 构建 FPakMountInfo */
	static FPakMountInfo MakeMountInfo(const FString& Path, const FPakMountRecord& Record);

	// ============================================================
	// 数据成员
	// ============================================================

	/// Pak 存储目录
	UPROPERTY(Transient)
	FString PakDirectory;

	/**
	 * Pak 记录表
	 * Key = 规范化后的 PakPath
	 * 替代原来的 TArray<FHotUpdatePakMetadata> MountedPaks
	 */
	TMap<FString, FPakMountRecord> PakRecords;

	/** 待延迟卸载的路径列表 */
	TSet<FString> PendingUnmounts;

	/**
	 * 每个 Pak 的已加载资源跟踪器
	 * Key = 规范化 PakPath，Value = 该 Pak 的资源弱引用跟踪器
	 * 与 PakRecords 共享 PakRecordsMutex 保护
	 */
	TMap<FString, FPakLoadedAssetTracker> PakAssetTrackers;

	/** 保护 PakRecords 和 PendingUnmounts 的临界区 */
	mutable FCriticalSection PakRecordsMutex;

	/** 泄漏检测阈值（秒）— RefCount 持续 > 0 超过此时长输出警告 */
	static constexpr double LeakWarningThresholdSeconds = 300.0;
};