// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdatePakTypes.generated.h"

class UHotUpdatePakManager;

// ============================================================
// 资源弱引用跟踪类型
// ============================================================

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

	FLoadedAsset(UObject* InAsset, const FString& InAssetPath, const FString& InPakPath)
		: Asset(InAsset)
		, AssetPath(InAssetPath)
		, PakPath(InPakPath)
		, RegisterTime(FPlatformTime::Seconds())
	{
	}

	/// 检查资源是否仍然存活
	bool IsAssetAlive() const { return Asset.IsValid(); }
};

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

// ============================================================
// Asset-Pak 映射相关类型
// ============================================================

/**
 * Asset → Pak 映射查询结果
 *
 * 记录一个 Asset 所在的 Pak 文件路径和 ChunkId
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FAssetPakInfo
{
	GENERATED_BODY()

	/// Asset 所在 Pak 的相对路径（如 "pakchunk1-Windows.pak"）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	FString PakPath;

	/// Asset 所在的 ChunkId
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	int32 ChunkId;

	FAssetPakInfo()
		: ChunkId(-1)
	{
	}

	FAssetPakInfo(const FString& InPakPath, int32 InChunkId)
		: PakPath(InPakPath)
		, ChunkId(InChunkId)
	{
	}
};

/**
 * 依赖项的 Pak 信息
 *
 * 记录一个被依赖 Asset 的路径及其所在的 Pak 信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FAssetDependencyPakInfo
{
	GENERATED_BODY()

	/// 被依赖的 Asset 路径
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	FString AssetPath;

	/// 被依赖 Asset 所在的 Pak 路径
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	FString PakPath;

	/// 被依赖 Asset 所在的 ChunkId
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	int32 ChunkId;

	FAssetDependencyPakInfo()
		: ChunkId(-1)
	{
	}

	FAssetDependencyPakInfo(const FString& InAssetPath, const FString& InPakPath, int32 InChunkId)
		: AssetPath(InAssetPath)
		, PakPath(InPakPath)
		, ChunkId(InChunkId)
	{
	}
};

/**
 * Asset 依赖信息
 *
 * 包含一个 Asset 的 Hard/Soft 依赖列表及其所在的 Pak 信息，
 * 以及预计算的去重 Pak 列表
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FAssetDependencyInfo
{
	GENERATED_BODY()

	/// Hard 依赖（加载前必须 Mount 的依赖 Asset 的 Pak 信息）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	TArray<FAssetDependencyPakInfo> HardDeps;

	/// Soft 依赖（可延迟 Mount 的依赖 Asset 的 Pak 信息）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	TArray<FAssetDependencyPakInfo> SoftDeps;

	/// Hard 依赖关联的去重 Pak 路径列表（不含 Asset 自身所在的 Pak）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	TArray<FString> RequiredPaks;

	/// Soft 依赖关联的去重 Pak 路径列表
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|AssetPak")
	TArray<FString> OptionalPaks;
};

// ============================================================
// AutoMount Handle 与委托
// ============================================================

/**
 * 自动挂载的内部跟踪信息
 */
struct HOTUPDATE_API FAutoMountTrackingInfo
{
	/// 此次加载 Mount 的 Pak 路径列表
	TArray<FString> MountedPakPaths;

	/// 加载的资源弱引用
	TWeakObjectPtr<UObject> LoadedAsset;

	/// 引用计数（同一 AssetPath 可被多次加载）
	int32 RefCount = 1;
};

/**
 * 自动挂载资源句柄
 *
 * RAII 风格的资源持有句柄，构造时持有 Pak 引用，
 * 析构时自动释放引用（调用 RequestUnmount）。
 * 仅支持移动语义，禁止拷贝。
 */
struct HOTUPDATE_API FAutoMountAssetHandle
{
	/** 默认构造（无效 Handle） */
	FAutoMountAssetHandle()
		: bIsValid(false)
	{
	}

	/** 带参构造 */
	FAutoMountAssetHandle(
		UHotUpdatePakManager* InPakManager,
		const TArray<FString>& InMountedPakPaths,
		UObject* InLoadedAsset,
		const FString& InAssetPath);

	/** 析构 — 自动释放 Pak 引用 */
	~FAutoMountAssetHandle();

	/** 移动构造 */
	FAutoMountAssetHandle(FAutoMountAssetHandle&& Other) noexcept;

	/** 移动赋值 */
	FAutoMountAssetHandle& operator=(FAutoMountAssetHandle&& Other) noexcept;

	/** 禁止拷贝 */
	FAutoMountAssetHandle(const FAutoMountAssetHandle&) = delete;
	FAutoMountAssetHandle& operator=(const FAutoMountAssetHandle&) = delete;

	/** 手动释放（Unmount 所有关联 Pak 并清空） */
	void Release();

	/** 获取加载的资源 */
	UObject* GetAsset() const;

	/** 模板便捷方法：获取指定类型的资源 */
	template<typename T>
	T* GetAsset() const
	{
		return Cast<T>(GetAsset());
	}

	/** 是否有效 */
	bool IsValid() const;

	/** 获取 Asset 路径 */
	const FString& GetAssetPath() const { return AssetPath; }

	/** 获取 Mount 的 Pak 列表 */
	const TArray<FString>& GetMountedPakPaths() const { return MountedPakPaths; }

private:
	TWeakObjectPtr<UHotUpdatePakManager> PakManager;
	TArray<FString> MountedPakPaths;
	TWeakObjectPtr<UObject> LoadedAsset;
	FString AssetPath;
	bool bIsValid = false;
};

/** 单个资源自动挂载加载完成回调 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAutoMountLoadComplete, bool, bSuccess, UObject*, LoadedAsset);

/** 批量资源自动挂载加载完成回调 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAutoMountBatchComplete, bool, bSuccess, const TArray<UObject*>&, LoadedAssets);

/**
 * Pak 文件元数据
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdatePakMetadata
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakPath;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakName;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int32 ChunkId;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	int64 PakSize;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FString PakHash;

	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	bool bIsMounted;

	/// 版本信息
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate")
	FHotUpdateVersionInfo Version;

	FHotUpdatePakMetadata()
		: PakSize(0)
		, bIsMounted(false)
	{
	}
};

/**
 * Pak 文件条目信息
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FHotUpdatePakEntry
{
	GENERATED_BODY()

	/// 文件在 Pak 中的路径
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	FString FileName;

	/// 文件大小（原始大小）
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 UncompressedSize;

	/// 压缩后大小
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 CompressedSize;

	/// 文件偏移量
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 Offset;

	/// 是否压缩
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsCompressed;

	/// 是否加密
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsEncrypted;

	/// SHA1 Hash
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	FString FileHash;

	FHotUpdatePakEntry()
		: UncompressedSize(0)
		, CompressedSize(0)
		, Offset(0)
		, bIsCompressed(false)
		, bIsEncrypted(false)
	{
	}
};

/**
 * Pak 挂载记录（内部使用）
 *
 * 管理每个 Pak/IoStore 容器的引用计数和挂载状态
 */
struct HOTUPDATE_API FPakMountRecord
{
	/// 原有元数据
	FHotUpdatePakMetadata Metadata;

	/// 引用计数
	int32 RefCount = 0;

	/// 实际挂载状态
	bool bIsMounted = false;

	/// 是否已注册（可用但可能未挂载）
	bool bIsRegistered = false;

	/// 挂载时使用的 PakOrder
	int32 PakOrder = 0;

	/// 挂载时使用的加密密钥
	FString EncryptionKey;

	/// RefCount 归零时的时间戳（用于泄漏检测）
	double LastZeroRefCountTime = 0.0;

	FPakMountRecord() = default;
};

/**
 * Pak 挂载信息（查询接口返回）
 *
 * 提供 Pak 容器的外部可见状态快照
 */
USTRUCT(BlueprintType)
struct HOTUPDATE_API FPakMountInfo
{
	GENERATED_BODY()

	/// Pak 文件路径
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	FString PakPath;

	/// 引用计数
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int32 RefCount;

	/// 所属 ChunkId
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int32 ChunkId;

	/// 是否已挂载
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsMounted;

	/// 是否已注册
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	bool bIsRegistered;

	/// 文件大小
	UPROPERTY(BlueprintReadOnly, Category = "HotUpdate|Pak")
	int64 PakSize;

	FPakMountInfo()
		: RefCount(0)
		, ChunkId(-1)
		, bIsMounted(false)
		, bIsRegistered(false)
		, PakSize(0)
	{
	}
};

