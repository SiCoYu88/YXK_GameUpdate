// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/HotUpdatePakTypes.h"
#include "Engine/StreamableManager.h"
#include "HotUpdateAutoMountLoader.generated.h"

class UHotUpdatePakManager;
class UHotUpdateAssetPakMapping;

/**
 * 自动按需挂载加载器
 *
 * 封装"查询依赖 → Mount 所有 Pak → 加载资源 → 管理引用"的完整流程。
 *
 * 使用方式：
 *  1. Initialize(PakManager, Mapping) 绑定依赖
 *  2. AsyncLoadAsset / SyncLoadAsset 加载资源（自动 Mount 依赖 Pak）
 *  3. ReleaseAsset 释放资源（自动 Unmount 关联 Pak）
 *
 * 或使用 Handle 模式：
 *  - FAutoMountAssetHandle 析构时自动释放引用
 */
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateAutoMountLoader : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 初始化，绑定 PakManager 和 Mapping
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AutoMount")
	void Initialize(UHotUpdatePakManager* InPakManager, UHotUpdateAssetPakMapping* InMapping);

	/**
	 * 异步加载资源（自动 Mount 依赖 Pak）
	 *
	 * 流程：
	 *  1. 通过 Mapping 查询主资源及其 Hard 依赖所在的全部 Pak
	 *  2. 对每个 Pak 调用 PakManager->RequestMount
	 *  3. 使用 StreamableManager 异步加载资源
	 *  4. 加载完成回调中触发用户回调
	 *  5. 通过 ReleaseAsset 或 Handle 析构释放引用
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AutoMount")
	void AsyncLoadAsset(const FString& AssetPath, const FOnAutoMountLoadComplete& OnComplete);

	/**
	 * 同步加载资源（自动 Mount 依赖 Pak）
	 * @return 加载的 UObject*，失败返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AutoMount")
	UObject* SyncLoadAsset(const FString& AssetPath);

	/**
	 * 释放资源引用（Unmount 关联的 Pak）
	 *
	 * 递减 ActiveLoads 中的引用计数，归零后 Unmount 所有关联 Pak。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AutoMount")
	void ReleaseAsset(const FString& AssetPath);

	/**
	 * 批量异步加载资源
	 *
	 * 收集所有 Asset 的 RequiredPaks 合并去重后批量 Mount，
	 * 使用 StreamableManager 批量加载。
	 */
	UFUNCTION(BlueprintCallable, Category = "HotUpdate|AutoMount")
	void AsyncLoadAssets(const TArray<FString>& AssetPaths, const FOnAutoMountBatchComplete& OnComplete);

	/**
	 * 检查是否已初始化
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AutoMount")
	bool IsInitialized() const { return PakManager != nullptr && Mapping != nullptr; }

	/**
	 * 获取当前活跃加载数
	 */
	UFUNCTION(BlueprintPure, Category = "HotUpdate|AutoMount")
	int32 GetActiveLoadCount() const { return ActiveLoads.Num(); }

	// ============================================================
	// 资源弱引用扫描接口
	// ============================================================

	/**
	 * 启动定时资源弱引用扫描
	 *
	 * 读取 UHotUpdateSettings::AssetScanInterval，使用 FTSTicker 注册定期扫描回调。
	 * 如果 bEnableAutoUnmountOnGC 为 false 或 AssetScanInterval 为 0，则不启动扫描。
	 */
	void StartAssetScan();

	/**
	 * 停止定时资源弱引用扫描
	 */
	void StopAssetScan();

	/**
	 * 检查扫描是否正在运行
	 */
	bool IsAssetScanRunning() const { return ScanTickerHandle.IsValid(); }

protected:
	virtual void BeginDestroy() override;

private:
	/**
	 * 内部：Mount 指定资源所需的所有 Pak，返回实际 Mount 的 Pak 路径列表
	 */
	TArray<FString> MountRequiredPaks(const FString& AssetPath);

	/**
	 * 内部：构建 Pak 完整本地路径
	 */
	FString BuildFullPakPath(const FString& RelativePakPath) const;

	/**
	 * Ticker 回调：定期扫描弱引用并触发自动卸载
	 * @return true 保持 Ticker 持续运行
	 */
	bool OnScanTick(float DeltaTime);

	/// PakManager 引用
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdatePakManager> PakManager;

	/// AssetPakMapping 引用
	UPROPERTY(Transient)
	TObjectPtr<UHotUpdateAssetPakMapping> Mapping;

	/// 活跃加载记录（AssetPath → 跟踪信息）
	TMap<FString, FAutoMountTrackingInfo> ActiveLoads;

	/// StreamableManager（用于异步加载）
	FStreamableManager StreamableManager;

	/// 定时扫描的 Ticker 句柄
	FTSTicker::FDelegateHandle ScanTickerHandle;
};
