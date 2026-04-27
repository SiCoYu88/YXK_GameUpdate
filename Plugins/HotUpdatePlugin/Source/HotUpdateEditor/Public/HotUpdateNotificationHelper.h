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

    /**
     * 显示进度通知，返回通知项供调用者管理生命周期
     * @param Message 进度消息
     * @param CancelDelegate 取消按钮回调，未绑定则不显示取消按钮
     * @return 进度通知项，调用者负责在适当时机调用 ExpireAndFadeout()
     */
    static TSharedPtr<SNotificationItem> ShowProgressNotification(const FText& Message, const FSimpleDelegate& CancelDelegate = FSimpleDelegate());
};