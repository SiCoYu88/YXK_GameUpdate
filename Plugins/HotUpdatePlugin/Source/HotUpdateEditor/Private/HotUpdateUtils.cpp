// Copyright czm. All Rights Reserved.

#include "HotUpdateUtils.h"
#include "HotUpdatePackageHelper.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 本地化文本命名空间
#define LOCTEXT_NAMESPACE "HotUpdateUtils"

FString HotUpdateUtils::GetPlatformString(EHotUpdatePlatform Platform)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return TEXT("Windows");
	case EHotUpdatePlatform::Android:
		return TEXT("Android");
	case EHotUpdatePlatform::IOS:
		return TEXT("IOS");
	default:
		return TEXT("Windows");
	}
}

FString HotUpdateUtils::GetPlatformDirectoryName(EHotUpdatePlatform Platform)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return TEXT("Win64");
	case EHotUpdatePlatform::Android:
		return TEXT("Android");
	case EHotUpdatePlatform::IOS:
		return TEXT("IOS");
	default:
		return TEXT("Win64");
	}
}

FString HotUpdateUtils::GetPlatformDirName(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat)
{
	FString PlatformDir = GetPlatformString(Platform);

	// Android 平台带纹理格式时，目录名为 Android_ASTC / Android_ETC2 / Android_DXT
	if (Platform == EHotUpdatePlatform::Android && AndroidTextureFormat != EHotUpdateAndroidTextureFormat::Multi)
	{
		FString TextureFormat;
		switch (AndroidTextureFormat)
		{
		case EHotUpdateAndroidTextureFormat::ETC2: TextureFormat = TEXT("ETC2"); break;
		case EHotUpdateAndroidTextureFormat::ASTC: TextureFormat = TEXT("ASTC"); break;
		case EHotUpdateAndroidTextureFormat::DXT:  TextureFormat = TEXT("DXT");  break;
		default: break;
		}
		if (!TextureFormat.IsEmpty())
		{
			PlatformDir = FString::Printf(TEXT("Android_%s"), *TextureFormat);
		}
	}

	return PlatformDir;
}

FString HotUpdateUtils::GetCookedPlatformDir(EHotUpdatePlatform Platform)
{
	return FHotUpdatePackageHelper::GetCookedPlatformDir(Platform);
}

FString HotUpdateUtils::GetCookedPlatformDir(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat)
{
	return FHotUpdatePackageHelper::GetCookedPlatformDir(Platform, AndroidTextureFormat);
}

FString HotUpdateUtils::ExtractVersionFromManifest(const FString& ManifestPath)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		return TEXT("");
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return TEXT("");
	}

	// 新格式：version.version
	const TSharedPtr<FJsonObject>* VersionObj;
	if (JsonObject->TryGetObjectField(TEXT("version"), VersionObj))
	{
		FString Version;
		if (VersionObj->Get()->TryGetStringField(TEXT("version"), Version))
		{
			return Version;
		}
	}

	// 旧格式：versionInfo.versionString
	const TSharedPtr<FJsonObject>* VersionInfoObj;
	if (JsonObject->TryGetObjectField(TEXT("versionInfo"), VersionInfoObj))
	{
		FString VersionString;
		if (VersionInfoObj->Get()->TryGetStringField(TEXT("versionString"), VersionString))
		{
			return VersionString;
		}
	}

	return TEXT("");
}

void HotUpdateUtils::CalculateAssetDiff(
	const TArray<FString>& CurrentPaths,
	const TMap<FString, FString>& CurrentHashes,
	const TMap<FString, int64>& CurrentSizes,
	const TMap<FString, FString>& BaseHashes,
	const TMap<FString, int64>& BaseSizes,
	TArray<FString>& OutChangedAssets,
	FHotUpdateDiffReport& OutReport)
{
	// 收集所有路径
	TSet<FString> AllPaths;
	for (const auto& Pair : BaseHashes) AllPaths.Add(Pair.Key);
	for (const FString& Path : CurrentPaths) AllPaths.Add(Path);

	for (const FString& Path : AllPaths)
	{
		const bool bInBase = BaseHashes.Contains(Path);
		const bool bInCurrent = CurrentHashes.Contains(Path);

		FHotUpdateAssetDiff Diff;
		Diff.AssetPath = Path;

		if (!bInBase && bInCurrent)
		{
			// 新增资源
			Diff.ChangeType = EHotUpdateFileChangeType::Added;
			Diff.NewHash = CurrentHashes[Path];
			Diff.NewSize = CurrentSizes.Contains(Path) ? CurrentSizes[Path] : 0;
			Diff.ChangeDescription = FString::Printf(TEXT("新增资源 (%lld bytes)"), Diff.NewSize);
			OutReport.AddedAssets.Add(Diff);
			OutChangedAssets.Add(Path);
		}
		else if (bInBase && !bInCurrent)
		{
			// 删除资源
			Diff.ChangeType = EHotUpdateFileChangeType::Deleted;
			Diff.OldHash = BaseHashes[Path];
			Diff.OldSize = BaseSizes.Contains(Path) ? BaseSizes[Path] : 0;
			Diff.ChangeDescription = FString::Printf(TEXT("删除资源 (%lld bytes)"), Diff.OldSize);
			OutReport.DeletedAssets.Add(Diff);
		}
		else if (bInBase && bInCurrent)
		{
			if (BaseHashes[Path] != CurrentHashes[Path])
			{
				// 修改资源
				Diff.ChangeType = EHotUpdateFileChangeType::Modified;
				Diff.OldHash = BaseHashes[Path];
				Diff.NewHash = CurrentHashes[Path];
				Diff.OldSize = BaseSizes.Contains(Path) ? BaseSizes[Path] : 0;
				Diff.NewSize = CurrentSizes.Contains(Path) ? CurrentSizes[Path] : 0;
				int64 SizeDiff = Diff.NewSize - Diff.OldSize;
				Diff.ChangeDescription = FString::Printf(TEXT("修改资源 (大小变化: %lld bytes)"), SizeDiff);
				OutReport.ModifiedAssets.Add(Diff);
				OutChangedAssets.Add(Path);
			}
			else
			{
				// 未变更
				Diff.ChangeType = EHotUpdateFileChangeType::Unchanged;
				Diff.OldHash = BaseHashes[Path];
				Diff.NewHash = CurrentHashes[Path];
				Diff.OldSize = BaseSizes.Contains(Path) ? BaseSizes[Path] : 0;
				Diff.NewSize = CurrentSizes.Contains(Path) ? CurrentSizes[Path] : 0;
				OutReport.UnchangedAssets.Add(Diff);
			}
		}
	}
}

FText HotUpdateUtils::GetPlatformDisplayName(EHotUpdatePlatform Platform)
{
	switch (Platform)
	{
	case EHotUpdatePlatform::Windows:
		return LOCTEXT("PlatformWindows", "Windows");
	case EHotUpdatePlatform::Android:
		return LOCTEXT("PlatformAndroid", "Android");
	case EHotUpdatePlatform::IOS:
		return LOCTEXT("PlatformIOS", "iOS");
	default:
		return LOCTEXT("PlatformWindows", "Windows");
	}
}

FText HotUpdateUtils::GetTextureFormatDisplayName(EHotUpdateAndroidTextureFormat TextureFormat)
{
	switch (TextureFormat)
	{
	case EHotUpdateAndroidTextureFormat::ETC2:
		return LOCTEXT("TextureFormatETC2", "ETC2");
	case EHotUpdateAndroidTextureFormat::ASTC:
		return LOCTEXT("TextureFormatASTC", "ASTC");
	case EHotUpdateAndroidTextureFormat::DXT:
		return LOCTEXT("TextureFormatDXT", "DXT");
	case EHotUpdateAndroidTextureFormat::Multi:
		return LOCTEXT("TextureFormatMulti", "Multi");
	default:
		return LOCTEXT("TextureFormatETC2", "ETC2");
	}
}

FText HotUpdateUtils::GetBuildConfigDisplayName(EHotUpdateBuildConfiguration BuildConfig)
{
	switch (BuildConfig)
	{
	case EHotUpdateBuildConfiguration::DebugGame:
		return LOCTEXT("BuildConfigDebugGame", "DebugGame (包含调试信息)");
	case EHotUpdateBuildConfiguration::Development:
		return LOCTEXT("BuildConfigDevelopment", "Development");
	case EHotUpdateBuildConfiguration::Shipping:
		return LOCTEXT("BuildConfigShipping", "Shipping (发布构建)");
	default:
		return LOCTEXT("BuildConfigDevelopment", "Development");
	}
}

FText HotUpdateUtils::GetChunkStrategyDisplayName(EHotUpdateChunkStrategy ChunkStrategy)
{
	switch (ChunkStrategy)
	{
	case EHotUpdateChunkStrategy::None:
		return LOCTEXT("ChunkStrategyNone", "不分包");
	case EHotUpdateChunkStrategy::Size:
		return LOCTEXT("ChunkStrategySize", "按大小分包");
	default:
		return LOCTEXT("ChunkStrategyNone", "不分包");
	}
}

FText HotUpdateUtils::GetDependencyStrategyDisplayName(EHotUpdateDependencyStrategy DependencyStrategy)
{
	switch (DependencyStrategy)
	{
	case EHotUpdateDependencyStrategy::IncludeAll:
		return LOCTEXT("DependencyIncludeAll", "包含所有依赖");
	case EHotUpdateDependencyStrategy::HardOnly:
		return LOCTEXT("DependencyHardOnly", "仅硬依赖");
	case EHotUpdateDependencyStrategy::SoftOnly:
		return LOCTEXT("DependencySoftOnly", "仅软依赖");
	case EHotUpdateDependencyStrategy::None:
		return LOCTEXT("DependencyNone", "不包含依赖");
	default:
		return LOCTEXT("DependencyHardOnly", "仅硬依赖");
	}
}

FString HotUpdateUtils::FormatFileSize(int64 Size)
{
	if (Size < 1024)
	{
		return FString::Printf(TEXT("%lld B"), Size);
	}
	else if (Size < 1024 * 1024)
	{
		return FString::Printf(TEXT("%.2f KB"), Size / 1024.0);
	}
	else if (Size < 1024 * 1024 * 1024)
	{
		return FString::Printf(TEXT("%.2f MB"), Size / (1024.0 * 1024.0));
	}
	else
	{
		return FString::Printf(TEXT("%.2f GB"), Size / (1024.0 * 1024.0 * 1024.0));
	}
}

#undef LOCTEXT_NAMESPACE