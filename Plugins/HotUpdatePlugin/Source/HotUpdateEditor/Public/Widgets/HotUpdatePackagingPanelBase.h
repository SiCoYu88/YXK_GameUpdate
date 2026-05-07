// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HotUpdateEditorTypes.h"

class SProgressBar;

/**
 * 打包面板基类
 * 封装平台选择、Android 纹理格式选择、输出目录浏览、进度显示等公共逻辑
 */
class HOTUPDATEEDITOR_API SHotUpdatePackagingPanelBase : public SCompoundWidget
{
public:
	virtual ~SHotUpdatePackagingPanelBase() = default;

protected:
	// === 初始化 ===
	void InitPlatformOptions();
	void InitAndroidTextureFormatOptions();

	// === 平台选择（Slate 委托签名） ===
	TSharedRef<SWidget> GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem);
	void OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedPlatformText() const;

	// === Android 纹理格式选择（Slate 委托签名） ===
	TSharedRef<SWidget> GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem);
	void OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo);
	FText GetSelectedAndroidTextureFormatText() const;
	EVisibility GetAndroidTextureFormatVisibility() const;

	// === 配置字段访问（子类必须实现，用于将选择结果写入各自的 Config） ===
	virtual EHotUpdatePlatform& GetTargetPlatformRef() = 0;
	virtual EHotUpdateAndroidTextureFormat& GetTargetTextureFormatRef() = 0;

	// === UI 构建辅助 ===
	static TSharedRef<SWidget> MakeSettingRow(const FText& Label, TSharedRef<SWidget> ValueWidget, float LabelWidth = 90.0f);

	// === 输出目录浏览 ===
	FReply BrowseOutputDirectory(const FString& CurrentPath, const TSharedPtr<SEditableText>& TextBox, FString& OutPath);

	// === 进度回调 ===
	void HandlePackagingProgress(const FHotUpdatePackageProgress& Progress);

	// === 完成回调通用实现 ===
	void HandlePackagingCompleteImpl(
		bool bSuccess,
		const FString& OutputPath,
		int64 PatchSize,
		int32 AssetCount,
		const FString& ErrorMessage);

	// === 取消操作 ===
	virtual void CancelCurrentBuild() {}

	// === 状态管理 ===
	void SetStatusText(const FString& Text, const FSlateColor& Color);
	void ResetProgressUI();

	// === 公共成员 ===
	TSharedPtr<SWindow> ParentWindow;
	bool bIsPackaging = false;
	TSharedPtr<SNotificationItem> ProgressNotification;

	// 平台相关
	TArray<TSharedPtr<EHotUpdatePlatform>> PlatformOptions;
	TSharedPtr<EHotUpdatePlatform> SelectedPlatform;
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdatePlatform>>> PlatformComboBox;

	// Android 纹理格式相关
	TArray<TSharedPtr<EHotUpdateAndroidTextureFormat>> AndroidTextureFormatOptions;
	TSharedPtr<EHotUpdateAndroidTextureFormat> SelectedAndroidTextureFormat;
	TSharedPtr<SComboBox<TSharedPtr<EHotUpdateAndroidTextureFormat>>> AndroidTextureFormatComboBox;

	// 通用 UI 控件
	TSharedPtr<SEditableText> OutputDirTextBox;
	TSharedPtr<STextBlock> StatusTextBlock;
	TSharedPtr<STextBlock> ProgressTextBlock;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SButton> PackageButton;
	TSharedPtr<SButton> CancelButton;
};
