// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateEditorTypes.h"

/**
 * 打包工具类（静态，无状态）
 * 提供编译、Cook、路径解析等共享逻辑，供多个 Builder 组合使用
 */
class HOTUPDATEEDITOR_API FHotUpdatePackageHelper
{
public:
	/** 编译项目 */
	static bool CompileProject(EHotUpdatePlatform Platform);

	/** 全量 Cook */
	static bool CookAssets(EHotUpdatePlatform Platform);

	/** 增量 Cook 指定资源 */
	static bool CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook);
	
	/**
	 * 判断插件属于引擎还是项目，返回 Cooked 目录的 SubDir
	 * @param PluginPath "Plugins/NNE/NNEDenoiser/Content/" 格式
	 * @return "Engine/Plugins/..." 或 "{ProjectName}/Plugins/..."
	 */
	static FString GetPluginCookedSubDir(const FString& PluginPath);
	
	/**
	 * 将绝对路径转换为 Pak 挂载格式
	 * @param AbsolutePath 绝对路径
	 * @param EngineDir 引擎目录（已规范化）
	 * @param ProjectDir 项目目录（已规范化）
	 * @return Pak 挂载路径
	 */
	static FString ConvertAbsolutePathToPakMount(const FString& AbsolutePath, const FString& EngineDir, const FString& ProjectDir);
	
	/** 资源路径 -> Cooked 文件路径 */
	static FString GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir);

	/** 资源路径 -> 源文件路径 */
	static FString GetAssetSourcePath(const FString& AssetPath);
	
	/** 文件名 -> 资源路径（UE Long Package Name 格式） */
	static FString FileNameToAssetPath(const FString& FileName);
	
	/**
	 * 将虚拟包路径映射为 Pak 内部挂载路径
	 * /Game/... -> ../../../{ProjectName}/Content/...
	 * /Engine/... -> ../../../Engine/Content/...
	 * 插件路径 -> 根据 FPackageName 解析结果映射
	 * @param AssetPath 虚拟包路径（不含扩展名，以 / 开头）
	 * @return Pak 内部 Dest 路径（不含扩展名）
	 */
	static FString GetAssetPakMountPath(const FString& AssetPath);
	
	/**
	 * 是否UAsset
	 * @param AssetPath
	 * @return 
	 */
	static bool IsUAsset(const FString& AssetPath);

	static bool IsExternalAsset(const FString& AssetPath);
};