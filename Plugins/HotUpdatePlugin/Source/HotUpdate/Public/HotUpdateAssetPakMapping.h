// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdatePakTypes.h"
#include "HotUpdateAssetPakMapping.generated.h"

/**
 * 运行时 Asset-Pak 映射查询
 *
 * 在热更完成后加载 asset_pak_manifest.json 和 asset_dependencies.json，
 * 构建内存索引用于快速查询 Asset → Pak 映射和依赖 Pak 信息。
 *
 * 使用方式：
 *  1. ApplyUpdate 成功后调用 LoadManifest + LoadDependencies
 *  2. 通过 GetPakForAsset / GetRequiredPaksForAsset 查询
 *  3. AutoMountLoader 内部使用此类进行查询
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateAssetPakMapping : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 加载 Asset-Pak Manifest 文件
	 * @param ManifestDir  包含 asset_pak_manifest.json 的目录路径
	 * @return true 表示加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AssetPak")
	bool LoadManifest(const FString& ManifestDir);

	/**
	 * 加载 Asset 依赖信息文件
	 * @param ManifestDir  包含 asset_dependencies.json 的目录路径
	 * @return true 表示加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AssetPak")
	bool LoadDependencies(const FString& ManifestDir);

	/**
	 * 查询 Asset 所在的 Pak 路径
	 * @return Pak 相对路径，未找到返回空串
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AssetPak")
	FString GetPakForAsset(const FString& AssetPath) const;

	/**
	 * 查询 Asset 所在的 ChunkId
	 * @return ChunkId，未找到返回 -1
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AssetPak")
	int32 GetChunkIdForAsset(const FString& AssetPath) const;

	/**
	 * 获取加载指定 Asset 需要 Mount 的全部 Pak 路径（含 Hard 依赖 + 自身 Pak）
	 * @return 去重的 Pak 路径列表
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AssetPak")
	TArray<FString> GetRequiredPaksForAsset(const FString& AssetPath) const;

	/**
	 * 获取加载指定 Asset 的可选 Pak 路径（Soft 依赖）
	 * @return 去重的可选 Pak 路径列表
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AssetPak")
	TArray<FString> GetOptionalPaksForAsset(const FString& AssetPath) const;

	/**
	 * 获取指定 Pak 中包含的全部 Asset 列表
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AssetPak")
	TArray<FString> GetAssetsInPak(const FString& PakPath) const;

	/**
	 * 检查 Manifest 是否已加载
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AssetPak")
	bool IsManifestLoaded() const { return bIsLoaded; }

	/**
	 * 获取已加载的 Asset 总数
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AssetPak")
	int32 GetAssetCount() const { return AssetToPakMap.Num(); }

	/**
	 * 获取已加载的 Pak 总数
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AssetPak")
	int32 GetPakCount() const { return PakToAssetsMap.Num(); }

private:
	/**
	 * 统一 Asset 路径格式
	 * 去除后缀、统一斜杠方向、小写化用于查找
	 */
	static FString NormalizeAssetPath(const FString& AssetPath);

	/** Asset → Pak 映射 */
	TMap<FString, FAssetPakInfo> AssetToPakMap;

	/** Pak → Asset 列表 */
	TMap<FString, TArray<FString>> PakToAssetsMap;

	/** Asset → 依赖 Pak 信息 */
	TMap<FString, FAssetDependencyInfo> AssetDependencies;

	/** 是否已加载 Manifest */
	bool bIsLoaded = false;

	/** 是否已加载依赖信息 */
	bool bDependenciesLoaded = false;
};
