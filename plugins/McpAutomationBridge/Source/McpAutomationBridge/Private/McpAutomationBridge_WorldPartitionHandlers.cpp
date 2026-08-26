// =============================================================================
// McpAutomationBridge_WorldPartitionHandlers.cpp
// =============================================================================
// MCP Automation Bridge - World Partition & Data Layer Handlers
//
// UE Version Support: 5.0, 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7
//
// Handler Summary:
// -----------------------------------------------------------------------------
// Action: manage_world_partition (Editor Only)
//   - load_cells: Load cells/region in World Partition level
//   - create_datalayer: Create new Data Layer asset
//   - set_datalayer: Add actor to Data Layer
//   - cleanup_invalid_datalayers: Remove invalid Data Layer instances
//
// Dependencies:
//   - Core: McpAutomationBridgeSubsystem, McpAutomationBridgeHelpers
//   - Engine: WorldPartition, DataLayer, EngineUtils
//   - Editor: EditorActorSubsystem, LevelEditor
//
// Version Compatibility Notes:
//   - UE 5.0-5.3: UWorldPartitionEditorSubsystem::LoadRegion()
//   - UE 5.4+: WorldPartitionEditorLoaderAdapter with FLoaderAdapterShape
//   - UE 5.1+: DataLayerEditorSubsystem with FDataLayerCreationParameters
//   - UE 5.0: Limited DataLayer API support
//   - UE 5.3+: UDataLayerManager for data layer operations
//
// Architecture:
//   - World Partition levels use external actor packages
//   - TActorIterator required for finding actors (FindObject unreliable)
//   - Data Layers require assets for persistence
// =============================================================================

#include "McpVersionCompatibility.h"  // MUST be first - UE version compatibility macros

// -----------------------------------------------------------------------------
// Core Includes
// -----------------------------------------------------------------------------
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeGlobals.h"
#include "McpHandlerUtils.h"

// -----------------------------------------------------------------------------
// Engine Includes
// -----------------------------------------------------------------------------
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "FileHelpers.h"
#include "EditorLevelUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "EngineUtils.h"

// -----------------------------------------------------------------------------
// Version-Specific Includes: WorldPartitionEditorSubsystem
// -----------------------------------------------------------------------------
#if defined(__has_include)
#  if __has_include("WorldPartition/WorldPartitionEditorSubsystem.h")
#    include "WorldPartition/WorldPartitionEditorSubsystem.h"
#    define MCP_HAS_WP_EDITOR_SUBSYSTEM 1
#  elif __has_include("WorldPartitionEditor/WorldPartitionEditorSubsystem.h")
#    include "WorldPartitionEditor/WorldPartitionEditorSubsystem.h"
#    define MCP_HAS_WP_EDITOR_SUBSYSTEM 1
#  else
#    define MCP_HAS_WP_EDITOR_SUBSYSTEM 0
#  endif
#else
#  define MCP_HAS_WP_EDITOR_SUBSYSTEM 0
#endif

// -----------------------------------------------------------------------------
// Version-Specific Includes: LoaderAdapter (UE 5.4+)
// -----------------------------------------------------------------------------
#if defined(__has_include)
#  if __has_include("WorldPartition/WorldPartitionEditorLoaderAdapter.h")
#    include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#    include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#    define MCP_HAS_WP_LOADER_ADAPTER 1
#  else
#    define MCP_HAS_WP_LOADER_ADAPTER 0
#  endif
#else
#  define MCP_HAS_WP_LOADER_ADAPTER 0
#endif

#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "AssetRegistry/AssetRegistryModule.h"

// -----------------------------------------------------------------------------
// Version-Specific Includes: DataLayerEditorSubsystem (UE 5.1+)
// -----------------------------------------------------------------------------
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#  if defined(__has_include)
#    if __has_include("DataLayer/DataLayerEditorSubsystem.h")
#      include "DataLayer/DataLayerEditorSubsystem.h"
#      define MCP_HAS_DATALAYER_EDITOR 1
#    elif __has_include("WorldPartition/DataLayer/DataLayerEditorSubsystem.h")
#      include "WorldPartition/DataLayer/DataLayerEditorSubsystem.h"
#      define MCP_HAS_DATALAYER_EDITOR 1
#    else
#      define MCP_HAS_DATALAYER_EDITOR 0
#    endif
#  else
#    define MCP_HAS_DATALAYER_EDITOR 0
#  endif
#else
// UE 5.0: DataLayer APIs not available
#  define MCP_HAS_DATALAYER_EDITOR 0
#endif

// DataLayerInstance.h and DataLayerAsset.h introduced in UE 5.1
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
#include "WorldPartition/DataLayer/DataLayerManager.h"
#endif

#endif // WITH_EDITOR

// =============================================================================
// World Partition Cell Subaction Helpers (get/pin/unpin/unload cells)
// =============================================================================
// UE 5.4+: editor loading is driven by registered editor loader adapters
// (UWorldPartition::GetRegisteredEditorLoaderAdapters / CreateEditorLoaderAdapter).
// There is no per-cell pin API in 5.4+; pinning is implemented as a user-created
// loaded region covering the requested cells (same mechanism as load_cells).
// =============================================================================

#if WITH_EDITOR
namespace McpHandlers
{
namespace WorldPartition
{

static bool ParseWpCellBoundsVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& OutVec)
{
    const TSharedPtr<FJsonObject>* VecObj = nullptr;
    if (!Object->TryGetObjectField(FString(Key), VecObj) || !VecObj || !VecObj->IsValid())
    {
        return false;
    }
    OutVec.X = GetJsonNumberField(*VecObj, TEXT("x"), 0.0);
    OutVec.Y = GetJsonNumberField(*VecObj, TEXT("y"), 0.0);
    OutVec.Z = GetJsonNumberField(*VecObj, TEXT("z"), 0.0);
    return true;
}

static bool ParseWpCellRegion(const TSharedPtr<FJsonObject>& Payload, FBox& OutBox)
{
    const TSharedPtr<FJsonObject>* RegionObj = nullptr;
    if (!Payload.IsValid() || !Payload->TryGetObjectField(TEXT("region"), RegionObj) || !RegionObj || !RegionObj->IsValid())
    {
        return false;
    }
    FVector Min = FVector::ZeroVector;
    FVector Max = FVector::ZeroVector;
    if (!ParseWpCellBoundsVector(*RegionObj, TEXT("min"), Min) ||
        !ParseWpCellBoundsVector(*RegionObj, TEXT("max"), Max))
    {
        return false;
    }
    OutBox = FBox(Min, Max);
    return true;
}

static bool GetWpCellIdFilter(const TSharedPtr<FJsonObject>& Payload, TSet<FString>& OutCellIds)
{
    const TArray<TSharedPtr<FJsonValue>>* CellIdsArray = nullptr;
    if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("cellIds"), CellIdsArray) || !CellIdsArray)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *CellIdsArray)
    {
        const FString CellId = Value.IsValid() ? Value->AsString() : FString();
        if (!CellId.IsEmpty())
        {
            OutCellIds.Add(CellId);
        }
    }
    return true;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
static void EnumerateWpCells(UWorldPartition* WorldPartition, const TOptional<FBox>& Region,
    const TSet<FString>& CellIdFilter, TArray<const UWorldPartitionRuntimeCell*>& OutCells)
{
    const UWorldPartitionRuntimeHash* RuntimeHash = WorldPartition ? WorldPartition->RuntimeHash : nullptr;
    if (!RuntimeHash)
    {
        return;
    }
    auto Collector = [&CellIdFilter, &OutCells](const UWorldPartitionRuntimeCell* Cell)
    {
        if (Cell && (CellIdFilter.Num() == 0 || CellIdFilter.Contains(Cell->GetDebugName())))
        {
            OutCells.Add(Cell);
        }
        return true;
    };
    if (Region.IsSet() && Region->IsValid)
    {
        FWorldPartitionStreamingQuerySource QuerySource;
        QuerySource.Location = Region->GetCenter();
        QuerySource.Radius = Region->GetExtent().Size();
        QuerySource.bUseGridLoadingRange = false;
        QuerySource.bIncludeAnyDataLayer = true;
        RuntimeHash->ForEachStreamingCellsQuery(QuerySource, Collector);
    }
    else
    {
        RuntimeHash->ForEachStreamingCells(Collector);
    }
}
#endif

#if MCP_HAS_WP_LOADER_ADAPTER
static void CollectWpLoadedAdapterBoxes(UWorldPartition* WorldPartition, TArray<TPair<FBox, bool>>& OutBoxes)
{
    for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
    {
        if (!EditorAdapter)
        {
            continue;
        }
        IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter();
        if (!Adapter || !Adapter->IsLoaded())
        {
            continue;
        }
        const TOptional<FBox> AdapterBox = Adapter->GetBoundingBox();
        const TOptional<FString> AdapterLabel = Adapter->GetLabel();
        if (!AdapterBox.IsSet() || !AdapterLabel.IsSet())
        {
            continue;
        }
        const bool bIsMcpPin = AdapterLabel->StartsWith(TEXT("MCP Pinned Cells"));
        OutBoxes.Add(TPair<FBox, bool>(FBox(*AdapterBox), bIsMcpPin));
    }
}
#endif

static void SendWpCellsUnsupported(UMcpAutomationBridgeSubsystem* Subsystem, TSharedPtr<FMcpBridgeWebSocket> Socket,
    const FString& RequestId, const TCHAR* SubActionName)
{
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetBoolField(TEXT("unsupported"), true);
    Result->SetNumberField(TEXT("changed"), 0);
    Result->SetArrayField(TEXT("cells"), TArray<TSharedPtr<FJsonValue>>());
    Result->SetStringField(TEXT("subAction"), SubActionName);
    Subsystem->SendAutomationResponse(Socket, RequestId, false,
        FString::Printf(TEXT("%s requires World Partition editor cell APIs that are unavailable in this engine version."), SubActionName),
        Result, TEXT("NOT_SUPPORTED"));
}

static void AddResolvedWpCells(UWorldPartition* WorldPartition, const FBox& Bounds, TSharedPtr<FJsonObject> Result)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
    TArray<const UWorldPartitionRuntimeCell*> Cells;
    EnumerateWpCells(WorldPartition, TOptional<FBox>(Bounds), TSet<FString>(), Cells);
    TArray<TSharedPtr<FJsonValue>> Names;
    for (const UWorldPartitionRuntimeCell* Cell : Cells)
    {
        Names.Add(MakeShared<FJsonValueString>(Cell->GetDebugName()));
    }
    Result->SetArrayField(TEXT("resolvedCells"), Names);
    Result->SetNumberField(TEXT("resolvedCellCount"), Names.Num());
#endif
    Result->SetStringField(TEXT("note"), TEXT("load is asynchronous; verify via get_wp_cell_status"));
}

bool HandleWorldPartitionGetCellStatus(UMcpAutomationBridgeSubsystem* Subsystem, UWorld* World,
    UWorldPartition* WorldPartition, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
    FBox RegionBox(ForceInit);
    const bool bHasRegion = ParseWpCellRegion(Payload, RegionBox);
    TSet<FString> CellIdFilter;
    const bool bHasCellIds = GetWpCellIdFilter(Payload, CellIdFilter);
    TArray<const UWorldPartitionRuntimeCell*> Cells;
    EnumerateWpCells(WorldPartition, bHasRegion ? TOptional<FBox>(RegionBox) : TOptional<FBox>(), CellIdFilter, Cells);

    TArray<TPair<FBox, bool>> LoadedAdapterBoxes;
#if MCP_HAS_WP_LOADER_ADAPTER
    CollectWpLoadedAdapterBoxes(WorldPartition, LoadedAdapterBoxes);
#endif

    TArray<TSharedPtr<FJsonValue>> CellEntries;
    for (const UWorldPartitionRuntimeCell* Cell : Cells)
    {
        TSharedPtr<FJsonObject> Entry = McpHandlerUtils::CreateResultObject();
        Entry->SetStringField(TEXT("cellId"), Cell->GetDebugName());
        Entry->SetStringField(TEXT("levelPackageName"), Cell->GetLevelPackageName().ToString());
        bool bAnyPackageLoaded = false;
        bool bAnyPackageDirty = false;
        for (const FName& ActorPackageName : Cell->GetActorPackageNames())
        {
            if (UPackage* LoadedPackage = FindPackage(nullptr, *ActorPackageName.ToString()))
            {
                bAnyPackageLoaded = true;
                if (LoadedPackage->IsDirty())
                {
                    bAnyPackageDirty = true;
                }
            }
        }
        const FBox CellBounds = Cell->GetCellBounds();
        bool bCoveredByLoadedAdapter = false;
        bool bCoveredByPinnedAdapter = false;
        if (CellBounds.IsValid)
        {
            for (const TPair<FBox, bool>& LoadedAdapterBox : LoadedAdapterBoxes)
            {
                if (CellBounds.Intersect(LoadedAdapterBox.Key))
                {
                    bCoveredByLoadedAdapter = true;
                    if (LoadedAdapterBox.Value)
                    {
                        bCoveredByPinnedAdapter = true;
                    }
                }
            }
        }
        Entry->SetBoolField(TEXT("loaded"), Cell->GetLevel() != nullptr || bCoveredByLoadedAdapter);
        Entry->SetBoolField(TEXT("pinned"), bCoveredByPinnedAdapter);
        Entry->SetBoolField(TEXT("dirty"), bAnyPackageDirty);
        Entry->SetBoolField(TEXT("loadedEditor"), bAnyPackageLoaded || bCoveredByLoadedAdapter);
        CellEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetArrayField(TEXT("cells"), CellEntries);
    Result->SetNumberField(TEXT("cellCount"), CellEntries.Num());
    Result->SetBoolField(TEXT("filteredByCellIds"), bHasCellIds);
    Result->SetBoolField(TEXT("filteredByRegion"), bHasRegion);
    if (!WorldPartition->RuntimeHash)
    {
        Result->SetStringField(TEXT("warning"), TEXT("No runtime streaming data is present in the editor; runtime cells come from generated streaming data. Cell list may be empty."));
    }
    Subsystem->SendAutomationResponse(Socket, RequestId, true, TEXT("Reported World Partition cell status."), Result);
#else
    SendWpCellsUnsupported(Subsystem, Socket, RequestId, TEXT("get_wp_cell_status"));
#endif
    return true;
}

bool HandleWorldPartitionPinCells(UMcpAutomationBridgeSubsystem* Subsystem, UWorld* World,
    UWorldPartition* WorldPartition, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
#if MCP_HAS_WP_LOADER_ADAPTER
    TSet<FString> CellIdFilter;
    GetWpCellIdFilter(Payload, CellIdFilter);
    if (CellIdFilter.Num() == 0)
    {
        Subsystem->SendAutomationResponse(Socket, RequestId, false,
            TEXT("pin_wp_cells requires a non-empty cellIds array."), nullptr, TEXT("INVALID_PARAMS"));
        return true;
    }
    TArray<const UWorldPartitionRuntimeCell*> Cells;
    EnumerateWpCells(WorldPartition, TOptional<FBox>(), CellIdFilter, Cells);
    if (Cells.Num() == 0)
    {
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetNumberField(TEXT("changed"), 0);
        Result->SetArrayField(TEXT("cells"), TArray<TSharedPtr<FJsonValue>>());
        Result->SetStringField(TEXT("note"), TEXT("No runtime streaming cells matched the provided cellIds; cell ids come from get_wp_cell_status."));
        Subsystem->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No World Partition cells matched the provided cellIds."), Result, TEXT("CELLS_NOT_FOUND"));
        return true;
    }
    FBox UnionBounds(ForceInit);
    TArray<TSharedPtr<FJsonValue>> CellEntries;
    for (const UWorldPartitionRuntimeCell* Cell : Cells)
    {
        UnionBounds += Cell->GetCellBounds();
        TSharedPtr<FJsonObject> Entry = McpHandlerUtils::CreateResultObject();
        Entry->SetStringField(TEXT("cellId"), Cell->GetDebugName());
        Entry->SetBoolField(TEXT("pinned"), true);
        Entry->SetBoolField(TEXT("changed"), true);
        CellEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    const FString PinLabel = FString::Printf(TEXT("MCP Pinned Cells %s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter =
        WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, UnionBounds, PinLabel);
    if (EditorLoaderAdapter && EditorLoaderAdapter->GetLoaderAdapter())
    {
        EditorLoaderAdapter->GetLoaderAdapter()->SetUserCreated(true);
        EditorLoaderAdapter->GetLoaderAdapter()->Load();
    }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("changed"), CellEntries.Num());
    Result->SetArrayField(TEXT("cells"), CellEntries);
    Result->SetStringField(TEXT("method"), TEXT("LoaderAdapter"));
    Result->SetStringField(TEXT("pinLabel"), PinLabel);
    Result->SetStringField(TEXT("note"), TEXT("UE 5.4+ has no per-cell pin API; pinning creates a user-created loaded region covering the union of the matched cells."));
    Subsystem->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Pinned World Partition cells via user-created loaded region."), Result);
#else
    SendWpCellsUnsupported(Subsystem, Socket, RequestId, TEXT("pin_wp_cells"));
#endif
    return true;
}

bool HandleWorldPartitionUnpinCells(UMcpAutomationBridgeSubsystem* Subsystem, UWorld* World,
    UWorldPartition* WorldPartition, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
#if MCP_HAS_WP_LOADER_ADAPTER
    TSet<FString> CellIdFilter;
    GetWpCellIdFilter(Payload, CellIdFilter);
    if (CellIdFilter.Num() == 0)
    {
        Subsystem->SendAutomationResponse(Socket, RequestId, false,
            TEXT("unpin_wp_cells requires a non-empty cellIds array."), nullptr, TEXT("INVALID_PARAMS"));
        return true;
    }
    TArray<const UWorldPartitionRuntimeCell*> Cells;
    EnumerateWpCells(WorldPartition, TOptional<FBox>(), CellIdFilter, Cells);
    if (Cells.Num() == 0)
    {
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetNumberField(TEXT("changed"), 0);
        Result->SetArrayField(TEXT("cells"), TArray<TSharedPtr<FJsonValue>>());
        Result->SetStringField(TEXT("note"), TEXT("No runtime streaming cells matched the provided cellIds; cell ids come from get_wp_cell_status."));
        Subsystem->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No World Partition cells matched the provided cellIds."), Result, TEXT("CELLS_NOT_FOUND"));
        return true;
    }
    TArray<TObjectPtr<UWorldPartitionEditorLoaderAdapter>> AdaptersToRelease;
    for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
    {
        if (!EditorAdapter)
        {
            continue;
        }
        IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter();
        if (!Adapter)
        {
            continue;
        }
        const TOptional<FString> AdapterLabel = Adapter->GetLabel();
        if (!AdapterLabel.IsSet() || !AdapterLabel->StartsWith(TEXT("MCP Pinned Cells")))
        {
            continue;
        }
        const TOptional<FBox> AdapterBox = Adapter->GetBoundingBox();
        if (!AdapterBox.IsSet())
        {
            continue;
        }
        for (const UWorldPartitionRuntimeCell* Cell : Cells)
        {
            const FBox CellBounds = Cell->GetCellBounds();
            if (CellBounds.IsValid && AdapterBox->Intersect(CellBounds))
            {
                AdaptersToRelease.Add(EditorAdapter);
                break;
            }
        }
    }
    int32 ReleasedPinCount = 0;
    for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : AdaptersToRelease)
    {
        if (IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter())
        {
            Adapter->Unload();
        }
        WorldPartition->ReleaseEditorLoaderAdapter(EditorAdapter);
        ++ReleasedPinCount;
    }
    int32 ChangedCells = 0;
    TArray<TSharedPtr<FJsonValue>> CellEntries;
    for (const UWorldPartitionRuntimeCell* Cell : Cells)
    {
        const FBox CellBounds = Cell->GetCellBounds();
        bool bWasReleased = false;
        for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : AdaptersToRelease)
        {
            const TOptional<FBox> AdapterBox = EditorAdapter->GetLoaderAdapter()
                ? EditorAdapter->GetLoaderAdapter()->GetBoundingBox()
                : TOptional<FBox>();
            if (AdapterBox.IsSet() && CellBounds.IsValid && AdapterBox->Intersect(CellBounds))
            {
                bWasReleased = true;
                break;
            }
        }
        if (bWasReleased)
        {
            ++ChangedCells;
        }
        TSharedPtr<FJsonObject> Entry = McpHandlerUtils::CreateResultObject();
        Entry->SetStringField(TEXT("cellId"), Cell->GetDebugName());
        Entry->SetBoolField(TEXT("pinned"), false);
        Entry->SetBoolField(TEXT("changed"), bWasReleased);
        CellEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("changed"), ChangedCells);
    Result->SetNumberField(TEXT("releasedPinCount"), ReleasedPinCount);
    Result->SetArrayField(TEXT("cells"), CellEntries);
    Result->SetStringField(TEXT("method"), TEXT("LoaderAdapter"));
    Subsystem->SendAutomationResponse(Socket, RequestId, true, TEXT("Unpinned World Partition cells by releasing MCP pin regions."), Result);
#else
    SendWpCellsUnsupported(Subsystem, Socket, RequestId, TEXT("unpin_wp_cells"));
#endif
    return true;
}

bool HandleWorldPartitionUnloadCells(UMcpAutomationBridgeSubsystem* Subsystem, UWorld* World,
    UWorldPartition* WorldPartition, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
#if MCP_HAS_WP_LOADER_ADAPTER
    TSet<FString> CellIdFilter;
    const bool bHasCellIds = GetWpCellIdFilter(Payload, CellIdFilter);
    TArray<const UWorldPartitionRuntimeCell*> Cells;
    TArray<TPair<FBox, bool>> ReleasedAdapterBoxes;
    TArray<TObjectPtr<UWorldPartitionEditorLoaderAdapter>> AdaptersToRelease;
    if (bHasCellIds && CellIdFilter.Num() > 0)
    {
        EnumerateWpCells(WorldPartition, TOptional<FBox>(), CellIdFilter, Cells);
        if (Cells.Num() == 0)
        {
            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetNumberField(TEXT("changed"), 0);
            Result->SetArrayField(TEXT("cells"), TArray<TSharedPtr<FJsonValue>>());
            Result->SetStringField(TEXT("note"), TEXT("No runtime streaming cells matched the provided cellIds; cell ids come from get_wp_cell_status."));
            Subsystem->SendAutomationResponse(Socket, RequestId, false,
                TEXT("No World Partition cells matched the provided cellIds."), Result, TEXT("CELLS_NOT_FOUND"));
            return true;
        }
    }
    for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
    {
        if (!EditorAdapter)
        {
            continue;
        }
        IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter();
        if (!Adapter)
        {
            continue;
        }
        const TOptional<FString> AdapterLabel = Adapter->GetLabel();
        if (!AdapterLabel.IsSet() || !AdapterLabel->StartsWith(TEXT("MCP ")))
        {
            continue;
        }
        const TOptional<FBox> AdapterBox = Adapter->GetBoundingBox();
        if (!AdapterBox.IsSet())
        {
            continue;
        }
        bool bMatches = Cells.Num() == 0;
        for (const UWorldPartitionRuntimeCell* Cell : Cells)
        {
            const FBox CellBounds = Cell->GetCellBounds();
            if (CellBounds.IsValid && AdapterBox->Intersect(CellBounds))
            {
                bMatches = true;
                break;
            }
        }
        if (bMatches)
        {
            AdaptersToRelease.Add(EditorAdapter);
        }
    }
    int32 ReleasedRegionCount = 0;
    for (const TObjectPtr<UWorldPartitionEditorLoaderAdapter>& EditorAdapter : AdaptersToRelease)
    {
        if (IWorldPartitionActorLoaderInterface::ILoaderAdapter* Adapter = EditorAdapter->GetLoaderAdapter())
        {
            const TOptional<FBox> AdapterBox = Adapter->GetBoundingBox();
            if (AdapterBox.IsSet())
            {
                ReleasedAdapterBoxes.Add(TPair<FBox, bool>(FBox(*AdapterBox), false));
            }
            Adapter->Unload();
        }
        WorldPartition->ReleaseEditorLoaderAdapter(EditorAdapter);
        ++ReleasedRegionCount;
    }
    int32 ChangedCells = 0;
    TArray<TSharedPtr<FJsonValue>> CellEntries;
    for (const UWorldPartitionRuntimeCell* Cell : Cells)
    {
        const FBox CellBounds = Cell->GetCellBounds();
        bool bWasReleased = false;
        for (const TPair<FBox, bool>& ReleasedAdapterBox : ReleasedAdapterBoxes)
        {
            if (CellBounds.IsValid && CellBounds.Intersect(ReleasedAdapterBox.Key))
            {
                bWasReleased = true;
                break;
            }
        }
        if (bWasReleased)
        {
            ++ChangedCells;
        }
        TSharedPtr<FJsonObject> Entry = McpHandlerUtils::CreateResultObject();
        Entry->SetStringField(TEXT("cellId"), Cell->GetDebugName());
        Entry->SetBoolField(TEXT("unloaded"), true);
        Entry->SetBoolField(TEXT("changed"), bWasReleased);
        CellEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("changed"), Cells.Num() > 0 ? ChangedCells : ReleasedRegionCount);
    Result->SetNumberField(TEXT("releasedRegionCount"), ReleasedRegionCount);
    Result->SetArrayField(TEXT("cells"), CellEntries);
    Result->SetStringField(TEXT("method"), TEXT("LoaderAdapter"));
    Result->SetStringField(TEXT("note"), TEXT("unload_cells unloads MCP-created regions (load_cells / pin_wp_cells). Unloading is asynchronous."));
    Subsystem->SendAutomationResponse(Socket, RequestId, true, TEXT("Unloaded MCP World Partition regions covering the requested cells."), Result);
#else
    SendWpCellsUnsupported(Subsystem, Socket, RequestId, TEXT("unload_cells"));
#endif
    return true;
}

} // namespace WorldPartition
} // namespace McpHandlers
#endif // WITH_EDITOR

// =============================================================================
// Handler Implementation
// =============================================================================

bool UMcpAutomationBridgeSubsystem::HandleWorldPartitionAction(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    // Validate action
    if (Action != TEXT("manage_world_partition"))
    {
        return false;
    }

#if WITH_EDITOR
    // Validate payload
    if (!Payload.IsValid())
    {
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("Missing payload."), TEXT("INVALID_PAYLOAD"));
        return true;
    }

    // -------------------------------------------------------------------------
    // Load target level if specified
    // -------------------------------------------------------------------------
    FString LevelPath = GetJsonStringField(Payload, TEXT("levelPath"));
    if (LevelPath.IsEmpty())
    {
        // Newer subactions accept "world" as an alias for the target map
        LevelPath = GetJsonStringField(Payload, TEXT("world"));
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!LevelPath.IsEmpty())
    {
        // Normalize the level path
        FString NormalizedLevelPath = LevelPath;
        if (!NormalizedLevelPath.StartsWith(TEXT("/Game/")) &&
            !NormalizedLevelPath.StartsWith(TEXT("/Engine/")))
        {
            NormalizedLevelPath = TEXT("/Game/") + NormalizedLevelPath;
        }

        // Check if we need to load a different level
        if (World)
        {
            FString CurrentWorldPath = World->GetOutermost()->GetName();
            if (!CurrentWorldPath.Equals(NormalizedLevelPath, ESearchCase::IgnoreCase))
            {
                UE_LOG(LogMcpAutomationBridgeSubsystem, Log,
                    TEXT("HandleWorldPartitionAction: Loading level %s (current: %s)"),
                    *NormalizedLevelPath, *CurrentWorldPath);

                FString Filename;
                if (FPackageName::TryConvertLongPackageNameToFilename(
                    NormalizedLevelPath, Filename, FPackageName::GetMapPackageExtension()))
                {
                    // CRITICAL FIX: Validate file exists BEFORE attempting load
                    // McpSafeLoadMap silently creates empty worlds for non-existent paths
                    // which causes confusing NOT_PARTITIONED errors instead of clear file-not-found
                    FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
                    if (!IFileManager::Get().FileExists(*FullPath))
                    {
                        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Level file not found: %s"), *FullPath),
                            TEXT("LEVEL_NOT_FOUND"));
                        return true;
                    }
                    FlushRenderingCommands();
                    if (!McpSafeLoadMap(NormalizedLevelPath))
                    {
                        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Failed to load level: %s"), *NormalizedLevelPath),
                            TEXT("LOAD_FAILED"));
                        return true;
                    }
                    World = GEditor->GetEditorWorldContext().World();
                }
                else
                {
                    SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Invalid level path: %s"), *NormalizedLevelPath),
                        TEXT("INVALID_PATH"));
                    return true;
                }
            }
        }
        else
        {
            // No current world - load the specified level
            FString Filename;
            if (FPackageName::TryConvertLongPackageNameToFilename(
                NormalizedLevelPath, Filename, FPackageName::GetMapPackageExtension()))
            {
                // CRITICAL FIX: Validate file exists BEFORE attempting load
                FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
                if (!IFileManager::Get().FileExists(*FullPath))
                {
                    SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Level file not found: %s"), *FullPath),
                        TEXT("LEVEL_NOT_FOUND"));
                    return true;
                }
                FlushRenderingCommands();
                if (!McpSafeLoadMap(NormalizedLevelPath))
                {
                    SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Failed to load level: %s"), *NormalizedLevelPath),
                        TEXT("LOAD_FAILED"));
                    return true;
                }
                World = GEditor->GetEditorWorldContext().World();
            }
            else
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Invalid level path: %s"), *NormalizedLevelPath),
                    TEXT("INVALID_PATH"));
                return true;
            }
        }
    }

    if (!World)
    {
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("No active editor world."), TEXT("NO_WORLD"));
        return true;
    }

    UWorldPartition* WorldPartition = World->GetWorldPartition();
    if (!WorldPartition)
    {
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("World is not partitioned."), TEXT("NOT_PARTITIONED"));
        return true;
    }

    // Extract subaction
    const FString SubAction = GetJsonStringField(Payload, TEXT("subAction"));

    // -------------------------------------------------------------------------
    // load_cells: Load cells/region in World Partition level
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("load_cells"))
    {
        // Default to reasonable area if no bounds provided
        FVector Origin = FVector::ZeroVector;
        FVector Extent = FVector(25000.0f, 25000.0f, 25000.0f);  // 500m box

        const TArray<TSharedPtr<FJsonValue>>* OriginArr;
        if (Payload->TryGetArrayField(TEXT("origin"), OriginArr) &&
            OriginArr && OriginArr->Num() >= 3)
        {
            Origin.X = (*OriginArr)[0]->AsNumber();
            Origin.Y = (*OriginArr)[1]->AsNumber();
            Origin.Z = (*OriginArr)[2]->AsNumber();
        }

        const TArray<TSharedPtr<FJsonValue>>* ExtentArr;
        if (Payload->TryGetArrayField(TEXT("extent"), ExtentArr) &&
            ExtentArr && ExtentArr->Num() >= 3)
        {
            Extent.X = (*ExtentArr)[0]->AsNumber();
            Extent.Y = (*ExtentArr)[1]->AsNumber();
            Extent.Z = (*ExtentArr)[2]->AsNumber();
        }

        FBox Bounds(Origin - Extent, Origin + Extent);

#if MCP_HAS_WP_EDITOR_SUBSYSTEM
        // UE 5.0-5.3: Use WorldPartitionEditorSubsystem
        UWorldPartitionEditorSubsystem* WPEditorSubsystem =
            GEditor->GetEditorSubsystem<UWorldPartitionEditorSubsystem>();
        if (WPEditorSubsystem)
        {
            WPEditorSubsystem->LoadRegion(Bounds);

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("action"), TEXT("manage_world_partition"));
            Result->SetStringField(TEXT("subAction"), TEXT("load_cells"));
            Result->SetStringField(TEXT("method"), TEXT("EditorSubsystem"));
            Result->SetBoolField(TEXT("requested"), true);
            McpHandlers::WorldPartition::AddResolvedWpCells(WorldPartition, Bounds, Result);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Region load requested."), Result);
            return true;
        }
#endif

#if MCP_HAS_WP_LOADER_ADAPTER
        // UE 5.4+: Use LoaderAdapter
        UWorldPartitionEditorLoaderAdapter* EditorLoaderAdapter =
            WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(
                World, Bounds, TEXT("MCP Loaded Region"));
        if (EditorLoaderAdapter && EditorLoaderAdapter->GetLoaderAdapter())
        {
            EditorLoaderAdapter->GetLoaderAdapter()->SetUserCreated(true);
            EditorLoaderAdapter->GetLoaderAdapter()->Load();

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("action"), TEXT("manage_world_partition"));
            Result->SetStringField(TEXT("subAction"), TEXT("load_cells"));
            Result->SetStringField(TEXT("method"), TEXT("LoaderAdapter"));
            Result->SetBoolField(TEXT("requested"), true);
            McpHandlers::WorldPartition::AddResolvedWpCells(WorldPartition, Bounds, Result);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Region load requested via LoaderAdapter."), Result);
            return true;
        }
#endif

        SendAutomationError(RequestingSocket, RequestId,
            TEXT("WorldPartition region loading not supported in this engine version."),
            TEXT("NOT_SUPPORTED"));
        return true;
    }

    // -------------------------------------------------------------------------
    // create_datalayer: Create new Data Layer asset
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("create_datalayer"))
    {
#if MCP_HAS_DATALAYER_EDITOR
        FString DataLayerName = GetJsonStringField(Payload, TEXT("dataLayerName"));

        if (DataLayerName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Missing dataLayerName."), TEXT("INVALID_PARAMS"));
            return true;
        }

        UDataLayerEditorSubsystem* DataLayerSubsystem =
            GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
        if (!DataLayerSubsystem)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("DataLayerEditorSubsystem not found."), TEXT("SUBSYSTEM_NOT_FOUND"));
            return true;
        }

        // Check existence
        bool bExists = false;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
        // UE 5.3+: Use UDataLayerManager
        if (UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager())
        {
            DataLayerManager->ForEachDataLayerInstance([&](UDataLayerInstance* LayerInstance) {
                if (LayerInstance->GetDataLayerShortName() == DataLayerName ||
                    LayerInstance->GetDataLayerFullName() == DataLayerName)
                {
                    bExists = true;
                    return false;
                }
                return true;
            });
        }
#else
        // UE 5.1-5.2: Use UDataLayerSubsystem
        if (UDataLayerSubsystem* DataLayerSubsys = World->GetSubsystem<UDataLayerSubsystem>())
        {
            TArray<UDataLayerInstance*> ExistingLayers =
                DataLayerSubsys->GetActorEditorContextDataLayers();
            for (UDataLayerInstance* LayerInstance : ExistingLayers)
            {
                if (LayerInstance &&
                    (LayerInstance->GetDataLayerShortName() == DataLayerName ||
                     LayerInstance->GetDataLayerFullName() == DataLayerName))
                {
                    bExists = true;
                    break;
                }
            }
        }
#endif

        if (bExists)
        {
            SendAutomationResponse(RequestingSocket, RequestId, true,
                FString::Printf(TEXT("DataLayer '%s' already exists."), *DataLayerName));
            return true;
        }

        // Create the Data Layer asset in a real package (not transient) so it persists
        FString AssetPath = GetJsonStringField(Payload, TEXT("assetPath"), TEXT(""));
        if (AssetPath.IsEmpty())
        {
            FString SanitizedName;
            SanitizedName.Reserve(DataLayerName.Len());
            for (const TCHAR Char : DataLayerName)
            {
                SanitizedName.AppendChar((FChar::IsAlnum(Char) || Char == TEXT('_')) ? Char : TEXT('_'));
            }
            if (SanitizedName.IsEmpty())
            {
                SanitizedName = TEXT("DataLayer");
            }
            AssetPath = FString::Printf(TEXT("/Game/__MCPDataLayers__/DL_%s"), *SanitizedName);
        }
        else
        {
            const FString SafeAssetPath = SanitizeProjectRelativePath(AssetPath);
            if (SafeAssetPath.IsEmpty() || !SafeAssetPath.StartsWith(TEXT("/Game/")))
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Invalid assetPath (must be under /Game): %s"), *AssetPath),
                    TEXT("INVALID_PATH"));
                return true;
            }
            AssetPath = SafeAssetPath;
        }
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid DataLayer asset package path: %s"), *AssetPath),
                TEXT("INVALID_PATH"));
            return true;
        }

        UPackage* AssetPackage = CreatePackage(*AssetPath);
        if (!AssetPackage)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to create package for DataLayer asset: %s"), *AssetPath),
                TEXT("PACKAGE_ERROR"));
            return true;
        }
        const FString AssetName = FPaths::GetBaseFilename(AssetPath);
        UDataLayerAsset* NewAsset = FindObject<UDataLayerAsset>(AssetPackage, *AssetName);
        if (!NewAsset)
        {
            NewAsset = NewObject<UDataLayerAsset>(
                AssetPackage,
                UDataLayerAsset::StaticClass(),
                FName(*AssetName),
                RF_Public | RF_Standalone | RF_Transactional);
            if (NewAsset)
            {
                FAssetRegistryModule::AssetCreated(NewAsset);
            }
        }

        bool bSavedAsset = false;
        if (NewAsset)
        {
            bSavedAsset = McpSafeAssetSave(NewAsset);
        }

        UDataLayerInstance* NewLayer = nullptr;
        if (NewAsset && DataLayerSubsystem)
        {
            FDataLayerCreationParameters Params;
            Params.DataLayerAsset = NewAsset;
            NewLayer = DataLayerSubsystem->CreateDataLayerInstance(Params);
        }

        if (NewLayer && NewAsset)
        {
            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("dataLayerName"), DataLayerName);
            Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
            Result->SetBoolField(TEXT("saved"), bSavedAsset);
            SendAutomationResponse(RequestingSocket, RequestId, true,
                FString::Printf(TEXT("DataLayer '%s' created with asset at %s."), *DataLayerName, *NewAsset->GetPathName()),
                Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to create DataLayer (asset save: %s, subsystem returned null)."),
                    bSavedAsset ? TEXT("ok") : TEXT("failed")),
                TEXT("CREATE_FAILED"));
        }
#else
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("DataLayerEditorSubsystem not available."), TEXT("NOT_SUPPORTED"));
#endif
        return true;
    }

    // -------------------------------------------------------------------------
    // set_datalayer: Add actor to Data Layer
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("set_datalayer"))
    {
        FString ActorPath = GetJsonStringField(Payload, TEXT("actorPath"));
        FString DataLayerName = GetJsonStringField(Payload, TEXT("dataLayerName"));

#if MCP_HAS_DATALAYER_EDITOR
        // CRITICAL: Use TActorIterator to find actors in World Partition levels
        // FindObject and GetAllLevelActors don't reliably find actors in WP packages
        AActor* Actor = nullptr;

        // First try FindObject with the path
        Actor = FindObject<AActor>(nullptr, *ActorPath);

        // If not found, use TActorIterator to search by label/name
        if (!Actor && World)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->GetActorLabel().Equals(ActorPath, ESearchCase::IgnoreCase) ||
                    It->GetName().Equals(ActorPath, ESearchCase::IgnoreCase))
                {
                    Actor = *It;
                    break;
                }
            }
        }

        if (!Actor)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Actor not found: %s"), *ActorPath),
                TEXT("ACTOR_NOT_FOUND"));
            return true;
        }

        UDataLayerEditorSubsystem* DataLayerSubsystem =
            GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
        if (!DataLayerSubsystem)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("DataLayerEditorSubsystem not found."), TEXT("SUBSYSTEM_NOT_FOUND"));
            return true;
        }

        UDataLayerInstance* TargetLayer = nullptr;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
        // UE 5.3+: Use UDataLayerManager
        if (UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager())
        {
            DataLayerManager->ForEachDataLayerInstance([&](UDataLayerInstance* LayerInstance) {
                if (LayerInstance->GetDataLayerShortName() == DataLayerName ||
                    LayerInstance->GetDataLayerFullName() == DataLayerName)
                {
                    TargetLayer = LayerInstance;
                    return false;
                }
                return true;
            });
        }
#else
        // UE 5.1-5.2: Use UDataLayerSubsystem
        if (UDataLayerSubsystem* DataLayerSubsys = World->GetSubsystem<UDataLayerSubsystem>())
        {
            TArray<UDataLayerInstance*> ExistingLayers =
                DataLayerSubsys->GetActorEditorContextDataLayers();
            for (UDataLayerInstance* LayerInstance : ExistingLayers)
            {
                if (LayerInstance &&
                    (LayerInstance->GetDataLayerShortName() == DataLayerName ||
                     LayerInstance->GetDataLayerFullName() == DataLayerName))
                {
                    TargetLayer = LayerInstance;
                    break;
                }
            }
        }
#endif

        if (TargetLayer)
        {
            TArray<AActor*> Actors;
            Actors.Add(Actor);
            TArray<UDataLayerInstance*> Layers;
            Layers.Add(TargetLayer);

            DataLayerSubsystem->AddActorsToDataLayers(Actors, Layers);

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("dataLayerName"), DataLayerName);
            Result->SetBoolField(TEXT("added"), true);
            McpHandlerUtils::AddVerification(Result, Actor);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Actor added to DataLayer."), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("DataLayer '%s' not found."), *DataLayerName),
                TEXT("DATALAYER_NOT_FOUND"));
        }
#else
        // DataLayerEditorSubsystem unavailable: report failure instead of simulating success
        UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
            TEXT("DataLayerEditorSubsystem not available. set_datalayer rejected."));

        SendAutomationError(RequestingSocket, RequestId,
            TEXT("DataLayerEditorSubsystem not available; set_datalayer is not supported in this engine configuration."),
            TEXT("NOT_SUPPORTED"));
#endif
        return true;
    }

    // -------------------------------------------------------------------------
    // cleanup_invalid_datalayers: Remove invalid Data Layer instances
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("cleanup_invalid_datalayers"))
    {
#if MCP_HAS_DATALAYER_EDITOR
        UDataLayerEditorSubsystem* DataLayerSubsystem =
            GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
        if (!DataLayerSubsystem)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("DataLayerEditorSubsystem not found."), TEXT("SUBSYSTEM_NOT_FOUND"));
            return true;
        }

        TArray<UDataLayerInstance*> InvalidInstances;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
        // UE 5.3+: Use UDataLayerManager
        UDataLayerManager* DataLayerManager =
            WorldPartition ? WorldPartition->GetDataLayerManager() : nullptr;
        if (!DataLayerManager)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("DataLayerManager not found."), TEXT("MANAGER_NOT_FOUND"));
            return true;
        }

        DataLayerManager->ForEachDataLayerInstance([&](UDataLayerInstance* LayerInstance) {
            if (LayerInstance && !LayerInstance->GetAsset())
            {
                InvalidInstances.Add(LayerInstance);
            }
            return true;
        });
#else
        // UE 5.1-5.2: Use UDataLayerSubsystem
        UDataLayerSubsystem* DataLayerSubsys =
            World ? World->GetSubsystem<UDataLayerSubsystem>() : nullptr;
        if (!DataLayerSubsys)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("DataLayerSubsystem not found."), TEXT("SUBSYSTEM_NOT_FOUND"));
            return true;
        }

        TArray<UDataLayerInstance*> ExistingLayers =
            DataLayerSubsys->GetActorEditorContextDataLayers();
        for (UDataLayerInstance* LayerInstance : ExistingLayers)
        {
            UDataLayerInstanceWithAsset* LayerWithAsset =
                Cast<UDataLayerInstanceWithAsset>(LayerInstance);
            if (LayerInstance && !LayerWithAsset)
            {
                InvalidInstances.Add(LayerInstance);
            }
        }
#endif

        int32 DeletedCount = 0;
        for (UDataLayerInstance* InvalidInstance : InvalidInstances)
        {
            DataLayerSubsystem->DeleteDataLayer(InvalidInstance);
            DeletedCount++;
        }

        SendAutomationResponse(RequestingSocket, RequestId, true,
            FString::Printf(TEXT("Cleaned up %d invalid Data Layer Instances."), DeletedCount));
#else
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("DataLayerEditorSubsystem not available."), TEXT("NOT_SUPPORTED"));
#endif
        return true;
    }

    // -------------------------------------------------------------------------
    // World Partition cell status / pin / unpin / unload (UE 5.4+ loader adapters)
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("get_wp_cell_status"))
    {
        return McpHandlers::WorldPartition::HandleWorldPartitionGetCellStatus(
            this, World, WorldPartition, RequestId, Payload, RequestingSocket);
    }
    if (SubAction == TEXT("pin_wp_cells"))
    {
        return McpHandlers::WorldPartition::HandleWorldPartitionPinCells(
            this, World, WorldPartition, RequestId, Payload, RequestingSocket);
    }
    if (SubAction == TEXT("unpin_wp_cells"))
    {
        return McpHandlers::WorldPartition::HandleWorldPartitionUnpinCells(
            this, World, WorldPartition, RequestId, Payload, RequestingSocket);
    }
    if (SubAction == TEXT("unload_cells"))
    {
        return McpHandlers::WorldPartition::HandleWorldPartitionUnloadCells(
            this, World, WorldPartition, RequestId, Payload, RequestingSocket);
    }

    return true;

#else
    // Non-editor build
    SendAutomationResponse(RequestingSocket, RequestId, false,
        TEXT("World Partition support disabled (non-editor build)"),
        nullptr, TEXT("NOT_IMPLEMENTED"));
    return true;
#endif
}
