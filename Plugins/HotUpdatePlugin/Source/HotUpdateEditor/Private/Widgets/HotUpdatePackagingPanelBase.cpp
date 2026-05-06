// Copyright czm. All Rights Reserved.

#include "Widgets/HotUpdatePackagingPanelBase.h"
#include "HotUpdateEditorStyle.h"
#include "HotUpdateNotificationHelper.h"
#include "HotUpdateUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "HotUpdatePackagingPanelBase"

void SHotUpdatePackagingPanelBase::InitPlatformOptions()
{
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Windows)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::Android)));
	PlatformOptions.Add(MakeShareable(new EHotUpdatePlatform(EHotUpdatePlatform::IOS)));
	SelectedPlatform = PlatformOptions[0];
}

void SHotUpdatePackagingPanelBase::InitAndroidTextureFormatOptions()
{
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ETC2)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::ASTC)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::DXT)));
	AndroidTextureFormatOptions.Add(MakeShareable(new EHotUpdateAndroidTextureFormat(EHotUpdateAndroidTextureFormat::Multi)));
	SelectedAndroidTextureFormat = AndroidTextureFormatOptions[0];
}

TSharedRef<SWidget> SHotUpdatePackagingPanelBase::GeneratePlatformComboBoxItem(TSharedPtr<EHotUpdatePlatform> InItem)
{
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetPlatformDisplayName(*InItem))
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdatePackagingPanelBase::OnPlatformSelected(TSharedPtr<EHotUpdatePlatform> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedPlatform = InItem;
	if (InItem.IsValid())
	{
		GetTargetPlatformRef() = *InItem;
	}
}

FText SHotUpdatePackagingPanelBase::GetSelectedPlatformText() const
{
	if (SelectedPlatform.IsValid())
	{
		return HotUpdateUtils::GetPlatformDisplayName(*SelectedPlatform);
	}
	return HotUpdateUtils::GetPlatformDisplayName(EHotUpdatePlatform::Windows);
}

TSharedRef<SWidget> SHotUpdatePackagingPanelBase::GenerateAndroidTextureFormatComboBoxItem(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem)
{
	return SNew(STextBlock)
		.Text(HotUpdateUtils::GetTextureFormatDisplayName(*InItem))
		.Font(FHotUpdateEditorStyle::GetNormalFont())
		.Margin(FMargin(4, 2));
}

void SHotUpdatePackagingPanelBase::OnAndroidTextureFormatSelected(TSharedPtr<EHotUpdateAndroidTextureFormat> InItem, ESelectInfo::Type SelectInfo)
{
	SelectedAndroidTextureFormat = InItem;
	if (InItem.IsValid())
	{
		GetTargetTextureFormatRef() = *InItem;
	}
}

FText SHotUpdatePackagingPanelBase::GetSelectedAndroidTextureFormatText() const
{
	if (SelectedAndroidTextureFormat.IsValid())
	{
		return HotUpdateUtils::GetTextureFormatDisplayName(*SelectedAndroidTextureFormat);
	}
	return HotUpdateUtils::GetTextureFormatDisplayName(EHotUpdateAndroidTextureFormat::ETC2);
}

EVisibility SHotUpdatePackagingPanelBase::GetAndroidTextureFormatVisibility() const
{
	if (SelectedPlatform.IsValid() && *SelectedPlatform == EHotUpdatePlatform::Android)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

TSharedRef<SWidget> SHotUpdatePackagingPanelBase::MakeSettingRow(const FText& Label, TSharedRef<SWidget> ValueWidget, float LabelWidth)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FHotUpdateEditorStyle::GetNormalFont())
				.ColorAndOpacity(FHotUpdateEditorStyle::GetTextSecondaryColor())
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(8, 4)
		.VAlign(VAlign_Center)
		[
			ValueWidget
		];
}

FReply SHotUpdatePackagingPanelBase::BrowseOutputDirectory(const FString& CurrentPath, const TSharedPtr<SEditableText>& TextBox, FString& OutPath)
{
	TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.IsValid() ? ParentWindow : FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr);
	void* ParentWindowHandle = ParentWindowPtr.IsValid() ? ParentWindowPtr->GetNativeWindow()->GetOSWindowHandle() : nullptr;

	FString SelectedDirectory;
	if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SelectOutputDir", "选择输出目录").ToString(),
		CurrentPath,
		SelectedDirectory))
	{
		OutPath = SelectedDirectory;
		if (TextBox.IsValid())
		{
			TextBox->SetText(FText::FromString(SelectedDirectory));
		}
	}

	return FReply::Handled();
}

void SHotUpdatePackagingPanelBase::HandlePackagingProgress(const FHotUpdatePackageProgress& Progress)
{
	FString ProgressMsg = FString::Printf(
		TEXT("%s - %d/%d 文件"),
		*Progress.StageDescription.ToString(),
		Progress.ProcessedFiles,
		Progress.TotalFiles
	);
	if (ProgressTextBlock.IsValid())
	{
		ProgressTextBlock->SetText(FText::FromString(ProgressMsg));
	}

	float Percent = Progress.GetProgressPercent() / 100.0f;
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(Percent);
	}
}

void SHotUpdatePackagingPanelBase::HandlePackagingCompleteImpl(
	bool bSuccess,
	const FString& OutputPath,
	int64 PatchSize,
	int32 AssetCount,
	const FString& ErrorMessage)
{
	bIsPackaging = false;

	if (ProgressNotification.IsValid())
	{
		ProgressNotification->ExpireAndFadeout();
		ProgressNotification.Reset();
	}

	if (bSuccess)
	{
		FString SuccessMsg = FString::Printf(
			TEXT("打包成功! 文件: %s, 大小: %.2f MB, 资源数: %d"),
			*OutputPath,
			PatchSize / (1024.0 * 1024.0),
			AssetCount);
		if (StatusTextBlock.IsValid())
		{
			StatusTextBlock->SetText(FText::FromString(SuccessMsg));
			StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetSuccessColor());
		}
		if (ProgressBar.IsValid())
		{
			ProgressBar->SetPercent(1.0f);
		}
		FHotUpdateNotificationHelper::ShowSuccessNotification(FText::FromString(SuccessMsg), FPaths::GetPath(OutputPath));
	}
	else
	{
		if (StatusTextBlock.IsValid())
		{
			StatusTextBlock->SetText(FText::FromString(ErrorMessage));
			StatusTextBlock->SetColorAndOpacity(FHotUpdateEditorStyle::GetErrorColor());
		}
		FHotUpdateNotificationHelper::ShowErrorNotification(FText::FromString(ErrorMessage));
	}

	if (ProgressTextBlock.IsValid())
	{
		ProgressTextBlock->SetText(FText::GetEmpty());
	}
}

void SHotUpdatePackagingPanelBase::SetStatusText(const FString& Text, const FSlateColor& Color)
{
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(FText::FromString(Text));
		StatusTextBlock->SetColorAndOpacity(Color);
	}
}

void SHotUpdatePackagingPanelBase::ResetProgressUI()
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(0.0f);
	}
	if (ProgressTextBlock.IsValid())
	{
		ProgressTextBlock->SetText(FText::GetEmpty());
	}
}

#undef LOCTEXT_NAMESPACE
