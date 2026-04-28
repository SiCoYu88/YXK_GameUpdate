// Copyright czm. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"

class HOTUPDATEEDITOR_API FHotUpdateNotificationHelper
{
public:
    /** 显示通知 */
    static void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State);

    /** 显示成功通知（带超链接） */
    static void ShowSuccessNotification(const FText& Message, const FString& OutputPath);

    /** 显示错误通知（带按钮） */
    static void ShowErrorNotification(const FText& Message);
};