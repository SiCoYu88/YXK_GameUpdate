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

