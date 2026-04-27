## Context

当前 HotUpdate 系统已具备引用计数的 Pak 挂载/卸载能力（`RequestMount` / `RequestUnmount` / `FScopedPakRef`），但缺少从 Asset 到 Pak 的映射关系。业务层在加载资源前必须事先知道哪些 Pak 需要挂载，这在资源分散于多个 Pak、且存在复杂依赖关系时非常不便。

需要建立两层信息：
1. **Asset → Pak 映射**：每个 Cook 后的 Asset 在哪个 Pak/IoStore 容器中
2. **Asset 依赖图 + 依赖 Pak**：每个 Asset 依赖哪些其他 Asset，以及这些依赖 Asset 各自在哪个 Pak 中

有了这两层信息，就可以实现加载主资源时自动 Mount 所有相关 Pak 的能力。

## Goals / Non-Goals

**Goals:**
- 编辑器打包时自动生成 Asset-Pak Manifest 和 Asset 依赖信息文件
- 运行时高效加载和查询 Asset → Pak 映射
- 加载 Asset 时自动发现并 Mount 所有依赖 Pak
- 基于已有引用计数系统管理自动 Mount 的 Pak 生命周期
- 支持异步加载和同步加载两种模式

**Non-Goals:**
- 不改变现有打包流程的核心逻辑（只在末尾追加 Manifest 生成步骤）
- 不实现 Asset 级别的增量更新（仍以 Pak 容器为增量粒度）
- 不实现运行时依赖信息的动态更新（Manifest 在打包时生成，运行时只读）
- 不替换 UE 原生的 AssetManager 资源加载机制（作为补充层，而非替代）
- 不实现跨版本的 Manifest 合并（每个版本独立生成完整 Manifest）

## Decisions

### Decision 1: Asset-Pak Manifest 文件格式

**选择**：使用 JSON 格式，结构为两级索引：按 Pak 路径分组，内部列举包含的 Asset 路径。

```json
{
  "version": "1.0.1",
  "platform": "Windows",
  "buildTime": "2026-04-28T00:00:00Z",
  "paks": [
    {
      "pakPath": "pakchunk1-Windows.pak",
      "chunkId": 1,
      "assets": [
        "/Game/Maps/Level01",
        "/Game/Maps/Level02",
        "/Game/Characters/Hero01"
      ]
    },
    {
      "pakPath": "pakchunk2-Windows.pak",
      "chunkId": 2,
      "assets": [
        "/Game/UI/MainMenu",
        "/Game/UI/HUD"
      ]
    }
  ],
  "assetIndex": {
    "/Game/Maps/Level01": { "pakPath": "pakchunk1-Windows.pak", "chunkId": 1 },
    "/Game/Maps/Level02": { "pakPath": "pakchunk1-Windows.pak", "chunkId": 1 },
    "/Game/Characters/Hero01": { "pakPath": "pakchunk1-Windows.pak", "chunkId": 1 },
    "/Game/UI/MainMenu": { "pakPath": "pakchunk2-Windows.pak", "chunkId": 2 },
    "/Game/UI/HUD": { "pakPath": "pakchunk2-Windows.pak", "chunkId": 2 }
  }
}
```

**理由**：
- `paks` 数组便于按 Pak 维度查看内容（调试、管理）
- `assetIndex` 是按 Asset 路径的平坦索引，运行时加载后直接构建 `TMap<FString, FAssetPakInfo>` O(1) 查找
- 同时包含两种视角，减少运行时转换开销

**替代方案**：
- 仅存 Pak→Asset 列表，运行时反转构建索引 — 增加启动耗时
- 二进制格式 — 查看调试不便，兼容性差

### Decision 2: Asset 依赖信息文件格式

**选择**：JSON 格式，记录每个 Asset 的直接依赖（Hard + Soft），并关联依赖 Asset 所在的 Pak。

```json
{
  "version": "1.0.1",
  "dependencies": {
    "/Game/Maps/Level01": {
      "hardDeps": [
        { "assetPath": "/Game/Characters/Hero01", "pakPath": "pakchunk1-Windows.pak", "chunkId": 1 },
        { "assetPath": "/Game/Textures/Terrain01", "pakPath": "pakchunk3-Windows.pak", "chunkId": 3 }
      ],
      "softDeps": [
        { "assetPath": "/Game/Audio/BGM01", "pakPath": "pakchunk2-Windows.pak", "chunkId": 2 }
      ],
      "requiredPaks": ["pakchunk1-Windows.pak", "pakchunk3-Windows.pak"],
      "optionalPaks": ["pakchunk2-Windows.pak"]
    }
  }
}
```

**理由**：
- 区分 Hard/Soft 依赖：Hard 依赖的 Pak 必须在加载前 Mount，Soft 依赖可按需延迟
- `requiredPaks` / `optionalPaks` 是预计算的去重列表，运行时直接使用无需再遍历
- 依赖信息已在编辑器的 AssetRegistry 中可用，打包时收集写入，运行时无需再解析 AssetRegistry

**替代方案**：
- 运行时通过 AssetRegistry 动态查询依赖 — 打包后的运行时环境中 AssetRegistry 信息有限，且无法关联到 Pak
- 只记录 Pak 间的依赖（而非 Asset 间）— 粒度太粗，会导致不必要的 Pak Mount

### Decision 3: Manifest 生成时机与集成点

**选择**：在 `UHotUpdateBaseVersionBuilder` 和 `UHotUpdatePatchPackageBuilder` 的构建流程末尾，在 GenerateManifest 之后追加 Asset-Pak Manifest 生成步骤。

```
BuildContainers → GenerateManifest → GenerateAssetPakManifest → GenerateAssetDependencies → RegisterVersion
```

**理由**：
- 此时 Pak 容器已构建完成，可以遍历获取每个 Pak 的文件列表
- 依赖信息在 Cook 后即可通过 AssetRegistry 获取
- 生成的 Manifest 文件放在与 manifest.json 同级目录，一并部署

**具体实现**：
- `FHotUpdateAssetPakManifestGenerator::Generate(OutputDir, Paks, Version, Platform)` — 静态方法，遍历所有 Pak 文件，读取内部文件列表，生成 `asset_pak_manifest.json`
- `FHotUpdateAssetDependencyCollector::Collect(OutputDir, AssetPaths, Version)` — 静态方法，通过 AssetRegistry 查询每个 Asset 的依赖，结合 AssetPakManifest 关联 Pak 信息，生成 `asset_dependencies.json`

### Decision 4: 运行时 Mapping 加载与查询

**选择**：新增 `UHotUpdateAssetPakMapping`（UObject），在热更完成后加载 Manifest 文件，构建内存索引。

```cpp
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateAssetPakMapping : public UObject
{
    GENERATED_BODY()

public:
    /** 加载 Asset-Pak Manifest 文件 */
    UFUNCTION(BlueprintCallable)
    bool LoadManifest(const FString& ManifestDir);

    /** 查询 Asset 所在的 Pak 路径 */
    UFUNCTION(BlueprintPure)
    FString GetPakForAsset(const FString& AssetPath) const;

    /** 查询 Asset 所在的 ChunkId */
    UFUNCTION(BlueprintPure)
    int32 GetChunkIdForAsset(const FString& AssetPath) const;

    /** 获取加载指定 Asset 需要 Mount 的全部 Pak 路径（含依赖） */
    UFUNCTION(BlueprintCallable)
    TArray<FString> GetRequiredPaksForAsset(const FString& AssetPath) const;

    /** 获取加载指定 Asset 的可选 Pak 路径（Soft 依赖） */
    UFUNCTION(BlueprintCallable)
    TArray<FString> GetOptionalPaksForAsset(const FString& AssetPath) const;

    /** 获取指定 Pak 中包含的全部 Asset 列表 */
    UFUNCTION(BlueprintCallable)
    TArray<FString> GetAssetsInPak(const FString& PakPath) const;

    /** 检查 Manifest 是否已加载 */
    UFUNCTION(BlueprintPure)
    bool IsManifestLoaded() const;

private:
    /** Asset → Pak 映射 */
    TMap<FString, FAssetPakInfo> AssetToPakMap;

    /** Pak → Asset 列表 */
    TMap<FString, TArray<FString>> PakToAssetsMap;

    /** Asset → 依赖 Pak 信息 */
    TMap<FString, FAssetDependencyInfo> AssetDependencies;

    bool bIsLoaded = false;
};

struct FAssetPakInfo
{
    FString PakPath;
    int32 ChunkId = -1;
};

struct FAssetDependencyInfo
{
    TArray<FString> RequiredPaks;   // Hard 依赖的 Pak
    TArray<FString> OptionalPaks;   // Soft 依赖的 Pak
};
```

**理由**：
- 使用 TMap 按 AssetPath 查找 O(1)，加载上万资源的 Manifest 也能快速查询
- 在热更完成后（Manifest 下载到本地后）立即加载，后续查询无 IO

### Decision 5: AutoMountLoader 自动加载器

**选择**：新增 `UHotUpdateAutoMountLoader`（UObject），封装"查询依赖 → Mount 所有 Pak → 加载资源 → 管理引用"的完整流程。

```cpp
UCLASS(BlueprintType)
class HOTUPDATE_API UHotUpdateAutoMountLoader : public UObject
{
    GENERATED_BODY()

public:
    /** 初始化，绑定 PakManager 和 Mapping */
    void Initialize(UHotUpdatePakManager* InPakManager, UHotUpdateAssetPakMapping* InMapping);

    /**
     * 异步加载资源（自动 Mount 依赖 Pak）
     *
     * 流程：
     *  1. 通过 Mapping 查询主资源及其 Hard 依赖所在的全部 Pak
     *  2. 对每个 Pak 调用 PakManager->RequestMount
     *  3. 使用 StreamableManager 异步加载资源
     *  4. 返回 FAutoMountAssetHandle，持有所有 Pak 的引用计数
     *  5. Handle 析构时自动 RequestUnmount 所有 Pak
     */
    UFUNCTION(BlueprintCallable)
    FAutoMountAssetHandle AsyncLoadAsset(const FString& AssetPath,
        const FOnAutoMountLoadComplete& OnComplete);

    /**
     * 同步加载资源（自动 Mount 依赖 Pak）
     */
    UFUNCTION(BlueprintCallable)
    UObject* SyncLoadAsset(const FString& AssetPath);

    /**
     * 释放资源引用（Unmount 关联的 Pak）
     */
    UFUNCTION(BlueprintCallable)
    void ReleaseAsset(const FString& AssetPath);

    /** 批量加载 */
    UFUNCTION(BlueprintCallable)
    FAutoMountBatchHandle AsyncLoadAssets(const TArray<FString>& AssetPaths,
        const FOnAutoMountBatchComplete& OnComplete);
};
```

**FAutoMountAssetHandle** 设计：

```cpp
struct HOTUPDATE_API FAutoMountAssetHandle
{
    /** 获取加载的资源 */
    UObject* GetAsset() const;

    /** 是否有效 */
    bool IsValid() const;

    /** 释放（Unmount 所有关联 Pak） */
    void Release();

    // Move only
    FAutoMountAssetHandle(FAutoMountAssetHandle&&);
    FAutoMountAssetHandle& operator=(FAutoMountAssetHandle&&);
    FAutoMountAssetHandle(const FAutoMountAssetHandle&) = delete;

    ~FAutoMountAssetHandle(); // 析构时自动 Release

private:
    TWeakObjectPtr<UHotUpdatePakManager> PakManager;
    TArray<FString> MountedPakPaths;  // 本次加载 Mount 的 Pak 列表
    TWeakObjectPtr<UObject> LoadedAsset;
    FString AssetPath;
};
```

**理由**：
- Handle 模式将 Pak 引用与资源使用绑定，确保 Pak 在资源使用期间不会被卸载
- 析构自动释放，配合 RAII 防止泄漏
- 支持批量加载共享 Pak 引用，避免重复 Mount/Unmount

### Decision 6: HotUpdateManager 集成

**选择**：在 `UHotUpdateManager` 中新增 `AssetPakMapping` 和 `AutoMountLoader` 成员，在 `ApplyUpdate()` 成功后自动加载 Manifest。

```cpp
// UHotUpdateManager 新增成员
UPROPERTY()
UHotUpdateAssetPakMapping* AssetPakMapping;

UPROPERTY()
UHotUpdateAutoMountLoader* AutoMountLoader;

// ApplyUpdate 末尾追加
AssetPakMapping->LoadManifest(ManifestDir);
AutoMountLoader->Initialize(PakManager, AssetPakMapping);
```

提供面向业务层的便捷接口：

```cpp
UFUNCTION(BlueprintCallable)
FAutoMountAssetHandle LoadAssetWithAutoMount(const FString& AssetPath, ...);

UFUNCTION(BlueprintCallable)
void ReleaseAutoMountAsset(const FString& AssetPath);
```

**理由**：
- 保持 HotUpdateManager 作为中枢调度器的角色
- 业务层只需与 Manager 交互，无需直接操作 PakManager、Mapping、Loader

### Decision 7: 编辑器 Manifest 生成器实现策略

**选择**：使用 `FPakFile` / `FPakPlatformFile` 读取已构建的 Pak 文件列表，结合 AssetRegistry 收集依赖。

**Asset-Pak Manifest 生成流程**：
```
1. 遍历输出目录的所有 .pak 文件
2. 对每个 Pak，使用 FPakFile 遍历内部文件列表（FFilenameIterator）
3. 将 Pak 内文件路径转换为 UE Asset 路径（ConvertFileNameToAssetPath）
4. 构建双向映射并写入 JSON
```

**依赖收集流程**：
```
1. 加载 AssetRegistry（Cook 后 DevelopmentAssetRegistry.bin）
2. 对 asset_pak_manifest.json 中的每个 Asset：
   a. 查询 AssetRegistry 获取 Hard/Soft 依赖
   b. 递归收集依赖（但不做循环遍历，只记录直接依赖）
   c. 关联每个依赖到其所在 Pak（通过 asset_pak_manifest 查找）
3. 预计算 requiredPaks / optionalPaks 去重列表
4. 写入 JSON
```

**理由**：
- 复用已有的 `GetPakEntries` 和 `FHotUpdatePackageHelper::ConvertFileNameToAssetPath` 逻辑
- AssetRegistry 在编辑器环境中可用，Cook 后包含完整的依赖信息

## Risks / Trade-offs

- **[Risk] Manifest 文件过大** → 对于超大项目（10万+ Asset），JSON 文件可能达到数 MB。运行时首次加载可能需要几十毫秒。**缓解**：异步加载 Manifest；未来可考虑二进制格式优化
- **[Risk] Asset 路径不匹配** → Cook 后的文件名与运行时 AssetPath 格式可能不同（如大小写、前缀）。**缓解**：统一使用 `NormalizeAssetPath` 进行路径标准化，查询时也做同样标准化
- **[Risk] 依赖图过深导致 Mount 大量 Pak** → 某些核心资源（如基础 Material）可能被大量 Asset 依赖，间接导致加载任意资源都需要 Mount 这些核心 Pak。**缓解**：这实际上是合理的行为（核心 Pak 的引用计数会很高，不会被频繁卸载）；可在 Manifest 中标记常驻 Pak 列表
- **[Trade-off] 只记录直接依赖 vs 递归展开全部依赖** → 选择只记录直接依赖 + 运行时递归查询的折中方案。记录直接依赖减小文件体积，运行时递归查询有缓存可避免重复计算。但首次查询某个依赖链很深的资源时可能稍慢
- **[Trade-off] Hard 依赖强制 Mount vs 全部 Mount** → 选择只对 Hard 依赖强制 Mount，Soft 依赖提供可选 Mount。这减少不必要的 Pak 挂载，但可能导致 Soft 依赖资源首次访问时需要再次 Mount
- **[Risk] 构建流程耗时增加** → 扫描 Pak 内容和收集依赖增加数秒到数十秒的构建时间。**缓解**：依赖收集可并行化；Manifest 生成相对轻量
