// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/**
 * 热更新打包构建器
 * 基于基础包生成差异更新包
 */
class HOTUPDATEEDITOR_API FHotUpdatePatchPackageBuilder : public TSharedFromThis<FHotUpdatePatchPackageBuilder>
{
public:
	FHotUpdatePatchPackageBuilder();

	/** 构建更新包 */
	FHotUpdatePatchPackageResult BuildPatchPackage(const FHotUpdatePatchPackageConfig& Config);

	/** 异步构建更新包 */
	void BuildPatchPackageAsync(const FHotUpdatePatchPackageConfig& Config);

	/** 取消构建 */
	void CancelBuild();

	/** 是否正在构建 */
	bool IsBuilding() const { return bIsBuilding; }

	/** 验证配置 */
	static bool ValidateConfig(const FHotUpdatePatchPackageConfig& Config, FString& OutErrorMessage);

	// 进度委托
	FOnPackageProgressDelegate OnProgress;

	// 完成委托
	FOnPatchPackageCompleteDelegate OnComplete;

private:
	/** 将 Hash 映射按资产类型分离 */
	static void SplitHashesByAssetType(
		const TMap<FString, FString>& AllHashes,
		const TMap<FString, int64>& AllSizes,
		TMap<FString, FString>& OutAssetHashes,
		TMap<FString, int64>& OutAssetSizes,
		TMap<FString, FString>& OutNonAssetHashes,
		TMap<FString, int64>& OutNonAssetSizes);

	/** 合并资产和非资产差异报告 */
	static FHotUpdateDiffReport MergeDiffReports(
		const FHotUpdateDiffReport& AssetReport,
		const FHotUpdateDiffReport& NonAssetReport,
		const FString& BaseVersion,
		const FString& TargetVersion);

	// === 阶段子函数 ===

	/** 收集资源 */
	bool CollectAssets(FString& OutErrorMessage);

	/** 收集源文件路径（分为 UE 资产和非资产） */
	bool CollectSourceFilePaths(
		TArray<FString>& OutAssetSourcePaths,
		TArray<FString>& OutNonAssetSourcePaths,
		FString& OutErrorMessage);
	

	/** 加载基础版本 FileManifest（返回分类结果） */
	static bool LoadBaseFileManifest(
		const FString& ManifestPath,
		FHotUpdateBaseManifestData& OutData,
		FString& OutErrorMessage);

	/** 计算差异（包含 Sizes） */
	static bool ComputeDiff(
		const TArray<FString>& CurrentAssets,
		const TMap<FString, FString>& CurrentHashes,
		const TMap<FString, int64>& CurrentSizes,
		const TMap<FString, FString>& BaseHashes,
		const TMap<FString, int64>& BaseSizes,
		TArray<FString>& OutChangedAssets,
		FHotUpdateDiffReport& OutReport);


	/** 生成 Manifest */
	bool GenerateManifest(
		const FString& ManifestPath,
		const FString& PatchUtocPath,
		const FString& PatchUcasPath,
		const FHotUpdateDiffReport& DiffReport,
		const TArray<FHotUpdateContainerInfo>& BaseContainers = TArray<FHotUpdateContainerInfo>()) const;

	/** 更新进度 */
	void UpdateProgress(
		const FString& Stage,
		const FString& CurrentFile,
		int32 ProcessedFiles,
		int32 TotalFiles);

	/** 创建错误结果 */
	static FHotUpdatePatchPackageResult MakeErrorResult(const FString& ErrorMessage);

	/// 构建配置
	FHotUpdatePatchPackageConfig CurrentConfig;

	/// 是否正在构建
	std::atomic<bool> bIsBuilding;

	/// 是否已取消
	std::atomic<bool> bIsCancelled;

	/// 当前进度
	FHotUpdatePackageProgress CurrentProgress;

	/// 进度临界区
	mutable FCriticalSection ProgressCriticalSection;

	/// 构建任务
	TFuture<void> BuildTask;
};