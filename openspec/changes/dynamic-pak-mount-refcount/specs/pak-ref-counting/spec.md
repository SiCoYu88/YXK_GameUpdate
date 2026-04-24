## ADDED Requirements

### Requirement: Reference-counted mount request
The system SHALL provide a `RequestMount(PakPath, PakOrder, EncryptionKey)` method that increments the reference count for the specified Pak/IoStore container and performs the actual mount operation only when the reference count transitions from 0 to 1.

#### Scenario: First mount request for an unregistered Pak
- **WHEN** `RequestMount` is called with a PakPath that has no existing record
- **THEN** the system SHALL create a new record with RefCount=1, execute the actual Pak mount, and return true if mount succeeds

#### Scenario: First mount request for a registered but unmounted Pak
- **WHEN** `RequestMount` is called with a PakPath that is registered (via `RegisterAvailablePak`) but has RefCount=0
- **THEN** the system SHALL execute the actual mount, set RefCount=1, and return true

#### Scenario: Subsequent mount request for an already-mounted Pak
- **WHEN** `RequestMount` is called with a PakPath that is already mounted with RefCount >= 1
- **THEN** the system SHALL increment RefCount by 1 without performing any mount operation and return true

#### Scenario: Mount request with invalid path
- **WHEN** `RequestMount` is called with a PakPath that does not exist on disk
- **THEN** the system SHALL return false and NOT create a record

### Requirement: Reference-counted unmount request
The system SHALL provide a `RequestUnmount(PakPath)` method that decrements the reference count and performs the actual unmount only when the count reaches zero.

#### Scenario: Unmount request reducing count to zero
- **WHEN** `RequestUnmount` is called and the resulting RefCount equals 0
- **THEN** the system SHALL schedule the actual Pak unmount on the next game-thread tick and return true

#### Scenario: Unmount request with remaining references
- **WHEN** `RequestUnmount` is called and the resulting RefCount is still > 0
- **THEN** the system SHALL decrement RefCount by 1 without performing any unmount and return true

#### Scenario: Unmount request for non-mounted Pak
- **WHEN** `RequestUnmount` is called for a PakPath with RefCount=0 or no record
- **THEN** the system SHALL log a warning, NOT decrement below 0, and return false

### Requirement: Explicit AddRef and Release
The system SHALL provide `AddRef(PakPath)` and `Release(PakPath)` methods for direct reference count manipulation on already-managed Paks.

#### Scenario: AddRef on mounted Pak
- **WHEN** `AddRef` is called on a Pak with RefCount >= 1
- **THEN** the system SHALL increment RefCount by 1

#### Scenario: Release triggers unmount
- **WHEN** `Release` is called and RefCount transitions from 1 to 0
- **THEN** the system SHALL schedule the actual Pak unmount on the next game-thread tick

#### Scenario: AddRef on unmounted Pak
- **WHEN** `AddRef` is called on a Pak with RefCount=0 (not mounted)
- **THEN** the system SHALL log an error and NOT increment the count (use `RequestMount` instead)

### Requirement: RAII scoped reference guard
The system SHALL provide an `FScopedPakRef` struct that acquires a reference on construction and releases it on destruction.

#### Scenario: Normal scope lifecycle
- **WHEN** an `FScopedPakRef` is constructed with a valid PakPath and PakManager
- **THEN** the system SHALL call `RequestMount` on construction and `RequestUnmount` on destruction

#### Scenario: Guard validity check
- **WHEN** `IsValid()` is called on an `FScopedPakRef`
- **THEN** it SHALL return true if the initial mount succeeded, false otherwise

#### Scenario: Move semantics
- **WHEN** an `FScopedPakRef` is moved to another instance
- **THEN** the original instance SHALL release ownership (no Release on its destruction) and the new instance SHALL take ownership

#### Scenario: Manager destroyed before guard
- **WHEN** the `UHotUpdatePakManager` is destroyed while `FScopedPakRef` instances still exist
- **THEN** the guard's destructor SHALL safely detect the invalid manager (via `TWeakObjectPtr`) and skip the Release call

### Requirement: Batch operations by ChunkId
The system SHALL provide `RequestMountByChunkId(ChunkId)` and `RequestUnmountByChunkId(ChunkId)` methods to mount/unmount all containers belonging to a specific chunk.

#### Scenario: Mount all containers in a chunk
- **WHEN** `RequestMountByChunkId(5)` is called and there are 2 registered containers with ChunkId=5
- **THEN** the system SHALL call `RequestMount` for each of the 2 containers and return the count of successfully mounted containers

#### Scenario: Unmount all containers in a chunk
- **WHEN** `RequestUnmountByChunkId(5)` is called
- **THEN** the system SHALL call `RequestUnmount` for each container with ChunkId=5

#### Scenario: No containers for ChunkId
- **WHEN** `RequestMountByChunkId` is called with a ChunkId that has no registered containers
- **THEN** the system SHALL return 0 and log a warning

### Requirement: Registration of available containers
The system SHALL provide a `RegisterAvailablePak(PakPath, Metadata)` method that marks a container as available for on-demand mounting without performing the actual mount.

#### Scenario: Register after download
- **WHEN** `RegisterAvailablePak` is called with a valid PakPath and metadata
- **THEN** the system SHALL create a record with `bIsRegistered=true`, `bIsMounted=false`, `RefCount=0`

#### Scenario: Register already-mounted Pak
- **WHEN** `RegisterAvailablePak` is called for a Pak that is already mounted
- **THEN** the system SHALL update the metadata without affecting the mount state or RefCount

### Requirement: Mount all registered convenience method
The system SHALL provide a `MountAllRegistered()` method that mounts all registered-but-unmounted containers with RefCount=1.

#### Scenario: Mount all pending containers
- **WHEN** `MountAllRegistered()` is called and there are 3 registered containers with RefCount=0
- **THEN** the system SHALL call `RequestMount` for each and return the count of successfully mounted containers

#### Scenario: No pending containers
- **WHEN** `MountAllRegistered()` is called and all containers are already mounted
- **THEN** the system SHALL return 0 without performing any operation

### Requirement: Query interfaces
The system SHALL provide methods to query the current state of Pak records: `GetRefCount(PakPath)`, `GetAllMountedPaks()`, `GetAllRegisteredPaks()`, `IsRegistered(PakPath)`.

#### Scenario: Query reference count
- **WHEN** `GetRefCount(PakPath)` is called for a mounted Pak with RefCount=3
- **THEN** it SHALL return 3

#### Scenario: Query reference count for unknown Pak
- **WHEN** `GetRefCount(PakPath)` is called for an unregistered PakPath
- **THEN** it SHALL return -1

#### Scenario: List all mounted Paks
- **WHEN** `GetAllMountedPaks()` is called
- **THEN** it SHALL return a list of `FPakMountInfo` structs containing PakPath, RefCount, ChunkId, and Metadata for all Paks with `bIsMounted=true`

### Requirement: Thread safety
All reference count operations SHALL be thread-safe using `FCriticalSection`. Actual Mount/Unmount operations SHALL execute on the game thread.

#### Scenario: Concurrent AddRef from multiple threads
- **WHEN** two threads simultaneously call `AddRef` on the same PakPath
- **THEN** the final RefCount SHALL be incremented by exactly 2 with no data corruption

#### Scenario: Release from non-game thread
- **WHEN** `Release` is called from an async loading thread and RefCount reaches 0
- **THEN** the actual Unmount SHALL be deferred to the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`

### Requirement: Deferred unmount on zero refcount
When a reference count reaches zero, the actual Pak unmount SHALL be deferred to the next game-thread tick rather than executing immediately.

#### Scenario: Deferred unmount allows reload
- **WHEN** RefCount drops to 0 and then a new `RequestMount` arrives within the same frame
- **THEN** the system SHALL cancel the pending unmount and increment RefCount to 1 without performing any unmount/remount

#### Scenario: Deferred unmount executes next tick
- **WHEN** RefCount drops to 0 and no new mount request arrives before the next tick
- **THEN** the system SHALL execute the actual Unmount on the next tick

### Requirement: Event notifications for actual mount/unmount
The system SHALL broadcast events when Paks are actually mounted or unmounted (not on every RefCount change).

#### Scenario: Broadcast on first mount
- **WHEN** a Pak transitions from unmounted to mounted (RefCount 0→1)
- **THEN** the system SHALL broadcast `OnPakMounted(PakPath, bSuccess)`

#### Scenario: Broadcast on final unmount
- **WHEN** a Pak transitions from mounted to unmounted (RefCount 1→0, deferred unmount executes)
- **THEN** the system SHALL broadcast `OnPakUnmounted(PakPath)`

#### Scenario: No broadcast on AddRef
- **WHEN** `AddRef` increments RefCount from 2 to 3
- **THEN** the system SHALL NOT broadcast any mount/unmount event

### Requirement: Backward compatibility
The existing `MountPak` and `UnmountPak` methods SHALL remain functional but be marked as deprecated, internally delegating to the new reference-counted system.

#### Scenario: Legacy MountPak call
- **WHEN** code calls the deprecated `MountPak(PakPath, PakOrder, EncryptionKey)`
- **THEN** the system SHALL internally call `RequestMount` and return the same result

#### Scenario: Legacy UnmountPak call
- **WHEN** code calls the deprecated `UnmountPak(PakPath)`
- **THEN** the system SHALL internally set RefCount to 0 and execute immediate unmount (bypassing deferred behavior for backward compatibility)
