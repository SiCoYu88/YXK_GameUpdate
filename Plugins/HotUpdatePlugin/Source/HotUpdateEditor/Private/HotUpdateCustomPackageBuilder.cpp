// Copyright czm. All Rights Reserved.

#include "HotUpdateCustomPackageBuilder.h"
#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "Core/HotUpdateFileUtils.h"
#include "HotUpdateIoStoreBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

FHotUpdateCustomPackageBuilder::FHotUpdateCustomPackageBuilder()
	: bIsBuilding(false)
	, bIsCancelled(false)
{
}

TArray<FString> FHotUpdateCustomPackageBuilder::ResolveUassetPathsForCook() const
{
	TArray<FString> AssetPathsToCook;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("ResolveUassetPathsForCook: UassetFilePaths.Num()=%d"), CurrentConfig.UAssetFilePaths.Num());

	for (const FString& UassetFilePath : CurrentConfig.UAssetFilePaths)
	{
		FString PackageName = FHotUpdatePackageHelper::FilePathToLongPackageName(UassetFilePath);
		if (!PackageName.IsEmpty())
		{
			AssetPathsToCook.Add(PackageName);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("ResolveUassetPathsForCook: 无法将 uasset 路径解析为包名: %s"), *UassetFilePath);
		}
	}

	return AssetPathsToCook;
}

FHotUpdateCustomPackageResult FHotUpdateCustomPackageBuilder::ExecuteBuild(const FHotUpdateCustomPackageConfig& Config, const TArray<FString>& AssetPathsToCook)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("ExecuteBuild 开始 (后台线程)"));

	FHotUpdateCustomPackageResult Result;
	bIsCancelled = false;

	int32 TotalAssetCount = Config.UAssetFilePaths.Num() + Config.NonAssetFilePaths.Num();
	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包: uasset %d 个, 非资产 %d 个"), Config.UAssetFilePaths.Num(), Config.NonAssetFilePaths.Num());

	if (TotalAssetCount == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		return Result;
	}

	// 编译项目
	if (!Config.bSkipBuild)
	{
		UpdateProgress(TEXT("编译项目"), TEXT(""), 0, 0);
		if (!FHotUpdatePackageHelper::CompileProject(Config.Platform))
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

	// 只 Cook uasset 资源（依赖已在主线程收集）
	if (!Config.bSkipCook && AssetPathsToCook.Num() > 0)
	{
		UpdateProgress(TEXT("增量 Cook 资源"), TEXT(""), 0, AssetPathsToCook.Num());
		if (!FHotUpdatePackageHelper::CookAssets(Config.Platform, AssetPathsToCook))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Cook 资源失败");
			bIsBuilding = false;
			return Result;
		}
	}

	// 构建合并的 AssetDiskPaths 映射
	TArray<FString> ValidAssetPaths;
	TArray<FString> ValidNonAssetPaths;

	// uasset 文件：磁盘路径已经可用
	FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(Config.Platform);
	for (const FString& UassetPath : Config.UAssetFilePaths)
	{
		if (FPaths::FileExists(*UassetPath))
		{
			ValidAssetPaths.Add(UassetPath);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包: 跳过无 cooked 文件的 uasset: %s"), *UassetPath);
		}
	}

	// non-asset 文件：直接使用原始磁盘路径
	for (const FString& NonAssetPath : Config.NonAssetFilePaths)
	{
		if (FPaths::FileExists(*NonAssetPath))
		{
			ValidNonAssetPaths.Add(NonAssetPath);
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包: 跳过不存在的非资产文件: %s"), *NonAssetPath);
		}
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包: 有效资源 %d 个"), ValidAssetPaths.Num());

	if (ValidAssetPaths.Num() == 0 && ValidNonAssetPaths.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源（Cook 后无有效文件）");
		bIsBuilding = false;
		return Result;
	}

	// 计算资源 Hash
	UpdateProgress(TEXT("计算资源 Hash"), TEXT(""), 0, ValidAssetPaths.Num());

	TMap<FString, FString> AssetHashes;
	TMap<FString, int64> AssetSizes;
	
	TArray<FString> AllAssets;
	AllAssets.Append(ValidAssetPaths);
	AllAssets.Append(ValidNonAssetPaths);

	for (int32 i = 0; i < AllAssets.Num(); i++)
	{
		if (bIsCancelled)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("构建已取消");
			bIsBuilding = false;
			return Result;
		}

		const FString& AssetPath = AllAssets[i];
		const FString SourcePath = FHotUpdatePackageHelper::GetAssetSourcePath(AssetPath);
		if (!FPaths::FileExists(*SourcePath))
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("自定义打包: 跳过不存在的文件: %s->%s"), *AssetPath, *SourcePath);
			continue;
		}
		
		if (!SourcePath.IsEmpty())
		{
			AssetHashes.Add(AssetPath, UHotUpdateFileUtils::CalculateFileHash(SourcePath));
			AssetSizes.Add(AssetPath, IFileManager::Get().FileSize(*SourcePath));
		}
		UpdateProgress(TEXT("计算资源 Hash"), AssetPath, i + 1, ValidAssetPaths.Num());
	}

	// 确定输出目录
	FString OutputDir = Config.OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("HotUpdateCustomPackages");
	}

	FString PlatformStr = HotUpdateUtils::GetPlatformString(Config.Platform);
	OutputDir = FPaths::Combine(OutputDir, Config.PatchVersion, PlatformStr);
	FPaths::NormalizeDirectoryName(OutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);

	Result.OutputDirectory = OutputDir;
	Result.PatchVersion = Config.PatchVersion;
	Result.AssetCount = ValidAssetPaths.Num();

	// 创建 IoStore 容器
	UpdateProgress(TEXT("创建打包容器"), TEXT(""), 0, ValidAssetPaths.Num());

	if (AllAssets.Num() > 0)
	{
		FHotUpdateIoStoreBuilder IoStoreBuilder;

		FHotUpdateIoStoreConfig IoStoreConfig = Config.IoStoreConfig;
		IoStoreConfig.bUseIoStore = false;
		FString PrioritySuffix = FString::Printf(TEXT("_%d_P"), Config.PakPriority);
		IoStoreConfig.ContainerName = FString::Printf(TEXT("%s%s"), *Config.PatchVersion, *PrioritySuffix);

		FString PaksDir = FPaths::Combine(OutputDir, TEXT("Paks"));
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*PaksDir);

		FString PatchOutputPath = FPaths::Combine(PaksDir, IoStoreConfig.ContainerName);

		FHotUpdateIoStoreResult IoStoreResult = IoStoreBuilder.BuildIoStoreContainer(AllAssets, PatchOutputPath, IoStoreConfig, CookedPlatformDir);

		if (!IoStoreResult.bSuccess)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("容器创建失败: %s"), *IoStoreResult.ErrorMessage);
			bIsBuilding = false;
			return Result;
		}

		Result.PatchUtocPath = IoStoreResult.UtocPath;
		Result.PatchSize = IoStoreResult.ContainerSize;

		UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包容器创建成功: %s"), *IoStoreResult.UtocPath);
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		return Result;
	}

	Result.bSuccess = true;
	bIsBuilding = false;

	UpdateProgress(TEXT("完成"), TEXT(""), 1, 1);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("自定义打包构建成功: %s, %d 个资源, 大小 %lld 字节"),
		*OutputDir, ValidAssetPaths.Num(), Result.PatchSize);

	return Result;
}

void FHotUpdateCustomPackageBuilder::BuildCustomPackageAsync(const FHotUpdateCustomPackageConfig& Config)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("BuildCustomPackageAsync 开始调用"));

	if (bIsBuilding)
	{
		if (BuildTask.IsValid() && !BuildTask.IsReady())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("已有自定义打包构建任务正在运行，拒绝新的构建请求"));
			FHotUpdateCustomPackageResult Result;
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("已有构建任务正在进行中");
			OnComplete.Broadcast(Result);
			return;
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("检测到之前的构建异常终止，正在重置构建状态"));
			bIsBuilding = false;
			bIsCancelled = false;
		}
	}

	bIsBuilding = true;
	bIsCancelled = false;
	CurrentConfig = Config;

	UE_LOG(LogHotUpdateEditor, Log, TEXT("CurrentConfig.UAssetFilePaths 数量: %d, NonAssetFilePaths 数量: %d"),
		CurrentConfig.UAssetFilePaths.Num(), CurrentConfig.NonAssetFilePaths.Num());

	// 在 GameThread 将 uasset 磁盘路径反向解析为 UE 包名（供 Cook 使用）
	TArray<FString> AssetPathsToCook = ResolveUassetPathsForCook();

	UE_LOG(LogHotUpdateEditor, Log, TEXT("GameThread 解析到 %d 个 Cook 路径"), AssetPathsToCook.Num());

	if (AssetPathsToCook.Num() == 0 && CurrentConfig.NonAssetFilePaths.Num() == 0)
	{
		FHotUpdateCustomPackageResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("没有找到可打包的资源");
		bIsBuilding = false;
		OnComplete.Broadcast(Result);
		return;
	}

	// 主线程收集依赖并过滤引擎资产（AssetRegistry 操作必须在主线程）
	TArray<FString> AssetsWithDeps = FHotUpdatePackageHelper::CollectDependenciesAndFilterEngine(AssetPathsToCook);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("GameThread 收集依赖后 %d 个资源，启动后台构建"), AssetsWithDeps.Num());

	TWeakPtr<FHotUpdateCustomPackageBuilder> WeakBuilder(AsShared());

	BuildTask = Async(EAsyncExecution::Thread, [WeakBuilder, AssetsWithDeps]()
	{
		const TSharedPtr<FHotUpdateCustomPackageBuilder> Builder = WeakBuilder.Pin();
		if (!Builder.IsValid())
		{
			return;
		}

		FHotUpdateCustomPackageResult Result = Builder->ExecuteBuild(Builder->CurrentConfig, AssetsWithDeps);

		AsyncTask(ENamedThreads::GameThread, [WeakBuilder, Result]()
		{
			const TSharedPtr<FHotUpdateCustomPackageBuilder> PinnedBuilder = WeakBuilder.Pin();
			if (PinnedBuilder.IsValid())
			{
				PinnedBuilder->OnComplete.Broadcast(Result);
			}
		});
	});
}

void FHotUpdateCustomPackageBuilder::CancelBuild()
{
	bIsCancelled = true;
}

FHotUpdatePackageProgress FHotUpdateCustomPackageBuilder::GetCurrentProgress() const
{
	FScopeLock Lock(&ProgressCriticalSection);
	return CurrentProgress;
}

void FHotUpdateCustomPackageBuilder::UpdateProgress(const FString& Stage, const FString& CurrentFile, int32 ProcessedFiles, int32 TotalFiles)
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