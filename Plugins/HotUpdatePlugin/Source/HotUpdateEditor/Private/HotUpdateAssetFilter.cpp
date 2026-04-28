// Copyright czm. All Rights Reserved.

#include "HotUpdateAssetFilter.h"

#include "HotUpdatePackageHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

DEFINE_LOG_CATEGORY_STATIC(LogHotUpdateAssetFilter, Log, All);

void FHotUpdateAssetFilter::FilterAssets(const TArray<FString>& InAssetPaths, const FHotUpdateMinimalPackageConfig& Config, IAssetRegistry* AssetRegistry, TArray<FString>& OutWhitelistAssets, TArray<FString>& OutExcludedAssets)
{
	TSet<FString> WhitelistSet;

	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("开始过滤资产，输入资产数: %d"), InAssetPaths.Num());

	// 1. 收集白名单目录中的资产
	if (Config.WhitelistDirectories.Num() > 0)
	{
		TArray<FString> WhitelistDirAssets = CollectAssetsFromDirectories(Config.WhitelistDirectories, AssetRegistry);
		for (const FString& Asset : WhitelistDirAssets)
		{
			WhitelistSet.Add(Asset);
		}
		UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("必须包含的目录收集资产: %d"), WhitelistDirAssets.Num());
	}

	// 2. 根据依赖策略收集白名单资产的依赖
	TSet<FString> FinalWhitelist = WhitelistSet;
	if (Config.DependencyStrategy != EHotUpdateDependencyStrategy::None && WhitelistSet.Num() > 0)
	{
		for (const FString& Asset : WhitelistSet)
		{
			TSet<FString> Dependencies;
			GetDependencies(Asset, AssetRegistry, Config.DependencyStrategy, Dependencies);
			FinalWhitelist.Append(Dependencies);
		}
		UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("依赖收集后，白名单资产数: %d (添加依赖数: %d)"), FinalWhitelist.Num(), FinalWhitelist.Num() - WhitelistSet.Num());
	}

	// 3. 输出结果
	OutWhitelistAssets = FinalWhitelist.Array();

	for (const FString& Asset : InAssetPaths)
	{
		if (!FinalWhitelist.Contains(Asset))
		{
			OutExcludedAssets.Add(Asset);
		}
	}

	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("过滤完成: 白名单资产 %d 个, 排除资产 %d 个"), OutWhitelistAssets.Num(), OutExcludedAssets.Num());
}

bool FHotUpdateAssetFilter::IsInDirectories(
	const FString& AssetPath,
	const TArray<FDirectoryPath>& Directories,
	bool bRecursive)
{
	for (const FDirectoryPath& Dir : Directories)
	{
		if (Dir.Path.IsEmpty())
		{
			continue;
		}

		if (bRecursive)
		{
			if (AssetPath.StartsWith(Dir.Path))
			{
				return true;
			}
		}
		else
		{
			// 非递归：检查是否在直接子目录中
			if (AssetPath.StartsWith(Dir.Path))
			{
				FString RelativePath = AssetPath.RightChop(Dir.Path.Len());
				if (RelativePath.IsEmpty() || !RelativePath.Contains(TEXT("/")))
				{
					return true;
				}
			}
		}
	}
	return false;
}

TArray<FString> FHotUpdateAssetFilter::CollectAssetsFromDirectories(const TArray<FDirectoryPath>& Directories, const IAssetRegistry* AssetRegistry)
{
	TArray<FString> Result;

	if (!AssetRegistry)
	{
		UE_LOG(LogHotUpdateAssetFilter, Warning, TEXT("AssetRegistry 为空，无法收集目录资产"));
		return Result;
	}

	for (const FDirectoryPath& Dir : Directories)
	{
		if (Dir.Path.IsEmpty())
		{
			continue;
		}

		// 使用 AssetRegistry 扫描目录
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Dir.Path));
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry->GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			Result.Add(AssetData.PackageName.ToString());
		}
	}

	UE_LOG(LogHotUpdateAssetFilter, Log, TEXT("从 %d 个目录收集了 %d 个资产"), Directories.Num(), Result.Num());
	return Result;
}

void FHotUpdateAssetFilter::GetDependencies(
	const FString& AssetPath,
	IAssetRegistry* AssetRegistry,
	EHotUpdateDependencyStrategy Strategy,
	TSet<FString>& OutDependencies)
{
	TSet<FString> Visited;
	GetDependenciesRecursive(AssetPath, AssetRegistry, Strategy, OutDependencies, Visited);
}

void FHotUpdateAssetFilter::GetDependenciesRecursive(
	const FString& AssetPath,
	IAssetRegistry* AssetRegistry,
	EHotUpdateDependencyStrategy Strategy,
	TSet<FString>& OutDependencies,
	TSet<FString>& Visited)
{
	if (!AssetRegistry)
	{
		return;
	}

	// 检查是否已访问
	if (Visited.Contains(AssetPath))
	{
		return;
	}
	Visited.Add(AssetPath);

	// 添加当前资产到结果
	OutDependencies.Add(AssetPath);

	// 根据策略获取依赖
	TArray<FName> Dependencies;
	UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package;

	switch (Strategy)
	{
	case EHotUpdateDependencyStrategy::IncludeAll:
		{
			// 必须分两次查询，Hard 和 Soft 不能用位或组合
			// 因为 Soft = NotHard，组合后会导致 Required 和 Excluded 同时设置 Hard，矛盾
			TArray<FName> HardDeps;
			TArray<FName> SoftDeps;
			AssetRegistry->GetDependencies(FName(*AssetPath), HardDeps, Category,
				UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
			AssetRegistry->GetDependencies(FName(*AssetPath), SoftDeps, Category,
				UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Soft));
			Dependencies.Append(HardDeps);
			Dependencies.Append(SoftDeps);
		}
		break;
	case EHotUpdateDependencyStrategy::HardOnly:
		AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, Category,
			UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
		break;
	case EHotUpdateDependencyStrategy::SoftOnly:
		AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, Category,
			UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Soft));
		break;
	case EHotUpdateDependencyStrategy::None:
		// 不收集依赖，直接返回
		return;
	default:
		AssetRegistry->GetDependencies(FName(*AssetPath), Dependencies, Category,
			UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
		break;
	}

	for (const FName& Dep : Dependencies)
	{
		FString DepStr = Dep.ToString();

		if (FPackageName::IsScriptPackage(DepStr) || FPackageName::IsMemoryPackage(DepStr))
		{
			continue;
		}

		// 添加到结果
		OutDependencies.Add(DepStr);

		// 递归获取依赖
		GetDependenciesRecursive(DepStr, AssetRegistry, Strategy, OutDependencies, Visited);
	}
}