// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdateTypes.h"
#include "HotUpdatePakTypes.generated.h"

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

