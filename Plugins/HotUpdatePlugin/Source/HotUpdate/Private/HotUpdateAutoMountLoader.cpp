// Copyright czm. All Rights Reserved.

#include "HotUpdateAutoMountLoader.h"
#include "HotUpdate.h"
#include "HotUpdatePakManager.h"
#include "HotUpdateAssetPakMapping.h"
#include "Core/HotUpdateSettings.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"

void UHotUpdateAutoMountLoader::Initialize(UHotUpdatePakManager* InPakManager, UHotUpdateAssetPakMapping* InMapping)
{
	PakManager = InPakManager;
	Mapping = InMapping;

	UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Initialized (PakManager=%s, Mapping=%s)"),
		InPakManager ? TEXT("valid") : TEXT("null"),
		InMapping ? TEXT("valid") : TEXT("null"));
}

void UHotUpdateAutoMountLoader::AsyncLoadAsset(const FString& AssetPath, const FOnAutoMountLoadComplete& OnComplete)
{
	if (!IsInitialized())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[AutoMount] Not initialized, cannot load: %s"), *AssetPath);
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	if (!Mapping->IsManifestLoaded())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AutoMount] Manifest not loaded, falling back to direct load: %s"), *AssetPath);
		// 退化为直接加载（不 Mount 任何 Pak）
	}

	// 1. Mount 所有需要的 Pak
	TArray<FString> MountedPaks = MountRequiredPaks(AssetPath);

	UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Loading %s, mounted %d paks: %s"),
		*AssetPath, MountedPaks.Num(),
		*FString::Join(MountedPaks, TEXT(", ")));

	// 2. 记录到 ActiveLoads
	FAutoMountTrackingInfo* ExistingInfo = ActiveLoads.Find(AssetPath);
	if (ExistingInfo)
	{
		ExistingInfo->RefCount++;
		UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] %s already tracked, RefCount -> %d"), *AssetPath, ExistingInfo->RefCount);
	}
	else
	{
		FAutoMountTrackingInfo TrackingInfo;
		TrackingInfo.MountedPakPaths = MountedPaks;
		TrackingInfo.RefCount = 1;
		ActiveLoads.Add(AssetPath, MoveTemp(TrackingInfo));
	}

	// 3. 异步加载资源
	FSoftObjectPath SoftPath(AssetPath);
	TWeakObjectPtr<UHotUpdateAutoMountLoader> WeakThis(this);
	FString CapturedAssetPath = AssetPath;

	StreamableManager.RequestAsyncLoad(
		SoftPath,
		FStreamableDelegate::CreateLambda([WeakThis, CapturedAssetPath, OnComplete]()
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			UObject* LoadedAsset = FSoftObjectPath(CapturedAssetPath).ResolveObject();
			bool bSuccess = LoadedAsset != nullptr;

			// 更新跟踪信息中的 LoadedAsset
			FAutoMountTrackingInfo* Info = WeakThis->ActiveLoads.Find(CapturedAssetPath);
			if (Info)
			{
				Info->LoadedAsset = LoadedAsset;

				// 注册弱引用跟踪到关联的 Pak
				if (bSuccess && WeakThis->PakManager)
				{
					for (const FString& PakPath : Info->MountedPakPaths)
					{
						WeakThis->PakManager->RegisterLoadedAsset(PakPath, LoadedAsset, CapturedAssetPath);
					}
				}
			}

			UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Async load %s: %s"),
				*CapturedAssetPath, bSuccess ? TEXT("success") : TEXT("failed"));

			OnComplete.ExecuteIfBound(bSuccess, LoadedAsset);
		})
	);
}

UObject* UHotUpdateAutoMountLoader::SyncLoadAsset(const FString& AssetPath)
{
	if (!IsInitialized())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[AutoMount] Not initialized, cannot sync load: %s"), *AssetPath);
		return nullptr;
	}

	// 1. Mount 所有需要的 Pak
	TArray<FString> MountedPaks = MountRequiredPaks(AssetPath);

	UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Sync loading %s, mounted %d paks"),
		*AssetPath, MountedPaks.Num());

	// 2. 记录到 ActiveLoads
	FAutoMountTrackingInfo* ExistingInfo = ActiveLoads.Find(AssetPath);
	if (ExistingInfo)
	{
		ExistingInfo->RefCount++;
	}
	else
	{
		FAutoMountTrackingInfo TrackingInfo;
		TrackingInfo.MountedPakPaths = MountedPaks;
		TrackingInfo.RefCount = 1;
		ActiveLoads.Add(AssetPath, MoveTemp(TrackingInfo));
	}

	// 3. 同步加载
	UObject* LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);

	if (LoadedAsset)
	{
		FAutoMountTrackingInfo* Info = ActiveLoads.Find(AssetPath);
		if (Info)
		{
			Info->LoadedAsset = LoadedAsset;

			// 注册弱引用跟踪到关联的 Pak
			if (PakManager)
			{
				for (const FString& PakPath : Info->MountedPakPaths)
				{
					PakManager->RegisterLoadedAsset(PakPath, LoadedAsset, AssetPath);
				}
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Sync load success: %s"), *AssetPath);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AutoMount] Sync load failed: %s"), *AssetPath);
	}

	return LoadedAsset;
}

void UHotUpdateAutoMountLoader::ReleaseAsset(const FString& AssetPath)
{
	FAutoMountTrackingInfo* Info = ActiveLoads.Find(AssetPath);
	if (!Info)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[AutoMount] ReleaseAsset: %s not found in active loads"), *AssetPath);
		return;
	}

	Info->RefCount--;

	UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Releasing %s, RefCount -> %d, unmounting %d paks"),
		*AssetPath, Info->RefCount, Info->MountedPakPaths.Num());

	if (Info->RefCount <= 0)
	{
		// 先从 Pak 的弱引用跟踪中移除该资源
		if (PakManager)
		{
			for (const FString& PakPath : Info->MountedPakPaths)
			{
				PakManager->UnregisterLoadedAsset(PakPath, AssetPath);
			}
		}

		// 引用归零，Unmount 所有关联 Pak
		if (PakManager)
		{
			for (const FString& PakPath : Info->MountedPakPaths)
			{
				PakManager->RequestUnmount(PakPath);
			}
		}

		ActiveLoads.Remove(AssetPath);

		UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Released %s, removed from active loads"), *AssetPath);
	}
}

void UHotUpdateAutoMountLoader::AsyncLoadAssets(const TArray<FString>& AssetPaths, const FOnAutoMountBatchComplete& OnComplete)
{
	if (!IsInitialized())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[AutoMount] Not initialized, cannot batch load"));
		OnComplete.ExecuteIfBound(false, TArray<UObject*>());
		return;
	}

	if (AssetPaths.Num() == 0)
	{
		OnComplete.ExecuteIfBound(true, TArray<UObject*>());
		return;
	}

	// 1. 收集所有需要的 Pak 并合并去重
	TSet<FString> AllRequiredPaks;
	TMap<FString, TArray<FString>> PerAssetPaks;

	for (const FString& AssetPath : AssetPaths)
	{
		TArray<FString> MountedPaks = MountRequiredPaks(AssetPath);
		PerAssetPaks.Add(AssetPath, MountedPaks);
		for (const FString& Pak : MountedPaks)
		{
			AllRequiredPaks.Add(Pak);
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Batch loading %d assets, %d unique paks"),
		AssetPaths.Num(), AllRequiredPaks.Num());

	// 2. 记录到 ActiveLoads
	for (const FString& AssetPath : AssetPaths)
	{
		FAutoMountTrackingInfo* ExistingInfo = ActiveLoads.Find(AssetPath);
		if (ExistingInfo)
		{
			ExistingInfo->RefCount++;
		}
		else
		{
			FAutoMountTrackingInfo TrackingInfo;
			TrackingInfo.MountedPakPaths = PerAssetPaks.FindRef(AssetPath);
			TrackingInfo.RefCount = 1;
			ActiveLoads.Add(AssetPath, MoveTemp(TrackingInfo));
		}
	}

	// 3. 批量异步加载
	TArray<FSoftObjectPath> SoftPaths;
	for (const FString& AssetPath : AssetPaths)
	{
		SoftPaths.Add(FSoftObjectPath(AssetPath));
	}

	TWeakObjectPtr<UHotUpdateAutoMountLoader> WeakThis(this);
	TArray<FString> CapturedAssetPaths = AssetPaths;

	StreamableManager.RequestAsyncLoad(
		SoftPaths,
		FStreamableDelegate::CreateLambda([WeakThis, CapturedAssetPaths, OnComplete]()
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			TArray<UObject*> LoadedAssets;
			bool bAllSuccess = true;

			for (const FString& AssetPath : CapturedAssetPaths)
			{
				UObject* Asset = FSoftObjectPath(AssetPath).ResolveObject();
				LoadedAssets.Add(Asset);

				if (Asset)
				{
					FAutoMountTrackingInfo* Info = WeakThis->ActiveLoads.Find(AssetPath);
					if (Info)
					{
						Info->LoadedAsset = Asset;

						// 注册弱引用跟踪到关联的 Pak
						if (WeakThis->PakManager)
						{
							for (const FString& PakPath : Info->MountedPakPaths)
							{
								WeakThis->PakManager->RegisterLoadedAsset(PakPath, Asset, AssetPath);
							}
						}
					}
				}
				else
				{
					bAllSuccess = false;
				}
			}

			UE_LOG(LogHotUpdate, Log, TEXT("[AutoMount] Batch load complete: %d/%d success"),
				LoadedAssets.Num(), CapturedAssetPaths.Num());

			OnComplete.ExecuteIfBound(bAllSuccess, LoadedAssets);
		})
	);
}

TArray<FString> UHotUpdateAutoMountLoader::MountRequiredPaks(const FString& AssetPath)
{
	TArray<FString> MountedPaks;

	if (!Mapping || !Mapping->IsManifestLoaded())
	{
		return MountedPaks;
	}

	TArray<FString> RequiredPaks = Mapping->GetRequiredPaksForAsset(AssetPath);

	for (const FString& RelativePakPath : RequiredPaks)
	{
		FString FullPakPath = BuildFullPakPath(RelativePakPath);

		if (PakManager->RequestMount(FullPakPath))
		{
			MountedPaks.Add(FullPakPath);
		}
		else
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("[AutoMount] Failed to mount Pak: %s"), *FullPakPath);
		}
	}

	return MountedPaks;
}

FString UHotUpdateAutoMountLoader::BuildFullPakPath(const FString& RelativePakPath) const
{
	// 如果已经是绝对路径，直接返回
	if (FPaths::IsRelative(RelativePakPath) == false)
	{
		return RelativePakPath;
	}

	// 拼接本地 Pak 存储目录
	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (Settings)
	{
		FString PakDir = Settings->GetLocalPakFullPath();
		return FPaths::Combine(PakDir, RelativePakPath);
	}

	return RelativePakPath;
}

// ============================================================
// 资源弱引用定时扫描
// ============================================================

void UHotUpdateAutoMountLoader::StartAssetScan()
{
	// 如果已在运行，先停止
	StopAssetScan();

	UHotUpdateSettings* Settings = UHotUpdateSettings::Get();
	if (!Settings)
	{
		return;
	}

	// 检查是否启用
	if (!Settings->bEnableAutoUnmountOnGC)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[AssetScan] Auto unmount on GC is disabled"));
		return;
	}

	float Interval = Settings->AssetScanInterval;
	if (Interval <= 0.0f)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[AssetScan] Scan interval is 0, auto scan disabled"));
		return;
	}

	ScanTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHotUpdateAutoMountLoader::OnScanTick),
		Interval
	);

	UE_LOG(LogHotUpdate, Log, TEXT("[AssetScan] Started asset scan with interval %.1f seconds"), Interval);
}

void UHotUpdateAutoMountLoader::StopAssetScan()
{
	if (ScanTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ScanTickerHandle);
		ScanTickerHandle.Reset();

		UE_LOG(LogHotUpdate, Log, TEXT("[AssetScan] Stopped asset scan"));
	}
}

bool UHotUpdateAutoMountLoader::OnScanTick(float DeltaTime)
{
	if (PakManager)
	{
		PakManager->ScanAndAutoUnmount();
	}

	// 返回 true 保持 Ticker 持续运行
	return true;
}

void UHotUpdateAutoMountLoader::BeginDestroy()
{
	StopAssetScan();
	Super::BeginDestroy();
}
