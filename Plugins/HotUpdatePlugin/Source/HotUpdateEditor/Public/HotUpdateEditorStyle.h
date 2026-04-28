// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"

struct FSlateBrush;
class FSlateStyleSet;

/**
 * 热更新编辑器样式定义
 * 参考 UE5 官方编辑器 FAppStyle 的实现方式
 */
class HOTUPDATEEDITOR_API FHotUpdateEditorStyle
{
public:
	/** 初始化样式 */
	static void Initialize();

	/** 清理样式 */
	static void Shutdown();

	/** 获取样式名称 */
	static FName GetStyleSetName();

	// ===== 颜色常量 =====

	/** 状态颜色 */
	struct FStatusColors
	{
		static const FLinearColor Success;
		static const FLinearColor Warning;
		static const FLinearColor Error;
		static const FLinearColor Processing;
	};

	/** 差异颜色 */
	struct FDiffColors
	{
		static const FLinearColor Added;
		static const FLinearColor Modified;
		static const FLinearColor Deleted;
	};

	/** UI 主题颜色 */
	struct FThemeColors
	{
		// 主色调
		static const FLinearColor Primary;        // 主强调色

		// 背景色
		static const FLinearColor Background;     // 主背景

		// 文字色
		static const FLinearColor TextPrimary;    // 主要文字
		static const FLinearColor TextSecondary;  // 次要文字
	};

	// ===== Slate 颜色获取 =====

	static FSlateColor GetSuccessColor() { return FStatusColors::Success; }
	static FSlateColor GetWarningColor() { return FStatusColors::Warning; }
	static FSlateColor GetErrorColor() { return FStatusColors::Error; }
	static FSlateColor GetProcessingColor() { return FStatusColors::Processing; }

	static FSlateColor GetAddedColor() { return FDiffColors::Added; }
	static FSlateColor GetModifiedColor() { return FDiffColors::Modified; }
	static FSlateColor GetDeletedColor() { return FDiffColors::Deleted; }

	// 主题颜色获取
	static FSlateColor GetPrimaryColor() { return FThemeColors::Primary; }
	static FSlateColor GetBackgroundColor() { return FThemeColors::Background; }
	static FSlateColor GetTextPrimaryColor() { return FThemeColors::TextPrimary; }
	static FSlateColor GetTextSecondaryColor() { return FThemeColors::TextSecondary; }

	// ===== 字体获取 =====

	/** 获取子标题字体 (中) */
	static FSlateFontInfo GetSubtitleFont();

	/** 获取正文字体 */
	static FSlateFontInfo GetNormalFont();

	/** 获取小字体 */
	static FSlateFontInfo GetSmallFont();

	/** 获取粗体正文字体 */
	static FSlateFontInfo GetBoldFont();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};