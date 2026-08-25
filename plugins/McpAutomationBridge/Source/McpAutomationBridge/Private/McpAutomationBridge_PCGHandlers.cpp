#include "McpVersionCompatibility.h"
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpHandlerUtils.h"
#include "MCP/McpConsolidatedActionRouting.h"

#include <initializer_list>

#ifndef MCP_HAS_PCG
#define MCP_HAS_PCG 0
#endif

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EditorAssetLibrary.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#endif

#if WITH_EDITOR && MCP_HAS_PCG
#include "PCGCommon.h"
#include "PCGComponent.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "PCGWorldActor.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGSpawnActor.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Helpers/PCGHelpers.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Misc/Crc.h"
#if __has_include("ScopedTransaction.h")
#include "ScopedTransaction.h"
#elif __has_include("Editor/ScopedTransaction.h")
#include "Editor/ScopedTransaction.h"
#elif __has_include("Misc/ScopedTransaction.h")
#include "Misc/ScopedTransaction.h"
#endif
#endif

#if WITH_EDITOR && MCP_HAS_PCG
namespace
{
FString NormalizePCGSubAction(const TSharedPtr<FJsonObject>& Payload)
{
    return McpConsolidatedActions::GetPayloadSubAction(Payload);
}

FString GetFirstStringField(const TSharedPtr<FJsonObject>& Payload, std::initializer_list<const TCHAR*> Fields)
{
    if (!Payload.IsValid())
    {
        return FString();
    }

    for (const TCHAR* Field : Fields)
    {
        FString Value;
        if (Payload->TryGetStringField(Field, Value) && !Value.IsEmpty())
        {
            return Value;
        }
    }

    return FString();
}

struct FPCGSettingsAlias
{
    const TCHAR* Alias;
    const TCHAR* SettingsClass;
};

const FPCGSettingsAlias* FindPCGSettingsAlias(const FString& RawAlias)
{
    static const FPCGSettingsAlias Aliases[] = {
        {TEXT("add_landscape_data_node"), TEXT("PCGGetLandscapeSettings")},
        {TEXT("landscape_data"), TEXT("PCGGetLandscapeSettings")},
        {TEXT("add_spline_data_node"), TEXT("PCGGetSplineSettings")},
        {TEXT("spline_data"), TEXT("PCGGetSplineSettings")},
        {TEXT("add_volume_data_node"), TEXT("PCGGetVolumeSettings")},
        {TEXT("volume_data"), TEXT("PCGGetVolumeSettings")},
        {TEXT("add_actor_data_node"), TEXT("PCGDataFromActorSettings")},
        {TEXT("actor_data"), TEXT("PCGDataFromActorSettings")},
        {TEXT("add_texture_data_node"), TEXT("PCGTextureSamplerSettings")},
        {TEXT("texture_data"), TEXT("PCGTextureSamplerSettings")},
        {TEXT("add_surface_sampler"), TEXT("PCGSurfaceSamplerSettings")},
        {TEXT("surface_sampler"), TEXT("PCGSurfaceSamplerSettings")},
        {TEXT("add_mesh_sampler"), TEXT("PCGPointFromMeshSettings")},
        {TEXT("mesh_sampler"), TEXT("PCGPointFromMeshSettings")},
        {TEXT("add_spline_sampler"), TEXT("PCGSplineSamplerSettings")},
        {TEXT("spline_sampler"), TEXT("PCGSplineSamplerSettings")},
        {TEXT("add_volume_sampler"), TEXT("PCGVolumeSamplerSettings")},
        {TEXT("volume_sampler"), TEXT("PCGVolumeSamplerSettings")},
        {TEXT("add_bounds_modifier"), TEXT("PCGBoundsModifierSettings")},
        {TEXT("bounds_modifier"), TEXT("PCGBoundsModifierSettings")},
        {TEXT("add_density_filter"), TEXT("PCGDensityFilterSettings")},
        {TEXT("density_filter"), TEXT("PCGDensityFilterSettings")},
        {TEXT("add_height_filter"), TEXT("PCGAttributeFilteringRangeSettings")},
        {TEXT("height_filter"), TEXT("PCGAttributeFilteringRangeSettings")},
        {TEXT("add_slope_filter"), TEXT("PCGNormalToDensitySettings")},
        {TEXT("slope_filter"), TEXT("PCGNormalToDensitySettings")},
        {TEXT("add_distance_filter"), TEXT("PCGDistanceSettings")},
        {TEXT("distance_filter"), TEXT("PCGDistanceSettings")},
        {TEXT("add_bounds_filter"), TEXT("PCGCullPointsOutsideActorBoundsSettings")},
        {TEXT("bounds_filter"), TEXT("PCGCullPointsOutsideActorBoundsSettings")},
        {TEXT("add_self_pruning"), TEXT("PCGSelfPruningSettings")},
        {TEXT("self_pruning"), TEXT("PCGSelfPruningSettings")},
        {TEXT("add_transform_points"), TEXT("PCGTransformPointsSettings")},
        {TEXT("transform_points"), TEXT("PCGTransformPointsSettings")},
        {TEXT("add_project_to_surface"), TEXT("PCGProjectionSettings")},
        {TEXT("project_to_surface"), TEXT("PCGProjectionSettings")},
        {TEXT("add_copy_points"), TEXT("PCGCopyPointsSettings")},
        {TEXT("copy_points"), TEXT("PCGCopyPointsSettings")},
        {TEXT("add_merge_points"), TEXT("PCGMergeSettings")},
        {TEXT("merge_points"), TEXT("PCGMergeSettings")},
        {TEXT("add_static_mesh_spawner"), TEXT("PCGStaticMeshSpawnerSettings")},
        {TEXT("static_mesh_spawner"), TEXT("PCGStaticMeshSpawnerSettings")},
        {TEXT("add_actor_spawner"), TEXT("PCGSpawnActorSettings")},
        {TEXT("actor_spawner"), TEXT("PCGSpawnActorSettings")},
        {TEXT("add_spline_spawner"), TEXT("PCGSpawnSplineSettings")},
        {TEXT("spline_spawner"), TEXT("PCGSpawnSplineSettings")}
    };

    FString Normalized = RawAlias.TrimStartAndEnd().ToLower();
    Normalized.ReplaceInline(TEXT("-"), TEXT("_"));
    Normalized.ReplaceInline(TEXT(" "), TEXT("_"));
    for (const FPCGSettingsAlias& Alias : Aliases)
    {
        if (Normalized.Equals(Alias.Alias, ESearchCase::IgnoreCase))
        {
            return &Alias;
        }
    }
    return nullptr;
}

bool IsPCGNodeCreationAction(const FString& SubAction)
{
    return SubAction.StartsWith(TEXT("add_"), ESearchCase::IgnoreCase) && FindPCGSettingsAlias(SubAction) != nullptr;
}

bool TryGetPCGAssetPath(const TSharedPtr<FJsonObject>& Payload, std::initializer_list<const TCHAR*> DirectFields, FString& OutPath, FString& OutError)
{
    OutPath = GetFirstStringField(Payload, DirectFields);
    if (OutPath.IsEmpty())
    {
        const FString Directory = GetJsonStringField(Payload, TEXT("path"), TEXT("/Game/PCG"));
        const FString Name = GetJsonStringField(Payload, TEXT("name"));
        if (!Name.IsEmpty())
        {
            OutPath = Directory / Name;
        }
    }

    if (OutPath.IsEmpty())
    {
        OutError = TEXT("Missing PCG asset path. Provide graphPath, subgraphPath, assetPath, or path + name.");
        return false;
    }

    FNormalizedAssetPath Normalized = NormalizeAssetPath(OutPath);
    if (!Normalized.bIsValid)
    {
        OutError = Normalized.ErrorMessage;
        return false;
    }

    OutPath = Normalized.Path;
    return true;
}

FString ToObjectPath(const FString& PackagePath)
{
    return FString::Printf(TEXT("%s.%s"), *PackagePath, *FPackageName::GetShortName(PackagePath));
}

UPCGGraph* LoadPCGGraph(const FString& RawPath, FString& OutPath, FString& OutError)
{
    FNormalizedAssetPath Normalized = NormalizeAssetPath(RawPath);
    if (!Normalized.bIsValid)
    {
        OutError = Normalized.ErrorMessage;
        return nullptr;
    }

    OutPath = Normalized.Path;
    UObject* Loaded = UEditorAssetLibrary::LoadAsset(OutPath);
    if (!Loaded)
    {
        Loaded = StaticLoadObject(UPCGGraph::StaticClass(), nullptr, *ToObjectPath(OutPath));
    }

    UPCGGraph* Graph = Cast<UPCGGraph>(Loaded);
    if (!Graph)
    {
        OutError = FString::Printf(TEXT("Could not load PCG graph at '%s'."), *OutPath);
    }

    return Graph;
}

UPCGGraph* CreateOrReusePCGGraph(const FString& GraphPath, bool bOverwrite, bool bSave, bool& bOutCreated, bool& bOutSaved, FString& OutError)
{
    bOutCreated = false;
    bOutSaved = false;

    if (UEditorAssetLibrary::DoesAssetExist(GraphPath))
    {
        FString LoadedPath;
        UPCGGraph* Existing = LoadPCGGraph(GraphPath, LoadedPath, OutError);
        if (!Existing)
        {
            return nullptr;
        }
        if (!bOverwrite)
        {
            OutError = FString::Printf(TEXT("PCG graph already exists at '%s'."), *GraphPath);
            return nullptr;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Overwrite PCG Graph")));
        Existing->Modify();
        TArray<UPCGNode*> NodesToRemove;
        for (UPCGNode* Node : Existing->GetNodes())
        {
            if (Node && Node != Existing->GetInputNode() && Node != Existing->GetOutputNode())
            {
                NodesToRemove.Add(Node);
            }
        }
        if (NodesToRemove.Num() > 0)
        {
            Existing->RemoveNodes(NodesToRemove);
            Existing->MarkPackageDirty();
        }
        if (bSave && !McpSafeAssetSave(Existing))
        {
            OutError = FString::Printf(TEXT("Failed to save overwritten PCG graph '%s'."), *GraphPath);
            return nullptr;
        }
        return Existing;
    }

    const FString AssetName = FPackageName::GetShortName(GraphPath);
    UPackage* Package = CreatePackage(*GraphPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package '%s'."), *GraphPath);
        return nullptr;
    }

    UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!Graph)
    {
        OutError = FString::Printf(TEXT("Failed to create PCG graph '%s'."), *GraphPath);
        return nullptr;
    }

    Graph->MarkPackageDirty();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Graph);
    bOutCreated = true;

    if (bSave)
    {
        bOutSaved = McpSafeAssetSave(Graph);
        if (!bOutSaved)
        {
            OutError = FString::Printf(TEXT("Created PCG graph '%s' but failed to save it."), *GraphPath);
            return nullptr;
        }
    }

    return Graph;
}

TSharedPtr<FJsonObject> BuildGraphResult(UPCGGraph* Graph, const FString& GraphPath, bool bCreated, bool bSaved)
{
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("graphPath"), GraphPath);
    Result->SetStringField(TEXT("assetPath"), GraphPath);
    Result->SetStringField(TEXT("name"), FPackageName::GetShortName(GraphPath));
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetBoolField(TEXT("saved"), bSaved);
    McpHandlerUtils::AddVerification(Result, Graph);
    return Result;
}

FString GetNodeTitleString(UPCGNode* Node)
{
    if (!Node)
    {
        return FString();
    }
    if (Node->NodeTitle != NAME_None)
    {
        return Node->NodeTitle.ToString();
    }
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    return Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
#else
    return Node->GetNodeTitle().ToString();
#endif
}

TSharedPtr<FJsonObject> BuildNodeResult(UPCGGraph* Graph, UPCGNode* Node, const FString& GraphPath)
{
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("graphPath"), GraphPath);
    TArray<TSharedPtr<FJsonValue>> ConnectionValues;
    if (Graph)
    {
        for (UPCGNode* SourceNode : Graph->GetNodes())
        {
            if (!SourceNode)
            {
                continue;
            }
            for (const TObjectPtr<UPCGPin>& SourcePin : SourceNode->GetOutputPins())
            {
                if (!SourcePin)
                {
                    continue;
                }
                for (const TObjectPtr<UPCGEdge>& Edge : SourcePin->Edges)
                {
                    UPCGPin* TargetPin = Edge ? Edge->GetOtherPin(SourcePin.Get()) : nullptr;
                    if (!TargetPin || !TargetPin->Node)
                    {
                        continue;
                    }
                    TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
                    Connection->SetStringField(TEXT("sourceNodeId"), SourceNode->GetName());
                    Connection->SetStringField(TEXT("sourcePin"), SourcePin->Properties.Label.ToString());
                    Connection->SetStringField(TEXT("targetNodeId"), TargetPin->Node->GetName());
                    Connection->SetStringField(TEXT("targetPin"), TargetPin->Properties.Label.ToString());
                    ConnectionValues.Add(MakeShared<FJsonValueObject>(Connection));
                }
            }
        }
    }
    Result->SetArrayField(TEXT("connections"), ConnectionValues);
    Result->SetNumberField(TEXT("connectionCount"), ConnectionValues.Num());
    if (Node)
    {
        Result->SetStringField(TEXT("nodeId"), Node->GetName());
        Result->SetStringField(TEXT("nodeName"), Node->GetName());
        Result->SetStringField(TEXT("title"), GetNodeTitleString(Node));
        if (UPCGSettings* Settings = Node->GetSettings())
        {
            Result->SetStringField(TEXT("nodeType"), Settings->GetClass()->GetName());
        }
        McpHandlerUtils::AddVerification(Result, Node);
    }
    else
    {
        McpHandlerUtils::AddVerification(Result, Graph);
    }
    return Result;
}

UPCGNode* FindPCGNode(UPCGGraph* Graph, const FString& NodeId)
{
    if (!Graph || NodeId.IsEmpty())
    {
        return nullptr;
    }

    const FString Needle = NodeId.TrimStartAndEnd();
    if (Needle.Equals(TEXT("input"), ESearchCase::IgnoreCase) || Needle.Equals(TEXT("input_node"), ESearchCase::IgnoreCase))
    {
        return Graph->GetInputNode();
    }
    if (Needle.Equals(TEXT("output"), ESearchCase::IgnoreCase) || Needle.Equals(TEXT("output_node"), ESearchCase::IgnoreCase))
    {
        return Graph->GetOutputNode();
    }
    if (Needle.IsNumeric())
    {
        const int32 Index = FCString::Atoi(*Needle);
        const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
        if (Index >= 0 && Index < Nodes.Num())
        {
            return Nodes[Index];
        }
    }

    const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
    for (UPCGNode* Node : Nodes)
    {
        if (!Node)
        {
            continue;
        }
        if (Node->GetName().Equals(Needle, ESearchCase::IgnoreCase) || Node->GetPathName().Equals(Needle, ESearchCase::IgnoreCase))
        {
            return Node;
        }
        if (Node->NodeTitle != NAME_None && Node->NodeTitle.ToString().Equals(Needle, ESearchCase::IgnoreCase))
        {
            return Node;
        }
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
        if (Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString().Equals(Needle, ESearchCase::IgnoreCase) ||
            Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString().Equals(Needle, ESearchCase::IgnoreCase))
#else
        if (Node->GetNodeTitle().ToString().Equals(Needle, ESearchCase::IgnoreCase))
#endif
        {
            return Node;
        }
    }

    return nullptr;
}

FString DescribePCGPinLabels(const TArray<TObjectPtr<UPCGPin>>& Pins)
{
    TArray<FString> Labels;
    for (const TObjectPtr<UPCGPin>& Pin : Pins)
    {
        if (Pin)
        {
            Labels.Add(Pin->Properties.Label.ToString());
        }
    }

    return Labels.IsEmpty() ? TEXT("<none>") : FString::Join(Labels, TEXT(", "));
}

UPCGPin* GetPCGPinByLabel(UPCGNode* Node, bool bOutputPin, const FName& Label)
{
    if (!Node)
    {
        return nullptr;
    }

    return bOutputPin ? Node->GetOutputPin(Label) : Node->GetInputPin(Label);
}

bool TryResolvePCGPinLabel(UPCGNode* Node, bool bOutputPin, const FString& RequestedPinLabel, FName& OutPinLabel, FString& OutError)
{
    if (!Node)
    {
        OutError = TEXT("PCG node is invalid.");
        return false;
    }

    const TCHAR* PinKind = bOutputPin ? TEXT("output") : TEXT("input");
    const TArray<TObjectPtr<UPCGPin>>& Pins = bOutputPin ? Node->GetOutputPins() : Node->GetInputPins();
    if (!RequestedPinLabel.IsEmpty())
    {
        const FName RequestedPin(*RequestedPinLabel);
        if (GetPCGPinByLabel(Node, bOutputPin, RequestedPin))
        {
            OutPinLabel = RequestedPin;
            return true;
        }

        OutError = FString::Printf(TEXT("PCG node '%s' has no %s pin '%s'. Available %s pins: %s."),
            *Node->GetName(), PinKind, *RequestedPinLabel, PinKind, *DescribePCGPinLabels(Pins));
        return false;
    }

    const FName PreferredPin = bOutputPin ? PCGPinConstants::DefaultOutputLabel : PCGPinConstants::DefaultInputLabel;
    if (GetPCGPinByLabel(Node, bOutputPin, PreferredPin))
    {
        OutPinLabel = PreferredPin;
        return true;
    }

    for (const TObjectPtr<UPCGPin>& Pin : Pins)
    {
        if (Pin)
        {
            OutPinLabel = Pin->Properties.Label;
            return true;
        }
    }

    OutError = FString::Printf(TEXT("PCG node '%s' has no %s pins."), *Node->GetName(), PinKind);
    return false;
}

bool HasPCGEdge(const UPCGPin* SourcePin, const UPCGPin* TargetPin)
{
    if (!SourcePin || !TargetPin)
    {
        return false;
    }

    for (const TObjectPtr<UPCGEdge>& Edge : SourcePin->Edges)
    {
        if (Edge && Edge->IsValid() && Edge->GetOtherPin(SourcePin) == TargetPin)
        {
            return true;
        }
    }

    return false;
}

UClass* ResolvePCGSettingsClass(const FString& RawClassName)
{
    if (RawClassName.IsEmpty())
    {
        return nullptr;
    }

    if (RawClassName.Equals(TEXT("subgraph"), ESearchCase::IgnoreCase) || RawClassName.Equals(TEXT("pcg_subgraph"), ESearchCase::IgnoreCase))
    {
        return UPCGSubgraphSettings::StaticClass();
    }

    TArray<FString> Candidates;
    const FString Trimmed = RawClassName.TrimStartAndEnd();
    Candidates.Add(Trimmed);

    if (const FPCGSettingsAlias* Alias = FindPCGSettingsAlias(Trimmed))
    {
        Candidates.Add(Alias->SettingsClass);
    }

    FString ShortName = Trimmed;
    int32 DotIndex = INDEX_NONE;
    if (ShortName.FindLastChar(TEXT('.'), DotIndex))
    {
        ShortName = ShortName.RightChop(DotIndex + 1);
    }
    if (ShortName.StartsWith(TEXT("U")) && ShortName.StartsWith(TEXT("UPCG")))
    {
        ShortName = ShortName.RightChop(1);
    }

    Candidates.Add(ShortName);
    if (!ShortName.EndsWith(TEXT("Settings")))
    {
        Candidates.Add(ShortName + TEXT("Settings"));
    }
    if (!ShortName.StartsWith(TEXT("PCG")))
    {
        Candidates.Add(TEXT("PCG") + ShortName);
        Candidates.Add(TEXT("PCG") + ShortName + TEXT("Settings"));
    }

    for (const FString& Candidate : Candidates)
    {
        if (UClass* Class = ResolveClassByName(Candidate))
        {
            if (Class->IsChildOf(UPCGSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
            {
                return Class;
            }
        }

        static const TCHAR* ScriptModules[] = {TEXT("PCG"), TEXT("PCGGeometryScriptInterop")};
        for (const TCHAR* ScriptModule : ScriptModules)
        {
            const FString ScriptPath = FString::Printf(TEXT("/Script/%s.%s"), ScriptModule, *Candidate);
            if (UClass* Class = FindObject<UClass>(nullptr, *ScriptPath))
            {
                if (Class->IsChildOf(UPCGSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
                {
                    return Class;
                }
            }
            if (UClass* Class = LoadObject<UClass>(nullptr, *ScriptPath))
            {
                if (Class->IsChildOf(UPCGSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
                {
                    return Class;
                }
            }
        }
    }

    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (Class && Class->IsChildOf(UPCGSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
        {
            for (const FString& Candidate : Candidates)
            {
                if (Class->GetName().Equals(Candidate, ESearchCase::IgnoreCase))
                {
                    return Class;
                }
            }
        }
    }

    return nullptr;
}

bool ApplySettingsObject(UPCGSettings* Settings, const TSharedPtr<FJsonObject>& SettingsObject, FString& OutError, int32& OutAppliedCount)
{
    OutAppliedCount = 0;
    if (!Settings || !SettingsObject.IsValid())
    {
        return true;
    }

    for (const auto& Pair : SettingsObject->Values)
    {
        const FString FieldName(Pair.Key.Len(), *Pair.Key);
        FProperty* Property = Settings->GetClass()->FindPropertyByName(FName(*FieldName));
        if (!Property)
        {
            OutError = FString::Printf(TEXT("PCG settings property '%s' was not found on '%s'."), *FieldName, *Settings->GetClass()->GetName());
            return false;
        }

        FString ApplyError;
        if (!ApplyJsonValueToProperty(Settings, Property, Pair.Value, ApplyError))
        {
            OutError = FString::Printf(TEXT("Failed to apply PCG settings property '%s': %s"), *FieldName, *ApplyError);
            return false;
        }
        ++OutAppliedCount;
    }

    return true;
}

bool ApplyStringSetting(UPCGSettings* Settings, const TCHAR* PropertyName, const FString& Value, FString& OutError)
{
    if (!Settings || Value.IsEmpty())
    {
        return true;
    }

    FProperty* Property = Settings->GetClass()->FindPropertyByName(FName(PropertyName));
    if (!Property)
    {
        OutError = FString::Printf(TEXT("PCG settings property '%s' was not found on '%s'."), PropertyName, *Settings->GetClass()->GetName());
        return false;
    }

    return ApplyJsonValueToProperty(Settings, Property, MakeShared<FJsonValueString>(Value), OutError);
}

bool ResolveClassForProperty(UObject* Target, const TCHAR* PropertyName, const FString& ClassName, UClass*& OutClass, FString& OutError)
{
    OutClass = nullptr;
    if (!Target || ClassName.IsEmpty())
    {
        return true;
    }

    FProperty* Property = Target->GetClass()->FindPropertyByName(FName(PropertyName));
    FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
    if (!ClassProperty)
    {
        OutError = FString::Printf(TEXT("Class property '%s' was not found on '%s'."), PropertyName, *Target->GetClass()->GetName());
        return false;
    }

    UClass* Class = ResolveClassByName(ClassName);
    if (!Class)
    {
        Class = LoadObject<UClass>(nullptr, *ClassName);
    }
    if (!Class && ClassName.StartsWith(TEXT("/Script/")))
    {
        Class = FindObject<UClass>(nullptr, *ClassName);
    }
    if (!Class)
    {
        OutError = FString::Printf(TEXT("Could not resolve class '%s'."), *ClassName);
        return false;
    }
    if (ClassProperty->MetaClass && !Class->IsChildOf(ClassProperty->MetaClass))
    {
        OutError = FString::Printf(TEXT("Class '%s' is not assignable to '%s'."), *Class->GetName(), *ClassProperty->MetaClass->GetName());
        return false;
    }

    OutClass = Class;
    return true;
}

bool ApplySpawnActorTemplateClass(UPCGSettings* Settings, const FString& ClassName, FString& OutError)
{
    if (ClassName.IsEmpty())
    {
        return true;
    }

    UPCGSpawnActorSettings* SpawnActorSettings = Cast<UPCGSpawnActorSettings>(Settings);
    if (!SpawnActorSettings)
    {
        OutError = FString::Printf(TEXT("PCG settings '%s' are not actor spawner settings."), Settings ? *Settings->GetClass()->GetName() : TEXT("<null>"));
        return false;
    }

    UClass* ActorClass = nullptr;
    if (!ResolveClassForProperty(SpawnActorSettings, TEXT("TemplateActorClass"), ClassName, ActorClass, OutError))
    {
        return false;
    }

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
    TSubclassOf<AActor> ActorSubclass = ActorClass;
    SpawnActorSettings->Modify();
    SpawnActorSettings->SetTemplateActorClass(ActorSubclass);
#else
    FClassProperty* ClassProperty = CastField<FClassProperty>(SpawnActorSettings->GetClass()->FindPropertyByName(FName(TEXT("TemplateActorClass"))));
    if (!ClassProperty)
    {
        OutError = FString::Printf(TEXT("Class property 'TemplateActorClass' was not found on '%s'."), *SpawnActorSettings->GetClass()->GetName());
        return false;
    }

    SpawnActorSettings->Modify();
    ClassProperty->SetPropertyValue_InContainer(SpawnActorSettings, ActorClass);
#endif
    return true;
}

bool IsPCGFallbackMeshPath(const FString& MeshPath)
{
    FString Normalized = MeshPath;
    Normalized.TrimStartAndEndInline();
    Normalized.ToLowerInline();
    if (Normalized.StartsWith(TEXT("/engine/basicshapes/")))
    {
        return true;
    }

    FString AssetName = Normalized;
    int32 DotIndex = INDEX_NONE;
    if (AssetName.FindLastChar(TEXT('.'), DotIndex))
    {
        AssetName.LeftInline(DotIndex);
    }
    int32 SlashIndex = INDEX_NONE;
    if (AssetName.FindLastChar(TEXT('/'), SlashIndex))
    {
        AssetName = AssetName.Mid(SlashIndex + 1);
    }

    static const TArray<FString> FallbackTokens = {
        TEXT("cube"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("plane"),
        TEXT("roundedcube"), TEXT("box"), TEXT("shape"), TEXT("default"), TEXT("fallback")
    };
    for (const FString& Token : FallbackTokens)
    {
        if (AssetName.Contains(Token))
        {
            return true;
        }
    }
    return false;
}

bool IsPCGEnvironmentMeshCandidate(const FString& MeshPath)
{
    FString SearchText = MeshPath;
    SearchText.ToLowerInline();
    static const TArray<FString> EnvironmentTerms = {
        TEXT("tree"), TEXT("foliage"), TEXT("plant"), TEXT("bush"), TEXT("shrub"),
        TEXT("forest"), TEXT("nature"), TEXT("grass"), TEXT("rock"), TEXT("stone"),
        TEXT("environment"), TEXT("branch"), TEXT("trunk"), TEXT("leaf"), TEXT("pine"),
        TEXT("oak"), TEXT("birch"), TEXT("palm"), TEXT("vegetation"), TEXT("volumetric"),
        TEXT("fog"), TEXT("sky"), TEXT("floor"), TEXT("landscape"), TEXT("terrain")
    };
    for (const FString& Term : EnvironmentTerms)
    {
        if (SearchText.Contains(Term))
        {
            return true;
        }
    }
    return false;
}

UStaticMesh* LoadPCGStaticMesh(const FString& RawMeshPath, FString& OutResolvedPath, FString& OutError, bool bAllowFallbackMesh);

TArray<FString> GetPCGStringArrayField(const TSharedPtr<FJsonObject>& Payload, std::initializer_list<const TCHAR*> Fields)
{
    TArray<FString> Values;
    if (!Payload.IsValid())
    {
        return Values;
    }

    for (const TCHAR* Field : Fields)
    {
        const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
        if (!Payload->TryGetArrayField(Field, JsonValues) || !JsonValues)
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& JsonValue : *JsonValues)
        {
            if (JsonValue && JsonValue->Type == EJson::String && !JsonValue->AsString().IsEmpty())
            {
                Values.Add(JsonValue->AsString());
            }
        }
        if (Values.Num() > 0)
        {
            break;
        }
    }
    return Values;
}

bool ValidatePCGStaticMeshPath(const FString& RawMeshPath, bool bAllowFallbackMesh, FString& OutResolvedPath, FString& OutError)
{
    if (!LoadPCGStaticMesh(RawMeshPath, OutResolvedPath, OutError, bAllowFallbackMesh))
    {
        return false;
    }

    UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(OutResolvedPath));
    if (!Mesh || Mesh->GetBounds().BoxExtent.IsNearlyZero())
    {
        OutError = FString::Printf(TEXT("Static Mesh '%s' has no usable geometry bounds."), *OutResolvedPath);
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> BuildPCGStaticMeshAssetResult(const FString& ResolvedPath, UStaticMesh* Mesh)
{
    TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
    AssetObject->SetStringField(TEXT("meshPath"), ResolvedPath);
    AssetObject->SetStringField(TEXT("assetName"), Mesh ? Mesh->GetName() : FString());
    AssetObject->SetBoolField(TEXT("fallback"), IsPCGFallbackMeshPath(ResolvedPath));
    AssetObject->SetBoolField(TEXT("environmentCandidate"), IsPCGEnvironmentMeshCandidate(ResolvedPath));
    if (Mesh)
    {
        const FVector Extent = Mesh->GetBounds().BoxExtent;
        TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
        Bounds->SetNumberField(TEXT("x"), Extent.X * 2.0);
        Bounds->SetNumberField(TEXT("y"), Extent.Y * 2.0);
        Bounds->SetNumberField(TEXT("z"), Extent.Z * 2.0);
        AssetObject->SetObjectField(TEXT("boundsSize"), Bounds);
    }
    return AssetObject;
}

UStaticMesh* LoadPCGStaticMesh(const FString& RawMeshPath, FString& OutResolvedPath, FString& OutError, bool bAllowFallbackMesh = false)
{
    FNormalizedAssetPath Normalized = NormalizeAssetPath(RawMeshPath);
    if (!Normalized.bIsValid)
    {
        OutError = Normalized.ErrorMessage;
        return nullptr;
    }

    OutResolvedPath = Normalized.Path;
    if (!bAllowFallbackMesh && IsPCGFallbackMeshPath(OutResolvedPath))
    {
        OutError = FString::Printf(TEXT("Static Mesh '%s' is a cube/fallback mesh. Pass allowFallbackMesh=true only when that fallback is explicitly requested."), *OutResolvedPath);
        return nullptr;
    }
    UObject* Loaded = UEditorAssetLibrary::LoadAsset(OutResolvedPath);
    if (!Loaded)
    {
        Loaded = StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *ToObjectPath(OutResolvedPath));
    }

    UStaticMesh* StaticMesh = Cast<UStaticMesh>(Loaded);
    if (!StaticMesh)
    {
        OutError = FString::Printf(TEXT("Could not load static mesh at '%s'."), *OutResolvedPath);
        return nullptr;
    }

    return StaticMesh;
}

bool ApplyStaticMeshSpawnerMeshPath(UPCGSettings* Settings, const FString& MeshPath, FString& OutError, bool bAllowFallbackMesh = false)
{
    if (MeshPath.IsEmpty())
    {
        return true;
    }

    UPCGStaticMeshSpawnerSettings* SpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(Settings);
    if (!SpawnerSettings)
    {
        OutError = FString::Printf(TEXT("PCG settings '%s' are not static mesh spawner settings."), Settings ? *Settings->GetClass()->GetName() : TEXT("<null>"));
        return false;
    }

    FString ResolvedMeshPath;
    UStaticMesh* StaticMesh = LoadPCGStaticMesh(MeshPath, ResolvedMeshPath, OutError, bAllowFallbackMesh);
    if (!StaticMesh)
    {
        return false;
    }

    SpawnerSettings->Modify();
    if (!SpawnerSettings->MeshSelectorParameters)
    {
        SpawnerSettings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
    }

    UObject* Selector = SpawnerSettings->MeshSelectorParameters;
    if (!Selector)
    {
        OutError = TEXT("PCG static mesh spawner did not create MeshSelectorParameters after selecting weighted.");
        return false;
    }

    FArrayProperty* EntriesProperty = FindFProperty<FArrayProperty>(Selector->GetClass(), TEXT("MeshEntries"));
    if (!EntriesProperty)
    {
        OutError = FString::Printf(TEXT("PCG mesh selector '%s' has no reflected property 'MeshEntries'; selector-specific entries are not supported."), *Selector->GetClass()->GetName());
        return false;
    }

    FScriptArrayHelper EntriesHelper(EntriesProperty, EntriesProperty->ContainerPtrToValuePtr<void>(Selector));
    EntriesHelper.EmptyValues();
    EntriesHelper.AddValue();
    void* EntryPtr = EntriesHelper.GetRawPtr(0);
    FStructProperty* EntryStructProperty = CastField<FStructProperty>(EntriesProperty->Inner);
    if (!EntryStructProperty || !EntryStructProperty->Struct)
    {
        OutError = FString::Printf(TEXT("Reflected property '%s.%s' is not an entry struct array."), *Selector->GetClass()->GetName(), *EntriesProperty->GetName());
        return false;
    }

    FStructProperty* DescriptorProperty = FindFProperty<FStructProperty>(EntryStructProperty->Struct, TEXT("Descriptor"));
    FSoftObjectProperty* MeshProperty = DescriptorProperty && DescriptorProperty->Struct
        ? FindFProperty<FSoftObjectProperty>(DescriptorProperty->Struct, TEXT("StaticMesh"))
        : nullptr;
    if (!DescriptorProperty || !MeshProperty)
    {
        OutError = FString::Printf(TEXT("Reflected mesh entry '%s' is missing Descriptor.StaticMesh soft-object property."), *EntryStructProperty->Struct->GetName());
        return false;
    }

    void* DescriptorPtr = DescriptorProperty->ContainerPtrToValuePtr<void>(EntryPtr);
    void* MeshPtr = MeshProperty->ContainerPtrToValuePtr<void>(DescriptorPtr);
    *static_cast<FSoftObjectPtr*>(MeshPtr) = FSoftObjectPath(StaticMesh->GetPathName());
    if (FProperty* WeightProperty = FindFProperty<FProperty>(EntryStructProperty->Struct, TEXT("Weight")))
    {
        ApplyJsonValueToProperty(EntryPtr, WeightProperty, MakeShared<FJsonValueNumber>(1.0), OutError);
    }
    return true;
}

FProperty* FindPCGProperty(UStruct* Struct, const TArray<FName>& Names)
{
    if (!Struct)
    {
        return nullptr;
    }
    for (const FName& Name : Names)
    {
        if (FProperty* Property = FindFProperty<FProperty>(Struct, Name))
        {
            return Property;
        }
    }
    return nullptr;
}

bool ResolvePCGMeshSelectorClass(const FString& RawType, UClass*& OutClass, FString& OutError)
{
    OutClass = nullptr;
    FString Type = RawType.TrimStartAndEnd();
    if (Type.IsEmpty())
    {
        OutError = TEXT("meshSelectorType must be a non-empty selector class or alias.");
        return false;
    }

    if (Type.Equals(TEXT("weighted"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("weighted_mesh"), ESearchCase::IgnoreCase))
    {
        OutClass = UPCGMeshSelectorWeighted::StaticClass();
    }
    else if (Type.Equals(TEXT("by_attribute"), ESearchCase::IgnoreCase))
    {
        OutClass = ResolveClassByName(TEXT("PCGMeshSelectorByAttribute"));
    }
    else if (Type.Equals(TEXT("weighted_by_category"), ESearchCase::IgnoreCase))
    {
        OutClass = ResolveClassByName(TEXT("PCGMeshSelectorWeightedByCategory"));
    }
    else
    {
        TArray<FString> Candidates;
        Candidates.Add(Type);
        if (Type.StartsWith(TEXT("U")) && Type.StartsWith(TEXT("UPCG")))
        {
            Candidates.Add(Type.RightChop(1));
        }
        if (!Type.StartsWith(TEXT("PCG")))
        {
            Candidates.Add(TEXT("PCG") + Type);
        }
        if (!Type.EndsWith(TEXT("Settings")) && !Type.EndsWith(TEXT("Selector")))
        {
            Candidates.Add(Type + TEXT("Selector"));
        }
        for (const FString& Candidate : Candidates)
        {
            if ((OutClass = ResolveClassByName(Candidate)) != nullptr)
            {
                break;
            }
            const FString ScriptPath = FString::Printf(TEXT("/Script/PCG.%s"), *Candidate);
            OutClass = FindObject<UClass>(nullptr, *ScriptPath);
            if (!OutClass)
            {
                OutClass = LoadObject<UClass>(nullptr, *ScriptPath);
            }
            if (OutClass)
            {
                break;
            }
        }
    }

    if (!OutClass || !OutClass->IsChildOf(UPCGMeshSelectorBase::StaticClass()) || OutClass->HasAnyClassFlags(CLASS_Abstract))
    {
        OutError = FString::Printf(TEXT("Could not resolve mesh selector '%s' as a concrete UPCGMeshSelectorBase."), *RawType);
        OutClass = nullptr;
        return false;
    }
    return true;
}

UObject* GetPCGMeshSelector(UPCGStaticMeshSpawnerSettings* Settings, FString& OutError)
{
    if (!Settings)
    {
        OutError = TEXT("PCG static mesh spawner settings are invalid.");
        return nullptr;
    }
    FObjectProperty* SelectorProperty = FindFProperty<FObjectProperty>(Settings->GetClass(), TEXT("MeshSelectorParameters"));
    if (!SelectorProperty)
    {
        OutError = FString::Printf(TEXT("Reflected property '%s.MeshSelectorParameters' was not found."), *Settings->GetClass()->GetName());
        return nullptr;
    }
    UObject* Selector = SelectorProperty->GetObjectPropertyValue_InContainer(Settings);
    if (!Selector)
    {
        OutError = FString::Printf(TEXT("Reflected property '%s.MeshSelectorParameters' is null."), *Settings->GetClass()->GetName());
        return nullptr;
    }
    return Selector;
}

FArrayProperty* GetPCGMeshEntriesProperty(UObject* Selector, FString& OutError)
{
    if (!Selector)
    {
        OutError = TEXT("PCG mesh selector is null.");
        return nullptr;
    }
    FArrayProperty* EntriesProperty = FindFProperty<FArrayProperty>(Selector->GetClass(), TEXT("MeshEntries"));
    if (!EntriesProperty)
    {
        OutError = FString::Printf(TEXT("Reflected property '%s.MeshEntries' was not found. Selector '%s' does not expose weighted mesh entries."), *Selector->GetClass()->GetName(), *Selector->GetClass()->GetName());
        return nullptr;
    }
    if (!CastField<FStructProperty>(EntriesProperty->Inner))
    {
        OutError = FString::Printf(TEXT("Reflected property '%s.MeshEntries' is not an array of structs."), *Selector->GetClass()->GetName());
        return nullptr;
    }
    return EntriesProperty;
}

bool ApplyPCGClassProperty(void* Container, FProperty* Property, const FString& RawClass, FString& OutError)
{
    FClassProperty* ClassProperty = CastField<FClassProperty>(Property);
    if (!ClassProperty)
    {
        OutError = FString::Printf(TEXT("Reflected property '%s' is not a class property."), Property ? *Property->GetName() : TEXT("<null>"));
        return false;
    }
    UClass* Class = ResolveClassByName(RawClass);
    if (!Class)
    {
        Class = LoadObject<UClass>(nullptr, *RawClass);
    }
    if (!Class || (ClassProperty->MetaClass && !Class->IsChildOf(ClassProperty->MetaClass)) || !Class->IsChildOf(UInstancedStaticMeshComponent::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Class '%s' is not assignable to reflected property '%s' or is not an instanced static mesh component class."), *RawClass, *Property->GetName());
        return false;
    }
    ClassProperty->SetObjectPropertyValue_InContainer(Container, Class);
    return true;
}

bool ApplyPCGDescriptorSettings(void* DescriptorContainer, UStruct* DescriptorStruct, const TSharedPtr<FJsonObject>& Object, FString& OutError, bool bAllowFallbackMesh = false)
{
    if (!DescriptorContainer || !DescriptorStruct || !Object.IsValid())
    {
        return true;
    }

    for (const auto& Pair : Object->Values)
    {
        const FString PairKey(*Pair.Key);
        FString PropertyName = PairKey;
        if (PropertyName.Equals(TEXT("meshPath"), ESearchCase::IgnoreCase) || PropertyName.Equals(TEXT("staticMesh"), ESearchCase::IgnoreCase))
        {
            PropertyName = TEXT("StaticMesh");
        }
        if (PropertyName.Equals(TEXT("collision"), ESearchCase::IgnoreCase))
        {
            FStructProperty* BodyInstanceProperty = FindFProperty<FStructProperty>(DescriptorStruct, TEXT("BodyInstance"));
            if (!BodyInstanceProperty)
            {
                OutError = FString::Printf(TEXT("Reflected property '%s.BodyInstance' was not found while applying collision settings."), *DescriptorStruct->GetName());
                return false;
            }
            void* BodyInstance = BodyInstanceProperty->ContainerPtrToValuePtr<void>(DescriptorContainer);
            if (Pair.Value->Type == EJson::String)
            {
                FProperty* ProfileProperty = FindFProperty<FProperty>(BodyInstanceProperty->Struct, TEXT("CollisionProfileName"));
                if (!ProfileProperty || !ApplyJsonValueToProperty(BodyInstance, ProfileProperty, Pair.Value, OutError))
                {
                    OutError = FString::Printf(TEXT("Failed reflected collision property '%s.BodyInstance.CollisionProfileName': %s"), *DescriptorStruct->GetName(), *OutError);
                    return false;
                }
            }
            else if (!ApplyJsonValueToProperty(DescriptorContainer, BodyInstanceProperty, Pair.Value, OutError))
            {
                OutError = FString::Printf(TEXT("Failed reflected collision property '%s.BodyInstance': %s"), *DescriptorStruct->GetName(), *OutError);
                return false;
            }
            continue;
        }

        FProperty* Property = FindFProperty<FProperty>(DescriptorStruct, FName(*PropertyName));
        if (!Property)
        {
            OutError = FString::Printf(TEXT("PCG descriptor property '%s' was not found on '%s' (requested '%s')."), *PropertyName, *DescriptorStruct->GetName(), *Pair.Key);
            return false;
        }
        if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
        {
            if (Pair.Value->Type != EJson::String || !ApplyPCGClassProperty(DescriptorContainer, ClassProperty, Pair.Value->AsString(), OutError))
            {
                return false;
            }
        }
        else if (Property->GetFName() == TEXT("StaticMesh"))
        {
            if (Pair.Value->Type != EJson::String)
            {
                OutError = FString::Printf(TEXT("PCG descriptor property '%s.StaticMesh' requires a valid soft-object asset path string."), *DescriptorStruct->GetName());
                return false;
            }
            FString ResolvedMeshPath;
            if (!LoadPCGStaticMesh(Pair.Value->AsString(), ResolvedMeshPath, OutError, bAllowFallbackMesh))
            {
                return false;
            }
            FSoftObjectProperty* StaticMeshProperty = CastField<FSoftObjectProperty>(Property);
            if (!StaticMeshProperty)
            {
                OutError = FString::Printf(TEXT("Reflected descriptor property '%s.StaticMesh' is not a soft-object property."), *DescriptorStruct->GetName());
                return false;
            }
            void* StaticMesh = StaticMeshProperty->ContainerPtrToValuePtr<void>(DescriptorContainer);
            *static_cast<FSoftObjectPtr*>(StaticMesh) = FSoftObjectPath(ResolvedMeshPath.Contains(TEXT(".")) ? ResolvedMeshPath : ToObjectPath(ResolvedMeshPath));
        }
        else if (!ApplyJsonValueToProperty(DescriptorContainer, Property, Pair.Value, OutError))
        {
            OutError = FString::Printf(TEXT("Failed to apply PCG descriptor property '%s.%s': %s"), *DescriptorStruct->GetName(), *PropertyName, *OutError);
            return false;
        }
    }
    return true;
}

bool ApplyPCGMeshEntry(void* EntryContainer, FStructProperty* EntryStructProperty, const TSharedPtr<FJsonObject>& EntryObject, bool bRequireMesh, FString& OutError)
{
    if (!EntryContainer || !EntryStructProperty || !EntryStructProperty->Struct || !EntryObject.IsValid())
    {
        OutError = TEXT("Invalid PCG mesh entry payload or reflected entry struct.");
        return false;
    }

    FStructProperty* DescriptorProperty = FindFProperty<FStructProperty>(EntryStructProperty->Struct, TEXT("Descriptor"));
    if (!DescriptorProperty || !DescriptorProperty->Struct)
    {
        OutError = FString::Printf(TEXT("Reflected mesh entry '%s.Descriptor' was not found."), *EntryStructProperty->Struct->GetName());
        return false;
    }
    void* Descriptor = DescriptorProperty->ContainerPtrToValuePtr<void>(EntryContainer);

    const bool bAllowFallbackMesh = GetJsonBoolField(EntryObject, TEXT("allowFallbackMesh"), false) ||
        GetJsonBoolField(EntryObject, TEXT("allowCube"), false);
    const FString MeshPath = GetFirstStringField(EntryObject, {TEXT("meshPath"), TEXT("staticMesh"), TEXT("StaticMesh")});
    if (bRequireMesh && MeshPath.IsEmpty())
    {
        OutError = TEXT("Static Mesh Spawner mesh entry requires meshPath (a valid UStaticMesh asset path); no cube fallback is used.");
        return false;
    }
    if (!MeshPath.IsEmpty())
    {
        FString ResolvedMeshPath;
        if (!LoadPCGStaticMesh(MeshPath, ResolvedMeshPath, OutError, bAllowFallbackMesh))
        {
            return false;
        }
        FSoftObjectProperty* StaticMeshProperty = FindFProperty<FSoftObjectProperty>(DescriptorProperty->Struct, TEXT("StaticMesh"));
        if (!StaticMeshProperty)
        {
            OutError = FString::Printf(TEXT("Reflected descriptor property '%s.StaticMesh' is not a soft-object property."), *DescriptorProperty->Struct->GetName());
            return false;
        }
        void* StaticMesh = StaticMeshProperty->ContainerPtrToValuePtr<void>(Descriptor);
        *static_cast<FSoftObjectPtr*>(StaticMesh) = FSoftObjectPath(ResolvedMeshPath.Contains(TEXT(".")) ? ResolvedMeshPath : ToObjectPath(ResolvedMeshPath));
    }

    double Weight = 0.0;
    if (EntryObject->TryGetNumberField(TEXT("weight"), Weight) || EntryObject->TryGetNumberField(TEXT("Weight"), Weight))
    {
        FProperty* WeightProperty = FindFProperty<FProperty>(EntryStructProperty->Struct, TEXT("Weight"));
        if (!WeightProperty || !ApplyJsonValueToProperty(EntryContainer, WeightProperty, MakeShared<FJsonValueNumber>(Weight), OutError))
        {
            OutError = FString::Printf(TEXT("Failed to apply reflected mesh entry property '%s.Weight': %s"), *EntryStructProperty->Struct->GetName(), *OutError);
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* DescriptorSettings = nullptr;
    if (EntryObject->TryGetObjectField(TEXT("descriptorSettings"), DescriptorSettings) && DescriptorSettings && DescriptorSettings->IsValid() &&
        !ApplyPCGDescriptorSettings(Descriptor, DescriptorProperty->Struct, *DescriptorSettings, OutError, bAllowFallbackMesh))
    {
        return false;
    }
    const TSharedPtr<FJsonValue>* CollisionValue = EntryObject->Values.Find(TEXT("collision"));
    if (CollisionValue && *CollisionValue)
    {
        TSharedPtr<FJsonObject> CollisionObject = MakeShared<FJsonObject>();
        CollisionObject->SetField(TEXT("collision"), *CollisionValue);
        if (!ApplyPCGDescriptorSettings(Descriptor, DescriptorProperty->Struct, CollisionObject, OutError, bAllowFallbackMesh))
        {
            return false;
        }
    }
    return true;
}

UPCGNode* FindStaticMeshSpawnerNode(UPCGGraph* Graph, const FString& RequestedNodeId, FString& OutError)
{
    if (!Graph)
    {
        OutError = TEXT("PCG graph is invalid.");
        return nullptr;
    }
    if (!RequestedNodeId.IsEmpty())
    {
        UPCGNode* Node = FindPCGNode(Graph, RequestedNodeId);
        if (!Node)
        {
            OutError = FString::Printf(TEXT("Could not resolve PCG node '%s' while looking for a Static Mesh Spawner."), *RequestedNodeId);
            return nullptr;
        }
        if (!Node->GetSettings() || !Node->GetSettings()->IsA<UPCGStaticMeshSpawnerSettings>())
        {
            OutError = FString::Printf(TEXT("PCG node '%s' is '%s', not UPCGStaticMeshSpawnerSettings."), *RequestedNodeId, Node->GetSettings() ? *Node->GetSettings()->GetClass()->GetName() : TEXT("<null>"));
            return nullptr;
        }
        return Node;
    }
    for (UPCGNode* Node : Graph->GetNodes())
    {
        if (Node && Node->GetSettings() && Node->GetSettings()->IsA<UPCGStaticMeshSpawnerSettings>())
        {
            return Node;
        }
    }
    OutError = TEXT("No UPCGStaticMeshSpawnerSettings node was found in the graph. Provide nodeId/nodeName or create one with add_static_mesh_spawner.");
    return nullptr;
}

TSharedPtr<FJsonObject> BuildStaticMeshSpawnerResult(UPCGGraph* Graph, UPCGNode* Node, const FString& GraphPath, FString& OutError)
{
    TSharedPtr<FJsonObject> Result = BuildNodeResult(Graph, Node, GraphPath);
    UPCGStaticMeshSpawnerSettings* Settings = Node ? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings()) : nullptr;
    UObject* Selector = GetPCGMeshSelector(Settings, OutError);
    if (!Selector)
    {
        return nullptr;
    }
    Result->SetStringField(TEXT("meshSelectorType"), Selector->GetClass()->GetName());
    Result->SetStringField(TEXT("meshSelectorClass"), Selector->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("deterministic"), Settings->UseSeed());

    FArrayProperty* EntriesProperty = GetPCGMeshEntriesProperty(Selector, OutError);
    if (!EntriesProperty)
    {
        return nullptr;
    }
    FStructProperty* EntryStructProperty = CastField<FStructProperty>(EntriesProperty->Inner);
    FScriptArrayHelper Entries(EntriesProperty, EntriesProperty->ContainerPtrToValuePtr<void>(Selector));
    TArray<TSharedPtr<FJsonValue>> EntryValues;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        void* Entry = Entries.GetRawPtr(Index);
        TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
        EntryObject->SetNumberField(TEXT("index"), Index);
        if (FProperty* WeightProperty = FindFProperty<FProperty>(EntryStructProperty->Struct, TEXT("Weight")))
        {
            if (FIntProperty* IntWeight = CastField<FIntProperty>(WeightProperty))
            {
                EntryObject->SetNumberField(TEXT("weight"), IntWeight->GetPropertyValue_InContainer(Entry));
            }
            else
            {
                EntryObject->SetField(TEXT("weight"), ExportPropertyToJsonValue(Entry, WeightProperty));
            }
        }
        if (FStructProperty* DescriptorProperty = FindFProperty<FStructProperty>(EntryStructProperty->Struct, TEXT("Descriptor")))
        {
            void* Descriptor = DescriptorProperty->ContainerPtrToValuePtr<void>(Entry);
            TSharedPtr<FJsonObject> DescriptorObject = MakeShared<FJsonObject>();
            if (FSoftObjectProperty* MeshProperty = FindFProperty<FSoftObjectProperty>(DescriptorProperty->Struct, TEXT("StaticMesh")))
            {
                const FSoftObjectPtr* Mesh = static_cast<const FSoftObjectPtr*>(MeshProperty->ContainerPtrToValuePtr<void>(Descriptor));
                const FString MeshPath = Mesh && !Mesh->IsNull() ? Mesh->ToSoftObjectPath().ToString() : FString();
                EntryObject->SetStringField(TEXT("meshPath"), MeshPath);
            }
            if (FClassProperty* ComponentClassProperty = FindFProperty<FClassProperty>(DescriptorProperty->Struct, TEXT("ComponentClass")))
            {
                if (UClass* ComponentClass = Cast<UClass>(ComponentClassProperty->GetObjectPropertyValue_InContainer(Descriptor)))
                {
                    DescriptorObject->SetStringField(TEXT("ComponentClass"), ComponentClass->GetPathName());
                }
            }
            for (TFieldIterator<FProperty> It(DescriptorProperty->Struct); It; ++It)
            {
                FProperty* Property = *It;
                if (!Property || Property->GetFName() == TEXT("StaticMesh") || Property->GetFName() == TEXT("ComponentClass") || Property->GetFName() == TEXT("Hash"))
                {
                    continue;
                }
                if (TSharedPtr<FJsonValue> Value = ExportPropertyToJsonValue(Descriptor, Property))
                {
                    DescriptorObject->SetField(Property->GetName(), Value);
                }
            }
            EntryObject->SetObjectField(TEXT("descriptorSettings"), DescriptorObject);
        }
        EntryValues.Add(MakeShared<FJsonValueObject>(EntryObject));
    }
    Result->SetArrayField(TEXT("meshEntries"), EntryValues);
    Result->SetNumberField(TEXT("entryCount"), EntryValues.Num());
    return Result;
}

bool ApplyStaticMeshSpawnerSettingsObject(UPCGStaticMeshSpawnerSettings* Settings, const TSharedPtr<FJsonObject>& SettingsObject, FString& OutError, int32& OutAppliedCount)
{
    OutAppliedCount = 0;
    if (!Settings || !SettingsObject.IsValid())
    {
        return true;
    }

    const bool bAllowFallbackMesh = GetJsonBoolField(SettingsObject, TEXT("allowFallbackMesh"), false) ||
        GetJsonBoolField(SettingsObject, TEXT("allowCube"), false);
    const TArray<TSharedPtr<FJsonValue>>* MeshEntries = nullptr;
    if (SettingsObject->TryGetArrayField(TEXT("meshEntries"), MeshEntries) && MeshEntries)
    {
        // Validate every asset before changing the reflected array. This keeps
        // an invalid path from leaving a partially authored selector behind.
        for (const TSharedPtr<FJsonValue>& EntryValue : *MeshEntries)
        {
            if (!EntryValue || EntryValue->Type != EJson::Object)
            {
                OutError = TEXT("meshEntries must contain JSON objects.");
                return false;
            }
            const FString MeshPath = GetFirstStringField(EntryValue->AsObject(), {TEXT("meshPath"), TEXT("staticMesh"), TEXT("StaticMesh")});
            FString ResolvedMeshPath;
            if (MeshPath.IsEmpty() || !LoadPCGStaticMesh(MeshPath, ResolvedMeshPath, OutError, bAllowFallbackMesh))
            {
                if (OutError.IsEmpty())
                {
                    OutError = TEXT("Static Mesh Spawner mesh entry requires a valid UStaticMesh asset path; no cube fallback is used.");
                }
                return false;
            }
        }
    }

    FString SelectorType = GetFirstStringField(SettingsObject, {TEXT("meshSelectorType"), TEXT("selectorType"), TEXT("MeshSelectorType")});
    if (!SelectorType.IsEmpty())
    {
        UClass* SelectorClass = nullptr;
        if (!ResolvePCGMeshSelectorClass(SelectorType, SelectorClass, OutError))
        {
            return false;
        }
        Settings->Modify();
        Settings->SetMeshSelectorType(SelectorClass);
        ++OutAppliedCount;
    }

    if (SettingsObject->TryGetArrayField(TEXT("meshEntries"), MeshEntries) && MeshEntries)
    {
        UObject* Selector = GetPCGMeshSelector(Settings, OutError);
        FArrayProperty* EntriesProperty = GetPCGMeshEntriesProperty(Selector, OutError);
        if (!EntriesProperty)
        {
            return false;
        }
        FStructProperty* EntryStructProperty = CastField<FStructProperty>(EntriesProperty->Inner);
        FScriptArrayHelper Entries(EntriesProperty, EntriesProperty->ContainerPtrToValuePtr<void>(Selector));
        Entries.EmptyValues();
        for (const TSharedPtr<FJsonValue>& EntryValue : *MeshEntries)
        {
            if (!EntryValue || EntryValue->Type != EJson::Object)
            {
                OutError = TEXT("meshEntries must contain JSON objects.");
                return false;
            }
            Entries.AddValue();
            if (!ApplyPCGMeshEntry(Entries.GetRawPtr(Entries.Num() - 1), EntryStructProperty, EntryValue->AsObject(), true, OutError))
            {
                return false;
            }
            ++OutAppliedCount;
        }
    }

    const FString MeshPath = GetFirstStringField(SettingsObject, {TEXT("meshPath"), TEXT("staticMesh"), TEXT("StaticMesh")});
    if (!MeshPath.IsEmpty())
    {
        if (!ApplyStaticMeshSpawnerMeshPath(Settings, MeshPath, OutError, bAllowFallbackMesh))
        {
            return false;
        }
        ++OutAppliedCount;
    }

    TSharedPtr<FJsonObject> DirectSettings = MakeShared<FJsonObject>();
    for (const auto& Pair : SettingsObject->Values)
    {
        const FString PairKey(*Pair.Key);
        const bool bSpecial = PairKey.Equals(TEXT("meshSelectorType"), ESearchCase::IgnoreCase) || PairKey.Equals(TEXT("selectorType"), ESearchCase::IgnoreCase) ||
            PairKey.Equals(TEXT("MeshSelectorType"), ESearchCase::IgnoreCase) || PairKey.Equals(TEXT("meshEntries"), ESearchCase::IgnoreCase) ||
            PairKey.Equals(TEXT("meshPath"), ESearchCase::IgnoreCase) || PairKey.Equals(TEXT("staticMesh"), ESearchCase::IgnoreCase) ||
            PairKey.Equals(TEXT("allowFallbackMesh"), ESearchCase::IgnoreCase) || PairKey.Equals(TEXT("allowCube"), ESearchCase::IgnoreCase);
        if (!bSpecial)
        {
            DirectSettings->SetField(Pair.Key, Pair.Value);
        }
    }
    if (!DirectSettings->Values.IsEmpty())
    {
        int32 DirectApplied = 0;
        if (!ApplySettingsObject(Settings, DirectSettings, OutError, DirectApplied))
        {
            return false;
        }
        OutAppliedCount += DirectApplied;
    }
    return true;
}

bool ApplyPCGConvenienceSettings(const FString& SubAction, UPCGSettings* Settings, const TSharedPtr<FJsonObject>& Payload, FString& OutError, int32& OutAppliedCount)
{
    OutAppliedCount = 0;
    if (!Settings || !Payload.IsValid())
    {
        return true;
    }

    if (SubAction == TEXT("add_texture_data_node") || Payload->HasField(TEXT("texturePath")))
    {
        const FString TexturePath = GetJsonStringField(Payload, TEXT("texturePath"));
        if (!TexturePath.IsEmpty())
        {
            if (!ApplyStringSetting(Settings, TEXT("Texture"), TexturePath, OutError))
            {
                return false;
            }
            ++OutAppliedCount;
        }
    }
    if (SubAction == TEXT("add_mesh_sampler") || (Payload->HasField(TEXT("meshPath")) && Settings->GetClass()->FindPropertyByName(FName(TEXT("Mesh")))))
    {
        const FString MeshPath = GetJsonStringField(Payload, TEXT("meshPath"));
        if (!MeshPath.IsEmpty())
        {
            const bool bAllowFallbackMesh = GetJsonBoolField(Payload, TEXT("allowFallbackMesh"), false) ||
                GetJsonBoolField(Payload, TEXT("allowCube"), false);
            FString ResolvedMeshPath;
            if (!LoadPCGStaticMesh(MeshPath, ResolvedMeshPath, OutError, bAllowFallbackMesh))
            {
                return false;
            }
            FSoftObjectProperty* MeshProperty = CastField<FSoftObjectProperty>(Settings->GetClass()->FindPropertyByName(FName(TEXT("Mesh"))));
            if (!MeshProperty)
            {
                OutError = FString::Printf(TEXT("PCG mesh sampler '%s' has no soft-object Mesh property."), *Settings->GetClass()->GetName());
                return false;
            }
            void* MeshValue = MeshProperty->ContainerPtrToValuePtr<void>(Settings);
            *static_cast<FSoftObjectPtr*>(MeshValue) = FSoftObjectPath(ResolvedMeshPath.Contains(TEXT(".")) ? ResolvedMeshPath : ToObjectPath(ResolvedMeshPath));
            ++OutAppliedCount;
        }
    }
    if (SubAction == TEXT("add_static_mesh_spawner") || (Payload->HasField(TEXT("meshPath")) && Settings->IsA<UPCGStaticMeshSpawnerSettings>()))
    {
        const FString MeshPath = GetJsonStringField(Payload, TEXT("meshPath"));
        if (!MeshPath.IsEmpty())
        {
            const bool bAllowFallbackMesh = GetJsonBoolField(Payload, TEXT("allowFallbackMesh"), false) ||
                GetJsonBoolField(Payload, TEXT("allowCube"), false);
            if (!ApplyStaticMeshSpawnerMeshPath(Settings, MeshPath, OutError, bAllowFallbackMesh))
            {
                return false;
            }
            ++OutAppliedCount;
        }
    }
    if (SubAction == TEXT("add_actor_spawner") || Payload->HasField(TEXT("actorClass")) || (Payload->HasField(TEXT("classPath")) && Settings->IsA<UPCGSpawnActorSettings>()))
    {
        const FString ActorClass = GetFirstStringField(Payload, {TEXT("actorClass"), TEXT("classPath")});
        if (!ActorClass.IsEmpty())
        {
            if (!ApplySpawnActorTemplateClass(Settings, ActorClass, OutError))
            {
                return false;
            }
            ++OutAppliedCount;
        }
    }

    const TArray<TPair<const TCHAR*, const TCHAR*>> ReflectedAliases = {
        {TEXT("seed"), TEXT("Seed")},
        {TEXT("scaleMin"), TEXT("ScaleMin")}, {TEXT("scaleMax"), TEXT("ScaleMax")},
        {TEXT("rotationMin"), TEXT("RotationMin")}, {TEXT("rotationMax"), TEXT("RotationMax")},
        {TEXT("offsetMin"), TEXT("OffsetMin")}, {TEXT("offsetMax"), TEXT("OffsetMax")}
    };
    for (const TPair<const TCHAR*, const TCHAR*>& Alias : ReflectedAliases)
    {
        const TSharedPtr<FJsonValue>* Value = Payload->Values.Find(Alias.Key);
        if (!Value || !(*Value))
        {
            continue;
        }
        FProperty* Property = FindFProperty<FProperty>(Settings->GetClass(), FName(Alias.Value));
        if (!Property)
        {
            OutError = FString::Printf(TEXT("PCG settings property '%s' was not found on '%s' while applying '%s'."), Alias.Value, *Settings->GetClass()->GetName(), Alias.Key);
            return false;
        }
        if (!ApplyJsonValueToProperty(Settings, Property, *Value, OutError))
        {
            OutError = FString::Printf(TEXT("Failed to apply PCG settings property '%s' for '%s': %s"), Alias.Value, Alias.Key, *OutError);
            return false;
        }
        ++OutAppliedCount;
    }
    if (Payload->HasField(TEXT("deterministic")) && GetJsonBoolField(Payload, TEXT("deterministic"), false))
    {
        if (!Settings->UseSeed())
        {
            OutError = FString::Printf(TEXT("Deterministic variation was requested, but '%s' does not support a seed."), *Settings->GetClass()->GetName());
            return false;
        }
        // PCG's deterministic variation is seed-driven. There is no private
        // bDeterministic field to write; retaining the explicit seed is the
        // UE-supported behavior across 5.0-5.8.
        ++OutAppliedCount;
    }

    return true;
}

UWorld* GetPCGEditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

AActor* FindPCGActor(UWorld* World, const FString& ActorName)
{
    if (!World || ActorName.IsEmpty())
    {
        return nullptr;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor && (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase) ||
            Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase)))
        {
            return Actor;
        }
    }

    return nullptr;
}

UPCGComponent* FindPCGComponentOnActor(AActor* Actor, const FString& ComponentName)
{
    if (!Actor)
    {
        return nullptr;
    }

    TArray<UPCGComponent*> Components;
    Actor->GetComponents<UPCGComponent>(Components);
    for (UPCGComponent* Component : Components)
    {
        if (!Component)
        {
            continue;
        }
        const bool bIdentifierLooksLikePath = ComponentName.Contains(TEXT(".")) || ComponentName.Contains(TEXT("/"));
        if (ComponentName.IsEmpty() || Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
            Component->GetFName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
            Component->GetPathName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
            Component->GetFullName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
            (bIdentifierLooksLikePath && Component->GetPathName().EndsWith(ComponentName, ESearchCase::IgnoreCase)))
        {
            return Component;
        }
    }

    return nullptr;
}

UPCGComponent* FindPCGComponent(UWorld* World, const FString& ActorName, const FString& ComponentName, AActor*& OutActor)
{
    OutActor = nullptr;
    if (!World)
    {
        return nullptr;
    }

    if (!ActorName.IsEmpty())
    {
        OutActor = FindPCGActor(World, ActorName);
        return FindPCGComponentOnActor(OutActor, ComponentName);
    }

    if (ComponentName.IsEmpty())
    {
        return nullptr;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (UPCGComponent* Component = FindPCGComponentOnActor(Actor, ComponentName))
        {
            OutActor = Actor;
            return Component;
        }
    }

    return nullptr;
}

bool HasPCGComponentSelector(const FString& ActorName, const FString& ComponentName)
{
    return !ActorName.IsEmpty() || !ComponentName.IsEmpty();
}

UPCGComponent* CreatePCGComponent(AActor* Actor, const FString& ComponentName)
{
    if (!Actor)
    {
        return nullptr;
    }

    Actor->Modify();
    const FName NewComponentName = ComponentName.IsEmpty() ? NAME_None : FName(*ComponentName);
    UPCGComponent* Component = NewObject<UPCGComponent>(Actor, NewComponentName, RF_Transactional);
    if (!Component)
    {
        return nullptr;
    }

    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();
    Component->Modify();
    return Component;
}

bool SaveEditorWorldIfRequested(UWorld* World, bool bSave, bool& bOutSaved, FString& OutError)
{
    bOutSaved = false;
    if (!World)
    {
        OutError = TEXT("Could not resolve the editor world for level save.");
        return false;
    }

    if (!bSave)
    {
        return true;
    }

    if (!World->PersistentLevel)
    {
        OutError = TEXT("Could not resolve the persistent level for PCG save.");
        return false;
    }

    UPackage* WorldPackage = World->GetOutermost();
    const FString LevelPath = WorldPackage ? WorldPackage->GetName() : FString();
    if (LevelPath.IsEmpty())
    {
        OutError = TEXT("Could not resolve the current level package path for PCG save.");
        return false;
    }

    World->Modify();
    World->MarkPackageDirty();
    World->PersistentLevel->Modify();
    World->PersistentLevel->MarkPackageDirty();

    bOutSaved = McpSafeLevelSave(World->PersistentLevel, LevelPath);
    if (!bOutSaved)
    {
        OutError = FString::Printf(TEXT("Failed to save current level '%s' after PCG change."), *LevelPath);
        return false;
    }

    return true;
}

void ApplyNodeMetadata(UPCGNode* Node, const TSharedPtr<FJsonObject>& Payload)
{
    if (!Node || !Payload.IsValid())
    {
        return;
    }

    const FString Title = GetFirstStringField(Payload, {TEXT("nodeName"), TEXT("title"), TEXT("name")});
    if (!Title.IsEmpty())
    {
        Node->NodeTitle = FName(*Title);
    }

    double X = 0.0;
    double Y = 0.0;
    const bool bHasX = Payload->TryGetNumberField(TEXT("x"), X) || Payload->TryGetNumberField(TEXT("posX"), X);
    const bool bHasY = Payload->TryGetNumberField(TEXT("y"), Y) || Payload->TryGetNumberField(TEXT("posY"), Y);
    if (bHasX || bHasY)
    {
        Node->SetNodePosition(static_cast<int32>(X), static_cast<int32>(Y));
    }
}

bool SaveGraphIfRequested(UPCGGraph* Graph, bool bSave, bool& bOutSaved, FString& OutError)
{
    bOutSaved = false;
    if (!Graph)
    {
        OutError = TEXT("PCG graph is invalid.");
        return false;
    }

    Graph->PostEditChange();
    Graph->MarkPackageDirty();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    Graph->ForceNotificationForEditor(EPCGChangeType::Structural);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
    Graph->ForceNotificationForEditor();
#else
    Graph->NotifyGraphChanged(EPCGChangeType::Structural);
#endif

    if (bSave)
    {
        bOutSaved = McpSafeAssetSave(Graph);
        if (!bOutSaved)
        {
            OutError = FString::Printf(TEXT("Failed to save PCG graph '%s'."), *Graph->GetPathName());
            return false;
        }
    }

    return true;
}

UPCGComponent* ResolveRequestedPCGComponent(const TSharedPtr<FJsonObject>& Payload, UWorld* World, AActor*& OutActor, FString& OutError)
{
    OutActor = nullptr;
    if (!World)
    {
        OutError = TEXT("Could not resolve the editor world for PCG component access.");
        return nullptr;
    }
    const FString ActorName = GetJsonStringField(Payload, TEXT("actorName"));
    const FString ComponentName = GetJsonStringField(Payload, TEXT("componentName"));
    const FString ComponentPath = GetJsonStringField(Payload, TEXT("componentPath"));
    const FString Selector = !ComponentPath.IsEmpty() ? ComponentPath : ComponentName;
    UPCGComponent* Component = FindPCGComponent(World, ActorName, Selector, OutActor);
    if (!Component)
    {
        OutError = TEXT("Could not resolve a PCG component. Provide actorName plus componentName/componentPath.");
    }
    return Component;
}

TSharedPtr<FJsonObject> BuildGeneratedInstancesResult(UPCGComponent* Component)
{
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    int32 TotalInstances = 0;
    int32 ISMInstances = 0;
    int32 HISMInstances = 0;
    int32 ISMComponents = 0;
    int32 HISMComponents = 0;
    uint32 AggregateTransformHash = 0;
    bool bHasFallbackMesh = false;
    bool bAllTransformsWithinBounds = true;
    TArray<TSharedPtr<FJsonValue>> InstanceValues;
    TArray<TSharedPtr<FJsonValue>> ActualMeshPaths;
    TArray<TSharedPtr<FJsonValue>> ActualMeshPackagePaths;
    TArray<FString> SeenMeshPaths;
    TArray<FString> SeenMeshPackagePaths;
    if (Component)
    {
        Component->ForEachConstManagedResource([&](const UPCGManagedResource* Resource)
        {
            const UPCGManagedISMComponent* ManagedISM = Cast<UPCGManagedISMComponent>(Resource);
            if (!ManagedISM)
            {
                return;
            }
            UInstancedStaticMeshComponent* ISM = ManagedISM->GetComponent();
            if (!ISM)
            {
                return;
            }
            const int32 Count = ISM->GetInstanceCount();
            const bool bIsHISM = ISM->IsA<UHierarchicalInstancedStaticMeshComponent>();
            TotalInstances += Count;
            if (bIsHISM)
            {
                ++HISMComponents;
                HISMInstances += Count;
            }
            else
            {
                ++ISMComponents;
                ISMInstances += Count;
            }
            TSharedPtr<FJsonObject> InstanceObject = MakeShared<FJsonObject>();
            InstanceObject->SetStringField(TEXT("componentPath"), ISM->GetPathName());
            InstanceObject->SetStringField(TEXT("componentType"), bIsHISM ? TEXT("HISM") : TEXT("ISM"));
            const FString MeshPath = ISM->GetStaticMesh() ? ISM->GetStaticMesh()->GetPathName() : FString();
            bHasFallbackMesh |= IsPCGFallbackMeshPath(MeshPath);
            InstanceObject->SetStringField(TEXT("meshPath"), MeshPath);
            InstanceObject->SetNumberField(TEXT("instanceCount"), Count);
            uint32 InstanceSignature = 0;
            const FBox ComponentBounds = ISM->GetBounds().GetBox();
            bool bTransformsWithinBounds = Count > 0;
            TArray<TSharedPtr<FJsonValue>> TransformValues;
            TransformValues.Reserve(Count);
            for (int32 InstanceIndex = 0; InstanceIndex < Count; ++InstanceIndex)
            {
                FTransform InstanceTransform;
                if (ISM->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
                {
                    const FString TransformString = InstanceTransform.ToString();
                    InstanceSignature = FCrc::StrCrc32(*TransformString, InstanceSignature);
                    AggregateTransformHash = FCrc::StrCrc32(*TransformString, AggregateTransformHash);

                    const FVector Location = InstanceTransform.GetLocation();
                    bTransformsWithinBounds &= ComponentBounds.IsInsideOrOn(Location);
                    const FRotator Rotation = InstanceTransform.Rotator();
                    const FVector Scale = InstanceTransform.GetScale3D();
                    TSharedPtr<FJsonObject> TransformObject = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> LocationObject = MakeShared<FJsonObject>();
                    LocationObject->SetNumberField(TEXT("x"), Location.X);
                    LocationObject->SetNumberField(TEXT("y"), Location.Y);
                    LocationObject->SetNumberField(TEXT("z"), Location.Z);
                    TSharedPtr<FJsonObject> RotationObject = MakeShared<FJsonObject>();
                    RotationObject->SetNumberField(TEXT("pitch"), Rotation.Pitch);
                    RotationObject->SetNumberField(TEXT("yaw"), Rotation.Yaw);
                    RotationObject->SetNumberField(TEXT("roll"), Rotation.Roll);
                    TSharedPtr<FJsonObject> ScaleObject = MakeShared<FJsonObject>();
                    ScaleObject->SetNumberField(TEXT("x"), Scale.X);
                    ScaleObject->SetNumberField(TEXT("y"), Scale.Y);
                    ScaleObject->SetNumberField(TEXT("z"), Scale.Z);
                    TransformObject->SetObjectField(TEXT("location"), LocationObject);
                    TransformObject->SetObjectField(TEXT("rotation"), RotationObject);
                    TransformObject->SetObjectField(TEXT("scale"), ScaleObject);
                    TransformValues.Add(MakeShared<FJsonValueObject>(TransformObject));
                }
                else
                {
                    bTransformsWithinBounds = false;
                }
            }
            InstanceObject->SetStringField(TEXT("instanceSignature"), FString::Printf(TEXT("%u"), InstanceSignature));
            InstanceObject->SetArrayField(TEXT("transforms"), TransformValues);
            InstanceObject->SetBoolField(TEXT("transformsWithinBounds"), bTransformsWithinBounds);
            bAllTransformsWithinBounds &= bTransformsWithinBounds;
            TSharedPtr<FJsonObject> BoundsObject = MakeShared<FJsonObject>();
            TSharedPtr<FJsonObject> BoundsMin = MakeShared<FJsonObject>();
            BoundsMin->SetNumberField(TEXT("x"), ComponentBounds.Min.X);
            BoundsMin->SetNumberField(TEXT("y"), ComponentBounds.Min.Y);
            BoundsMin->SetNumberField(TEXT("z"), ComponentBounds.Min.Z);
            TSharedPtr<FJsonObject> BoundsMax = MakeShared<FJsonObject>();
            BoundsMax->SetNumberField(TEXT("x"), ComponentBounds.Max.X);
            BoundsMax->SetNumberField(TEXT("y"), ComponentBounds.Max.Y);
            BoundsMax->SetNumberField(TEXT("z"), ComponentBounds.Max.Z);
            BoundsObject->SetObjectField(TEXT("min"), BoundsMin);
            BoundsObject->SetObjectField(TEXT("max"), BoundsMax);
            InstanceObject->SetObjectField(TEXT("bounds"), BoundsObject);
            InstanceValues.Add(MakeShared<FJsonValueObject>(InstanceObject));
            if (!MeshPath.IsEmpty() && !SeenMeshPaths.Contains(MeshPath))
            {
                SeenMeshPaths.Add(MeshPath);
                ActualMeshPaths.Add(MakeShared<FJsonValueString>(MeshPath));
            }
            const FNormalizedAssetPath NormalizedMeshPath = NormalizeAssetPath(MeshPath);
            const FString MeshPackagePath = NormalizedMeshPath.bIsValid ? NormalizedMeshPath.Path : MeshPath;
            if (!MeshPackagePath.IsEmpty() && !SeenMeshPackagePaths.Contains(MeshPackagePath))
            {
                SeenMeshPackagePaths.Add(MeshPackagePath);
                ActualMeshPackagePaths.Add(MakeShared<FJsonValueString>(MeshPackagePath));
            }
        });
    }
    Result->SetNumberField(TEXT("instanceCount"), TotalInstances);
    Result->SetNumberField(TEXT("ismInstanceCount"), ISMInstances);
    Result->SetNumberField(TEXT("hismInstanceCount"), HISMInstances);
    Result->SetNumberField(TEXT("ismComponentCount"), ISMComponents);
    Result->SetNumberField(TEXT("hismComponentCount"), HISMComponents);
    Result->SetNumberField(TEXT("componentCount"), ISMComponents + HISMComponents);
    Result->SetNumberField(TEXT("materializedInstanceCount"), TotalInstances);
    Result->SetStringField(TEXT("transformHash"), FString::Printf(TEXT("%u"), AggregateTransformHash));
    Result->SetArrayField(TEXT("actualMeshPaths"), ActualMeshPaths);
    Result->SetArrayField(TEXT("actualMeshPackagePaths"), ActualMeshPackagePaths);
    Result->SetBoolField(TEXT("hasFallbackMesh"), bHasFallbackMesh);
    Result->SetArrayField(TEXT("instances"), InstanceValues);
    Result->SetBoolField(TEXT("transformsWithinBounds"), bAllTransformsWithinBounds);
    Result->SetBoolField(TEXT("materialized"), TotalInstances > 0 && (ISMComponents + HISMComponents) > 0);
    Result->SetBoolField(TEXT("generated"), Component ? Component->bGenerated : false);
    Result->SetBoolField(TEXT("generationInProgress"), Component ? Component->IsGenerating() : false);
    if (Component)
    {
        McpHandlerUtils::AddVerification(Result, Component);
    }
    return Result;
}

void SchedulePCGGenerationWait(
    UMcpAutomationBridgeSubsystem* Subsystem,
    const TSharedPtr<FMcpBridgeWebSocket>& Socket,
    const FString& RequestId,
    UWorld* World,
    AActor* Actor,
    UPCGComponent* Component,
    const FString& GraphPath,
    FPCGTaskId TaskId,
    bool bSave,
    int32 TimeoutMs)
{
    const TWeakObjectPtr<UMcpAutomationBridgeSubsystem> WeakSubsystem(Subsystem);
    const TWeakObjectPtr<UPCGComponent> WeakComponent(Component);
    const TWeakObjectPtr<AActor> WeakActor(Actor);
    const double StartSeconds = FPlatformTime::Seconds();
    const double TimeoutSeconds = FMath::Max(1.0, static_cast<double>(TimeoutMs) / 1000.0);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakSubsystem, Socket, RequestId, World, WeakActor, WeakComponent, GraphPath, TaskId, bSave, StartSeconds, TimeoutSeconds](float)
        {
            UMcpAutomationBridgeSubsystem* LiveSubsystem = WeakSubsystem.Get();
            UPCGComponent* LiveComponent = WeakComponent.Get();
            if (!LiveSubsystem || !LiveComponent)
            {
                if (LiveSubsystem)
                {
                    LiveSubsystem->SendAutomationError(Socket, RequestId, TEXT("PCG component was destroyed while waiting for generation."), TEXT("COMPONENT_DESTROYED"));
                }
                return false;
            }

            const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
            if (LiveComponent->IsGenerating())
            {
                if (ElapsedSeconds >= TimeoutSeconds)
                {
                    LiveSubsystem->SendAutomationError(Socket, RequestId, TEXT("Timed out waiting for PCG generation to materialize ISM/HISM output."), TEXT("GENERATION_TIMEOUT"));
                    return false;
                }
                LiveSubsystem->SendProgressUpdate(RequestId, -1.0f, TEXT("Waiting for PCG generation and materialized ISM/HISM components."), true);
                return true;
            }

            bool bLevelSaved = false;
            FString SaveError;
            if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, SaveError))
            {
                LiveSubsystem->SendAutomationError(Socket, RequestId, SaveError, TEXT("SAVE_FAILED"));
                return false;
            }

            TSharedPtr<FJsonObject> Result = BuildGeneratedInstancesResult(LiveComponent);
            AActor* LiveActor = WeakActor.Get();
            Result->SetStringField(TEXT("graphPath"), GraphPath);
            Result->SetStringField(TEXT("actorName"), LiveActor ? LiveActor->GetName() : FString());
            Result->SetStringField(TEXT("componentName"), LiveComponent->GetName());
            Result->SetStringField(TEXT("componentPath"), LiveComponent->GetPathName());
            Result->SetNumberField(TEXT("taskId"), static_cast<double>(TaskId));
            Result->SetBoolField(TEXT("waited"), true);
            Result->SetBoolField(TEXT("saved"), bLevelSaved);
            McpHandlerUtils::AddVerification(Result, LiveComponent);
            LiveSubsystem->SendAutomationResponse(Socket, RequestId, true, TEXT("PCG generation completed; output was verified from materialized ISM/HISM components."), Result);
            return false;
        }));
}
}
#endif

bool UMcpAutomationBridgeSubsystem::HandleManagePCGAction(
    const FString& RequestId, const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    if (Action != TEXT("manage_pcg"))
    {
        return false;
    }

#if !WITH_EDITOR
    SendAutomationError(Socket, RequestId, TEXT("manage_pcg requires an editor build."), TEXT("EDITOR_ONLY"));
    return true;
#elif !MCP_HAS_PCG
    SendAutomationError(Socket, RequestId, TEXT("PCG plugin support is not available in this build."), TEXT("PCG_PLUGIN_NOT_AVAILABLE"));
    return true;
#else
    if (!Payload.IsValid())
    {
        SendAutomationError(Socket, RequestId, TEXT("Missing payload."), TEXT("INVALID_PAYLOAD"));
        return true;
    }

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("PCG")) &&
        (!FModuleManager::Get().ModuleExists(TEXT("PCG")) || !FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("PCG"))))
    {
        SendAutomationError(Socket, RequestId, TEXT("PCG plugin is not enabled in this project."), TEXT("PCG_PLUGIN_NOT_ENABLED"));
        return true;
    }

    const FString SubAction = NormalizePCGSubAction(Payload);
    if (SubAction.IsEmpty() || !McpConsolidatedActions::IsPCGAction(SubAction))
    {
        SendAutomationError(Socket, RequestId, FString::Printf(TEXT("Unknown PCG subAction: %s"), *SubAction), TEXT("INVALID_SUBACTION"));
        return true;
    }

    const bool bSave = GetJsonBoolField(Payload, TEXT("save"), true);

    if (SubAction == TEXT("search_static_mesh_assets") || SubAction == TEXT("validate_static_mesh_assets"))
    {
        const bool bAllowFallbackMesh = GetJsonBoolField(Payload, TEXT("allowFallbackMesh"), false) ||
            GetJsonBoolField(Payload, TEXT("allowCube"), false);
        const bool bSuitableOnly = GetJsonBoolField(Payload, TEXT("suitableOnly"), true);
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        TArray<TSharedPtr<FJsonValue>> ValidValues;
        TArray<TSharedPtr<FJsonValue>> InvalidValues;

        if (SubAction == TEXT("validate_static_mesh_assets"))
        {
            TArray<FString> RequestedPaths = GetPCGStringArrayField(Payload, {TEXT("meshPaths"), TEXT("paths")});
            const FString SingleMeshPath = GetFirstStringField(Payload, {TEXT("meshPath"), TEXT("staticMesh")});
            if (!SingleMeshPath.IsEmpty())
            {
                RequestedPaths.AddUnique(SingleMeshPath);
            }
            const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
            if (Payload->TryGetArrayField(TEXT("meshEntries"), Entries) && Entries)
            {
                for (const TSharedPtr<FJsonValue>& Entry : *Entries)
                {
                    if (Entry && Entry->Type == EJson::Object)
                    {
                        const FString EntryPath = GetFirstStringField(Entry->AsObject(), {TEXT("meshPath"), TEXT("staticMesh"), TEXT("StaticMesh")});
                        if (!EntryPath.IsEmpty())
                        {
                            RequestedPaths.AddUnique(EntryPath);
                        }
                    }
                }
            }
            for (const FString& RequestedPath : RequestedPaths)
            {
                FString ResolvedPath;
                FString ValidationError;
                if (ValidatePCGStaticMeshPath(RequestedPath, bAllowFallbackMesh, ResolvedPath, ValidationError))
                {
                    UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(ResolvedPath));
                    ValidValues.Add(MakeShared<FJsonValueObject>(BuildPCGStaticMeshAssetResult(ResolvedPath, Mesh)));
                }
                else
                {
                    TSharedPtr<FJsonObject> Invalid = MakeShared<FJsonObject>();
                    Invalid->SetStringField(TEXT("requestedPath"), RequestedPath);
                    Invalid->SetStringField(TEXT("error"), ValidationError);
                    InvalidValues.Add(MakeShared<FJsonValueObject>(Invalid));
                }
            }
            Result->SetArrayField(TEXT("validAssets"), ValidValues);
            Result->SetArrayField(TEXT("invalidAssets"), InvalidValues);
            Result->SetNumberField(TEXT("validCount"), ValidValues.Num());
            Result->SetNumberField(TEXT("invalidCount"), InvalidValues.Num());
            Result->SetBoolField(TEXT("valid"), InvalidValues.Num() == 0 && ValidValues.Num() > 0);
            Result->SetBoolField(TEXT("allowFallbackMesh"), bAllowFallbackMesh);
            SendAutomationResponse(Socket, RequestId, true, TEXT("Static Mesh assets validated."), Result);
            return true;
        }

        FString Query = GetJsonStringField(Payload, TEXT("query"));
        Query.ToLowerInline();
        const int32 Limit = FMath::Clamp(GetJsonIntField(Payload, TEXT("limit"), 24), 1, 256);
        const int32 Offset = FMath::Max(0, GetJsonIntField(Payload, TEXT("offset"), 0));
        TArray<FString> SearchPaths = GetPCGStringArrayField(Payload, {TEXT("searchPaths"), TEXT("packagePaths")});
        if (SearchPaths.Num() == 0)
        {
            SearchPaths.Add(TEXT("/Game"));
        }

        FARFilter Filter;
        Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        for (const FString& SearchPath : SearchPaths)
        {
            FString NormalizedSearchPath = SearchPath;
            NormalizedSearchPath.TrimStartAndEndInline();
            if (!NormalizedSearchPath.StartsWith(TEXT("/")))
            {
                NormalizedSearchPath = TEXT("/") + NormalizedSearchPath;
            }
            Filter.PackagePaths.Add(FName(*NormalizedSearchPath));
        }
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        FAssetRegistryModule::GetRegistry().GetAssets(Filter, Assets);
        Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
        {
            return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
        });
        int32 MatchingIndex = 0;
        for (const FAssetData& Asset : Assets)
        {
            if (ValidValues.Num() >= Limit)
            {
                break;
            }
            const FString CandidatePath = Asset.GetSoftObjectPath().ToString();
            FString SearchText = CandidatePath;
            SearchText.ToLowerInline();
            if (!Query.IsEmpty() && !SearchText.Contains(Query))
            {
                continue;
            }
            if (IsPCGFallbackMeshPath(CandidatePath) && !bAllowFallbackMesh)
            {
                continue;
            }
            if (bSuitableOnly && !IsPCGEnvironmentMeshCandidate(CandidatePath))
            {
                continue;
            }
            if (MatchingIndex++ < Offset)
            {
                continue;
            }
            FString ResolvedPath;
            FString ValidationError;
            if (!ValidatePCGStaticMeshPath(CandidatePath, bAllowFallbackMesh, ResolvedPath, ValidationError))
            {
                continue;
            }
            UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(ResolvedPath));
            ValidValues.Add(MakeShared<FJsonValueObject>(BuildPCGStaticMeshAssetResult(ResolvedPath, Mesh)));
        }
        Result->SetArrayField(TEXT("assets"), ValidValues);
        Result->SetNumberField(TEXT("count"), ValidValues.Num());
        Result->SetNumberField(TEXT("candidateCount"), Assets.Num());
        Result->SetBoolField(TEXT("suitableOnly"), bSuitableOnly);
        Result->SetBoolField(TEXT("allowFallbackMesh"), bAllowFallbackMesh);
        SendAutomationResponse(Socket, RequestId, true, TEXT("Static Mesh assets searched and validated."), Result);
        return true;
    }

    if (SubAction == TEXT("create_pcg_graph"))
    {
        FString GraphPath;
        FString Error;
        if (!TryGetPCGAssetPath(Payload, {TEXT("graphPath"), TEXT("assetPath")}, GraphPath, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_ARGUMENT"));
            return true;
        }

        const bool bOverwrite = GetJsonBoolField(Payload, TEXT("overwrite"), false);
        bool bCreated = false;
        bool bSaved = false;
        UPCGGraph* Graph = CreateOrReusePCGGraph(GraphPath, bOverwrite, bSave, bCreated, bSaved, Error);
        if (!Graph)
        {
            SendAutomationError(Socket, RequestId, Error, bOverwrite ? TEXT("CREATE_FAILED") : TEXT("ASSET_ALREADY_EXISTS"));
            return true;
        }

        SendAutomationResponse(Socket, RequestId, true, bCreated ? TEXT("PCG graph created.") : TEXT("PCG graph already exists."), BuildGraphResult(Graph, GraphPath, bCreated, bSaved));
        return true;
    }

    if (SubAction == TEXT("create_pcg_subgraph"))
    {
        FString SubgraphPath;
        FString Error;
        if (!TryGetPCGAssetPath(Payload, {TEXT("subgraphPath"), TEXT("graphPath"), TEXT("assetPath")}, SubgraphPath, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_ARGUMENT"));
            return true;
        }

        const FString ParentGraphRawPath = GetJsonStringField(Payload, TEXT("parentGraphPath"));
        FString ParentGraphPath;
        UPCGGraph* ParentGraph = nullptr;
        if (!ParentGraphRawPath.IsEmpty())
        {
            ParentGraph = LoadPCGGraph(ParentGraphRawPath, ParentGraphPath, Error);
            if (!ParentGraph)
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("ASSET_NOT_FOUND"));
                return true;
            }
        }

        const bool bOverwrite = GetJsonBoolField(Payload, TEXT("overwrite"), false);
        bool bCreated = false;
        bool bSubgraphSaved = false;
        UPCGGraph* Subgraph = CreateOrReusePCGGraph(SubgraphPath, bOverwrite, bSave, bCreated, bSubgraphSaved, Error);
        if (!Subgraph)
        {
            SendAutomationError(Socket, RequestId, Error, bOverwrite ? TEXT("CREATE_FAILED") : TEXT("ASSET_ALREADY_EXISTS"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = BuildGraphResult(Subgraph, SubgraphPath, bCreated, bSubgraphSaved);
        Result->SetStringField(TEXT("subgraphPath"), SubgraphPath);

        if (ParentGraph)
        {
            UPCGSettings* DefaultSettings = nullptr;
            UPCGNode* Node = ParentGraph->AddNodeOfType(UPCGSubgraphSettings::StaticClass(), DefaultSettings);
            UPCGBaseSubgraphSettings* SubgraphSettings = Cast<UPCGBaseSubgraphSettings>(DefaultSettings);
            if (!Node || !SubgraphSettings)
            {
                SendAutomationError(Socket, RequestId, TEXT("Failed to create PCG subgraph node."), TEXT("CREATE_FAILED"));
                return true;
            }

            SubgraphSettings->SetSubgraph(Subgraph);
            ApplyNodeMetadata(Node, Payload);
            Node->UpdateAfterSettingsChangeDuringCreation();
            SubgraphSettings->PostEditChange();
            bool bParentSaved = false;
            if (!SaveGraphIfRequested(ParentGraph, bSave, bParentSaved, Error))
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
                return true;
            }

            Result->SetStringField(TEXT("parentGraphPath"), ParentGraphPath);
            Result->SetStringField(TEXT("nodeId"), Node->GetName());
            Result->SetStringField(TEXT("nodeName"), Node->GetName());
            Result->SetBoolField(TEXT("parentSaved"), bParentSaved);
        }

        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG subgraph created."), Result);
        return true;
    }

    if (SubAction == TEXT("regenerate_pcg_component") || SubAction == TEXT("read_pcg_generated_instances") || SubAction == TEXT("clear_pcg_generated_output"))
    {
        UWorld* World = GetPCGEditorWorld();
        AActor* Actor = nullptr;
        FString ComponentError;
        UPCGComponent* Component = ResolveRequestedPCGComponent(Payload, World, Actor, ComponentError);
        if (!Component)
        {
            SendAutomationError(Socket, RequestId, ComponentError, TEXT("COMPONENT_NOT_FOUND"));
            return true;
        }

        if (SubAction == TEXT("read_pcg_generated_instances"))
        {
            TSharedPtr<FJsonObject> Result = BuildGeneratedInstancesResult(Component);
            Result->SetStringField(TEXT("actorName"), Actor ? Actor->GetName() : FString());
            Result->SetStringField(TEXT("componentName"), Component->GetName());
            Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
            SendAutomationResponse(Socket, RequestId, true, TEXT("PCG generated instances read."), Result);
            return true;
        }

        if (SubAction == TEXT("clear_pcg_generated_output"))
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Clear PCG Generated Output")));
            Component->Modify();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
            Component->CleanupLocalImmediate(true, true);
            Component->ClearPerPinGeneratedOutput();
#else
            Component->CleanupLocal(true);
#endif
            bool bLevelSaved = false;
            FString SaveError;
            if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, SaveError))
            {
                SendAutomationError(Socket, RequestId, SaveError, TEXT("SAVE_FAILED"));
                return true;
            }
            TSharedPtr<FJsonObject> Result = BuildGeneratedInstancesResult(Component);
            Result->SetStringField(TEXT("actorName"), Actor ? Actor->GetName() : FString());
            Result->SetStringField(TEXT("componentName"), Component->GetName());
            Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
            Result->SetBoolField(TEXT("saved"), bLevelSaved);
            SendAutomationResponse(Socket, RequestId, true, TEXT("PCG generated output cleared safely."), Result);
            return true;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Regenerate PCG Component")));
        Component->Modify();
        const bool bForceGenerate = GetJsonBoolField(Payload, TEXT("force"), true);
        const FPCGTaskId TaskId = Component->GenerateLocalGetTaskId(bForceGenerate);
        if (TaskId == InvalidPCGTaskId)
        {
            SendAutomationError(Socket, RequestId, TEXT("PCG regeneration was not scheduled. The component may already be generating, be up to date with force=false, or have no graph."), TEXT("GENERATION_NOT_SCHEDULED"));
            return true;
        }
        const bool bWaitForGeneration = GetJsonBoolField(Payload, TEXT("wait"), false) ||
            GetJsonBoolField(Payload, TEXT("waitForGeneration"), false);
        if (bWaitForGeneration)
        {
            SchedulePCGGenerationWait(this, Socket, RequestId, World, Actor, Component, FString(), TaskId, bSave,
                FMath::Clamp(GetJsonIntField(Payload, TEXT("timeoutMs"), 120000), 1000, 600000));
            return true;
        }
        bool bLevelSaved = false;
        FString SaveError;
        if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, SaveError))
        {
            SendAutomationError(Socket, RequestId, SaveError, TEXT("SAVE_FAILED"));
            return true;
        }
        TSharedPtr<FJsonObject> Result = BuildGeneratedInstancesResult(Component);
        Result->SetStringField(TEXT("actorName"), Actor ? Actor->GetName() : FString());
        Result->SetStringField(TEXT("componentName"), Component->GetName());
        Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
        Result->SetNumberField(TEXT("taskId"), static_cast<double>(TaskId));
        Result->SetBoolField(TEXT("force"), bForceGenerate);
        Result->SetBoolField(TEXT("waited"), false);
        Result->SetBoolField(TEXT("saved"), bLevelSaved);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG component regeneration started."), Result);
        return true;
    }

    if (SubAction == TEXT("execute_pcg_graph"))
    {
        UWorld* World = GetPCGEditorWorld();
        if (!World)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve the editor world for PCG execution."), TEXT("WORLD_NOT_FOUND"));
            return true;
        }

        FString Error;
        FString GraphPath;
        UPCGGraph* Graph = nullptr;
        const FString GraphRawPath = GetFirstStringField(Payload, {TEXT("graphPath"), TEXT("assetPath")});
        if (!GraphRawPath.IsEmpty())
        {
            Graph = LoadPCGGraph(GraphRawPath, GraphPath, Error);
            if (!Graph)
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("ASSET_NOT_FOUND"));
                return true;
            }
        }

        const FString ActorName = GetJsonStringField(Payload, TEXT("actorName"));
        const FString ComponentName = GetJsonStringField(Payload, TEXT("componentName"));
        const FString ComponentPath = GetJsonStringField(Payload, TEXT("componentPath"));
        const FString ComponentSelector = !ComponentPath.IsEmpty() ? ComponentPath : ComponentName;
        const bool bCreateComponent = GetJsonBoolField(Payload, TEXT("createComponent"), false);
        AActor* Actor = nullptr;
        UPCGComponent* Component = FindPCGComponent(World, ActorName, ComponentSelector, Actor);
        if (!Component && !bCreateComponent && !HasPCGComponentSelector(ActorName, ComponentSelector))
        {
            SendAutomationError(Socket, RequestId, TEXT("execute_pcg_graph requires actorName, componentName, or componentPath when createComponent is false."), TEXT("INVALID_ARGUMENT"));
            return true;
        }
        if (!Component && bCreateComponent)
        {
            Actor = FindPCGActor(World, ActorName);
            if (!Actor)
            {
                SendAutomationError(Socket, RequestId, TEXT("createComponent requires an existing actorName."), TEXT("ACTOR_NOT_FOUND"));
                return true;
            }
            Component = CreatePCGComponent(Actor, ComponentName);
        }
        if (!Component)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve a PCG component. Provide actorName/componentName, or actorName with createComponent=true."), TEXT("COMPONENT_NOT_FOUND"));
            return true;
        }

        Component->Modify();
        if (Graph)
        {
            Component->SetGraphLocal(Graph);
        }

        const bool bForceGenerate = GetJsonBoolField(Payload, TEXT("force"), true);
        const FPCGTaskId TaskId = Component->GenerateLocalGetTaskId(bForceGenerate);
        if (TaskId == InvalidPCGTaskId)
        {
            SendAutomationError(Socket, RequestId, TEXT("PCG generation was not scheduled. The component may already be generating, be up to date with force=false, have no graph, or have invalid bounds."), TEXT("GENERATION_NOT_SCHEDULED"));
            return true;
        }

        const bool bWaitForGeneration = GetJsonBoolField(Payload, TEXT("wait"), false) ||
            GetJsonBoolField(Payload, TEXT("waitForGeneration"), false);
        if (bWaitForGeneration)
        {
            SchedulePCGGenerationWait(this, Socket, RequestId, World, Actor, Component, GraphPath, TaskId, bSave,
                FMath::Clamp(GetJsonIntField(Payload, TEXT("timeoutMs"), 120000), 1000, 600000));
            return true;
        }

        bool bLevelSaved = false;
        if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("graphPath"), GraphPath);
        Result->SetStringField(TEXT("actorName"), Actor ? Actor->GetName() : FString());
        Result->SetStringField(TEXT("componentName"), Component->GetName());
        Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
        Result->SetNumberField(TEXT("taskId"), static_cast<double>(TaskId));
        Result->SetBoolField(TEXT("force"), bForceGenerate);
        Result->SetBoolField(TEXT("waited"), false);
        Result->SetBoolField(TEXT("saved"), bLevelSaved);
        McpHandlerUtils::AddVerification(Result, Component);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG graph generation started."), Result);
        return true;
    }

    if (SubAction == TEXT("set_pcg_partition_grid_size"))
    {
        UWorld* World = GetPCGEditorWorld();
        if (!World)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve the editor world for PCG partition grid size."), TEXT("WORLD_NOT_FOUND"));
            return true;
        }

        const int32 GridSize = GetJsonIntField(Payload, TEXT("gridSize"), 0);
        if (GridSize <= 0)
        {
            SendAutomationError(Socket, RequestId, TEXT("set_pcg_partition_grid_size requires a positive gridSize."), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString Scope = TEXT("world");
        if (Payload.IsValid() && Payload->HasField(TEXT("scope")) && !Payload->TryGetStringField(TEXT("scope"), Scope))
        {
            SendAutomationError(Socket, RequestId, TEXT("set_pcg_partition_grid_size scope must be a string: 'world' or 'component'."), TEXT("INVALID_ARGUMENT"));
            return true;
        }
        Scope = Scope.ToLower();
        if (Scope != TEXT("world") && Scope != TEXT("component"))
        {
            SendAutomationError(Socket, RequestId, TEXT("set_pcg_partition_grid_size scope must be 'world' or 'component'."), TEXT("INVALID_ARGUMENT"));
            return true;
        }
        if (Scope == TEXT("component"))
        {
            const FString ActorName = GetJsonStringField(Payload, TEXT("actorName"));
            const FString ComponentName = GetJsonStringField(Payload, TEXT("componentName"));
            const FString ComponentPath = GetJsonStringField(Payload, TEXT("componentPath"));
            const FString ComponentSelector = !ComponentPath.IsEmpty() ? ComponentPath : ComponentName;
            if (!HasPCGComponentSelector(ActorName, ComponentSelector))
            {
                SendAutomationError(Socket, RequestId, TEXT("component-scoped partition grid size requires actorName, componentName, or componentPath."), TEXT("INVALID_ARGUMENT"));
                return true;
            }
            AActor* Actor = nullptr;
            UPCGComponent* Component = FindPCGComponent(World, ActorName, ComponentSelector, Actor);
            if (!Component)
            {
                SendAutomationError(Socket, RequestId, TEXT("Could not resolve a PCG component for component-scoped grid size."), TEXT("COMPONENT_NOT_FOUND"));
                return true;
            }

            const uint32 PreviousGridSize = Component->GetGenerationGridSize();
            Component->Modify();
            Component->SetGenerationGridSize(static_cast<uint32>(GridSize));
            Component->PostEditChange();

            FString Error;
            bool bLevelSaved = false;
            if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, Error))
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
                return true;
            }

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("scope"), TEXT("component"));
            Result->SetStringField(TEXT("actorName"), Actor ? Actor->GetName() : FString());
            Result->SetStringField(TEXT("componentName"), Component->GetName());
            Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
            Result->SetNumberField(TEXT("previousGridSize"), PreviousGridSize);
            Result->SetNumberField(TEXT("gridSize"), GridSize);
            Result->SetBoolField(TEXT("saved"), bLevelSaved);
            McpHandlerUtils::AddVerification(Result, Component);
            SendAutomationResponse(Socket, RequestId, true, TEXT("PCG component generation grid size updated."), Result);
            return true;
        }

        APCGWorldActor* PCGWorldActor = PCGHelpers::GetPCGWorldActor(World);
        if (!PCGWorldActor)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve or create the PCG world actor."), TEXT("PCG_WORLD_ACTOR_NOT_FOUND"));
            return true;
        }

        const uint32 PreviousGridSize = PCGWorldActor->PartitionGridSize;
        FProperty* PartitionGridSizeProperty = FindFProperty<FProperty>(APCGWorldActor::StaticClass(), GET_MEMBER_NAME_CHECKED(APCGWorldActor, PartitionGridSize));
        PCGWorldActor->Modify();
        if (PartitionGridSizeProperty)
        {
            PCGWorldActor->PreEditChange(PartitionGridSizeProperty);
        }
        PCGWorldActor->PartitionGridSize = static_cast<uint32>(GridSize);
        if (PartitionGridSizeProperty)
        {
            FPropertyChangedEvent PropertyChangedEvent(PartitionGridSizeProperty, EPropertyChangeType::ValueSet);
            PCGWorldActor->PostEditChangeProperty(PropertyChangedEvent);
        }
        else
        {
            PCGWorldActor->PostEditChange();
        }

        FString Error;
        bool bLevelSaved = false;
        if (!SaveEditorWorldIfRequested(World, bSave, bLevelSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("scope"), TEXT("world"));
        Result->SetNumberField(TEXT("previousGridSize"), PreviousGridSize);
        Result->SetNumberField(TEXT("gridSize"), PCGWorldActor->PartitionGridSize);
        Result->SetBoolField(TEXT("saved"), bLevelSaved);
        McpHandlerUtils::AddVerification(Result, PCGWorldActor);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG partition grid size updated."), Result);
        return true;
    }

    FString GraphRawPath = GetFirstStringField(Payload, {TEXT("graphPath"), TEXT("assetPath")});
    if (GraphRawPath.IsEmpty())
    {
        SendAutomationError(Socket, RequestId, TEXT("Missing 'graphPath'."), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    FString GraphPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGGraph(GraphRawPath, GraphPath, Error);
    if (!Graph)
    {
        SendAutomationError(Socket, RequestId, Error, TEXT("ASSET_NOT_FOUND"));
        return true;
    }

    if (SubAction == TEXT("find_static_mesh_spawner") || SubAction == TEXT("inspect_static_mesh_spawner") ||
        SubAction == TEXT("configure_static_mesh_spawner") || SubAction == TEXT("add_static_mesh_entry") ||
        SubAction == TEXT("update_static_mesh_entry") || SubAction == TEXT("remove_static_mesh_entry"))
    {
        const FString RequestedNodeId = GetFirstStringField(Payload, {TEXT("nodeId"), TEXT("nodeName")});
        UPCGNode* Node = FindStaticMeshSpawnerNode(Graph, RequestedNodeId, Error);
        if (!Node)
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("NODE_NOT_FOUND"));
            return true;
        }
        if (SubAction == TEXT("find_static_mesh_spawner") || SubAction == TEXT("inspect_static_mesh_spawner"))
        {
            TSharedPtr<FJsonObject> Result = BuildStaticMeshSpawnerResult(Graph, Node, GraphPath, Error);
            if (!Result.IsValid())
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("REFLECTION_ERROR"));
                return true;
            }
            SendAutomationResponse(Socket, RequestId, true, TEXT("Static Mesh Spawner inspected."), Result);
            return true;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Author PCG Static Mesh Spawner")));
        UPCGStaticMeshSpawnerSettings* SpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings());
        UObject* Selector = GetPCGMeshSelector(SpawnerSettings, Error);
        FArrayProperty* EntriesProperty = nullptr;
        if (Selector)
        {
            EntriesProperty = GetPCGMeshEntriesProperty(Selector, Error);
        }
        if (SubAction != TEXT("configure_static_mesh_spawner") && !EntriesProperty)
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("REFLECTION_ERROR"));
            return true;
        }
        FStructProperty* EntryStructProperty = EntriesProperty ? CastField<FStructProperty>(EntriesProperty->Inner) : nullptr;
        TUniquePtr<FScriptArrayHelper> Entries;
        if (EntriesProperty)
        {
            Entries = MakeUnique<FScriptArrayHelper>(EntriesProperty, EntriesProperty->ContainerPtrToValuePtr<void>(Selector));
        }

        int32 Applied = 0;
        if (SubAction == TEXT("configure_static_mesh_spawner"))
        {
            TSharedPtr<FJsonObject> Configuration = MakeShared<FJsonObject>();
            for (const TCHAR* Field : {TEXT("meshSelectorType"), TEXT("selectorType"), TEXT("meshEntries"), TEXT("meshPath"), TEXT("staticMesh"), TEXT("allowFallbackMesh"), TEXT("allowCube")})
            {
                if (const TSharedPtr<FJsonValue>* Value = Payload->Values.Find(Field))
                {
                    Configuration->SetField(Field, *Value);
                }
            }
            if (const TSharedPtr<FJsonObject>* SettingsObject = nullptr; Payload->TryGetObjectField(TEXT("settings"), SettingsObject) && SettingsObject && SettingsObject->IsValid())
            {
                if (!ApplyStaticMeshSpawnerSettingsObject(SpawnerSettings, *SettingsObject, Error, Applied))
                {
                    SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                    return true;
                }
            }
            if (!Configuration->Values.IsEmpty())
            {
                int32 ConfigurationApplied = 0;
                if (!ApplyStaticMeshSpawnerSettingsObject(SpawnerSettings, Configuration, Error, ConfigurationApplied))
                {
                    SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                    return true;
                }
                Applied += ConfigurationApplied;
            }
        }
        else
        {
            int32 EntryIndex = GetJsonIntField(Payload, TEXT("entryIndex"), INDEX_NONE);
            if (SubAction == TEXT("add_static_mesh_entry"))
            {
                const TSharedPtr<FJsonObject>* EntryObject = nullptr;
                const TSharedPtr<FJsonObject> EntryPayload = (Payload->TryGetObjectField(TEXT("entry"), EntryObject) && EntryObject && EntryObject->IsValid()) ? *EntryObject : Payload;
                Entries->AddValue();
                if (!ApplyPCGMeshEntry(Entries->GetRawPtr(Entries->Num() - 1), EntryStructProperty, EntryPayload, true, Error))
                {
                    Entries->RemoveValues(Entries->Num() - 1, 1);
                    SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                    return true;
                }
                EntryIndex = Entries->Num() - 1;
                Applied = 1;
            }
            else if (EntryIndex < 0 || EntryIndex >= Entries->Num())
            {
                SendAutomationError(Socket, RequestId, FString::Printf(TEXT("entryIndex %d is outside reflected MeshEntries range [0, %d)."), EntryIndex, Entries->Num()), TEXT("INVALID_ARGUMENT"));
                return true;
            }
            else if (SubAction == TEXT("remove_static_mesh_entry"))
            {
                Entries->RemoveValues(EntryIndex, 1);
                Applied = 1;
            }
            else
            {
                const TSharedPtr<FJsonObject>* EntryObject = nullptr;
                const TSharedPtr<FJsonObject> EntryPayload = (Payload->TryGetObjectField(TEXT("entry"), EntryObject) && EntryObject && EntryObject->IsValid()) ? *EntryObject : Payload;
                if (!ApplyPCGMeshEntry(Entries->GetRawPtr(EntryIndex), EntryStructProperty, EntryPayload, false, Error))
                {
                    SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                    return true;
                }
                Applied = 1;
            }
        }

        SpawnerSettings->Modify();
        Node->UpdateAfterSettingsChangeDuringCreation();
        SpawnerSettings->PostEditChange();
        bool bSaved = false;
        if (!SaveGraphIfRequested(Graph, bSave, bSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }
        TSharedPtr<FJsonObject> Result = BuildStaticMeshSpawnerResult(Graph, Node, GraphPath, Error);
        if (!Result.IsValid())
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("REFLECTION_ERROR"));
            return true;
        }
        Result->SetNumberField(TEXT("settingsApplied"), Applied);
        Result->SetNumberField(TEXT("entryIndex"), GetJsonIntField(Payload, TEXT("entryIndex"), SubAction == TEXT("add_static_mesh_entry") ? Entries->Num() - 1 : INDEX_NONE));
        Result->SetBoolField(TEXT("saved"), bSaved);
        SendAutomationResponse(Socket, RequestId, true, TEXT("Static Mesh Spawner updated."), Result);
        return true;
    }

    if (SubAction == TEXT("add_pcg_node") || IsPCGNodeCreationAction(SubAction))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Add PCG Node")));
        FString NodeType = GetFirstStringField(Payload, {TEXT("settingsClass"), TEXT("nodeType")});
        if (NodeType.IsEmpty())
        {
            NodeType = SubAction;
        }
        UClass* SettingsClass = ResolvePCGSettingsClass(NodeType);
        if (!SettingsClass)
        {
            SendAutomationError(Socket, RequestId, FString::Printf(TEXT("Could not resolve PCG settings class '%s'."), *NodeType), TEXT("CLASS_NOT_FOUND"));
            return true;
        }

        TSubclassOf<UPCGSettings> SettingsSubclass;
        SettingsSubclass = SettingsClass;
        UPCGSettings* DefaultSettings = nullptr;
        UPCGNode* Node = Graph->AddNodeOfType(SettingsSubclass, DefaultSettings);
        if (!Node || !DefaultSettings)
        {
            SendAutomationError(Socket, RequestId, TEXT("Failed to add PCG node."), TEXT("CREATE_FAILED"));
            return true;
        }

        const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
        int32 AppliedSettings = 0;
        if (Payload->TryGetObjectField(TEXT("settings"), SettingsObject) && SettingsObject && SettingsObject->IsValid())
        {
            const bool bStaticMeshSpawnerSettings = DefaultSettings->IsA<UPCGStaticMeshSpawnerSettings>();
            if ((bStaticMeshSpawnerSettings && !ApplyStaticMeshSpawnerSettingsObject(Cast<UPCGStaticMeshSpawnerSettings>(DefaultSettings), *SettingsObject, Error, AppliedSettings)) ||
                (!bStaticMeshSpawnerSettings && !ApplySettingsObject(DefaultSettings, *SettingsObject, Error, AppliedSettings)))
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                return true;
            }
        }

        int32 AppliedConvenienceSettings = 0;
        if (!ApplyPCGConvenienceSettings(SubAction, DefaultSettings, Payload, Error, AppliedConvenienceSettings))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
            return true;
        }

        ApplyNodeMetadata(Node, Payload);
        Node->UpdateAfterSettingsChangeDuringCreation();
        DefaultSettings->PostEditChange();

        bool bSaved = false;
        if (!SaveGraphIfRequested(Graph, bSave, bSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = BuildNodeResult(Graph, Node, GraphPath);
        Result->SetNumberField(TEXT("settingsApplied"), AppliedSettings + AppliedConvenienceSettings);
        Result->SetBoolField(TEXT("saved"), bSaved);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG node added."), Result);
        return true;
    }

    if (SubAction == TEXT("connect_pcg_pins"))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Connect PCG Pins")));
        const FString SourceNodeId = GetJsonStringField(Payload, TEXT("sourceNodeId"));
        const FString TargetNodeId = GetJsonStringField(Payload, TEXT("targetNodeId"));
        if (SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty())
        {
            SendAutomationError(Socket, RequestId, TEXT("connect_pcg_pins requires sourceNodeId and targetNodeId."), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UPCGNode* SourceNode = FindPCGNode(Graph, SourceNodeId);
        UPCGNode* TargetNode = FindPCGNode(Graph, TargetNodeId);
        if (!SourceNode || !TargetNode)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve source or target PCG node."), TEXT("NODE_NOT_FOUND"));
            return true;
        }

        const FString SourcePinLabel = GetFirstStringField(Payload, {TEXT("sourcePin"), TEXT("outputName")});
        const FString TargetPinLabel = GetFirstStringField(Payload, {TEXT("targetPin"), TEXT("inputName")});
        FName SourcePin;
        FName TargetPin;
        if (!TryResolvePCGPinLabel(SourceNode, true, SourcePinLabel, SourcePin, Error) ||
            !TryResolvePCGPinLabel(TargetNode, false, TargetPinLabel, TargetPin, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("PIN_NOT_FOUND"));
            return true;
        }

        UPCGPin* SourcePinObject = SourceNode->GetOutputPin(SourcePin);
        UPCGPin* TargetPinObject = TargetNode->GetInputPin(TargetPin);
        if (!HasPCGEdge(SourcePinObject, TargetPinObject))
        {
            Graph->AddLabeledEdge(SourceNode, SourcePin, TargetNode, TargetPin);
        }

        if (!HasPCGEdge(SourcePinObject, TargetPinObject))
        {
            SendAutomationError(Socket, RequestId, TEXT("Failed to connect PCG pins."), TEXT("CONNECT_FAILED"));
            return true;
        }

        bool bSaved = false;
        if (!SaveGraphIfRequested(Graph, bSave, bSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("graphPath"), GraphPath);
        Result->SetStringField(TEXT("sourceNodeId"), SourceNode->GetName());
        Result->SetStringField(TEXT("targetNodeId"), TargetNode->GetName());
        Result->SetStringField(TEXT("sourcePin"), SourcePin.ToString());
        Result->SetStringField(TEXT("targetPin"), TargetPin.ToString());
        Result->SetBoolField(TEXT("saved"), bSaved);
        McpHandlerUtils::AddVerification(Result, Graph);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG pins connected."), Result);
        return true;
    }

    if (SubAction == TEXT("set_pcg_node_settings"))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Set PCG Node Settings")));
        const FString NodeId = GetFirstStringField(Payload, {TEXT("nodeId"), TEXT("nodeName")});
        UPCGNode* Node = FindPCGNode(Graph, NodeId);
        if (!Node)
        {
            SendAutomationError(Socket, RequestId, TEXT("Could not resolve PCG node."), TEXT("NODE_NOT_FOUND"));
            return true;
        }

        UPCGSettings* Settings = Node->GetSettings();
        if (!Settings)
        {
            SendAutomationError(Socket, RequestId, TEXT("PCG node has no editable settings."), TEXT("SETTINGS_NOT_FOUND"));
            return true;
        }

        int32 AppliedSettings = 0;
        const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
        if (Payload->TryGetObjectField(TEXT("settings"), SettingsObject) && SettingsObject && SettingsObject->IsValid())
        {
            const bool bStaticMeshSpawnerSettings = Settings->IsA<UPCGStaticMeshSpawnerSettings>();
            if ((bStaticMeshSpawnerSettings && !ApplyStaticMeshSpawnerSettingsObject(Cast<UPCGStaticMeshSpawnerSettings>(Settings), *SettingsObject, Error, AppliedSettings)) ||
                (!bStaticMeshSpawnerSettings && !ApplySettingsObject(Settings, *SettingsObject, Error, AppliedSettings)))
            {
                SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
                return true;
            }
        }

        int32 AppliedConvenienceSettings = 0;
        if (!ApplyPCGConvenienceSettings(SubAction, Settings, Payload, Error, AppliedConvenienceSettings))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("INVALID_SETTINGS"));
            return true;
        }

        ApplyNodeMetadata(Node, Payload);
        Node->UpdateAfterSettingsChangeDuringCreation();
        Settings->PostEditChange();

        bool bSaved = false;
        if (!SaveGraphIfRequested(Graph, bSave, bSaved, Error))
        {
            SendAutomationError(Socket, RequestId, Error, TEXT("SAVE_FAILED"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = BuildNodeResult(Graph, Node, GraphPath);
        Result->SetNumberField(TEXT("settingsApplied"), AppliedSettings + AppliedConvenienceSettings);
        Result->SetBoolField(TEXT("saved"), bSaved);
        SendAutomationResponse(Socket, RequestId, true, TEXT("PCG node settings updated."), Result);
        return true;
    }

    SendAutomationError(Socket, RequestId, FString::Printf(TEXT("Unhandled PCG subAction: %s"), *SubAction), TEXT("INVALID_SUBACTION"));
    return true;
#endif
}
