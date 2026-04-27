// Copyright czm. All Rights Reserved.

#include "HotUpdatePakManager.h"
#include "HotUpdate.h"
#include "Core/HotUpdateFileUtils.h"
#include "IPlatformFilePak.h"
#include "Misc/AES.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"

// ============================================================
// FScopedPakRef 实现
// ============================================================

FScopedPakRef::FScopedPakRef(UHotUpdatePakManager* InManager, const FString& InPakPath,
	int32 InPakOrder, const FString& InEncryptionKey)
	: Manager(InManager)
	, PakPath(InPakPath)
	, bIsValid(false)
{
	if (InManager)
	{
		bIsValid = InManager->RequestMount(InPakPath, InPakOrder, InEncryptionKey);
	}
}

FScopedPakRef::~FScopedPakRef()
{
	ReleaseRef();
}

FScopedPakRef::FScopedPakRef(FScopedPakRef&& Other) noexcept
	: Manager(Other.Manager)
	, PakPath(MoveTemp(Other.PakPath))
	, bIsValid(Other.bIsValid)
{
	// 原实例放弃所有权
	Other.Manager.Reset();
	Other.bIsValid = false;
}

FScopedPakRef& FScopedPakRef::operator=(FScopedPakRef&& Other) noexcept
{
	if (this != &Other)
	{
		// 释放当前持有的引用
		ReleaseRef();

		Manager = Other.Manager;
		PakPath = MoveTemp(Other.PakPath);
		bIsValid = Other.bIsValid;

		Other.Manager.Reset();
		Other.bIsValid = false;
	}
	return *this;
}

void FScopedPakRef::ReleaseRef()
{
	if (bIsValid && Manager.IsValid())
	{
		Manager->RequestUnmount(PakPath);
	}
	bIsValid = false;
}

// ============================================================
// UHotUpdatePakManager 实现
// ============================================================

UHotUpdatePakManager::UHotUpdatePakManager()
{
}

void UHotUpdatePakManager::Initialize(const FString& InPakDirectory)
{
	PakDirectory = InPakDirectory;

	// 确保目录存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*PakDirectory))
	{
		PlatformFile.CreateDirectoryTree(*PakDirectory);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("PakManager initialized. Directory: %s"), *PakDirectory);
}

// ============================================================
// 路径规范化
// ============================================================

FString UHotUpdatePakManager::NormalizePakPath(const FString& PakPath)
{
	FString Normalized = FPaths::ConvertRelativePathToFull(PakPath);
	FPaths::NormalizeFilename(Normalized);
	return Normalized;
}

// ============================================================
// 内部 Mount/Unmount
// ============================================================

bool UHotUpdatePakManager::MountPakInternal(const FString& NormalizedPath, int32 PakOrder, const FString& EncryptionKey)
{
	check(IsInGameThread());

	// 获取 Pak 平台文件
	IPlatformFile* FoundFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
	FPakPlatformFile* PakPlatformFile = FoundFile ? static_cast<FPakPlatformFile*>(FoundFile) : nullptr;
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("PakPlatformFile not found"));
		return false;
	}

	// 检查文件是否存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*NormalizedPath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Pak file not found: %s"), *NormalizedPath);
		return false;
	}

	// 处理加密密钥
	bool bUseEncryption = false;
	if (!EncryptionKey.IsEmpty())
	{
		bUseEncryption = true;

		TArray<uint8> KeyBytes;
		if (UHotUpdateFileUtils::HexToBytes(EncryptionKey, KeyBytes))
		{
			constexpr int32 AESKeySize = 32;
			if (KeyBytes.Num() < AESKeySize)
			{
				KeyBytes.SetNumZeroed(AESKeySize);
			}
			else if (KeyBytes.Num() > AESKeySize)
			{
				KeyBytes.SetNum(AESKeySize);
			}

			FAES::FAESKey AesKey;
			FMemory::Memcpy(AesKey.Key, KeyBytes.GetData(), AESKeySize);

			FGuid TempGuid = FGuid::NewGuid();
			FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(TempGuid, AesKey);
			UE_LOG(LogHotUpdate, Log, TEXT("Registered encryption key for Pak: %s"), *NormalizedPath);
		}
		else
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("Failed to convert encryption key to bytes: %s"), *EncryptionKey);
		}
	}

	bool bSuccess = PakPlatformFile->Mount(*NormalizedPath, PakOrder);
	if (bSuccess)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Mounted Pak: %s (Order: %d, Encrypted: %s)"),
			*NormalizedPath, PakOrder, bUseEncryption ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to mount Pak: %s"), *NormalizedPath);
	}

	return bSuccess;
}

bool UHotUpdatePakManager::UnmountPakInternal(const FString& NormalizedPath)
{
	check(IsInGameThread());

	FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
		FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
	if (!PakPlatformFile)
	{
		return false;
	}

	bool bSuccess = PakPlatformFile->Unmount(*NormalizedPath);
	if (bSuccess)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Unmounted Pak: %s"), *NormalizedPath);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to unmount Pak: %s"), *NormalizedPath);
	}

	return bSuccess;
}

// ============================================================
// 引用计数核心接口
// ============================================================

bool UHotUpdatePakManager::RequestMount(const FString& PakPath, int32 PakOrder, const FString& EncryptionKey)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	// 检查是否在待卸载列表中 → 取消卸载
	if (PendingUnmounts.Contains(NormalizedPath))
	{
		PendingUnmounts.Remove(NormalizedPath);
		UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] Cancelled pending unmount for: %s"), *NormalizedPath);
	}

	FPakMountRecord* Record = PakRecords.Find(NormalizedPath);

	if (Record)
	{
		int32 OldRefCount = Record->RefCount;

		if (Record->bIsMounted)
		{
			// 已挂载 → 仅递增计数
			Record->RefCount++;
			UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: %d -> %d (already mounted)"),
				*NormalizedPath, OldRefCount, Record->RefCount);
			return true;
		}
		else
		{
			// 已注册但未挂载 → 执行 Mount
			bool bSuccess = false;
			if (IsInGameThread())
			{
				bSuccess = MountPakInternal(NormalizedPath, PakOrder, EncryptionKey);
			}
			else
			{
				// 从非游戏线程调用：同步等待游戏线程执行
				FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
				AsyncTask(ENamedThreads::GameThread, [this, &bSuccess, &NormalizedPath, PakOrder, &EncryptionKey, Event]()
				{
					bSuccess = MountPakInternal(NormalizedPath, PakOrder, EncryptionKey);
					Event->Trigger();
				});
				Event->Wait();
				FPlatformProcess::ReturnSynchEventToPool(Event);
			}

			if (bSuccess)
			{
				Record->bIsMounted = true;
				Record->RefCount = 1;
				Record->PakOrder = PakOrder;
				Record->EncryptionKey = EncryptionKey;
				UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: %d -> 1 (mounted)"),
					*NormalizedPath, OldRefCount);

				// 解锁后广播事件
				Lock.Unlock();
				OnPakMounted.Broadcast(NormalizedPath, true);
			}
			else
			{
				Lock.Unlock();
				OnPakMounted.Broadcast(NormalizedPath, false);
			}
			return bSuccess;
		}
	}
	else
	{
		// 无记录 → 检查文件存在性后创建记录并挂载
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!PlatformFile.FileExists(*NormalizedPath))
			{
				UE_LOG(LogHotUpdate, Error, TEXT("[RefCount] Pak file not found, cannot create record: %s"), *NormalizedPath);
				Lock.Unlock();
				OnPakMounted.Broadcast(NormalizedPath, false);
				return false;
			}
		}

		bool bSuccess = false;
		if (IsInGameThread())
		{
			bSuccess = MountPakInternal(NormalizedPath, PakOrder, EncryptionKey);
		}
		else
		{
			FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
			AsyncTask(ENamedThreads::GameThread, [this, &bSuccess, &NormalizedPath, PakOrder, &EncryptionKey, Event]()
			{
				bSuccess = MountPakInternal(NormalizedPath, PakOrder, EncryptionKey);
				Event->Trigger();
			});
			Event->Wait();
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}

		if (bSuccess)
		{
			FPakMountRecord NewRecord;
			NewRecord.Metadata = ParsePakMetadata(NormalizedPath);
			NewRecord.Metadata.bIsMounted = true;
			NewRecord.RefCount = 1;
			NewRecord.bIsMounted = true;
			NewRecord.bIsRegistered = true;
			NewRecord.PakOrder = PakOrder;
			NewRecord.EncryptionKey = EncryptionKey;

			PakRecords.Add(NormalizedPath, MoveTemp(NewRecord));
			UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: 0 -> 1 (new record, mounted)"), *NormalizedPath);

			Lock.Unlock();
			OnPakMounted.Broadcast(NormalizedPath, true);
		}
		else
		{
			Lock.Unlock();
			OnPakMounted.Broadcast(NormalizedPath, false);
		}
		return bSuccess;
	}
}

bool UHotUpdatePakManager::RequestUnmount(const FString& PakPath)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	if (!Record || Record->RefCount <= 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[RefCount] RequestUnmount: Pak not mounted or RefCount already 0: %s"), *NormalizedPath);
		return false;
	}

	int32 OldRefCount = Record->RefCount;
	Record->RefCount--;

	UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: %d -> %d"),
		*NormalizedPath, OldRefCount, Record->RefCount);

	if (Record->RefCount == 0)
	{
		// 注册延迟卸载
		PendingUnmounts.Add(NormalizedPath);
		Record->LastZeroRefCountTime = FPlatformTime::Seconds();

		UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s scheduled for deferred unmount (next tick)"), *NormalizedPath);

		// 注册下一帧回调
		if (IsInGameThread())
		{
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateWeakLambda(this, [this](float)
				{
					ProcessPendingUnmounts();
					return false; // 一次性
				}),
				0.0f // 下一帧
			);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<UHotUpdatePakManager>(this)]()
			{
				if (WeakThis.IsValid())
				{
					FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateWeakLambda(WeakThis.Get(), [WeakThis](float)
						{
							if (WeakThis.IsValid())
							{
								WeakThis->ProcessPendingUnmounts();
							}
							return false;
						}),
						0.0f
					);
				}
			});
		}
	}

	return true;
}

void UHotUpdatePakManager::AddRef(const FString& PakPath)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	if (!Record || Record->RefCount < 1 || !Record->bIsMounted)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[RefCount] AddRef: Pak not mounted (use RequestMount instead): %s"), *NormalizedPath);
		return;
	}

	int32 OldRefCount = Record->RefCount;
	Record->RefCount++;

	UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: %d -> %d (AddRef)"),
		*NormalizedPath, OldRefCount, Record->RefCount);
}

void UHotUpdatePakManager::Release(const FString& PakPath)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	if (!Record || Record->RefCount <= 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[RefCount] Release: Pak not found or RefCount already 0: %s"), *NormalizedPath);
		return;
	}

	int32 OldRefCount = Record->RefCount;
	Record->RefCount--;

	UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s RefCount: %d -> %d (Release)"),
		*NormalizedPath, OldRefCount, Record->RefCount);

	if (Record->RefCount == 0)
	{
		// 注册延迟卸载
		PendingUnmounts.Add(NormalizedPath);
		Record->LastZeroRefCountTime = FPlatformTime::Seconds();

		UE_LOG(LogHotUpdate, Log, TEXT("[RefCount] %s scheduled for deferred unmount (next tick) via Release"), *NormalizedPath);

		if (IsInGameThread())
		{
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateWeakLambda(this, [this](float)
				{
					ProcessPendingUnmounts();
					return false;
				}),
				0.0f
			);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<UHotUpdatePakManager>(this)]()
			{
				if (WeakThis.IsValid())
				{
					FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateWeakLambda(WeakThis.Get(), [WeakThis](float)
						{
							if (WeakThis.IsValid())
							{
								WeakThis->ProcessPendingUnmounts();
							}
							return false;
						}),
						0.0f
					);
				}
			});
		}
	}
}

// ============================================================
// 延迟卸载处理
// ============================================================

void UHotUpdatePakManager::ProcessPendingUnmounts()
{
	check(IsInGameThread());

	TArray<FString> PathsToUnmount;

	{
		FScopeLock Lock(&PakRecordsMutex);

		for (const FString& Path : PendingUnmounts)
		{
			FPakMountRecord* Record = PakRecords.Find(Path);
			if (Record && Record->RefCount == 0 && Record->bIsMounted)
			{
				PathsToUnmount.Add(Path);
			}
			// RefCount 已恢复 → 取消卸载（不需要做任何事）
		}

		// 清空待卸载列表
		PendingUnmounts.Empty();
	}

	// 在锁外执行实际卸载
	for (const FString& Path : PathsToUnmount)
	{
		bool bSuccess = UnmountPakInternal(Path);

		{
			FScopeLock Lock(&PakRecordsMutex);
			FPakMountRecord* Record = PakRecords.Find(Path);
			if (Record && bSuccess)
			{
				Record->bIsMounted = false;
				Record->Metadata.bIsMounted = false;
			}
		}

		if (bSuccess)
		{
			OnPakUnmounted.Broadcast(Path);
		}
	}
}

// ============================================================
// 容器注册与批量操作
// ============================================================

void UHotUpdatePakManager::RegisterAvailablePak(const FString& PakPath, const FHotUpdatePakMetadata& Metadata)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	if (Record)
	{
		// 已存在 → 更新元数据，不影响挂载状态和 RefCount
		Record->Metadata = Metadata;
		Record->bIsRegistered = true;
		UE_LOG(LogHotUpdate, Log, TEXT("[Register] Updated metadata for: %s (RefCount=%d, Mounted=%s)"),
			*NormalizedPath, Record->RefCount, Record->bIsMounted ? TEXT("true") : TEXT("false"));
	}
	else
	{
		FPakMountRecord NewRecord;
		NewRecord.Metadata = Metadata;
		NewRecord.RefCount = 0;
		NewRecord.bIsMounted = false;
		NewRecord.bIsRegistered = true;

		PakRecords.Add(NormalizedPath, MoveTemp(NewRecord));
		UE_LOG(LogHotUpdate, Log, TEXT("[Register] Registered available Pak: %s (ChunkId=%d)"),
			*NormalizedPath, Metadata.ChunkId);
	}

	Lock.Unlock();
	OnPaksAvailable.Broadcast(NormalizedPath);
}

int32 UHotUpdatePakManager::MountAllRegistered()
{
	// 收集需要挂载的路径（避免在锁内执行 RequestMount 导致死锁）
	TArray<FString> PathsToMount;

	{
		FScopeLock Lock(&PakRecordsMutex);
		for (const auto& Pair : PakRecords)
		{
			if (Pair.Value.bIsRegistered && Pair.Value.RefCount == 0 && !Pair.Value.bIsMounted)
			{
				PathsToMount.Add(Pair.Key);
			}
		}
	}

	int32 MountedCount = 0;
	for (const FString& Path : PathsToMount)
	{
		if (RequestMount(Path))
		{
			MountedCount++;
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[MountAllRegistered] Mounted %d / %d registered containers"),
		MountedCount, PathsToMount.Num());

	return MountedCount;
}

int32 UHotUpdatePakManager::RequestMountByChunkId(int32 ChunkId)
{
	TArray<FString> PathsToMount;

	{
		FScopeLock Lock(&PakRecordsMutex);
		for (const auto& Pair : PakRecords)
		{
			if (Pair.Value.Metadata.ChunkId == ChunkId)
			{
				PathsToMount.Add(Pair.Key);
			}
		}
	}

	if (PathsToMount.Num() == 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[MountByChunkId] No containers found for ChunkId=%d"), ChunkId);
		return 0;
	}

	int32 MountedCount = 0;
	for (const FString& Path : PathsToMount)
	{
		if (RequestMount(Path))
		{
			MountedCount++;
		}
	}

	return MountedCount;
}

void UHotUpdatePakManager::RequestUnmountByChunkId(int32 ChunkId)
{
	TArray<FString> PathsToUnmount;

	{
		FScopeLock Lock(&PakRecordsMutex);
		for (const auto& Pair : PakRecords)
		{
			if (Pair.Value.Metadata.ChunkId == ChunkId && Pair.Value.bIsMounted)
			{
				PathsToUnmount.Add(Pair.Key);
			}
		}
	}

	for (const FString& Path : PathsToUnmount)
	{
		RequestUnmount(Path);
	}
}

// ============================================================
// 查询接口
// ============================================================

int32 UHotUpdatePakManager::GetRefCount(const FString& PakPath) const
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	const FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	if (!Record)
	{
		return -1;
	}
	return Record->RefCount;
}

TArray<FPakMountInfo> UHotUpdatePakManager::GetAllMountedPaks() const
{
	TArray<FPakMountInfo> Result;

	FScopeLock Lock(&PakRecordsMutex);

	for (const auto& Pair : PakRecords)
	{
		if (Pair.Value.bIsMounted)
		{
			Result.Add(MakeMountInfo(Pair.Key, Pair.Value));
		}
	}

	return Result;
}

TArray<FPakMountInfo> UHotUpdatePakManager::GetAllRegisteredPaks() const
{
	TArray<FPakMountInfo> Result;

	FScopeLock Lock(&PakRecordsMutex);

	for (const auto& Pair : PakRecords)
	{
		if (Pair.Value.bIsRegistered)
		{
			Result.Add(MakeMountInfo(Pair.Key, Pair.Value));
		}
	}

	return Result;
}

bool UHotUpdatePakManager::IsRegistered(const FString& PakPath) const
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	const FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	return Record && Record->bIsRegistered;
}

FPakMountInfo UHotUpdatePakManager::MakeMountInfo(const FString& Path, const FPakMountRecord& Record)
{
	FPakMountInfo Info;
	Info.PakPath = Path;
	Info.RefCount = Record.RefCount;
	Info.ChunkId = Record.Metadata.ChunkId;
	Info.bIsMounted = Record.bIsMounted;
	Info.bIsRegistered = Record.bIsRegistered;
	Info.PakSize = Record.Metadata.PakSize;
	return Info;
}

// ============================================================
// 向后兼容接口（已废弃）
// ============================================================

PRAGMA_DISABLE_DEPRECATION_WARNINGS

bool UHotUpdatePakManager::MountPak(const FString& PakPath, int32 PakOrder, const FString& EncryptionKey)
{
	// 内部转发到 RequestMount
	return RequestMount(PakPath, PakOrder, EncryptionKey);
}

bool UHotUpdatePakManager::UnmountPak(const FString& PakPath)
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	// 向后兼容：强制 RefCount=0 并执行立即卸载（不走延迟路径）
	{
		FScopeLock Lock(&PakRecordsMutex);

		// 从待卸载列表移除（避免 ProcessPendingUnmounts 重复处理）
		PendingUnmounts.Remove(NormalizedPath);

		FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
		if (Record)
		{
			Record->RefCount = 0;
		}
	}

	bool bSuccess = false;
	if (IsInGameThread())
	{
		bSuccess = UnmountPakInternal(NormalizedPath);
	}
	else
	{
		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
		AsyncTask(ENamedThreads::GameThread, [this, &bSuccess, &NormalizedPath, Event]()
		{
			bSuccess = UnmountPakInternal(NormalizedPath);
			Event->Trigger();
		});
		Event->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Event);
	}

	if (bSuccess)
	{
		FScopeLock Lock(&PakRecordsMutex);
		FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
		if (Record)
		{
			Record->bIsMounted = false;
			Record->Metadata.bIsMounted = false;
		}
	}

	if (bSuccess)
	{
		OnPakUnmounted.Broadcast(NormalizedPath);
	}

	return bSuccess;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

bool UHotUpdatePakManager::IsPakMounted(const FString& PakPath) const
{
	FString NormalizedPath = NormalizePakPath(PakPath);

	FScopeLock Lock(&PakRecordsMutex);

	const FPakMountRecord* Record = PakRecords.Find(NormalizedPath);
	return Record && Record->bIsMounted;
}

// ============================================================
// 原有功能保留
// ============================================================

TArray<FHotUpdatePakEntry> UHotUpdatePakManager::GetPakEntries(const FString& PakPath)
{
	TArray<FHotUpdatePakEntry> Entries;

	// 检查文件是否存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*PakPath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Pak file not found: %s"), *PakPath);
		return Entries;
	}

	// 获取文件大小
	int64 FileSize = PlatformFile.FileSize(*PakPath);
	UE_LOG(LogHotUpdate, Log, TEXT("Pak file size: %lld bytes, Path: %s"), FileSize, *PakPath);

	// 使用 IPlatformFile 创建 FPakFile
	TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformFile, *PakPath, false);
	if (!PakFile.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to create FPakFile: %s"), *PakPath);
		return Entries;
	}

	if (!PakFile->IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("FPakFile is not valid: %s"), *PakPath);
		return Entries;
	}

	// 输出 Pak 文件信息
	int32 NumFiles = PakFile->GetNumFiles();
	bool bHasFilenames = PakFile->HasFilenames();
	UE_LOG(LogHotUpdate, Log, TEXT("Pak info - NumFiles: %d, HasFilenames: %s"), NumFiles, bHasFilenames ? TEXT("true") : TEXT("false"));

	// 遍历 Pak 文件中的所有条目
	int32 IterationCount = 0;
	for (FPakFile::FFilenameIterator It(*PakFile); It; ++It)
	{
		IterationCount++;
		const FPakEntry& PakEntry = It.Info();
		const FString& Filename = It.Filename();

		FHotUpdatePakEntry Entry;
		Entry.FileName = Filename;
		Entry.UncompressedSize = PakEntry.UncompressedSize;
		Entry.CompressedSize = PakEntry.Size;
		Entry.Offset = PakEntry.Offset;
		Entry.bIsCompressed = PakEntry.CompressionMethodIndex != 0;
		Entry.bIsEncrypted = (PakEntry.Flags & FPakEntry::Flag_Encrypted) != 0;
		Entry.FileHash = UHotUpdateFileUtils::BytesToHex(PakEntry.Hash, sizeof(PakEntry.Hash));

		Entries.Add(Entry);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("FFilenameIterator iterations: %d, Final entries: %d"), IterationCount, Entries.Num());

	// 如果 FFilenameIterator 没有结果，尝试使用 FPakEntryIterator
	if (Entries.Num() == 0 && NumFiles > 0)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Trying FPakEntryIterator as fallback..."));
		IterationCount = 0;
		for (FPakFile::FPakEntryIterator It(*PakFile); It; ++It)
		{
			IterationCount++;
			const FPakEntry& PakEntry = It.Info();
			const FString* Filename = It.TryGetFilename();

			if (Filename && !Filename->IsEmpty())
			{
				FHotUpdatePakEntry Entry;
				Entry.FileName = *Filename;
				Entry.UncompressedSize = PakEntry.UncompressedSize;
				Entry.CompressedSize = PakEntry.Size;
				Entry.Offset = PakEntry.Offset;
				Entry.bIsCompressed = PakEntry.CompressionMethodIndex != 0;
				Entry.bIsEncrypted = (PakEntry.Flags & FPakEntry::Flag_Encrypted) != 0;
				Entry.FileHash = UHotUpdateFileUtils::BytesToHex(PakEntry.Hash, sizeof(PakEntry.Hash));
				Entries.Add(Entry);
			}
		}
		UE_LOG(LogHotUpdate, Log, TEXT("FPakEntryIterator iterations: %d, entries with filename: %d"), IterationCount, Entries.Num());
	}

	return Entries;
}

FHotUpdatePakMetadata UHotUpdatePakManager::ParsePakMetadata(const FString& PakPath)
{
	FHotUpdatePakMetadata Metadata;
	Metadata.PakPath = PakPath;
	Metadata.PakName = FPaths::GetCleanFilename(PakPath);
	Metadata.bIsMounted = false;

	// 获取文件大小
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	Metadata.PakSize = PlatformFile.FileSize(*PakPath);

	// 尝试从文件名解析版本信息
	FString Filename = Metadata.PakName;
	Filename.RemoveFromEnd(TEXT(".pak"));
	Filename.RemoveFromEnd(TEXT(".utoc"));

	TArray<FString> Parts;
	Filename.ParseIntoArray(Parts, TEXT("_"));

	for (const FString& Part : Parts)
	{
		if (Part.Contains(TEXT(".")))
		{
			TArray<FString> VersionParts;
			Part.ParseIntoArray(VersionParts, TEXT("."));

			if (VersionParts.Num() >= 2)
			{
				Metadata.Version = FHotUpdateVersionInfo::FromString(Part);
			}
		}
	}

	return Metadata;
}

int32 UHotUpdatePakManager::CalculatePakOrder(const FHotUpdateVersionInfo& Version)
{
	int32 BaseOrder = 100;
	BaseOrder += Version.MajorVersion * 10000;
	BaseOrder += Version.MinorVersion * 100;
	BaseOrder += Version.PatchVersion;
	return BaseOrder;
}
