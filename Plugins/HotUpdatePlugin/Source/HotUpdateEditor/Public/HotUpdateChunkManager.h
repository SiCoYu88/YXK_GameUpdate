// Copyright czm. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"
#include "HAL/CriticalSection.h"

class IAssetRegistry;

/**
 * Chunk 分析结果
 */

struct HOTUPDATEEDITOR_API FHotUpdateChunkAnalysisResult
{
	/// 所有 Chunk 定义
	TArray<FHotUpdateChunkDefinition> Chunks;

	/// 资源到 Chunk 的映射
	TMap<FString, int32> AssetToChunkMap;

	/// 统计信息
	int32 TotalAssetCount;
	int32 TotalChunkCount;
	int64 TotalSize;

	/// 是否成功
	bool bSuccess;

	/// 错误信息
	FString ErrorMessage;

	FHotUpdateChunkAnalysisResult()
		: TotalAssetCount(0)
		, TotalChunkCount(0)
		, TotalSize(0)
		, bSuccess(false)
	{
	}
};

/**
 * Chunk 管理器
 * 使用 UE5 标准方法进行 Chunk 划分
 */
class HOTUPDATEEDITOR_API FHotUpdateChunkManager
{
public:
	FHotUpdateChunkManager();

	/**
	 * 分析资源并创建 Chunk 划分
	 * @param AssetPaths 资源路径列表
	 * @param AssetDiskPaths 资源磁盘路径映射
	 * @param Config 分析配置
	 * @return 分析结果
	 */
	static FHotUpdateChunkAnalysisResult AnalyzeAndCreateChunks(
		const TArray<FString>& AssetPaths,
		const TMap<FString, FString>& AssetDiskPaths,
		const FHotUpdateChunkAnalysisConfig& Config);

	/**
	 * 为更新包创建 Chunk 划分
	 * @param ChangedAssets 变更的资源
	 * @param AssetDiskPaths 资源磁盘路径映射
	 * @param Config 分析配置
	 * @return 分析结果
	 */
	static FHotUpdateChunkAnalysisResult CreatePatchChunks(
		const TArray<FString>& ChangedAssets,
		const TMap<FString, FString>& AssetDiskPaths,
		const FHotUpdateChunkAnalysisConfig& Config);

private:
	/**
	 * 按大小划分（扩展版，支持详细配置）
	 */
	static bool DivideBySizeWithConfig(
		const TArray<FString>& AssetPaths,
		const TMap<FString, FString>& AssetDiskPaths,
		const FHotUpdateSizeBasedChunkConfig& Config,
		TArray<FHotUpdateChunkDefinition>& OutChunks,
		TMap<FString, int32>& OutAssetToChunk);

	/**
	 * 不分包（所有资源放入一个 Chunk）
	 */
	static bool CreateSingleChunk(
		const TArray<FString>& AssetPaths,
		const TMap<FString, FString>& AssetDiskPaths,
		const FString& ChunkName,
		int32 ChunkId,
		TArray<FHotUpdateChunkDefinition>& OutChunks,
		TMap<FString, int32>& OutAssetToChunk);

	/**
	 * 构建依赖关系
	 */
	static bool BuildDependencies(
		TArray<FHotUpdateChunkDefinition>& Chunks,
		const TMap<FString, int32>& AssetToChunk,
		const IAssetRegistry* AssetRegistry);

	/**
	 * 获取资源文件大小
	 */
	static int64 GetAssetSize(const FString& AssetPath, const TMap<FString, FString>& AssetDiskPaths);

	/**
	 * 分配下一个 Chunk ID
	 */
	static int32 AllocateNextChunkId();

private:
	/// 下一个自动分配的 Chunk ID
	static int32 NextAutoChunkId;

	/// Chunk ID 分配锁
	static FCriticalSection ChunkIdLock;
};