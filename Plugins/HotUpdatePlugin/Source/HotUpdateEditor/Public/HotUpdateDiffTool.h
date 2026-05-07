// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 资源差异比较工具
 * 用于比较两个版本间的资源差异
 *
 * 注意：CompareManifests 与 FHotUpdatePatchPackageBuilder::ComputeDiff 有相似的差异比较核心逻辑，
 * 但两者输入数据格式不同：
 * - CompareManifests：解析 manifest.json 文件，使用 FHotUpdateManifestEntry 结构
 * - ComputeDiff：直接接收 Hash/Size 映射，用于打包流程
 */
class HOTUPDATEEDITOR_API FHotUpdateDiffTool
{
public:

	/**
	 * 比较两个Manifest文件的差异
	 */
	FHotUpdateDiffReport CompareManifests(
		const FString& BaseManifestPath,
		const FString& TargetManifestPath) const;

	/**
	 * 获取资源类型图标名称
	 */
	static FName GetAssetIconName(const FString& AssetPath);

	/**
	 * 在版本目录中查找 filemanifest.json 文件路径
	 * 查找顺序：1) VersionDir/Windows/filemanifest.json 2) VersionDir/filemanifest.json
	 * 如果找不到 filemanifest.json，回退查找 manifest.json
	 * @param VersionDirectory 版本目录路径
	 * @return 找到的 manifest 文件路径，找不到返回空字符串
	 */
	static FString FindFileManifestPath(const FString& VersionDirectory);

private:
	/**
	 * 解析Manifest文件
	 */
	static bool ParseManifestFile(
		const FString& ManifestPath,
		TMap<FString, FHotUpdateManifestEntry>& OutEntries);
};