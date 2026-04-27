// Copyright czm. All Rights Reserved.

#include "HotUpdateEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"

// ===== 颜色常量定义 =====

// 状态颜色
const FLinearColor FHotUpdateEditorStyle::FStatusColors::Success = FLinearColor(0.25f, 0.75f, 0.35f);
const FLinearColor FHotUpdateEditorStyle::FStatusColors::Warning = FLinearColor(0.95f, 0.7f, 0.15f);
const FLinearColor FHotUpdateEditorStyle::FStatusColors::Error = FLinearColor(0.9f, 0.25f, 0.2f);
const FLinearColor FHotUpdateEditorStyle::FStatusColors::Processing = FLinearColor(0.35f, 0.6f, 0.95f);

// 差异颜色
const FLinearColor FHotUpdateEditorStyle::FDiffColors::Added = FLinearColor(0.25f, 0.75f, 0.35f);
const FLinearColor FHotUpdateEditorStyle::FDiffColors::Modified = FLinearColor(0.85f, 0.6f, 0.2f);
const FLinearColor FHotUpdateEditorStyle::FDiffColors::Deleted = FLinearColor(0.85f, 0.25f, 0.25f);

// 主题颜色
const FLinearColor FHotUpdateEditorStyle::FThemeColors::Primary = FLinearColor(0.25f, 0.55f, 0.85f);

const FLinearColor FHotUpdateEditorStyle::FThemeColors::Background = FLinearColor(0.04f, 0.04f, 0.04f);
const FLinearColor FHotUpdateEditorStyle::FThemeColors::BackgroundDark = FLinearColor(0.02f, 0.02f, 0.02f);

const FLinearColor FHotUpdateEditorStyle::FThemeColors::Border = FLinearColor(0.1f, 0.1f, 0.1f);

const FLinearColor FHotUpdateEditorStyle::FThemeColors::TextPrimary = FLinearColor(0.95f, 0.95f, 0.95f);
const FLinearColor FHotUpdateEditorStyle::FThemeColors::TextSecondary = FLinearColor(0.7f, 0.7f, 0.7f);

TSharedPtr<FSlateStyleSet> FHotUpdateEditorStyle::StyleSet = nullptr;

FName FHotUpdateEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("HotUpdateEditorStyle"));
	return StyleSetName;
}

void FHotUpdateEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FHotUpdateEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

FSlateFontInfo FHotUpdateEditorStyle::GetSubtitleFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 13);
}

FSlateFontInfo FHotUpdateEditorStyle::GetNormalFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 10);
}

FSlateFontInfo FHotUpdateEditorStyle::GetSmallFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 9);
}

FSlateFontInfo FHotUpdateEditorStyle::GetBoldFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 10);
}