// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Asset-Pak Manifest 生成器
 *
 * 在编辑器打包流程末尾调用，扫描每个 Pak 文件内部的 Cook 后 Asset，
 * 生成 asset_pak_manifest.json 文件用于运行时 Asset→Pak 映射查询。
 *
 * 输出 JSON 格式：
 * {
 *   "version": "1.0.0",
 *   "platform": "Windows",
 *   "buildTime": "...",
 *   "paks": [
 *     { "pakPath": "pakchunk1-Windows.pak", "chunkId": 1, "assets": [...] }
 *   ],
 *   "assetIndex": {
 *     "/Game/Maps/Level01": { "pakPath": "...", "chunkId": 1 }
 *   }
 * }
 */
class HOTUPDATEEDITOR_API FHotUpdateAssetPakManifestGenerator
{
public:
	/**
	 * 生成 Asset-Pak Manifest 文件
	 *
	 * @param PakSearchDir   包含 .pak 文件的目录（绝对路径）
	 * @param OutputDir      输出目录（asset_pak_manifest.json 将写入此处）
	 * @param Version        版本号字符串
	 * @param Platform       平台名称字符串（如 "Windows"、"Android"）
	 * @return true 表示生成成功
	 */
	static bool Generate(
		const FString& PakSearchDir,
		const FString& OutputDir,
		const FString& Version,
		const FString& Platform);

private:
	/**
	 * 将 Pak 内部文件路径转换为 UE Asset 路径
	 *
	 * 去除 MountPoint 前缀和文件后缀（.uasset/.uexp/.ubulk/.umap），
	 * 转换为 /Game/... 或 /Engine/... 格式的虚拟包路径。
	 *
	 * @param InternalPath   Pak 内部文件路径
	 * @param MountPoint     Pak 的 MountPoint
	 * @return UE Asset 路径，转换失败返回空串
	 */
	static FString ConvertPakPathToAssetPath(const FString& InternalPath, const FString& MountPoint);

	/**
	 * 从 Pak 文件名中解析 ChunkId
	 *
	 * 例如 "pakchunk1-Windows.pak" → 1
	 * 如果无法解析则返回 -1
	 */
	static int32 ParseChunkIdFromPakName(const FString& PakFileName);
};
