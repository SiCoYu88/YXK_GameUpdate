// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 打包工具类（静态，无状态）
 * 提供编译、Cook、路径解析等共享逻辑，供多个 Builder 组合使用
 *
 * 路径格式约定：
 * - Long Package Name:  UE 虚拟路径格式，如 /Game/Maps/Start（不含扩展名）
 * - Pak Mount Path:      Pak 内部路径格式，如 ../../../GameUpdate/Content/Maps/Start.umap（含扩展名）
 * - Absolute Path:       磁盘绝对路径，如 E:/Test/HotPatch/GameUpdate/Content/Maps/Start.umap
 * - Cooked Path:         Cooked 输出路径，如 {ProjectDir}/Saved/Cooked/Windows/GameUpdate/Content/Maps/Start.umap
 */
class HOTUPDATEEDITOR_API FHotUpdatePackageHelper
{
public:
	/** 编译项目 */
	static bool CompileProject(EHotUpdatePlatform Platform);

	/** 全量 Cook */
	static bool CookAssets(EHotUpdatePlatform Platform);

	/** 增量 Cook 指定资源（不含依赖收集） */
	static bool CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook);

	/**
	 * 收集硬依赖并过滤引擎资产（必须在主线程调用）
	 * 统一入口，供 PatchPackageBuilder 和 CustomPackageBuilder 共用
	 * @param AssetsToCook 要 Cook 的资源列表（Long Package Name 格式）
	 * @return 包含硬依赖的资源列表（已过滤引擎资产）
	 */
	static TArray<FString> CollectDependenciesAndFilterEngine(const TArray<FString>& AssetsToCook);

	// ==================== 路径转换函数 ====================

	/**
	 * 判断插件属于引擎还是项目，返回 Cooked 目录的 SubDir
	 * @param PluginPath "Plugins/NNE/NNEDenoiser/Content/" 格式
	 * @return "Engine/Plugins/..." 或 "{ProjectName}/Plugins/..."
	 */
	static FString GetPluginCookedSubDir(const FString& PluginPath);

	/**
	 * 将 FilePathRoot 规范化为 Pak 挂载格式
	 * FilePathRoot 可能是绝对路径或相对路径，需要转换为标准 Pak 格式
	 * @param FilePathRoot FPackageName::TryGetMountPointForPath 返回的 FilePathRoot
	 * @param PackageNameRoot 对应的虚拟路径根（如 /Game/、/Engine/）
	 * @return Pak 挂载根路径，如 ../../../GameUpdate/Content/ 或 ../../../Engine/Content/
	 */
	static FString NormalizeFilePathRootToPakMount(const FString& FilePathRoot, const FString& PackageNameRoot);

	/** 资源路径 -> Cooked 文件路径 */
	static FString GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir);

	/** 资源路径 -> 源文件路径 */
	static FString GetAssetSourcePath(const FString& AssetPath);

	/** 文件路径 -> UE Long Package Name（仅处理 .uasset/.umap） */
	static FString FilePathToLongPackageName(const FString& FilePath);

	/** 文件路径 -> Content 目录虚拟路径（用于非资产文件如 .txt/.json） */
	static FString FilePathToContentMountPath(const FString& FilePath);

	/**
	 * 将虚拟包路径映射为 Pak 内部挂载路径
	 * /Game/... -> ../../../{ProjectName}/Content/...
	 * /Engine/... -> ../../../Engine/Content/...
	 * 插件路径 -> 根据 FPackageName 解析结果映射
	 * @param AssetPath 虚拟包路径（不含扩展名，以 / 开头）
	 * @return Pak 内部 Dest 路径（不含扩展名）
	 */
	static FString GetAssetPakMountPath(const FString& AssetPath);

	// ==================== 辅助判断函数 ====================

	/** 是否是外部资产（ExternalActors/ExternalObjects/Script/Memory 包） */
	static bool IsExternalAsset(const FString& AssetPath);

	/** 判断路径是否是有效的 UE Package（可以被 Cook） */
	static bool IsValidPackagePath(const FString& AssetPath);

private:
	/** 规范化路径分隔符，统一使用正斜杠 */
	static FString NormalizePathToForwardSlash(const FString& Path);

	/** 确保路径末尾有斜杠 */
	static FString EnsureTrailingSlash(const FString& Path);

	/** 获取规范化后的引擎/项目目录（带末尾斜杠） */
	struct FNormalizedDirectories
	{
		FString EngineDir;
		FString ProjectDir;
		FString EnginePluginsDir;
		FString ProjectPluginsDir;
	};
	static FNormalizedDirectories GetNormalizedDirectories();

	/** 从路径中提取 Plugins/ 开始的相对部分 */
	static FString ExtractPluginsRelativePath(const FString& Path);
};