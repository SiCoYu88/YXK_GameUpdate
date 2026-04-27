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

bool UHotUpdatePakManager::MountPak(const FString& PakPath, int32 PakOrder, const FString& EncryptionKey)
{
	// 检查是否已挂载
	if (IsPakMounted(PakPath))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("Pak already mounted, skipping: %s"), *PakPath);
		return true;
	}

	// 获取 Pak 平台文件
	IPlatformFile* FoundFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
	FPakPlatformFile* PakPlatformFile = FoundFile ? static_cast<FPakPlatformFile*>(FoundFile) : nullptr;
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("PakPlatformFile not found"));
		OnPakMounted.Broadcast(PakPath, false);
		return false;
	}

	// 检查文件是否存在
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*PakPath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Pak file not found: %s"), *PakPath);
		OnPakMounted.Broadcast(PakPath, false);
		return false;
	}

	// 处理加密密钥
	bool bUseEncryption = false;

	if (!EncryptionKey.IsEmpty())
	{
		bUseEncryption = true;

		// 将密钥注册到引擎
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
			UE_LOG(LogHotUpdate, Log, TEXT("Registered encryption key with engine for Pak: %s"), *PakPath);
		}
		else
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("Failed to convert encryption key to bytes: %s"), *EncryptionKey);
		}
	}

	// 使用 UE5.7 的 Mount API
	bool bSuccess = PakPlatformFile->Mount(*PakPath, PakOrder);
	if (bSuccess)
	{
		// 添加到已挂载列表
		FHotUpdatePakMetadata Metadata = ParsePakMetadata(PakPath);
		Metadata.bIsMounted = true;
		MountedPaks.Add(Metadata);

		UE_LOG(LogHotUpdate, Log, TEXT("Mounted Pak: %s (Order: %d, Encrypted: %s)"),
			*PakPath, PakOrder, bUseEncryption ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("Failed to mount Pak: %s"), *PakPath);
	}

	OnPakMounted.Broadcast(PakPath, bSuccess);
	return bSuccess;
}

bool UHotUpdatePakManager::UnmountPak(const FString& PakPath)
{
	FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
	if (!PakPlatformFile)
	{
		return false;
	}

	bool bSuccess = PakPlatformFile->Unmount(*PakPath);
	if (bSuccess)
	{
		// 从已挂载列表移除
		for (int32 i = MountedPaks.Num() - 1; i >= 0; i--)
		{
			if (MountedPaks[i].PakPath == PakPath)
			{
				MountedPaks.RemoveAt(i);
			}
		}

		UE_LOG(LogHotUpdate, Log, TEXT("Unmounted Pak: %s"), *PakPath);
		OnPakUnmounted.Broadcast(PakPath);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("Failed to unmount Pak: %s"), *PakPath);
	}

	return bSuccess;
}

bool UHotUpdatePakManager::IsPakMounted(const FString& PakPath) const
{
	for (const FHotUpdatePakMetadata& Metadata : MountedPaks)
	{
		if (Metadata.PakPath == PakPath)
		{
			return true;
		}
	}
	return false;
}

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
		// CompressionMethodIndex != 0 表示压缩
		Entry.bIsCompressed = PakEntry.CompressionMethodIndex != 0;
		Entry.bIsEncrypted = (PakEntry.Flags & FPakEntry::Flag_Encrypted) != 0;

		// 计算 SHA1 Hash 字符串
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
	// 假设文件名格式: "HotUpdate_1.2.3.pak" 或 "Chunk_100_1.2.3.pak" 或 "Patch_1.2.3.utoc"
	FString Filename = Metadata.PakName;
	Filename.RemoveFromEnd(TEXT(".pak"));
	Filename.RemoveFromEnd(TEXT(".utoc"));

	TArray<FString> Parts;
	Filename.ParseIntoArray(Parts, TEXT("_"));

	for (const FString& Part : Parts)
	{
		// 检查是否为版本号格式 (x.x.x)
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
	// Pak 顺序规则：
	// 1. 基础 Pak (Chunk 0) 优先级最低
	// 2. 更高版本的 Pak 优先级更高
	// 3. 补丁 Pak 优先级最高

	int32 BaseOrder = 100; // 基础顺序

	// 版本号影响顺序
	BaseOrder += Version.MajorVersion * 10000;
	BaseOrder += Version.MinorVersion * 100;
	BaseOrder += Version.PatchVersion;

	return BaseOrder;
}
