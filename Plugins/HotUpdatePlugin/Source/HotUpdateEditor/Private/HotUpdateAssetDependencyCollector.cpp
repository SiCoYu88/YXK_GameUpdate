// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetDependencyCollector.h"
#include "HotUpdateEditor.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

bool FHotUpdateAssetDependencyCollector::Collect(
	const FString& AssetPakManifestPath,
	const FString& OutputDir,
	const FString& Version)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetDependency] Collecting dependencies..."));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetDependency] ManifestPath: %s"), *AssetPakManifestPath);

	// 1. 加载 Asset→Pak 映射
	TMap<FString, FString> AssetToPak;
	TMap<FString, int32> AssetToChunk;

	if (!LoadAssetPakMapping(AssetPakManifestPath, AssetToPak, AssetToChunk))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("[AssetDependency] Failed to load asset_pak_manifest.json"));
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetDependency] Loaded %d asset-pak mappings"), AssetToPak.Num());

	// 2. 获取 AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// 确保 AssetRegistry 加载完成
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.SearchAllAssets(true);
	}

	// 3. 构建依赖 JSON
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField(TEXT("version"), Version);

	TSharedPtr<FJsonObject> DependenciesObject = MakeShareable(new FJsonObject);

	int32 ProcessedCount = 0;
	int32 WithDepsCount = 0;

	for (const auto& AssetPair : AssetToPak)
	{
		const FString& AssetPath = AssetPair.Key;
		const FString& OwnerPakPath = AssetPair.Value;

		// 查询 Hard 依赖
		TArray<FName> HardDependencies;
		AssetRegistry.GetDependencies(
			FName(*AssetPath),
			HardDependencies,
			UE::AssetRegistry::EDependencyCategory::Package);

		// 查询 Soft 依赖
		// 注意：UE5 中 SearchableNames 和 Manage 类别也存在，但我们只关注 Package 类别
		// Soft 依赖通过 EDependencyProperty::Soft 过滤
		TArray<FName> AllDependencies;
		AssetRegistry.GetDependencies(
			FName(*AssetPath),
			AllDependencies);

		// 区分 Hard 和 Soft（Hard 已通过 Package 类别获取，Soft = All - Hard）
		TSet<FName> HardDepSet(HardDependencies);
		TArray<FName> SoftDependencies;
		for (const FName& Dep : AllDependencies)
		{
			if (!HardDepSet.Contains(Dep))
			{
				SoftDependencies.Add(Dep);
			}
		}

		// 构建依赖 Pak 信息
		TArray<TSharedPtr<FJsonValue>> HardDepsArray;
		TArray<TSharedPtr<FJsonValue>> SoftDepsArray;
		TSet<FString> RequiredPakSet;
		TSet<FString> OptionalPakSet;

		// 处理 Hard 依赖
		for (const FName& DepName : HardDependencies)
		{
			FString DepPath = DepName.ToString();

			// 过滤掉脚本包和引擎核心包
			if (DepPath.StartsWith(TEXT("/Script/")) || DepPath.StartsWith(TEXT("/Engine/")))
			{
				continue;
			}

			const FString* DepPakPath = AssetToPak.Find(DepPath);
			const int32* DepChunkId = AssetToChunk.Find(DepPath);

			if (DepPakPath && DepChunkId)
			{
				TSharedPtr<FJsonObject> DepObj = MakeShareable(new FJsonObject);
				DepObj->SetStringField(TEXT("assetPath"), DepPath);
				DepObj->SetStringField(TEXT("pakPath"), *DepPakPath);
				DepObj->SetNumberField(TEXT("chunkId"), *DepChunkId);
				HardDepsArray.Add(MakeShareable(new FJsonValueObject(DepObj)));

				// 排除自身所在的 Pak
				if (*DepPakPath != OwnerPakPath)
				{
					RequiredPakSet.Add(*DepPakPath);
				}
			}
		}

		// 处理 Soft 依赖
		for (const FName& DepName : SoftDependencies)
		{
			FString DepPath = DepName.ToString();

			if (DepPath.StartsWith(TEXT("/Script/")) || DepPath.StartsWith(TEXT("/Engine/")))
			{
				continue;
			}

			const FString* DepPakPath = AssetToPak.Find(DepPath);
			const int32* DepChunkId = AssetToChunk.Find(DepPath);

			if (DepPakPath && DepChunkId)
			{
				TSharedPtr<FJsonObject> DepObj = MakeShareable(new FJsonObject);
				DepObj->SetStringField(TEXT("assetPath"), DepPath);
				DepObj->SetStringField(TEXT("pakPath"), *DepPakPath);
				DepObj->SetNumberField(TEXT("chunkId"), *DepChunkId);
				SoftDepsArray.Add(MakeShareable(new FJsonValueObject(DepObj)));

				if (*DepPakPath != OwnerPakPath && !RequiredPakSet.Contains(*DepPakPath))
				{
					OptionalPakSet.Add(*DepPakPath);
				}
			}
		}

		// 只记录有跨 Pak 依赖的 Asset（减少文件大小）
		if (HardDepsArray.Num() > 0 || SoftDepsArray.Num() > 0)
		{
			TSharedPtr<FJsonObject> AssetDepObj = MakeShareable(new FJsonObject);
			AssetDepObj->SetArrayField(TEXT("hardDeps"), HardDepsArray);
			AssetDepObj->SetArrayField(TEXT("softDeps"), SoftDepsArray);

			// requiredPaks
			TArray<TSharedPtr<FJsonValue>> RequiredPaksArray;
			for (const FString& Pak : RequiredPakSet)
			{
				RequiredPaksArray.Add(MakeShareable(new FJsonValueString(Pak)));
			}
			AssetDepObj->SetArrayField(TEXT("requiredPaks"), RequiredPaksArray);

			// optionalPaks
			TArray<TSharedPtr<FJsonValue>> OptionalPaksArray;
			for (const FString& Pak : OptionalPakSet)
			{
				OptionalPaksArray.Add(MakeShareable(new FJsonValueString(Pak)));
			}
			AssetDepObj->SetArrayField(TEXT("optionalPaks"), OptionalPaksArray);

			DependenciesObject->SetObjectField(AssetPath, AssetDepObj);
			WithDepsCount++;
		}

		ProcessedCount++;
	}

	RootObject->SetObjectField(TEXT("dependencies"), DependenciesObject);

	// 4. 序列化并写入文件
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	FString OutputPath = FPaths::Combine(OutputDir, TEXT("asset_dependencies.json"));
	if (!FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("[AssetDependency] Failed to save dependencies to: %s"), *OutputPath);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetDependency] Dependencies saved: %s (%d assets processed, %d with cross-pak deps)"),
		*OutputPath, ProcessedCount, WithDepsCount);

	return true;
}

bool FHotUpdateAssetDependencyCollector::LoadAssetPakMapping(
	const FString& ManifestPath,
	TMap<FString, FString>& OutAssetToPak,
	TMap<FString, int32>& OutAssetToChunk)
{
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *ManifestPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	// 从 assetIndex 构建映射（更高效）
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

				OutAssetToPak.Add(Pair.Key, PakPath);
				OutAssetToChunk.Add(Pair.Key, ChunkId);
			}
		}
		return true;
	}

	// 回退：从 paks 数组构建
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
					for (const auto& AssetValue : *AssetsPtr)
					{
						FString AssetPath = AssetValue->AsString();
						OutAssetToPak.Add(AssetPath, PakPath);
						OutAssetToChunk.Add(AssetPath, ChunkId);
					}
				}
			}
		}
		return true;
	}

	return false;
}
