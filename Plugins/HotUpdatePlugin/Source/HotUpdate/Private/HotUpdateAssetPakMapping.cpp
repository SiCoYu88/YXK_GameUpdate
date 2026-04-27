// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetPakMapping.h"
#include "HotUpdate.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UHotUpdateAssetPakMapping::LoadManifest(const FString& ManifestDir)
{
	FString ManifestPath = FPaths::Combine(ManifestDir, TEXT("asset_pak_manifest.json"));

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *ManifestPath))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AssetPakMapping] Failed to load manifest: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[AssetPakMapping] Failed to parse manifest JSON"));
		return false;
	}

	// 清空旧数据
	AssetToPakMap.Empty();
	PakToAssetsMap.Empty();

	// 优先从 assetIndex 加载（O(N) 直接构建 Map）
	const TSharedPtr<FJsonObject>* AssetIndexPtr;
	if (JsonObject->TryGetObjectField(TEXT("assetIndex"), AssetIndexPtr) && AssetIndexPtr)
	{
		for (const auto& Pair : (*AssetIndexPtr)->Values)
		{
			const TSharedPtr<FJsonObject>* EntryPtr;
			if (Pair.Value.IsValid() && Pair.Value->TryGetObject(EntryPtr) && EntryPtr)
			{
				FString PakPath;
				(*EntryPtr)->TryGetStringField(TEXT("pakPath"), PakPath);
				int32 ChunkId = (*EntryPtr)->GetIntegerField(TEXT("chunkId"));

				FString NormalizedAssetPath = NormalizeAssetPath(Pair.Key);
				AssetToPakMap.Add(NormalizedAssetPath, FAssetPakInfo(PakPath, ChunkId));

				// 同时构建反向映射
				PakToAssetsMap.FindOrAdd(PakPath).AddUnique(NormalizedAssetPath);
			}
		}
	}
	else
	{
		// 回退：从 paks 数组加载
		const TArray<TSharedPtr<FJsonValue>>* PaksArrayPtr;
		if (JsonObject->TryGetArrayField(TEXT("paks"), PaksArrayPtr) && PaksArrayPtr)
		{
			for (const auto& PakValue : *PaksArrayPtr)
			{
				const TSharedPtr<FJsonObject>* PakObjPtr;
				if (PakValue.IsValid() && PakValue->TryGetObject(PakObjPtr) && PakObjPtr)
				{
					FString PakPath;
					(*PakObjPtr)->TryGetStringField(TEXT("pakPath"), PakPath);
					int32 ChunkId = (*PakObjPtr)->GetIntegerField(TEXT("chunkId"));

					const TArray<TSharedPtr<FJsonValue>>* AssetsPtr;
					if ((*PakObjPtr)->TryGetArrayField(TEXT("assets"), AssetsPtr) && AssetsPtr)
					{
						TArray<FString>& PakAssets = PakToAssetsMap.FindOrAdd(PakPath);
						for (const auto& AssetValue : *AssetsPtr)
						{
							FString AssetPath = NormalizeAssetPath(AssetValue->AsString());
							AssetToPakMap.Add(AssetPath, FAssetPakInfo(PakPath, ChunkId));
							PakAssets.AddUnique(AssetPath);
						}
					}
				}
			}
		}
	}

	bIsLoaded = AssetToPakMap.Num() > 0;

	UE_LOG(LogHotUpdate, Log, TEXT("[AssetPakMapping] Manifest loaded: %d assets in %d paks"),
		AssetToPakMap.Num(), PakToAssetsMap.Num());

	return bIsLoaded;
}

bool UHotUpdateAssetPakMapping::LoadDependencies(const FString& ManifestDir)
{
	FString DepsPath = FPaths::Combine(ManifestDir, TEXT("asset_dependencies.json"));

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *DepsPath))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AssetPakMapping] Failed to load dependencies: %s"), *DepsPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[AssetPakMapping] Failed to parse dependencies JSON"));
		return false;
	}

	AssetDependencies.Empty();

	const TSharedPtr<FJsonObject>* DepsObjectPtr;
	if (!JsonObject->TryGetObjectField(TEXT("dependencies"), DepsObjectPtr) || !DepsObjectPtr)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AssetPakMapping] No 'dependencies' field in JSON"));
		return false;
	}

	for (const auto& Pair : (*DepsObjectPtr)->Values)
	{
		FString NormalizedAssetPath = NormalizeAssetPath(Pair.Key);

		const TSharedPtr<FJsonObject>* DepObjPtr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(DepObjPtr) || !DepObjPtr)
		{
			continue;
		}

		FAssetDependencyInfo DepInfo;

		// Hard 依赖
		const TArray<TSharedPtr<FJsonValue>>* HardDepsPtr;
		if ((*DepObjPtr)->TryGetArrayField(TEXT("hardDeps"), HardDepsPtr) && HardDepsPtr)
		{
			for (const auto& HardDepValue : *HardDepsPtr)
			{
				const TSharedPtr<FJsonObject>* HardDepObjPtr;
				if (HardDepValue.IsValid() && HardDepValue->TryGetObject(HardDepObjPtr) && HardDepObjPtr)
				{
					FAssetDependencyPakInfo Info;
					(*HardDepObjPtr)->TryGetStringField(TEXT("assetPath"), Info.AssetPath);
					(*HardDepObjPtr)->TryGetStringField(TEXT("pakPath"), Info.PakPath);
					Info.ChunkId = (*HardDepObjPtr)->GetIntegerField(TEXT("chunkId"));
					DepInfo.HardDeps.Add(Info);
				}
			}
		}

		// Soft 依赖
		const TArray<TSharedPtr<FJsonValue>>* SoftDepsPtr;
		if ((*DepObjPtr)->TryGetArrayField(TEXT("softDeps"), SoftDepsPtr) && SoftDepsPtr)
		{
			for (const auto& SoftDepValue : *SoftDepsPtr)
			{
				const TSharedPtr<FJsonObject>* SoftDepObjPtr;
				if (SoftDepValue.IsValid() && SoftDepValue->TryGetObject(SoftDepObjPtr) && SoftDepObjPtr)
				{
					FAssetDependencyPakInfo Info;
					(*SoftDepObjPtr)->TryGetStringField(TEXT("assetPath"), Info.AssetPath);
					(*SoftDepObjPtr)->TryGetStringField(TEXT("pakPath"), Info.PakPath);
					Info.ChunkId = (*SoftDepObjPtr)->GetIntegerField(TEXT("chunkId"));
					DepInfo.SoftDeps.Add(Info);
				}
			}
		}

		// requiredPaks
		const TArray<TSharedPtr<FJsonValue>>* RequiredPaksPtr;
		if ((*DepObjPtr)->TryGetArrayField(TEXT("requiredPaks"), RequiredPaksPtr) && RequiredPaksPtr)
		{
			for (const auto& PakValue : *RequiredPaksPtr)
			{
				DepInfo.RequiredPaks.Add(PakValue->AsString());
			}
		}

		// optionalPaks
		const TArray<TSharedPtr<FJsonValue>>* OptionalPaksPtr;
		if ((*DepObjPtr)->TryGetArrayField(TEXT("optionalPaks"), OptionalPaksPtr) && OptionalPaksPtr)
		{
			for (const auto& PakValue : *OptionalPaksPtr)
			{
				DepInfo.OptionalPaks.Add(PakValue->AsString());
			}
		}

		AssetDependencies.Add(NormalizedAssetPath, MoveTemp(DepInfo));
	}

	bDependenciesLoaded = AssetDependencies.Num() > 0;

	UE_LOG(LogHotUpdate, Log, TEXT("[AssetPakMapping] Dependencies loaded: %d assets with cross-pak deps"),
		AssetDependencies.Num());

	return true;
}

FString UHotUpdateAssetPakMapping::GetPakForAsset(const FString& AssetPath) const
{
	FString Normalized = NormalizeAssetPath(AssetPath);
	const FAssetPakInfo* Info = AssetToPakMap.Find(Normalized);
	return Info ? Info->PakPath : FString();
}

int32 UHotUpdateAssetPakMapping::GetChunkIdForAsset(const FString& AssetPath) const
{
	FString Normalized = NormalizeAssetPath(AssetPath);
	const FAssetPakInfo* Info = AssetToPakMap.Find(Normalized);
	return Info ? Info->ChunkId : -1;
}

TArray<FString> UHotUpdateAssetPakMapping::GetRequiredPaksForAsset(const FString& AssetPath) const
{
	FString Normalized = NormalizeAssetPath(AssetPath);

	TSet<FString> ResultSet;

	// 主资源自身所在的 Pak
	const FAssetPakInfo* SelfInfo = AssetToPakMap.Find(Normalized);
	if (SelfInfo)
	{
		ResultSet.Add(SelfInfo->PakPath);
	}

	// Hard 依赖的 Pak
	const FAssetDependencyInfo* DepInfo = AssetDependencies.Find(Normalized);
	if (DepInfo)
	{
		for (const FString& RequiredPak : DepInfo->RequiredPaks)
		{
			ResultSet.Add(RequiredPak);
		}
	}

	return ResultSet.Array();
}

TArray<FString> UHotUpdateAssetPakMapping::GetOptionalPaksForAsset(const FString& AssetPath) const
{
	FString Normalized = NormalizeAssetPath(AssetPath);

	const FAssetDependencyInfo* DepInfo = AssetDependencies.Find(Normalized);
	if (DepInfo)
	{
		return DepInfo->OptionalPaks;
	}

	return TArray<FString>();
}

TArray<FString> UHotUpdateAssetPakMapping::GetAssetsInPak(const FString& PakPath) const
{
	const TArray<FString>* Assets = PakToAssetsMap.Find(PakPath);
	return Assets ? *Assets : TArray<FString>();
}

FString UHotUpdateAssetPakMapping::NormalizeAssetPath(const FString& AssetPath)
{
	FString Result = AssetPath;

	// 去除常见后缀
	Result.RemoveFromEnd(TEXT(".uasset"));
	Result.RemoveFromEnd(TEXT(".umap"));
	Result.RemoveFromEnd(TEXT(".uexp"));
	Result.RemoveFromEnd(TEXT(".ubulk"));

	// 统一斜杠方向
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));

	// 去除末尾斜杠
	while (Result.EndsWith(TEXT("/")))
	{
		Result.RemoveFromEnd(TEXT("/"));
	}

	return Result;
}
