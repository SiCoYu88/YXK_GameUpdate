// Copyright czm. All Rights Reserved.

#include "HotUpdatePackageHelper.h"
#include "HotUpdateEditor.h"
#include "HotUpdateUtils.h"
#include "HotUpdateAssetFilter.h"
#include "Core/HotUpdateFileUtils.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/StringBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"

// ==================== 编译和 Cook 函数 ====================

bool FHotUpdatePackageHelper::CompileProject(EHotUpdatePlatform Platform)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始编译项目..."));

	FString EngineDir = FPaths::EngineDir();
	const FString UBTPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(EngineDir, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll")));

	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString PlatformName = HotUpdateUtils::GetPlatformDirectoryName(Platform);
	const FString BuildConfig = TEXT("Development");

	const FString Params = FString::Printf(
		TEXT("\"%s\" GameUpdate %s %s -project=\"%s\""),
		*UBTPath, *PlatformName, *BuildConfig, *ProjectPath);

	UE_LOG(LogHotUpdateEditor, Log, TEXT("执行编译: dotnet %s"), *Params);

	const FString CommandLine = FString::Printf(TEXT("/c dotnet %s"), *Params);

	FMonitoredProcess Process(TEXT("cmd.exe"), CommandLine, true);

	Process.OnOutput().BindLambda([](const FString& Output)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("%s"), *Output);
	});

	if (!Process.Launch())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法启动编译进程"));
		return false;
	}

	while (Process.Update())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (ReturnCode != 0)
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("编译失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("编译完成"));
	return true;
}

bool FHotUpdatePackageHelper::CookAssets(EHotUpdatePlatform Platform)
{
	return CookAssets(Platform, TArray<FString>());
}

bool FHotUpdatePackageHelper::CookAssets(EHotUpdatePlatform Platform, const TArray<FString>& AssetsToCook)
{
	UE_LOG(LogHotUpdateEditor, Log, TEXT("开始 Cook 资源..."));

	FString EngineDir = FPaths::EngineDir();
#if PLATFORM_WINDOWS
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
#elif PLATFORM_MAC
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Mac/UnrealEditor-Cmd"));
#else
	FString ExePath = FPaths::ConvertRelativePathToFull(EngineDir / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe"));
#endif
	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString CookPlatform = HotUpdateUtils::GetPlatformString(Platform);

	FString Params;
	if (AssetsToCook.Num() > 0)
	{
		FString PackageList;
		for (int32 i = 0; i < AssetsToCook.Num(); i++)
		{
			if (i > 0) PackageList += TEXT("+");
			PackageList += AssetsToCook[i];
		}

		Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -PACKAGE=%s -cooksinglepackage -NullRHI -unattended -NoSound"),
			*ProjectPath, *CookPlatform, *PackageList);

		UE_LOG(LogHotUpdateEditor, Display, TEXT("增量 Cook: 只 Cook %d 个资源（含硬引用）: %s"), AssetsToCook.Num(), *PackageList);
	}
	else
	{
		Params = FString::Printf(TEXT("\"%s\" -run=cook -targetplatform=%s -NullRHI -unattended -NoSound"),
			*ProjectPath, *CookPlatform);

		UE_LOG(LogHotUpdateEditor, Display, TEXT("全量 Cook"));
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("执行 Cook 命令: %s %s"), *ExePath, *Params);

	FMonitoredProcess Process(ExePath, Params, true);

	Process.OnOutput().BindLambda([](const FString& Output)
	{
		UE_LOG(LogHotUpdateEditor, Log, TEXT("%s"), *Output);
	});

	if (!Process.Launch())
	{
		UE_LOG(LogHotUpdateEditor, Error, TEXT("无法启动 Cook 进程"));
		return false;
	}

	while (Process.Update())
	{
		FPlatformProcess::Sleep(0.1f);
	}

	int32 ReturnCode = Process.GetReturnCode();

	if (ReturnCode != 0)
	{
		bool bIsIncremental = AssetsToCook.Num() > 0;
		if (bIsIncremental && ReturnCode == 1)
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("增量 Cook 返回警告码 1，检查 Cook 输出..."));
			FString CookedPlatformDir = HotUpdateUtils::GetCookedPlatformDir(Platform);
			int32 FoundCount = 0;
			for (const FString& AssetPath : AssetsToCook)
			{
				FString DiskPath = GetCookedAssetPath(AssetPath, CookedPlatformDir);
				if (!DiskPath.IsEmpty() && FPaths::FileExists(*DiskPath))
				{
					FoundCount++;
				}
			}
			if (FoundCount > 0)
			{
				UE_LOG(LogHotUpdateEditor, Log, TEXT("增量 Cook: %d/%d 个目标文件已生成，视为成功"), FoundCount, AssetsToCook.Num());
				return true;
			}
		}
		UE_LOG(LogHotUpdateEditor, Error, TEXT("Cook 失败，返回码: %d"), ReturnCode);
		return false;
	}

	UE_LOG(LogHotUpdateEditor, Log, TEXT("Cook 完成"));
	return true;
}

TArray<FString> FHotUpdatePackageHelper::CollectDependenciesAndFilterEngine(const TArray<FString>& AssetsToCook)
{
	if (AssetsToCook.Num() == 0)
	{
		return TArray<FString>();
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("收集依赖: 需要 Cook %d 个资源"), AssetsToCook.Num());

	TArray<FString> AssetsWithDeps;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry* AssetRegistry = &AssetRegistryModule.Get();
	if (AssetRegistry)
	{
		// 强制刷新 AssetRegistry 确保依赖信息完整
		AssetRegistry->SearchAllAssets(true);
		while (AssetRegistry->IsLoadingAssets())
		{
			FPlatformProcess::Sleep(0.1f);
		}

		TSet<FString> AssetsSet(AssetsToCook);
		for (const FString& AssetPath : AssetsToCook)
		{
			TSet<FString> Dependencies;
			// 使用 IncludeAll 策略收集所有依赖（包括软引用，如地图中放置的 Actor）
			FHotUpdateAssetFilter::GetDependencies(AssetPath, AssetRegistry, EHotUpdateDependencyStrategy::IncludeAll, Dependencies);

			// 过滤掉引擎资产（引擎资产不需要 Cook，已在引擎 Pak 中）
			for (const FString& Dep : Dependencies)
			{
				if (!UHotUpdateFileUtils::IsEngineAsset(Dep))
				{
					AssetsSet.Add(Dep);
				}
			}
		}
		AssetsWithDeps = AssetsSet.Array();
	}
	else
	{
		AssetsWithDeps = AssetsToCook;
	}

	UE_LOG(LogHotUpdateEditor, Display, TEXT("收集依赖: 共 %d 个资源 (新增依赖 %d 个)"),
		AssetsWithDeps.Num(), AssetsWithDeps.Num() - AssetsToCook.Num());

	return AssetsWithDeps;
}

// ==================== 路径转换函数实现 ====================

// ==================== 私有辅助函数 ====================

FString FHotUpdatePackageHelper::NormalizePathToForwardSlash(const FString& Path)
{
	FString Result = Path;
	Result.ReplaceCharInline('\\', '/');
	return Result;
}

FString FHotUpdatePackageHelper::EnsureTrailingSlash(const FString& Path)
{
	if (!Path.EndsWith(TEXT("/")))
	{
		return Path + TEXT("/");
	}
	return Path;
}

FHotUpdatePackageHelper::FNormalizedDirectories FHotUpdatePackageHelper::GetNormalizedDirectories()
{
	FNormalizedDirectories Dirs;

	Dirs.EngineDir = NormalizePathToForwardSlash(FPaths::EngineDir());
	Dirs.ProjectDir = NormalizePathToForwardSlash(FPaths::ProjectDir());
	Dirs.EnginePluginsDir = NormalizePathToForwardSlash(FPaths::EnginePluginsDir());
	Dirs.ProjectPluginsDir = NormalizePathToForwardSlash(FPaths::ProjectPluginsDir());

	Dirs.EngineDir = EnsureTrailingSlash(Dirs.EngineDir);
	Dirs.ProjectDir = EnsureTrailingSlash(Dirs.ProjectDir);
	Dirs.EnginePluginsDir = EnsureTrailingSlash(Dirs.EnginePluginsDir);
	Dirs.ProjectPluginsDir = EnsureTrailingSlash(Dirs.ProjectPluginsDir);

	return Dirs;
}

FString FHotUpdatePackageHelper::ExtractPluginsRelativePath(const FString& Path)
{
	int32 PluginsIdx = Path.Find(TEXT("Plugins/"));
	if (PluginsIdx == INDEX_NONE)
	{
		return TEXT("");
	}
	return Path.RightChop(PluginsIdx);
}

FString FHotUpdatePackageHelper::GetPluginCookedSubDir(const FString& PluginPath)
{
	// "Plugins/" 是 8 个字符，去掉后得到插件相对路径（如 "NNE/NNEDenoiser/Content/"）
	static constexpr int32 PluginsPrefixLen = 8;
	const FString PluginRelPath = PluginPath.RightChop(PluginsPrefixLen);

	FNormalizedDirectories Dirs = GetNormalizedDirectories();

	FString EnginePluginDir = Dirs.EnginePluginsDir + PluginRelPath;
	FString ProjectPluginDir = Dirs.ProjectPluginsDir + PluginRelPath;

	if (FPaths::DirectoryExists(EnginePluginDir))
	{
		return TEXT("Engine/") + PluginPath;
	}
	else if (FPaths::DirectoryExists(ProjectPluginDir))
	{
		return FString(FApp::GetProjectName()) + TEXT("/") + PluginPath;
	}

	return TEXT("");
}

FString FHotUpdatePackageHelper::NormalizeFilePathRootToPakMount(const FString& FilePathRoot, const FString& PackageNameRoot)
{
	FString Result = NormalizePathToForwardSlash(FilePathRoot);

	// 如果已经是 Pak 格式（以 ../../../ 开头），直接使用
	if (Result.StartsWith(TEXT("../../../")))
	{
		return EnsureTrailingSlash(Result);
	}

	FNormalizedDirectories Dirs = GetNormalizedDirectories();

	// 如果是绝对路径（包含盘符或以 / 开头的 Unix 路径），转换为相对路径
	if (Result.Contains(TEXT(":/")) || Result.StartsWith(TEXT("/")))
	{
		if (Result.StartsWith(Dirs.EngineDir))
		{
			// 引擎路径: ../../../Engine/...
			FString RelativePart = Result.RightChop(Dirs.EngineDir.Len());
			return TEXT("../../../Engine/") + RelativePart;
		}
		else if (Result.StartsWith(Dirs.ProjectDir))
		{
			// 项目路径: ../../../{ProjectName}/...
			FString RelativePart = Result.RightChop(Dirs.ProjectDir.Len());
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + RelativePart;
		}
		else if (Result.StartsWith(Dirs.EnginePluginsDir))
		{
			// 引擎插件: ../../../Engine/Plugins/...
			FString PluginRelPath = ExtractPluginsRelativePath(Result);
			return TEXT("../../../Engine/") + PluginRelPath;
		}
		else if (Result.StartsWith(Dirs.ProjectPluginsDir))
		{
			// 项目插件: ../../../{ProjectName}/Plugins/...
			FString PluginRelPath = ExtractPluginsRelativePath(Result);
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + PluginRelPath;
		}
		else
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法识别的绝对路径: %s"), *FilePathRoot);
			return TEXT("");
		}
	}

	// 其他相对路径格式，尝试根据 PackageNameRoot 推导 Pak 格式
	if (PackageNameRoot == TEXT("/Game/"))
	{
		return FString::Printf(TEXT("../../../%s/Content/"), FApp::GetProjectName());
	}
	else if (PackageNameRoot == TEXT("/Engine/"))
	{
		return TEXT("../../../Engine/Content/");
	}
	else if (PackageNameRoot.Contains(TEXT("/Plugins/")))
	{
		// 插件路径，从 FilePathRoot 提取 Plugins/ 部分并转换
		FString PluginRelPath = ExtractPluginsRelativePath(Result);
		if (PluginRelPath.IsEmpty())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法提取插件路径: %s"), *FilePathRoot);
			return TEXT("");
		}

		// 判断是引擎插件还是项目插件
		if (Result.StartsWith(Dirs.EnginePluginsDir))
		{
			return TEXT("../../../Engine/") + PluginRelPath;
		}
		else if (Result.StartsWith(Dirs.ProjectPluginsDir))
		{
			return FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName()) + PluginRelPath;
		}
		else if (Result.StartsWith(TEXT("../../../")))
		{
			// 已经是 Pak 格式（前面已处理，此处为保险）
			return EnsureTrailingSlash(Result);
		}

		UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 无法确定插件类型: %s"), *FilePathRoot);
		return TEXT("");
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("NormalizeFilePathRootToPakMount: 未知的 PackageNameRoot: %s"), *PackageNameRoot);
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetCookedAssetPath(const FString& AssetPath, const FString& CookedPlatformDir)
{
	if (IsExternalAsset(AssetPath))
	{
		return TEXT("");
	}

	// 内联 IsUAsset 检查：检查扩展名是否为 UE 资产格式
	FString Extension = FPaths::GetExtension(AssetPath);
	if (!Extension.IsEmpty() && Extension != TEXT("umap") && Extension != TEXT("uasset"))
	{
		return TEXT("");
	}

	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: TryGetMountPointForPath 失败: %s"), *AssetPath);
		return TEXT("");
	}

	FString RootStr = FString(PackageNameRoot);
	FString FilePathRootStr = FString(FilePathRoot);
	FString CookedBaseDir;

	// Cooked 目录结构: {CookedPlatformDir}/{MountPoint}/Content/...
	// MountPoint 可以是 ProjectName、Engine 或 Plugin 相对路径
	if (RootStr == TEXT("/Game/"))
	{
		// /Game/ 映射到 {ProjectName}/Content/
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, FString(FApp::GetProjectName()), TEXT("Content"));
	}
	else if (RootStr == TEXT("/Engine/"))
	{
		// /Engine/ 映射到 Engine/Content/
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, TEXT("Engine"), TEXT("Content"));
	}
	else if (FilePathRootStr.Contains(TEXT("Plugins/")))
	{
		// 插件路径：使用 GetPluginCookedSubDir 确定 Cooked 子目录
		const int32 PluginsIdx = FilePathRootStr.Find(TEXT("Plugins/"));
		FString PluginPath = FilePathRootStr.Mid(PluginsIdx);
		FString SubDir = GetPluginCookedSubDir(PluginPath);
		if (SubDir.IsEmpty())
		{
			UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 插件目录不存在: %s"), *PluginPath);
			return TEXT("");
		}
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, SubDir, TEXT("Content"));
	}
	else
	{
		// 其他路径：去掉 PackageNameRoot 开头的 / 后直接使用
		FString CleanRoot = RootStr;
		if (CleanRoot.StartsWith(TEXT("/")))
		{
			CleanRoot = CleanRoot.RightChop(1);
		}
		CookedBaseDir = FPaths::Combine(CookedPlatformDir, CleanRoot);
	}

	// 拼接完整 Cooked 路径
	// 注意：RelPath 可能包含扩展名（当 AssetPath 是绝对路径时），需要去除
	FString RelPathStr = FString(RelPath);
	RelPathStr.RemoveFromEnd(TEXT(".uasset"));
	RelPathStr.RemoveFromEnd(TEXT(".umap"));
	const FString CookedPath = FPaths::Combine(CookedBaseDir, RelPathStr);

	// 检查文件是否存在（优先 .umap，然后 .uasset）
	FString UmapPath = CookedPath + TEXT(".umap");
	if (FPaths::FileExists(UmapPath))
	{
		return UmapPath;
	}
	FString UassetPath = CookedPath + TEXT(".uasset");
	if (FPaths::FileExists(UassetPath))
	{
		return UassetPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetCookedAssetPath: 文件不存在: %s (AssetPath: %s, CookedBaseDir: %s, RelPath: %s)"),
		*CookedPath, *AssetPath, *CookedBaseDir, *FString(RelPath));
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetSourcePath(const FString& AssetPath)
{
	// 内联 IsUAsset 检查：检查扩展名是否为 UE 资产格式
	FString Extension = FPaths::GetExtension(AssetPath);
	bool bIsUAsset = Extension.IsEmpty() || Extension == TEXT("umap") || Extension == TEXT("uasset");
	if (!bIsUAsset)
	{
		return AssetPath;
	}

	FString NormalizedPath = NormalizePathToForwardSlash(AssetPath);
	FPaths::NormalizeFilename(NormalizedPath);
	NormalizedPath = NormalizePathToForwardSlash(NormalizedPath);

	// 情况1：已经是绝对路径（磁盘路径），直接检查文件是否存在
	if (NormalizedPath.Contains(TEXT(":/")) || NormalizedPath.StartsWith(TEXT("/")))
	{
		// 可能是绝对路径（如 E:/...）或虚拟路径（如 /Game/...）
		// 绝对路径特征：包含 :/ 盘符分隔符
		if (NormalizedPath.Contains(TEXT(":/")))
		{
			// 绝对路径：直接使用
			if (FPaths::FileExists(NormalizedPath))
			{
				return NormalizedPath;
			}
			// 尝试去掉扩展名再检查
			FString BasePath = NormalizedPath;
			BasePath.RemoveFromEnd(TEXT(".uasset"));
			BasePath.RemoveFromEnd(TEXT(".umap"));
			if (FPaths::FileExists(BasePath + TEXT(".umap")))
			{
				return BasePath + TEXT(".umap");
			}
			if (FPaths::FileExists(BasePath + TEXT(".uasset")))
			{
				return BasePath + TEXT(".uasset");
			}
			UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath: 绝对路径文件不存在: %s"), *NormalizedPath);
			return TEXT("");
		}
	}

	// 情况2：虚拟路径（Long Package Name），使用 UE API 转换
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename))
	{
		UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath FAILED TryConvertLongPackageNameToFilename : %s"), *AssetPath);
		return TEXT("");
	}

	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Filename);

	if (FPaths::FileExists(AbsolutePath + TEXT(".umap")))
		return AbsolutePath + TEXT(".umap");
	if (FPaths::FileExists(AbsolutePath + TEXT(".uasset")))
		return AbsolutePath + TEXT(".uasset");

	UE_LOG(LogHotUpdateEditor, Display, TEXT("GetAssetSourcePath FAILED: %s"), *AssetPath);
	return TEXT("");
}

FString FHotUpdatePackageHelper::FilePathToLongPackageName(const FString& FileName)
{
	FString Result = FileName;
	FPaths::NormalizeFilename(Result);

	// 方式1：UE 标准 API（适用于已注册的 Mount Point）
	FString LongPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(Result, LongPackageName))
	{
		return LongPackageName;
	}

	// 方式2：通过 Mount Point 解析（支持引擎、项目、插件路径）
	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (FPackageName::TryGetMountPointForPath(Result, PackageNameRoot, FilePathRoot, RelPath))
	{
		FString AssetPath = FString(PackageNameRoot) + FString(RelPath);
		// 移除扩展名，返回 Long Package Name 格式
		AssetPath.RemoveFromEnd(TEXT(".uasset"));
		AssetPath.RemoveFromEnd(TEXT(".umap"));
		return AssetPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("FilePathToLongPackageName: 无法解析资产路径: %s"), *Result);
	return Result;
}

FString FHotUpdatePackageHelper::FilePathToContentMountPath(const FString& FileName)
{
	FString Result = NormalizePathToForwardSlash(FileName);
	FPaths::NormalizeFilename(Result);
	Result = NormalizePathToForwardSlash(Result);

	// 检查是否在项目 Content 目录下
	FString ProjectContentDir = NormalizePathToForwardSlash(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
	ProjectContentDir = EnsureTrailingSlash(ProjectContentDir);

	if (Result.StartsWith(ProjectContentDir))
	{
		FString RelativePath = Result.RightChop(ProjectContentDir.Len());
		// 返回虚拟路径格式: /Game/{RelativePath}（不含扩展名）
		FString VirtualPath = TEXT("/Game/") + RelativePath;
		VirtualPath.RemoveFromEnd(TEXT(".uasset"));
		VirtualPath.RemoveFromEnd(TEXT(".umap"));
		// 对于非资产文件（如 .txt），保留扩展名，因为后续 IoStoreBuilder 会正确处理
		return VirtualPath;
	}

	UE_LOG(LogHotUpdateEditor, Warning, TEXT("FilePathToContentMountPath: 文件不在项目 Content 目录: %s"), *Result);
	return TEXT("");
}

FString FHotUpdatePackageHelper::GetAssetPakMountPath(const FString& AssetPath)
{
	TStringBuilder<256> PackageNameRoot, FilePathRoot, RelPath;
	if (!FPackageName::TryGetMountPointForPath(AssetPath, PackageNameRoot, FilePathRoot, RelPath))
	{
		UE_LOG(LogHotUpdateEditor, Warning, TEXT("GetAssetPakMountPath: TryGetMountPointForPath 失败: %s"), *AssetPath);
		return TEXT("");
	}

	FString FilePathRootStr = FString(FilePathRoot);
	FString RootStr = FString(PackageNameRoot);

	// 使用统一的规范化函数将 FilePathRoot 转换为 Pak 格式
	FString PakMountRoot = NormalizeFilePathRootToPakMount(FilePathRootStr, RootStr);
	if (PakMountRoot.IsEmpty())
	{
		return TEXT("");
	}

	// 拼接完整 Pak 内部路径
	FString Result = NormalizePathToForwardSlash(PakMountRoot + FString(RelPath));

	return Result;
}

// ==================== 辅助判断函数实现 ====================

bool FHotUpdatePackageHelper::IsExternalAsset(const FString& AssetPath)
{
	if (FPackageName::IsScriptPackage(AssetPath) || FPackageName::IsMemoryPackage(AssetPath))
	{
		return true;
	}
	if (AssetPath.Contains(FPackagePath::GetExternalActorsFolderName()) || AssetPath.Contains(FPackagePath::GetExternalObjectsFolderName()))
	{
		return true;
	}
	return false;
}

bool FHotUpdatePackageHelper::IsValidPackagePath(const FString& AssetPath)
{
	// 排除外部资产
	if (IsExternalAsset(AssetPath))
	{
		return false;
	}

	// 检查扩展名是否是 UE 资产格式
	FString Extension = FPaths::GetExtension(AssetPath);
	if (Extension != TEXT("uasset") && Extension != TEXT("umap"))
	{
		// 非 UE 资产文件不需要 Cook
		return false;
	}

	// UE 标准 API：TryConvertFilenameToLongPackageName
	// 如果能成功将磁盘路径转换为 Long Package Name，说明是有效的 UE Package
	FString LongPackageName;
	return FPackageName::TryConvertFilenameToLongPackageName(AssetPath, LongPackageName);
}