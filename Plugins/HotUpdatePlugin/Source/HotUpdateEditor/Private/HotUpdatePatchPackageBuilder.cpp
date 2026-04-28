// Copyright czm. All Rights Reserved.

#include "HotUpdatePatchPackageBuilder.h"
#include "HotUpdatePackageHelper.h"
#include "HotUpdateAssetFilter.h"
#include "HotUpdateUtils.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateEditor.h"
#include "HotUpdateIoStoreBuilder.h"
#include "HotUpdatePackagingSettingsHelper.h"
#include "HotUpdateVersionManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FHotUpdatePatchPackageBuilder::FHotUpdatePatchPackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

FHotUpdatePatchPackageResult FHotUpdatePatchPackageBuilder::BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackage (同步) 开始调用"));
	FHotUpdatePatchPackageResult Result;

	// 验证配置
	FString ErrorMessage;
	CurrentConfig = Config;
	if (!ValidateConfig(Config, ErrorMessage))
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("配置验证失败: %s"), *ErrorMessage);
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		return Result;
	}

	// 设置构建状态
	bIsBuilding = true;
	bIsCancelled = false;

	// 编译项目：确保 Cook 使用最新的游戏代码
	if (!CurrentConfig.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!FHotUpdatePackageHelper::CompileProject(CurrentConfig.Platform))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("项目编译失败");
			bIsBuilding = false;
			return Result;
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过编译步骤 (bSkipBuild = true)"));
	}

	// Cook 资源
	if (!CurrentConfig.bSkipCook)
	{
		if (CurrentConfig.bIncrementalCook)
		{
			// 增量 Cook 模式：Cook 移到 Diff 之后，只 Cook 变化的资源
			UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook 模式: Cook 将在 Diff 之后执行"));
		}
		else
		{
			// 全量 Cook 模式：先 Cook 再 Diff
			UpdateProgress(TEXT("Cook 资源"), TEXT(""), 0, 0);
			if (!FHotUpdatePackageHelper::CookAssets(CurrentConfig.Platform))
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Cook 资源失败");
				bIsBuilding = false;
				return Result;
			}
		}
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("跳过 Cook 步骤 (bSkipCook = true)"));
	}

	// === 热更新差异打包流程 ===

	// 1. 加载基础版本 FileManifest（使用新的分类数据结构）
	UpdateProgress(TEXT("加载基础版本"), TEXT(""), 0, 0);

	FHotUpdateBaseManifestData BaseManifestData;
	FString ManifestLoadError;
	if (!LoadBaseFileManifest(CurrentConfig.BaseFileManifestPath.FilePath, BaseManifestData, ManifestLoadError))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ManifestLoadError;
		bIsBuilding = false;
		return Result;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("加载了基础版本 %d 个资产, %d 个非资产"),
		BaseManifestData.AssetHashes.Num(), BaseManifestData.NonAssetHashes.Num());

	// 使用公共函数从 Manifest 中提取版本号
	FString ActualBaseVersion = HotUpdateUtils::ExtractVersionFromManifest(CurrentConfig.BaseFileManifestPath.FilePath);
	if (ActualBaseVersion.IsEmpty())
	{
		ActualBaseVersion = CurrentConfig.BaseVersion;
	}
	else
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 更新 BaseVersion 为: %s"), *ActualBaseVersion);
	}

	// 2. 收集当前资源
	UpdateProgress(TEXT("收集资源"), TEXT(""), 0, 0);
	
	if (!CollectAssets(ErrorMessage))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		bIsBuilding = false;
		return Result;
	}

		// 2. 收集源文件路径（使用辅助函数）
		TArray<FString> AssetSourcePaths;
		TArray<FString> NonAssetSourcePaths;
		FString CollectError;
		if (!CollectSourceFilePaths(AssetSourcePaths, NonAssetSourcePaths, CollectError))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("没有可打包的资源文件");
			bIsBuilding = false;
			return Result;
		}

		// 3. 计算当前资源 Hash 和 Size
		TMap<FString, FString> CurrentAssetHashes;      // UE 资产 Hash
		TMap<FString, int64> CurrentAssetSizes;         // UE 资产 Size
		TMap<FString, FString> CurrentNonAssetHashes;   // 非资产文件 Hash
		TMap<FString, int64> CurrentNonAssetSizes;      // 非资产文件 Size

		// 计算资产 Hash 和 Size
		UpdateProgress(TEXT("计算资产 Hash"), TEXT(""), 0, AssetSourcePaths.Num());
		for (int32 i = 0; i < AssetSourcePaths.Num(); i++)
		{
			if (bIsCancelled)
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("构建已取消");
				bIsBuilding = false;
				return Result;
			}

			const FString& SourcePath = AssetSourcePaths[i];
			CurrentAssetHashes.Add(SourcePath, UHotUpdateFileUtils::CalculateFileHash(SourcePath));
			CurrentAssetSizes.Add(SourcePath, IFileManager::Get().FileSize(*SourcePath));
			UpdateProgress(TEXT("计算资产 Hash"), SourcePath, i + 1, AssetSourcePaths.Num());
		}

		// 计算非资产文件 Hash 和 Size
		UpdateProgress(TEXT("计算非资产 Hash"), TEXT(""), 0, NonAssetSourcePaths.Num());
		for (int32 i = 0; i < NonAssetSourcePaths.Num(); i++)
		{
			if (bIsCancelled)
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("构建已取消");
				bIsBuilding = false;
				return Result;
			}

			const FString& SourcePath = NonAssetSourcePaths[i];
			CurrentNonAssetHashes.Add(SourcePath, UHotUpdateFileUtils::CalculateFileHash(SourcePath));
			CurrentNonAssetSizes.Add(SourcePath, IFileManager::Get().FileSize(*SourcePath));
			UpdateProgress(TEXT("计算非资产 Hash"), SourcePath, i + 1, NonAssetSourcePaths.Num());
		}

		// 4. 计算差异（分开处理，直接使用分类后的数据）
		UpdateProgress(TEXT("计算差异"), TEXT(""), 0, 0);

		TArray<FString> ChangedAssetPaths;    // 变更的 UE 资产路径
		TArray<FString> ChangedNonAssetPaths; // 变更的非资产文件路径
		FHotUpdateDiffReport AssetDiffReport;
		FHotUpdateDiffReport NonAssetDiffReport;

		// 计算 UE 资产差异（直接使用已分类的数据）
		if (!ComputeDiff(AssetSourcePaths, CurrentAssetHashes, CurrentAssetSizes, BaseManifestData.AssetHashes, BaseManifestData.AssetSizes, ChangedAssetPaths, AssetDiffReport))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("计算资产差异失败");
			bIsBuilding = false;
			return Result;
		}

		// 计算非资产文件差异
		if (!ComputeDiff(NonAssetSourcePaths, CurrentNonAssetHashes, CurrentNonAssetSizes, BaseManifestData.NonAssetHashes, BaseManifestData.NonAssetSizes, ChangedNonAssetPaths, NonAssetDiffReport))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("计算非资产差异失败");
			bIsBuilding = false;
			return Result;
		}

		// 合并 DiffReport（用于日志和后续处理）
		FHotUpdateDiffReport DiffReport;
		DiffReport.BaseVersion = CurrentConfig.BaseVersion;
		DiffReport.TargetVersion = CurrentConfig.PatchVersion;
		DiffReport.AddedAssets = AssetDiffReport.AddedAssets;
		DiffReport.AddedAssets.Append(NonAssetDiffReport.AddedAssets);
		DiffReport.ModifiedAssets = AssetDiffReport.ModifiedAssets;
		DiffReport.ModifiedAssets.Append(NonAssetDiffReport.ModifiedAssets);
		DiffReport.DeletedAssets = AssetDiffReport.DeletedAssets;
		DiffReport.DeletedAssets.Append(NonAssetDiffReport.DeletedAssets);
		DiffReport.UnchangedAssets = AssetDiffReport.UnchangedAssets;
		DiffReport.UnchangedAssets.Append(NonAssetDiffReport.UnchangedAssets);

		UE_LOG(LogHotUpdateEditor, Display, TEXT("差异: 资产(新增 %d, 修改 %d), 非资产(新增 %d, 修改 %d)"), 
			AssetDiffReport.AddedAssets.Num(), AssetDiffReport.ModifiedAssets.Num(),
			NonAssetDiffReport.AddedAssets.Num(), NonAssetDiffReport.ModifiedAssets.Num());
	
	
	// 增量模式：只打包变更的资源（新增 + 修改）
	// 用于打包的资源列表（只包含变更资源）

	// === 增量 Cook：在 Diff 之后执行 ===
	if (CurrentConfig.bIncrementalCook && !CurrentConfig.bSkipCook)
	{
			// 提取修改资源（有 Cooked 输出，Hash 变了）
			// 只添加 UE Package 格式的资产，非资产文件不需要 Cook
			TArray<FString> AssetsToCook;
				TSet<FString> AddedAssetsSet;  // 避免重复添加
				UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: ModifiedAssets.Num=%d"), DiffReport.ModifiedAssets.Num());
			for (const FHotUpdateAssetDiff& Diff : DiffReport.ModifiedAssets)
			{
				// UE Package 格式：/Game/..., /Engine/..., /Plugin/... 等（以 / 开头）
				// 非资产文件：磁盘路径格式（如 E:/.../txt_pak.txt）
				if (FHotUpdatePackageHelper::IsValidPackagePath(Diff.AssetPath))
				{
					// 将磁盘路径转换为 Package Name
					FString PackageName = FHotUpdatePackageHelper::FilePathToLongPackageName(Diff.AssetPath);
					if (!PackageName.IsEmpty())
					{
						if (!AddedAssetsSet.Contains(PackageName))
						{
							AssetsToCook.Add(PackageName);
							AddedAssetsSet.Add(PackageName);
							UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 添加修改资产: %s -> %s"), *Diff.AssetPath, *PackageName);
						}
					}
				}
				else
				{
					UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 跳过非资产文件: %s"), *Diff.AssetPath);
				}
			}
		
		// 不在基础 FileManifest 中的资源 = 新增资源
		TArray<FString> AllPackagingAssetPaths;
		if (CurrentConfig.AssetPaths.Num() > 0)
		{
			AllPackagingAssetPaths = CurrentConfig.AssetPaths;
		}
		else
		{
			AllPackagingAssetPaths = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true).AssetPaths;
		}

		// 构建基础 Manifest 资源集合
		TSet<FString> BaseAssetSet;
		for (const auto& Pair : BaseManifestData.AssetHashes)
		{
			BaseAssetSet.Add(Pair.Key);
		}

			for (const FString& AssetPath : AllPackagingAssetPaths)
			{
				// 跳过 OFPA 数据
				if (FHotUpdatePackageHelper::IsExternalAsset(AssetPath))
				{
					continue;
				}

				// 将 Package Name 转换为磁盘路径后比较
				// AssetPath 是 Package Name 格式（如 /Game/TopDown/Lvl_TopDown）
				// BaseAssetSet 存储的是磁盘路径格式（如 E:/.../Lvl_TopDown.umap）
				FString AssetSourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
				if (AssetSourcePath.IsEmpty())
				{
					// 无法获取源路径，保守处理：视为需要 Cook
					AssetsToCook.Add(AssetPath);
					UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook: 无法获取源路径，视为新增: %s"), *AssetPath);
					continue;
				}

				// 不在基础 Manifest 中 = 新增资源
				if (!BaseAssetSet.Contains(AssetSourcePath))
				{
					if (!AddedAssetsSet.Contains(AssetPath))
					{
						AssetsToCook.Add(AssetPath);
						AddedAssetsSet.Add(AssetPath);
						UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 发现新增资源: %s"), *AssetPath);
					}
				}
			}

		UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 需要 Cook %d 个资源 (修改 %d + 新增)"), AssetsToCook.Num(), DiffReport.ModifiedAssets.Num());

		if (AssetsToCook.Num() > 0)
		{
			// 主线程收集依赖并过滤引擎资产
			TArray<FString> AssetsWithDeps = FHotUpdatePackageHelper::CollectDependenciesAndFilterEngine(AssetsToCook);

			UpdateProgress(TEXT("增量 Cook 资源"), TEXT(""), 0, AssetsWithDeps.Num());
			if (!FHotUpdatePackageHelper::CookAssets(CurrentConfig.Platform, AssetsWithDeps))
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook 失败"));
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Cook 资源失败");
				bIsBuilding = false;
				return Result;
			}
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook: 没有需要 Cook 的资源变更"));
		}
	}
	
	UE_LOG(LogHotUpdateEditor, Log, TEXT("增量模式: 变更资源 (新增 %d + 修改 %d)"), DiffReport.AddedAssets.Num(), DiffReport.ModifiedAssets.Num());

	// 检查是否有变更
	UE_LOG(LogHotUpdateEditor, Display, TEXT("ChangedAssetPaths.Num=%d, ChangedNonAssetPaths.Num=%d"), ChangedAssetPaths.Num(), ChangedNonAssetPaths.Num());
	bool bHasChanges = (ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num()) > 0;

	if (!bHasChanges)
	{
		Result.bSuccess = true;
		Result.ErrorMessage = TEXT("没有发现资源变更");
		Result.DiffReport = DiffReport;
		bIsBuilding = false;
		return Result;
	}

	// 5. 确定输出目录
	FString OutputDir = CurrentConfig.OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdateVersions");
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(CurrentConfig.Platform);
	OutputDir = FPaths::Combine(OutputDir, CurrentConfig.PatchVersion, PlatformStr);
	FPaths::NormalizeDirectoryName(OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);

	Result.OutputDirectory = OutputDir;
	Result.PatchVersion = CurrentConfig.PatchVersion;
	Result.BaseVersion = ActualBaseVersion;
	Result.DiffReport = DiffReport;
	Result.ChangedAssetCount = (ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num());

	// 6. 创建 Patch IoStore 容器
	// 全量热更新模式：即使没有变更，也需要复制基础容器
	// 增量模式：没有变更资源则跳过 Patch 容器创建
	UpdateProgress(TEXT("创建 Patch 容器"), TEXT(""), 0, (ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num()));

	FString PatchUtocPath;
	FString PatchUcasPath;
	int64 PatchSize = 0;

	if ((ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num()) > 0)
	{
		FHotUpdateIoStoreBuilder IoStoreBuilder;

		FHotUpdateIoStoreConfig IoStoreConfig = CurrentConfig.IoStoreConfig;
		IoStoreConfig.bUseIoStore = false;
		// UE5 标准补丁命名格式：{项目名}-{平台}_{PatchIndex}_P
		IoStoreConfig.ContainerName = FString::Printf(TEXT("Patch_%s_P"), *CurrentConfig.PatchVersion);

		FString PaksDir = FPaths::Combine(OutputDir, TEXT("Paks"));
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*PaksDir);

		FString PatchOutputPath = FPaths::Combine(PaksDir, IoStoreConfig.ContainerName);

		// 将绝对路径转换为虚拟包路径（分开处理）
		// UE 资产：调用 FilePathToLongPackageName（返回 Long Package Name）
		// 非资产：调用 FilePathToContentMountPath（返回 Pak 内部路径）
		TArray<FString> VirtualPackagePaths;
		VirtualPackagePaths.Reserve(ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num());

		// 转换 UE 资产路径
		for (const FString& AbsolutePath : ChangedAssetPaths)
		{
			FString VirtualPath = FHotUpdatePackageHelper::FilePathToLongPackageName(AbsolutePath);
			if (!VirtualPath.IsEmpty())
			{
				VirtualPackagePaths.Add(VirtualPath);
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("无法转换资产路径: %s"), *AbsolutePath);
			}
		}

		// 转换非资产文件路径
		for (const FString& AbsolutePath : ChangedNonAssetPaths)
		{
			FString VirtualPath = FHotUpdatePackageHelper::FilePathToContentMountPath(AbsolutePath);
			if (!VirtualPath.IsEmpty())
			{
				VirtualPackagePaths.Add(VirtualPath);
			}
			else
			{
				UE_LOG(LogHotUpdateEditor, Warning, TEXT("无法转换非资产路径: %s"), *AbsolutePath);
			}
		}

		// 获取 Cooked 平台目录，用于查找 .uexp/.ubulk 配套文件
		FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(CurrentConfig.Platform);
		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder.BuildIoStoreContainer(VirtualPackagePaths, PatchOutputPath, IoStoreConfig, CookedPlatformDir);

		if (!IoStoreResult.bSuccess)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("Patch 容器创建失败: %s"), *IoStoreResult.ErrorMessage);
			bIsBuilding = false;
			return Result;
		}

		PatchUtocPath = IoStoreResult.UtocPath;
		PatchUcasPath = IoStoreResult.UcasPath;
		PatchSize = IoStoreResult.ContainerSize;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("Patch 容器创建成功: %s, 大小 %lld 字节"), *PatchUtocPath, PatchSize);
	}

	Result.PatchUtocPath = PatchUtocPath;
	Result.PatchUcasPath = PatchUcasPath;
	Result.PatchSize = PatchSize;

// 用于存储从 Manifest 解析的历史 Patch 容器引用（链式 Patch）
	TArray<FHotUpdateContainerInfo> BaseContainers;


	// 6.7 从基础版本 Manifest 解析容器信息（写入 manifest 供客户端下载）
	{
		FString BaseManifestJson;
		if (FFileHelper::LoadFileToString(BaseManifestJson, *CurrentConfig.BaseFileManifestPath.FilePath))
		{
			TSharedPtr<FJsonObject> BaseManifestObj;
			TSharedRef<TJsonReader<>> BaseReader = TJsonReaderFactory<>::Create(BaseManifestJson);
			if (FJsonSerializer::Deserialize(BaseReader, BaseManifestObj) && BaseManifestObj.IsValid())
			{
				// 兼容 chunks 和 containers 两种字段名
				const TArray<TSharedPtr<FJsonValue>>* ContainersArray = nullptr;
				bool bFound = BaseManifestObj->TryGetArrayField(TEXT("chunks"), ContainersArray);
				if (!bFound)
				{
					bFound = BaseManifestObj->TryGetArrayField(TEXT("containers"), ContainersArray);
				}

				if (bFound && ContainersArray)
				{
					for (const TSharedPtr<FJsonValue>& ContainerValue : *ContainersArray)
					{
						TSharedPtr<FJsonObject> ContainerObj = ContainerValue->AsObject();
						if (!ContainerObj.IsValid()) continue;

						// 只保留 patch 类型的容器（base 类型属于基础包，不在 patch manifest 中引用）
						FString ContainerType;
						if (ContainerObj->TryGetStringField(TEXT("containerType"), ContainerType) && !ContainerType.StartsWith(TEXT("patch")))
						{
							continue;
						}

						FHotUpdateContainerInfo Info;
						// 兼容 ChunkName 和 containerName
						if (ContainerObj->HasField(TEXT("ChunkName")))
						{
							Info.ContainerName = ContainerObj->GetStringField(TEXT("ChunkName"));
						}
						else
						{
							Info.ContainerName = ContainerObj->GetStringField(TEXT("containerName"));
						}
						// IoStore 格式字段（可选）
						ContainerObj->TryGetStringField(TEXT("utocPath"), Info.UtocPath);
						ContainerObj->TryGetNumberField(TEXT("utocSize"), Info.UtocSize);
						ContainerObj->TryGetStringField(TEXT("utocHash"), Info.UtocHash);

						// 传统 Pak 格式字段（可选，当 utocPath 不存在时使用）
						if (Info.UtocPath.IsEmpty() && ContainerObj->HasField(TEXT("pakPath")))
						{
							Info.UtocPath = ContainerObj->GetStringField(TEXT("pakPath"));
							ContainerObj->TryGetNumberField(TEXT("pakSize"), Info.UtocSize);
							ContainerObj->TryGetStringField(TEXT("pakHash"), Info.UtocHash);
						}

						if (ContainerObj->HasField(TEXT("ucasPath")))
						{
							Info.UcasPath = ContainerObj->GetStringField(TEXT("ucasPath"));
							Info.UcasSize = (int64)ContainerObj->GetNumberField(TEXT("ucasSize"));
							Info.UcasHash = ContainerObj->GetStringField(TEXT("ucasHash"));
						}

						Info.ContainerType = EHotUpdateContainerType::Patch;
						// 优先使用 manifest 中的 version 字段，否则使用 CurrentConfig.BaseVersion
						if (ContainerObj->HasField(TEXT("version")))
						{
							Info.Version = ContainerObj->GetStringField(TEXT("version"));
						}
						else
						{
							Info.Version = ActualBaseVersion;
						}

						BaseContainers.Add(Info);
					}

					UE_LOG(LogHotUpdateEditor, Log, TEXT("从 Manifest 加载了 %d 个 patch 容器引用"), BaseContainers.Num());
				}
			}
		}

	}

	// 7. 生成 Manifest
	UpdateProgress(TEXT("生成 Manifest"), TEXT(""), 0, 0);

	// 更新 CurrentConfig 中的 BaseVersion
	CurrentConfig.BaseVersion = ActualBaseVersion;

	FString ManifestPath = FPaths::Combine(OutputDir, TEXT("manifest.json"));

	if (!GenerateManifest(ManifestPath, Result.PatchUtocPath, Result.PatchUcasPath, DiffReport, BaseContainers))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("生成 Manifest 失败");
		bIsBuilding = false;
		return Result;
	}

	Result.ManifestFilePath = ManifestPath;

	// 8. 注册版本
	UpdateProgress(TEXT("注册版本"), TEXT(""), 0, 0);

	FHotUpdateVersionManager VersionManager;

	FHotUpdateEditorVersionInfo VersionInfo;
	VersionInfo.VersionString = CurrentConfig.PatchVersion;
	VersionInfo.PackageKind = EHotUpdatePackageKind::Patch;
	VersionInfo.BaseVersion = CurrentConfig.BaseVersion;
	VersionInfo.Platform = CurrentConfig.Platform;
	VersionInfo.CreatedTime = FDateTime::Now();
	VersionInfo.FileManifestPath = FPaths::Combine(OutputDir, TEXT("filemanifest.json"));
	VersionInfo.UtocPath = Result.PatchUtocPath;
	VersionInfo.AssetCount = DiffReport.AddedAssets.Num() + DiffReport.ModifiedAssets.Num() + DiffReport.UnchangedAssets.Num();
	VersionInfo.PackageSize = Result.PatchSize;

	VersionManager.RegisterVersion(VersionInfo);

	// 9. 完成
	Result.bSuccess = true;

	bIsBuilding = false;

	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("更新包构建成功: %s, 变更 %d 个资源, 大小 %lld 字节"), *OutputDir, (ChangedAssetPaths.Num() + ChangedNonAssetPaths.Num()), Result.PatchSize);

	return Result;
}

void FHotUpdatePatchPackageBuilder::BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildPatchPackageAsync 开始调用"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  bIsBuilding: %s"), bIsBuilding ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsValid(): %s"), BuildTask.IsValid() ? TEXT("true") : TEXT("false"));
	UE_LOG(LogHotUpdateEditor, Log, TEXT("  BuildTask.IsReady(): %s"), BuildTask.IsReady() ? TEXT("true") : TEXT("false"));

	// 检查是否有正在运行的构建任务
	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			// 任务仍在运行
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有更新包构建任务正在运行，拒绝新的构建请求"));
			FHotUpdatePatchPackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("已有构建任务正在进行中");
			OnComplete.Broadcast(Result);
			return;
		}
		else
		{
			// 之前的构建异常终止，重置状态
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始新的更新包构建任务，版本: %s，基础版本: %s"), *CurrentConfig.PatchVersion, *CurrentConfig.BaseVersion);

	bIsBuilding = true;
	bIsCancelled = false;

	CurrentConfig = Config;

	// 始终从打包配置读取资源路径
	UE_LOG(LogHotUpdateEditor, Log, TEXT("在游戏线程预收集打包设置中的资源..."));
	FString ErrorMessage;
	if (!CollectAssets(ErrorMessage))
	{
		bIsBuilding = false;
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("CollectAssets : %s"), *ErrorMessage);
		return;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("预收集完成（含依赖），共 %d 个资源, %d 个非资源文件"), CurrentConfig.AssetPaths.Num(), CurrentConfig.NonAssetPaths.Num());

	TWeakPtr<FHotUpdatePatchPackageBuilder> WeakBuilder(AsShared());
	BuildTask = Async(EAsyncExecution::Thread, [WeakBuilder](){
		TSharedPtr<FHotUpdatePatchPackageBuilder> Builder = WeakBuilder.Pin();
		if (!Builder.IsValid())
		{
			return;
		}

		FHotUpdatePatchPackageResult Result = Builder->BuildPatchPackage(Builder->CurrentConfig);

		AsyncTask(ENamedThreads::GameThread, [WeakBuilder, Result]()
		{
			TSharedPtr<FHotUpdatePatchPackageBuilder> PinnedBuilder = WeakBuilder.Pin();
			if (PinnedBuilder.IsValid())
			{
				PinnedBuilder->OnComplete.Broadcast(Result);
			}
		});
	});
}

void FHotUpdatePatchPackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

bool FHotUpdatePatchPackageBuilder::ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage)
{
	if (Config.PatchVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("更新包版本号不能为空");
		return false;
	}

	// 热更新打包需要基础版本相关配置
	if (Config.BaseVersion.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本号不能为空");
		return false;
	}

	if (Config.BaseFileManifestPath.FilePath.IsEmpty())
	{
		OutErrorMessage = TEXT("基础版本 Manifest 路径不能为空");
		return false;
	}

	if (!FPaths::FileExists(Config.BaseFileManifestPath.FilePath))
	{
		OutErrorMessage = FString::Printf(TEXT("基础版本 Manifest 文件不存在: %s"), *Config.BaseFileManifestPath.FilePath);
		return false;
	}

	return true;
}

bool FHotUpdatePatchPackageBuilder::CollectAssets( FString& OutErrorMessage)
{
	// 已经收集了
	if (CurrentConfig.AssetPaths.Num() > 0)
	{
		return true;
	}
	FHotUpdatePackagingSettingsResult SettingsResult = FHotUpdatePackagingSettingsHelper::ParsePackagingSettings(true);
	if (SettingsResult.Errors.Num() > 0)
	{
		OutErrorMessage = FString::Join(SettingsResult.Errors, TEXT("\n"));
		return false;
	}
	CurrentConfig.AssetPaths = SettingsResult.AssetPaths;
	CurrentConfig.NonAssetPaths = SettingsResult.NonAssetPaths;
	
	return true;
}

bool FHotUpdatePatchPackageBuilder::LoadBaseFileManifest(
	const FString& ManifestPath,
	FHotUpdateBaseManifestData& OutData,
	FString& OutErrorMessage)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		OutErrorMessage = FString::Printf(TEXT("无法读取 Manifest 文件: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutErrorMessage = FString::Printf(TEXT("无法解析 Manifest JSON: %s"), *ManifestPath);
		return false;
	}

	// 临时存储所有文件
	TMap<FString, FString> AllHashes;
	TMap<FString, int64> AllSizes;

	const TArray<TSharedPtr<FJsonValue>>* FilesArray;
	if (JsonObject->TryGetArrayField(TEXT("files"), FilesArray))
	{
		for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
		{
			TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
			if (!FileObj.IsValid()) continue;

			FString FilePath = FileObj->GetStringField(TEXT("filePath"));
			FString Hash = FileObj->GetStringField(TEXT("fileHash"));
			int64 Size = (int64)FileObj->GetNumberField(TEXT("fileSize"));

			FPaths::NormalizeFilename(FilePath);
			AllHashes.Add(FilePath, Hash);
			AllSizes.Add(FilePath, Size);
		}
	}

	// 按资产类型分类
	SplitHashesByAssetType(AllHashes, AllSizes,
		OutData.AssetHashes, OutData.AssetSizes,
		OutData.NonAssetHashes, OutData.NonAssetSizes);

	UE_LOG(LogHotUpdateEditor, Display, TEXT("LoadBaseFileManifest: 加载 %d 个资产, %d 个非资产文件"),
		OutData.AssetHashes.Num(), OutData.NonAssetHashes.Num());

	return OutData.IsValid();
}

bool FHotUpdatePatchPackageBuilder::ComputeDiff(
	const TArray<FString>& CurrentAssets,
	const TMap<FString, FString>& CurrentHashes,
	const TMap<FString, int64>& CurrentSizes,
	const TMap<FString, FString>& BaseHashes,
	const TMap<FString, int64>& BaseSizes,
	TArray<FString>& OutChangedAssets,
	FHotUpdateDiffReport& OutReport)
{
	UE_LOG(LogHotUpdateEditor, Display, TEXT("ComputeDiff: CurrentAssets.Num=%d, CurrentHashes.Num=%d, BaseHashes.Num=%d"), CurrentAssets.Num(), CurrentHashes.Num(), BaseHashes.Num());

	// 使用公共差异计算函数
	HotUpdateUtils::CalculateAssetDiff(CurrentAssets, CurrentHashes, CurrentSizes, BaseHashes, BaseSizes, OutChangedAssets, OutReport);

	UE_LOG(LogHotUpdateEditor, Display, TEXT("ComputeDiff: Added=%d, Modified=%d, Deleted=%d, Unchanged=%d"), 
		OutReport.AddedAssets.Num(), OutReport.ModifiedAssets.Num(), OutReport.DeletedAssets.Num(), OutReport.UnchangedAssets.Num());

	return true;
}

bool FHotUpdatePatchPackageBuilder::GenerateManifest(
	const FString& ManifestPath,
	const FString& PatchUtocPath,
	const FString& PatchUcasPath,
	const FHotUpdateDiffReport& DiffReport,
	const TArray<FHotUpdateContainerInfo>& BaseContainers) const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// 包类型
	RootObject->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));

	// 版本信息
	TSharedPtr<FJsonObject> VersionInfo = MakeShareable(new FJsonObject);

	TArray<FString> VersionParts;
	CurrentConfig.PatchVersion.ParseIntoArray(VersionParts, TEXT("."));
	
	VersionInfo->SetStringField(TEXT("version"), CurrentConfig.PatchVersion);
	VersionInfo->SetStringField(TEXT("platform"), HotUpdateUtils::GetPlatformString(CurrentConfig.Platform));
	VersionInfo->SetNumberField(TEXT("timestamp"), FDateTime::Now().ToUnixTimestamp());

	RootObject->SetObjectField(TEXT("version"), VersionInfo);

	// 基础版本
	RootObject->SetStringField(TEXT("baseVersion"), CurrentConfig.BaseVersion);

	// 差异摘要
	TSharedPtr<FJsonObject> DiffSummary = MakeShareable(new FJsonObject);
	DiffSummary->SetNumberField(TEXT("addedCount"), DiffReport.AddedAssets.Num());
	DiffSummary->SetNumberField(TEXT("modifiedCount"), DiffReport.ModifiedAssets.Num());
	DiffSummary->SetNumberField(TEXT("deletedCount"), DiffReport.DeletedAssets.Num());
	DiffSummary->SetNumberField(TEXT("unchangedCount"), DiffReport.UnchangedAssets.Num());
	RootObject->SetObjectField(TEXT("diffSummary"), DiffSummary);
	
	// 容器文件列表
	TArray<TSharedPtr<FJsonValue>> ContainersArray;

	// 1. 先添加基础版本容器（全量热更新模式）
	for (const FHotUpdateContainerInfo& BaseContainer : BaseContainers)
	{
		TSharedPtr<FJsonObject> BaseContainerObj = MakeShareable(new FJsonObject);
		BaseContainerObj->SetStringField(TEXT("containerName"), BaseContainer.ContainerName);
		BaseContainerObj->SetStringField(TEXT("utocPath"), BaseContainer.UtocPath);
		BaseContainerObj->SetNumberField(TEXT("utocSize"), BaseContainer.UtocSize);
		BaseContainerObj->SetStringField(TEXT("utocHash"), BaseContainer.UtocHash);

		if (!BaseContainer.UcasPath.IsEmpty())
		{
			BaseContainerObj->SetStringField(TEXT("ucasPath"), BaseContainer.UcasPath);
			BaseContainerObj->SetNumberField(TEXT("ucasSize"), BaseContainer.UcasSize);
			BaseContainerObj->SetStringField(TEXT("ucasHash"), BaseContainer.UcasHash);
			BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
		}
		else
		{
			BaseContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			BaseContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			BaseContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			BaseContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}

		BaseContainerObj->SetStringField(TEXT("version"), BaseContainer.Version);
		ContainersArray.Add(MakeShareable(new FJsonValueObject(BaseContainerObj)));
	}

	// 添加当前 Patch 容器

	if (!PatchUtocPath.IsEmpty() && FPaths::FileExists(*PatchUtocPath))
	{
		TSharedPtr<FJsonObject> PatchContainerObj = MakeShareable(new FJsonObject);

			FString ContainerName = FString::Printf(TEXT("Patch_%s_P"), *CurrentConfig.PatchVersion);
		PatchContainerObj->SetStringField(TEXT("containerName"), ContainerName);

		// .utoc 文件信息
		FString UtocFileName = FPaths::GetCleanFilename(PatchUtocPath);
		PatchContainerObj->SetStringField(TEXT("utocPath"), TEXT("Paks/") + UtocFileName);
		PatchContainerObj->SetNumberField(TEXT("utocSize"), IFileManager::Get().FileSize(*PatchUtocPath));
		PatchContainerObj->SetStringField(TEXT("utocHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUtocPath));

		// .ucas 文件信息（可选）
		if (!PatchUcasPath.IsEmpty() && FPaths::FileExists(*PatchUcasPath))
		{
			FString UcasFileName = FPaths::GetCleanFilename(PatchUcasPath);
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT("Paks/") + UcasFileName);
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), IFileManager::Get().FileSize(*PatchUcasPath));
			PatchContainerObj->SetStringField(TEXT("ucasHash"), UHotUpdateFileUtils::CalculateFileHash(PatchUcasPath));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch"));
		}
		else if (PatchUtocPath.EndsWith(TEXT(".pak")))
		{
			// 传统 .pak 格式
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			PatchContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_pak"));
		}
		else
		{
			// 单文件格式，数据嵌入在 utoc 中
			PatchContainerObj->SetStringField(TEXT("ucasPath"), TEXT(""));
			PatchContainerObj->SetNumberField(TEXT("ucasSize"), 0);
			PatchContainerObj->SetStringField(TEXT("ucasHash"), TEXT(""));
			PatchContainerObj->SetStringField(TEXT("containerType"), TEXT("patch_embedded"));
		}

			PatchContainerObj->SetStringField(TEXT("version"), CurrentConfig.PatchVersion);

		ContainersArray.Add(MakeShareable(new FJsonValueObject(PatchContainerObj)));
	}

	RootObject->SetArrayField(TEXT("containers"), ContainersArray);

	// 注意: 不生成 files 字段，客户端只关心需要下载的容器文件
	// files 信息仅供编辑器端差异计算使用，存储在单独的 fileManifest 文件中

	// 序列化客户端 manifest
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(OutputString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}

	// 生成编辑器端 fileManifest（包含 files 信息，用于差异计算）
	FString FileManifestPath = FPaths::Combine(FPaths::GetPath(ManifestPath), TEXT("filemanifest.json"));

	TSharedPtr<FJsonObject> FileManifestObj = MakeShareable(new FJsonObject);
	FileManifestObj->SetNumberField(TEXT("packageKind"), static_cast<int32>(EHotUpdatePackageKind::Patch));
	FileManifestObj->SetObjectField(TEXT("version"), VersionInfo);
	FileManifestObj->SetStringField(TEXT("baseVersion"), CurrentConfig.BaseVersion);

	FileManifestObj->SetObjectField(TEXT("diffSummary"), DiffSummary);
	FileManifestObj->SetArrayField(TEXT("containers"), ContainersArray);

	// 收集所有资源路径（从 DiffReport 推导）
	TSet<FString> AllAssetPaths;
	TMap<FString, const FHotUpdateAssetDiff*> AssetDiffMap; // 快速查找

	for (const FHotUpdateAssetDiff& Diff : DiffReport.AddedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}
	for (const FHotUpdateAssetDiff& Diff : DiffReport.ModifiedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}
	for (const FHotUpdateAssetDiff& Diff : DiffReport.UnchangedAssets)
	{
		AllAssetPaths.Add(Diff.AssetPath);
		AssetDiffMap.Add(Diff.AssetPath, &Diff);
	}

	// 文件列表（仅用于编辑器端差异计算）
	TArray<TSharedPtr<FJsonValue>> FilesArray;

	for (const FString& AssetPath : AllAssetPaths)
	{
		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);

		// filePath 使用源文件绝对路径
		FString FilePath;
		FString AssetSourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!AssetSourcePath.IsEmpty() && FPaths::FileExists(*AssetSourcePath))
		{
			FilePath = FPaths::ConvertRelativePathToFull(AssetSourcePath);
		}
		else
		{
			FilePath = AssetPath;
		}

		FileObj->SetStringField(TEXT("filePath"), FilePath);

		// 从 AssetDiffMap 获取差异信息
		const FHotUpdateAssetDiff* Diff = AssetDiffMap.FindRef(AssetPath);
		if (Diff)
		{
			if (Diff->ChangeType == EHotUpdateFileChangeType::Added || Diff->ChangeType == EHotUpdateFileChangeType::Modified)
			{
				// 变更资源：使用 NewHash/NewSize
				FileObj->SetStringField(TEXT("fileHash"), Diff->NewHash);
				FileObj->SetNumberField(TEXT("fileSize"), Diff->NewSize);
				FileObj->SetStringField(TEXT("source"), TEXT("patch"));
			}
			else if (Diff->ChangeType == EHotUpdateFileChangeType::Unchanged)
			{
					// 未变更资源：使用 DiffReport 中的 Hash/Size
					FileObj->SetStringField(TEXT("fileHash"), Diff->OldHash);
					FileObj->SetNumberField(TEXT("fileSize"), Diff->OldSize);
					FileObj->SetStringField(TEXT("source"), TEXT("base"));
			}
		}
		
		FileObj->SetBoolField(TEXT("isCompressed"), CurrentConfig.IoStoreConfig.CompressionFormat != TEXT("None"));

		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}

	FileManifestObj->SetArrayField(TEXT("files"), FilesArray);

	// 序列化 fileManifest
	FString FileManifestString;
	TSharedRef<TJsonWriter<>> FileWriter = TJsonWriterFactory<>::Create(&FileManifestString);
	FJsonSerializer::Serialize(FileManifestObj.ToSharedRef(), FileWriter);

	return FFileHelper::SaveStringToFile(FileManifestString, *FileManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FHotUpdatePatchPackageBuilder::UpdateProgress(
	const FString& Stage,
	const FString& CurrentFile,
	int32 ProcessedFiles,
	int32 TotalFiles)
{
	FHotUpdatePackageProgress ProgressCopy;
	{
		FScopeLock Lock(&ProgressCriticalSection);
		HotUpdateProgressHelper::UpdateProgressData(CurrentProgress, Stage, CurrentFile, ProcessedFiles, TotalFiles);
		ProgressCopy = CurrentProgress;
	}

	if (CurrentConfig.bSynchronousMode)
	{
		HotUpdateProgressHelper::BroadcastProgressSync(ProgressCopy, OnProgress);
	}
	else
	{
		HotUpdateProgressHelper::BroadcastProgressAsync(ProgressCopy, OnProgress);
	}
}

void FHotUpdatePatchPackageBuilder::SplitHashesByAssetType(
	const TMap<FString, FString>& AllHashes,
	const TMap<FString, int64>& AllSizes,
	TMap<FString, FString>& OutAssetHashes,
	TMap<FString, int64>& OutAssetSizes,
	TMap<FString, FString>& OutNonAssetHashes,
	TMap<FString, int64>& OutNonAssetSizes)
{
	for (const auto& Pair : AllHashes)
	{
		const FString& Path = Pair.Key;
		if (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap")))
		{
			OutAssetHashes.Add(Path, Pair.Value);
			if (const int64* Size = AllSizes.Find(Path))
			{
				OutAssetSizes.Add(Path, *Size);
			}
		}
		else
		{
			OutNonAssetHashes.Add(Path, Pair.Value);
			if (const int64* Size = AllSizes.Find(Path))
			{
				OutNonAssetSizes.Add(Path, *Size);
			}
		}
	}
}

FHotUpdateDiffReport FHotUpdatePatchPackageBuilder::MergeDiffReports(
	const FHotUpdateDiffReport& AssetReport,
	const FHotUpdateDiffReport& NonAssetReport,
	const FString& BaseVersion,
	const FString& TargetVersion)
{
	FHotUpdateDiffReport Result;
	Result.BaseVersion = BaseVersion;
	Result.TargetVersion = TargetVersion;

	Result.AddedAssets = AssetReport.AddedAssets;
	Result.AddedAssets.Append(NonAssetReport.AddedAssets);

	Result.ModifiedAssets = AssetReport.ModifiedAssets;
	Result.ModifiedAssets.Append(NonAssetReport.ModifiedAssets);

	Result.DeletedAssets = AssetReport.DeletedAssets;
	Result.DeletedAssets.Append(NonAssetReport.DeletedAssets);

	Result.UnchangedAssets = AssetReport.UnchangedAssets;
	Result.UnchangedAssets.Append(NonAssetReport.UnchangedAssets);

	return Result;
}

FHotUpdatePatchPackageResult FHotUpdatePatchPackageBuilder::MakeErrorResult(const FString& ErrorMessage)
{
	FHotUpdatePatchPackageResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = ErrorMessage;
	return Result;
}

// === 阶段子函数实现 ===

bool FHotUpdatePatchPackageBuilder::CollectSourceFilePaths(
	TArray<FString>& OutAssetSourcePaths,
	TArray<FString>& OutNonAssetSourcePaths,
	FString& OutErrorMessage)
{
	// 收集 UE 资产源文件路径
	for (const FString& AssetPath : CurrentConfig.AssetPaths)
	{
		const FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!SourcePath.IsEmpty() && FPaths::FileExists(*SourcePath))
		{
			OutAssetSourcePaths.Add(FPaths::ConvertRelativePathToFull(SourcePath));
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过源文件不存在的资产: %s -> %s"), *AssetPath, *SourcePath);
		}
	}

	// 收集非资产源文件路径
	for (const FString& FilePath : CurrentConfig.NonAssetPaths)
	{
		if (FPaths::FileExists(*FilePath))
		{
			OutNonAssetSourcePaths.Add(FPaths::ConvertRelativePathToFull(FilePath));
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Verbose, TEXT("跳过源文件不存在的非资产文件: %s"), *FilePath);
		}
	}

	return OutAssetSourcePaths.Num() > 0 || OutNonAssetSourcePaths.Num() > 0;
}


