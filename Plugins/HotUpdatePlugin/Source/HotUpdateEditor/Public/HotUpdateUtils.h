// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 热更新工具函数集合
 */
namespace HotUpdateUtils
{
	/**
	 * 将平台枚举转换为显示名称字符串
	 * @param Platform 平台枚举值
	 * @return 平台显示名称 ("Windows", "Android", "IOS")
	 */
	HOTUPDATEEDITOR_API FString GetPlatformString(EHotUpdatePlatform Platform);

	/**
	 * 将平台枚举转换为目录名称字符串
	 * @param Platform 平台枚举值
	 * @return 平台目录名称 ("Win64", "Android", "IOS")
	 */
	HOTUPDATEEDITOR_API FString GetPlatformDirectoryName(EHotUpdatePlatform Platform);

	/**
	 * 获取平台目录名称（考虑 Android 纹理格式后缀）
	 * @param Platform 平台枚举值
	 * @param AndroidTextureFormat Android 纹理格式（仅 Android 平台有效）
	 * @return 平台目录名称 ("Windows", "Android_ASTC", "Android_ETC2" 等)
	 */
	HOTUPDATEEDITOR_API FString GetPlatformDirName(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat = EHotUpdateAndroidTextureFormat::Multi);

	/**
	 * 获取 Cooked 平台目录路径（Saved/Cooked/{PlatformName}）
	 * @param Platform 平台枚举值
	 * @return Cooked 平台目录的完整路径
	 */
	HOTUPDATEEDITOR_API FString GetCookedPlatformDir(EHotUpdatePlatform Platform);

	/**
	 * 获取 Cooked 平台目录路径（考虑 Android 纹理格式后缀，如 Android_ASTC）
	 * @param Platform 平台枚举值
	 * @param AndroidTextureFormat Android 纹理格式（仅 Android 平台有效）
	 * @return Cooked 平台目录的完整路径
	 */
	HOTUPDATEEDITOR_API FString GetCookedPlatformDir(EHotUpdatePlatform Platform, EHotUpdateAndroidTextureFormat AndroidTextureFormat);

	/**
	 * 从 Manifest JSON 文件中提取版本号
	 * @param ManifestPath Manifest 文件的完整路径
	 * @return 版本号字符串，解析失败返回空字符串
	 */
	HOTUPDATEEDITOR_API FString ExtractVersionFromManifest(const FString& ManifestPath);

	/**
	 * 计算资源差异（公共核心函数）
	 * @param CurrentPaths 当前资源路径列表
	 * @param CurrentHashes 当前资源 Hash 映射
	 * @param CurrentSizes 当前资源 Size 映射
	 * @param BaseHashes 基础版本 Hash 映射
	 * @param BaseSizes 基础版本 Size 映射
	 * @param OutChangedAssets 变更的资源路径列表（新增+修改）
	 * @param OutReport 差异报告
	 */
	HOTUPDATEEDITOR_API void CalculateAssetDiff(
		const TArray<FString>& CurrentPaths,
		const TMap<FString, FString>& CurrentHashes,
		const TMap<FString, int64>& CurrentSizes,
		const TMap<FString, FString>& BaseHashes,
		const TMap<FString, int64>& BaseSizes,
		TArray<FString>& OutChangedAssets,
		FHotUpdateDiffReport& OutReport);

	/**
	 * 将平台枚举转换为本地化显示文本
	 * @param Platform 平台枚举值
	 * @return 平台显示文本（如 "Windows", "Android", "iOS"）
	 */
	HOTUPDATEEDITOR_API FText GetPlatformDisplayName(EHotUpdatePlatform Platform);

	/**
	 * 将 Android 纹理格式枚举转换为本地化显示文本
	 * @param TextureFormat 纹理格式枚举值
	 * @return 纹理格式显示文本（如 "ETC2", "ASTC", "DXT", "Multi"）
	 */
	HOTUPDATEEDITOR_API FText GetTextureFormatDisplayName(EHotUpdateAndroidTextureFormat TextureFormat);

	/**
	 * 将构建配置枚举转换为本地化显示文本
	 * @param BuildConfig 构建配置枚举值
	 * @return 构建配置显示文本（如 "DebugGame", "Development", "Shipping"）
	 */
	HOTUPDATEEDITOR_API FText GetBuildConfigDisplayName(EHotUpdateBuildConfiguration BuildConfig);

	/**
	 * 将分包策略枚举转换为本地化显示文本
	 * @param ChunkStrategy 分包策略枚举值
	 * @return 分包策略显示文本（如 "不分包", "按大小分包"）
	 */
	HOTUPDATEEDITOR_API FText GetChunkStrategyDisplayName(EHotUpdateChunkStrategy ChunkStrategy);

	/**
	 * 将依赖策略枚举转换为本地化显示文本
	 * @param DependencyStrategy 依赖策略枚举值
	 * @return 依赖策略显示文本（如 "包含所有依赖", "仅硬依赖"）
	 */
	HOTUPDATEEDITOR_API FText GetDependencyStrategyDisplayName(EHotUpdateDependencyStrategy DependencyStrategy);

	/**
	 * 格式化文件大小为友好字符串
	 * @param Size 文件大小（字节）
	 * @return 格式化后的字符串（如 "1.5 KB", "2.3 MB"）
	 */
	HOTUPDATEEDITOR_API FString FormatFileSize(int64 Size);
}