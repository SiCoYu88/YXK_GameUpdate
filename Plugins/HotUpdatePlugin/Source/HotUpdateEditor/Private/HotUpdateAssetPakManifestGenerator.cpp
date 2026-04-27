// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetPakManifestGenerator.h"
#include "HotUpdateEditor.h"
#include "IPlatformFilePak.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"

bool FHotUpdateAssetPakManifestGenerator::Generate(
	const FString& PakSearchDir,
	const FString& OutputDir,
	const FString& Version,
	const FString& Platform)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] Generating manifest..."));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] PakSearchDir: %s"), *PakSearchDir);
	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] OutputDir: %s"), *OutputDir);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*PakSearchDir))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("[AssetPakManifest] PakSearchDir does not exist: %s"), *PakSearchDir);
		return false;
	}

	// 查找所有 .pak 文件
	TArray<FString> PakFiles;
	PlatformFile.FindFilesRecursively(PakFiles, *PakSearchDir, TEXT(".pak"));

	if (PakFiles.Num() == 0)
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("[AssetPakManifest] No .pak files found in: %s"), *PakSearchDir);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] Found %d .pak files"), PakFiles.Num());

	// 构建 JSON
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField(TEXT("version"), Version);
	RootObject->SetStringField(TEXT("platform"), Platform);
	RootObject->SetStringField(TEXT("buildTime"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> PaksArray;
	TSharedPtr<FJsonObject> AssetIndexObject = MakeShareable(new FJsonObject);

	int32 TotalAssetCount = 0;

	for (const FString& PakFilePath : PakFiles)
	{
		FString PakFileName = FPaths::GetCleanFilename(PakFilePath);
		int32 ChunkId = ParseChunkIdFromPakName(PakFileName);

		// 计算相对路径
		FString RelativePakPath = PakFilePath;
		FPaths::MakePathRelativeTo(RelativePakPath, *(PakSearchDir / TEXT("")));

		UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] Processing: %s (ChunkId=%d)"), *PakFileName, ChunkId);

		// 使用 FPakFile 读取 Pak 内部文件列表
		TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformFile, *PakFilePath, false);
		if (!PakFile.IsValid() || !PakFile->IsValid())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("[AssetPakManifest] Failed to open Pak: %s"), *PakFilePath);
			continue;
		}

		FString MountPoint = PakFile->GetMountPoint();

		// 收集 Asset 路径（去重）
		TSet<FString> AssetPathSet;

		for (FPakFile::FFilenameIterator It(*PakFile); It; ++It)
		{
			const FString& InternalFileName = It.Filename();

			// 只处理 UE Asset 文件
			if (!InternalFileName.EndsWith(TEXT(".uasset")) &&
				!InternalFileName.EndsWith(TEXT(".umap")))
			{
				continue;
			}

			FString AssetPath = ConvertPakPathToAssetPath(InternalFileName, MountPoint);
			if (!AssetPath.IsEmpty())
			{
				AssetPathSet.Add(AssetPath);
			}
		}

		if (AssetPathSet.Num() == 0)
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] No assets in Pak: %s"), *PakFileName);
			continue;
		}

		// 构建此 Pak 的 JSON 对象
		TSharedPtr<FJsonObject> PakObject = MakeShareable(new FJsonObject);
		PakObject->SetStringField(TEXT("pakPath"), RelativePakPath);
		PakObject->SetNumberField(TEXT("chunkId"), ChunkId);

		TArray<TSharedPtr<FJsonValue>> AssetsArray;
		for (const FString& AssetPath : AssetPathSet)
		{
			AssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));

			// 同时添加到平坦索引
			TSharedPtr<FJsonObject> IndexEntry = MakeShareable(new FJsonObject);
			IndexEntry->SetStringField(TEXT("pakPath"), RelativePakPath);
			IndexEntry->SetNumberField(TEXT("chunkId"), ChunkId);
			AssetIndexObject->SetObjectField(AssetPath, IndexEntry);
		}
		PakObject->SetArrayField(TEXT("assets"), AssetsArray);
		PaksArray.Add(MakeShareable(new FJsonValueObject(PakObject)));

		TotalAssetCount += AssetPathSet.Num();

		UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] Pak %s: %d assets"), *PakFileName, AssetPathSet.Num());
	}

	RootObject->SetArrayField(TEXT("paks"), PaksArray);
	RootObject->SetObjectField(TEXT("assetIndex"), AssetIndexObject);

	// 序列化并写入文件
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	// 确保输出目录存在
	PlatformFile.CreateDirectoryTree(*OutputDir);

	FString OutputPath = FPaths::Combine(OutputDir, TEXT("asset_pak_manifest.json"));
	if (!FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("[AssetPakManifest] Failed to save manifest to: %s"), *OutputPath);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("[AssetPakManifest] Manifest saved: %s (%d assets in %d paks)"),
		*OutputPath, TotalAssetCount, PaksArray.Num());

	return true;
}

FString FHotUpdateAssetPakManifestGenerator::ConvertPakPathToAssetPath(const FString& InternalPath, const FString& MountPoint)
{
	// Pak 内部路径格式：../../../ProjectName/Content/Maps/Level01.uasset
	// 需要转换为：/Game/Maps/Level01

	FString FullPath = MountPoint / InternalPath;
	FPaths::NormalizeFilename(FullPath);

	// 去除文件后缀
	FString PathWithoutExt = FPaths::GetBaseFilename(FullPath, false);  // false = 保留路径
	
	// 尝试使用 FPackageName 进行路径映射
	FString LongPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(PathWithoutExt, LongPackageName))
	{
		return LongPackageName;
	}

	// 回退：手动解析常见路径模式
	// ../../../{ProjectName}/Content/... -> /Game/...
	int32 ContentIndex = FullPath.Find(TEXT("/Content/"), ESearchCase::IgnoreCase);
	if (ContentIndex != INDEX_NONE)
	{
		// 检查是否是 Engine 路径
		if (FullPath.Contains(TEXT("/Engine/")))
		{
			FString Remainder = FullPath.Mid(ContentIndex + 9);  // +9 for "/Content/"
			// 去除后缀
			Remainder = FPaths::GetBaseFilename(Remainder, false);
			return TEXT("/Engine/") + Remainder;
		}
		else
		{
			FString Remainder = FullPath.Mid(ContentIndex + 9);
			Remainder = FPaths::GetBaseFilename(Remainder, false);
			return TEXT("/Game/") + Remainder;
		}
	}

	// 检查 Plugins 路径
	int32 PluginsIndex = FullPath.Find(TEXT("/Plugins/"), ESearchCase::IgnoreCase);
	if (PluginsIndex != INDEX_NONE)
	{
		// 查找 Content 子目录
		int32 PluginContentIndex = FullPath.Find(TEXT("/Content/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PluginsIndex);
		if (PluginContentIndex != INDEX_NONE)
		{
			// 提取插件名
			FString PluginPart = FullPath.Mid(PluginsIndex + 9);  // 跳过 "/Plugins/"
			int32 SlashIndex;
			if (PluginPart.FindChar(TEXT('/'), SlashIndex))
			{
				FString PluginName = PluginPart.Left(SlashIndex);
				FString Remainder = FullPath.Mid(PluginContentIndex + 9);
				Remainder = FPaths::GetBaseFilename(Remainder, false);
				return FString::Printf(TEXT("/%s/%s"), *PluginName, *Remainder);
			}
		}
	}

	return FString();
}

int32 FHotUpdateAssetPakManifestGenerator::ParseChunkIdFromPakName(const FString& PakFileName)
{
	// 格式：pakchunk0-Windows.pak 或 pakchunk11-Windows.pak
	FString BaseName = FPaths::GetBaseFilename(PakFileName);

	if (BaseName.StartsWith(TEXT("pakchunk")))
	{
		// 提取数字部分
		FString NumberPart;
		for (int32 i = 8; i < BaseName.Len(); ++i)  // 8 = "pakchunk".Len()
		{
			if (FChar::IsDigit(BaseName[i]))
			{
				NumberPart += BaseName[i];
			}
			else
			{
				break;
			}
		}

		if (!NumberPart.IsEmpty())
		{
			return FCString::Atoi(*NumberPart);
		}
	}

	return -1;
}
