// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Asset 依赖信息收集器
 *
 * 在编辑器打包流程中，基于已生成的 asset_pak_manifest.json 和 AssetRegistry，
 * 收集每个 Asset 的 Hard/Soft 依赖关系，并关联依赖 Asset 所在的 Pak 信息。
 *
 * 输出 JSON 格式：
 * {
 *   "version": "1.0.0",
 *   "dependencies": {
 *     "/Game/Maps/Level01": {
 *       "hardDeps": [{ "assetPath": "...", "pakPath": "...", "chunkId": 1 }],
 *       "softDeps": [...],
 *       "requiredPaks": ["pakchunk1-Windows.pak"],
 *       "optionalPaks": ["pakchunk2-Windows.pak"]
 *     }
 *   }
 * }
 */
class HOTUPDATEEDITOR_API FHotUpdateAssetDependencyCollector
{
public:
	/**
	 * 收集 Asset 依赖信息并生成 asset_dependencies.json
	 *
	 * @param AssetPakManifestPath  asset_pak_manifest.json 的完整路径
	 * @param OutputDir             输出目录（asset_dependencies.json 将写入此处）
	 * @param Version               版本号字符串
	 * @return true 表示生成成功
	 */
	static bool Collect(
		const FString& AssetPakManifestPath,
		const FString& OutputDir,
		const FString& Version);

private:
	/**
	 * 从 asset_pak_manifest.json 加载 Asset→Pak 映射
	 *
	 * @param ManifestPath  Manifest 文件路径
	 * @param OutAssetToPak Asset→PakPath 映射
	 * @param OutAssetToChunk Asset→ChunkId 映射
	 * @return true 表示加载成功
	 */
	static bool LoadAssetPakMapping(
		const FString& ManifestPath,
		TMap<FString, FString>& OutAssetToPak,
		TMap<FString, int32>& OutAssetToChunk);
};
