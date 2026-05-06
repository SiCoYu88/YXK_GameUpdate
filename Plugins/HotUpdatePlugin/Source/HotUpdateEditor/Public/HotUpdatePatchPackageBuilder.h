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

	// === BuildPatchPackage 阶段上下文 ===
	struct FPatchBuildContext
	{
		FHotUpdateBaseManifestData BaseManifestData;
		FString ActualBaseVersion;
		TArray<FString> AssetSourcePaths;
		TArray<FString> NonAssetSourcePaths;
		TMap<FString, FString> CurrentAssetHashes;
		TMap<FString, int64> CurrentAssetSizes;
		TMap<FString, FString> CurrentNonAssetHashes;
		TMap<FString, int64> CurrentNonAssetSizes;
		FHotUpdateDiffReport DiffReport;
		TArray<FString> ChangedAssetPaths;
		TArray<FString> ChangedNonAssetPaths;
		FString OutputDir;
		FString PatchUtocPath;
		FString PatchUcasPath;
		int64 PatchSize = 0;
		TArray<FHotUpdateContainerInfo> BaseContainers;
		// 绝对路径 -> 资产虚拟路径（/Game/...）映射，用于 manifest 生成
		TMap<FString, FString> AbsolutePathToAssetPath;
	};

	// === BuildPatchPackage 阶段函数 ===

	/** 阶段1: 验证配置、编译项目、Cook 资源 */
	bool PrepareBuild(FPatchBuildContext& Ctx);

	/** 阶段2: 加载基础 Manifest、收集资源、计算差异、增量 Cook */
	bool ComputeChanges(FPatchBuildContext& Ctx);

	/** 阶段3: 确定输出目录、创建 Patch IoStore 容器 */
	bool CreatePatchContainer(FPatchBuildContext& Ctx);

	/** 阶段4: 解析基础容器引用、生成 Manifest、注册版本 */
	bool BuildAndRegisterManifest(FPatchBuildContext& Ctx, FHotUpdatePatchPackageResult& Result);

	/// 构建配置
	FHotUpdatePatchPackageConfig CurrentConfig;

	/// 当前构建结果（阶段函数失败时设置）
	FHotUpdatePatchPackageResult CurrentResult;

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