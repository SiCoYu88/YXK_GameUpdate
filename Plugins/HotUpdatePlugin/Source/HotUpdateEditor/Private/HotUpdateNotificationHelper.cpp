// Copyright czm. All Rights Reserved.

#include "HotUpdateNotificationHelper.h"
#include "Styling/AppStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "HotUpdateNotificationHelper"

void FHotUpdateNotificationHelper::ShowNotification(const FText& Message, SNotificationItem::ECompletionState State)
{
    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
        FNotificationInfo(Message)
    );

    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(State);
    }
}

void FHotUpdateNotificationHelper::ShowSuccessNotification(const FText& Message, const FString& OutputPath)
{
    FNotificationInfo Info(Message);
    Info.ExpireDuration = 5.0f;
    Info.bUseSuccessFailIcons = true;
    Info.Image = FAppStyle::GetBrush("Icons.SuccessWithColor");

    Info.Hyperlink = FSimpleDelegate::CreateLambda([OutputPath]() {
        FPlatformProcess::ExploreFolder(*OutputPath);
    });
    Info.HyperlinkText = LOCTEXT("OpenOutputDir", "打开输出目录");

    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
    }
}

void FHotUpdateNotificationHelper::ShowErrorNotification(const FText& Message)
{
    FNotificationInfo Info(Message);
    Info.ExpireDuration = 8.0f;
    Info.bUseSuccessFailIcons = true;
    Info.Image = FAppStyle::GetBrush("Icons.ErrorWithColor");

    Info.ButtonDetails.Add(FNotificationButtonInfo(
        LOCTEXT("ViewLog", "查看日志"),
        LOCTEXT("ViewLogTooltip", "打开输出日志窗口"),
        FSimpleDelegate::CreateLambda([]() {
            FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
        }),
        SNotificationItem::ECompletionState::CS_Fail
    ));

    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
    }
}
#undef LOCTEXT_NAMESPACE