// UE 5.8.1 landscape and foliage authoring extensions.
//
// This file intentionally owns only actors tagged MCP.GeneratedLandscapeFoliage.
// It never uses a primitive fallback mesh and therefore clear/regenerate cannot
// affect hand-authored foliage, Landscape Grass, PCG output, or unrelated HISM.

#include "McpVersionCompatibility.h"
#include "McpHandlerUtils.h"
#include "McpAutomationBridgeGlobals.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "McpSafeOperations.h"

#if WITH_EDITOR
#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Math/NumericLimits.h"
#include "Math/RandomStream.h"
#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMcpLandscapeFoliageAuthoring, Log, All);

#if WITH_EDITOR
namespace
{
constexpr const TCHAR* GeneratedFoliageTag = TEXT("MCP.GeneratedLandscapeFoliage");
constexpr const TCHAR* GeneratedFoliageNamePrefix = TEXT("MCP.GeneratedLandscapeFoliage.Name=");
constexpr const TCHAR* GeneratedFoliageSeedPrefix = TEXT("MCP.GeneratedLandscapeFoliage.Seed=");

struct FMcpScatterMesh
{
    FString Path;
    int32 Count = 0;
    float Density = 0.0f;
    float MinScale = 1.0f;
    float MaxScale = 1.0f;
};

struct FMcpExclusionBox
{
    FBox Box = FBox(EForceInit::ForceInit);
};

static bool McpReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& Out)
{
    if (!Object.IsValid()) return false;
    const TSharedPtr<FJsonObject>* VectorObject = nullptr;
    if (!Object->TryGetObjectField(Field, VectorObject) || !VectorObject || !VectorObject->IsValid()) return false;
    double X = 0.0, Y = 0.0, Z = 0.0;
    const bool bX = (*VectorObject)->TryGetNumberField(TEXT("x"), X);
    const bool bY = (*VectorObject)->TryGetNumberField(TEXT("y"), Y);
    (*VectorObject)->TryGetNumberField(TEXT("z"), Z);
    if (!bX || !bY) return false;
    Out = FVector(X, Y, Z);
    return true;
}

static ALandscape* McpFindLandscape(UWorld* World, const FString& Name, const FString& Path)
{
    if (!World) return nullptr;
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        ALandscape* Landscape = *It;
        if (!Landscape) continue;
        if (!Name.IsEmpty() && Landscape->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase)) return Landscape;
        if (!Path.IsEmpty() && (Landscape->GetPathName().Equals(Path, ESearchCase::IgnoreCase) ||
            Landscape->GetPackage()->GetPathName().Equals(Path, ESearchCase::IgnoreCase))) return Landscape;
    }
    return nullptr;
}

static bool McpHasGeneratedTag(const AActor* Actor)
{
    return Actor && Actor->Tags.Contains(FName(GeneratedFoliageTag));
}

static bool McpIsExcluded(const FVector& Location, const TArray<FMcpExclusionBox>& Exclusions)
{
    for (const FMcpExclusionBox& Exclusion : Exclusions)
    {
        if (Exclusion.Box.IsInsideOrOn(Location)) return true;
    }
    return false;
}

static void McpAddGeneratedFoliageSummary(AActor* Actor, TSharedPtr<FJsonObject> Result)
{
    TArray<UHierarchicalInstancedStaticMeshComponent*> Components;
    Actor->GetComponents<UHierarchicalInstancedStaticMeshComponent>(Components);
    TArray<TSharedPtr<FJsonValue>> Meshes;
    int32 Count = 0;
    for (const UHierarchicalInstancedStaticMeshComponent* Component : Components)
    {
        if (!Component) continue;
        Count += Component->GetInstanceCount();
        if (const UStaticMesh* Mesh = Component->GetStaticMesh())
        {
            Meshes.Add(MakeShared<FJsonValueString>(Mesh->GetPathName()));
        }
    }
    Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
    Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
    Result->SetNumberField(TEXT("instanceCount"), Count);
    Result->SetArrayField(TEXT("meshPaths"), Meshes);
    const FBox Bounds = Actor->GetComponentsBoundingBox(true);
    Result->SetNumberField(TEXT("minX"), Bounds.Min.X);
    Result->SetNumberField(TEXT("minY"), Bounds.Min.Y);
    Result->SetNumberField(TEXT("maxX"), Bounds.Max.X);
    Result->SetNumberField(TEXT("maxY"), Bounds.Max.Y);
}

static FString McpExternalOrOwningPackagePath(const UObject* Object)
{
    if (!Object) return FString();
    if (const UPackage* ExternalPackage = Object->GetExternalPackage()) return ExternalPackage->GetName();
    return Object->GetOutermost() ? Object->GetOutermost()->GetName() : FString();
}

static bool McpSaveLandscapeActorAndWorld(AActor* Actor, FString& OutError)
{
    UWorld* World = Actor ? Actor->GetWorld() : nullptr;
    if (!Actor || !World || !World->PersistentLevel || !World->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))) {
        OutError = TEXT("Landscape authoring requires a saved /Game world.");
        return false;
    }
    Actor->MarkPackageDirty();
    World->PersistentLevel->MarkPackageDirty();
    if (!McpSafeAssetSave(Actor) || !McpSafeLevelSave(World->PersistentLevel, World->GetOutermost()->GetName())) {
        OutError = FString::Printf(TEXT("Failed to save actor package %s or owning world %s."),
            *McpExternalOrOwningPackagePath(Actor), *World->GetOutermost()->GetName());
        return false;
    }
    return true;
}

static void McpAddLandscapeInspection(ALandscape* Landscape, TSharedPtr<FJsonObject> Result)
{
    ULandscapeInfo* LandscapeInfo = Landscape->CreateLandscapeInfo();
    if (!LandscapeInfo) return;
    LandscapeInfo->UpdateLayerInfoMap(Landscape);

    Result->SetNumberField(TEXT("componentCount"), Landscape->LandscapeComponents.Num());
    Result->SetStringField(TEXT("actorPackagePath"), McpExternalOrOwningPackagePath(Landscape));
    Result->SetStringField(TEXT("externalPackagePath"), McpExternalOrOwningPackagePath(Landscape));

    TArray<TSharedPtr<FJsonValue>> ComponentPaths;
    for (const ULandscapeComponent* Component : Landscape->LandscapeComponents) {
        if (Component) ComponentPaths.Add(MakeShared<FJsonValueString>(Component->GetPathName()));
    }
    Result->SetArrayField(TEXT("componentPaths"), ComponentPaths);

    TArray<TSharedPtr<FJsonValue>> ProxyPackagePaths;
    for (TActorIterator<ALandscapeProxy> It(Landscape->GetWorld()); It; ++It) {
        ALandscapeProxy* Proxy = *It;
        if (Proxy && Proxy->GetLandscapeGuid() == Landscape->GetLandscapeGuid()) {
            ProxyPackagePaths.Add(MakeShared<FJsonValueString>(McpExternalOrOwningPackagePath(Proxy)));
        }
    }
    Result->SetArrayField(TEXT("proxyPackagePaths"), ProxyPackagePaths);

    int32 MinX = 0, MinY = 0, MaxX = -1, MaxY = -1;
    LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
    const int64 PixelCount = MaxX >= MinX && MaxY >= MinY
        ? static_cast<int64>(MaxX - MinX + 1) * static_cast<int64>(MaxY - MinY + 1) : 0;
    TArray<TSharedPtr<FJsonValue>> LayerAssignments;
    for (const FLandscapeInfoLayerSettings& Settings : LandscapeInfo->Layers) {
        ULandscapeLayerInfoObject* LayerInfo = Settings.LayerInfoObj;
        if (!LayerInfo) continue;
        int32 NonZeroWeightCount = 0;
        if (PixelCount > 0 && PixelCount <= 16777216) {
            TArray<uint8> Weights;
            Weights.SetNumUninitialized(static_cast<int32>(PixelCount));
            FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, false);
            LandscapeEdit.GetWeightData(LayerInfo, MinX, MinY, MaxX, MaxY, Weights.GetData(), MaxX - MinX + 1);
            for (uint8 Weight : Weights) if (Weight > 0) ++NonZeroWeightCount;
        }
        TSharedPtr<FJsonObject> Assignment = McpHandlerUtils::CreateResultObject();
        Assignment->SetStringField(TEXT("layerName"), Settings.LayerName.ToString());
        Assignment->SetStringField(TEXT("layerInfoPath"), LayerInfo->GetPathName());
        Assignment->SetNumberField(TEXT("nonZeroWeightCount"), NonZeroWeightCount);
        LayerAssignments.Add(MakeShared<FJsonValueObject>(Assignment));
    }
    Result->SetArrayField(TEXT("layerAssignments"), LayerAssignments);

    int32 LayerBlendCount = 0;
    bool bConnectedToBaseColor = false;
    if (UMaterial* Material = Cast<UMaterial>(Landscape->LandscapeMaterial)) {
#if WITH_EDITORONLY_DATA
        for (UMaterialExpression* Expression : MCP_GET_MATERIAL_EXPRESSIONS(Material)) {
            if (UMaterialExpressionLandscapeLayerBlend* Blend = Cast<UMaterialExpressionLandscapeLayerBlend>(Expression)) {
                LayerBlendCount += Blend->Layers.Num();
                if (const FExpressionInput* BaseColor = Material->GetExpressionInputForProperty(MP_BaseColor)) {
                    bConnectedToBaseColor |= BaseColor->Expression == Blend;
                }
            }
        }
#endif
    }
    Result->SetNumberField(TEXT("layerCount"), LayerBlendCount);
    Result->SetBoolField(TEXT("layerBlendConnectedToBaseColor"), bConnectedToBaseColor);
}
}
#endif

bool UMcpAutomationBridgeSubsystem::HandleLandscapeFoliageAuthoring(
    const FString& RequestId, const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    const FString Lower = Action.ToLower();
    static const TSet<FString> SupportedActions = {
        TEXT("inspect_landscape"), TEXT("delete_landscape"),
        TEXT("scatter_landscape_foliage"), TEXT("regenerate_generated_foliage"),
        TEXT("inspect_generated_foliage"), TEXT("clear_generated_foliage") };
    if (!SupportedActions.Contains(Lower)) return false;

#if WITH_EDITOR
    if (!Payload.IsValid() || !GEditor || !GEditor->GetEditorWorldContext().World())
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("Editor world and payload are required."), TEXT("EDITOR_NOT_AVAILABLE"));
        return true;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    FString LandscapeName;
    FString LandscapePath;
    Payload->TryGetStringField(TEXT("landscapeName"), LandscapeName);
    Payload->TryGetStringField(TEXT("landscapePath"), LandscapePath);

    if (Lower == TEXT("inspect_landscape"))
    {
        ALandscape* Landscape = McpFindLandscape(World, LandscapeName, LandscapePath);
        if (!Landscape)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Landscape not found."), TEXT("LANDSCAPE_NOT_FOUND"));
            return true;
        }
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("landscapeName"), Landscape->GetActorLabel());
        Result->SetStringField(TEXT("landscapePath"), Landscape->GetPathName());
        Result->SetStringField(TEXT("materialPath"), Landscape->LandscapeMaterial ? Landscape->LandscapeMaterial->GetPathName() : FString());
        Result->SetNumberField(TEXT("componentSizeQuads"), Landscape->ComponentSizeQuads);
        Result->SetNumberField(TEXT("numSubsections"), Landscape->NumSubsections);
        const FBox Bounds = Landscape->GetComponentsBoundingBox(true);
        Result->SetNumberField(TEXT("minX"), Bounds.Min.X);
        Result->SetNumberField(TEXT("minY"), Bounds.Min.Y);
        Result->SetNumberField(TEXT("maxX"), Bounds.Max.X);
        Result->SetNumberField(TEXT("maxY"), Bounds.Max.Y);
        McpAddLandscapeInspection(Landscape, Result);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Landscape inspected."), Result, FString());
        return true;
    }

    if (Lower == TEXT("delete_landscape"))
    {
        ALandscape* Landscape = McpFindLandscape(World, LandscapeName, LandscapePath);
        if (!Landscape)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Landscape not found."), TEXT("LANDSCAPE_NOT_FOUND"));
            return true;
        }
        const FScopedTransaction Transaction(FText::FromString(TEXT("Delete MCP Landscape")));
        Landscape->Modify();
        const FString DeletedPath = Landscape->GetPathName();
        World->DestroyActor(Landscape, true);
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("deletedLandscapePath"), DeletedPath);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Landscape deleted."), Result, FString());
        return true;
    }

    if (Lower == TEXT("inspect_generated_foliage") || Lower == TEXT("clear_generated_foliage"))
    {
        FString RequestedName;
        Payload->TryGetStringField(TEXT("foliageName"), RequestedName);
        int32 ActorCount = 0;
        int32 InstanceCount = 0;
        TArray<TSharedPtr<FJsonValue>> Actors;
        const FScopedTransaction Transaction(FText::FromString(TEXT("Clear MCP Generated Foliage")));
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!McpHasGeneratedTag(Actor) || (!RequestedName.IsEmpty() && !Actor->GetActorLabel().Equals(RequestedName, ESearchCase::IgnoreCase))) continue;
            TSharedPtr<FJsonObject> Summary = McpHandlerUtils::CreateResultObject();
            McpAddGeneratedFoliageSummary(Actor, Summary);
            InstanceCount += static_cast<int32>(Summary->GetNumberField(TEXT("instanceCount")));
            ++ActorCount;
            if (Lower == TEXT("clear_generated_foliage"))
            {
                Actor->Modify();
                World->DestroyActor(Actor, true);
            }
            else Actors.Add(MakeShared<FJsonValueObject>(Summary));
        }
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetNumberField(Lower == TEXT("clear_generated_foliage") ? TEXT("actorsCleared") : TEXT("actorCount"), ActorCount);
        Result->SetNumberField(Lower == TEXT("clear_generated_foliage") ? TEXT("instancesCleared") : TEXT("instanceCount"), InstanceCount);
        if (Lower == TEXT("inspect_generated_foliage")) Result->SetArrayField(TEXT("actors"), Actors);
        SendAutomationResponse(RequestingSocket, RequestId, true,
            Lower == TEXT("clear_generated_foliage") ? TEXT("Only MCP-generated foliage cleared.") : TEXT("Generated foliage inspected."), Result, FString());
        return true;
    }

    bool bCancel = false;
    Payload->TryGetBoolField(TEXT("cancel"), bCancel);
    if (bCancel)
    {
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Landscape foliage operation cancelled before mutation."), nullptr, FString());
        return true;
    }

    ALandscape* Landscape = McpFindLandscape(World, LandscapeName, LandscapePath);
    if (!Landscape)
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("landscapeName or landscapePath must identify an existing landscape."), TEXT("LANDSCAPE_NOT_FOUND"));
        return true;
    }
    const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
    if (!Payload->TryGetArrayField(TEXT("foliageTypes"), Types) || !Types || Types->Num() == 0)
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("foliageTypes must contain one or more requested mesh assets."), TEXT("INVALID_ARGUMENT"));
        return true;
    }
    TArray<FMcpScatterMesh> Meshes;
    int32 DefaultCount = 0;
    Payload->TryGetNumberField(TEXT("count"), DefaultCount);
    for (const TSharedPtr<FJsonValue>& Value : *Types)
    {
        const TSharedPtr<FJsonObject>* TypeObject = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(TypeObject) || !TypeObject || !TypeObject->IsValid()) continue;
        FMcpScatterMesh Mesh;
        if (!(*TypeObject)->TryGetStringField(TEXT("meshPath"), Mesh.Path) || Mesh.Path.IsEmpty() || !LoadObject<UStaticMesh>(nullptr, *Mesh.Path))
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Every foliageTypes entry requires an existing static mesh meshPath; fallback meshes are never used."), TEXT("ASSET_NOT_FOUND"));
            return true;
        }
        (*TypeObject)->TryGetNumberField(TEXT("count"), Mesh.Count);
        (*TypeObject)->TryGetNumberField(TEXT("density"), Mesh.Density);
        (*TypeObject)->TryGetNumberField(TEXT("minScale"), Mesh.MinScale);
        (*TypeObject)->TryGetNumberField(TEXT("maxScale"), Mesh.MaxScale);
        if (Mesh.Count < 0 || Mesh.MinScale <= 0.0f || Mesh.MaxScale < Mesh.MinScale)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Foliage count and scale range are invalid."), TEXT("INVALID_ARGUMENT"));
            return true;
        }
        Meshes.Add(Mesh);
    }
    if (Meshes.Num() == 0)
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("No valid foliage mesh entries supplied."), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    FBox ScatterBounds = Landscape->GetComponentsBoundingBox(true);
    const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
    if (Payload->TryGetObjectField(TEXT("bounds"), BoundsObject) && BoundsObject && BoundsObject->IsValid())
    {
        FVector Min, Max;
        if (McpReadVector(*BoundsObject, TEXT("min"), Min) && McpReadVector(*BoundsObject, TEXT("max"), Max) && Min.X < Max.X && Min.Y < Max.Y)
            ScatterBounds = FBox(Min, Max);
    }
    if (!ScatterBounds.IsValid)
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("Landscape has no valid scatter bounds."), TEXT("INVALID_LANDSCAPE"));
        return true;
    }
    TArray<FMcpExclusionBox> Exclusions;
    const TArray<TSharedPtr<FJsonValue>>* Zones = nullptr;
    if (Payload->TryGetArrayField(TEXT("exclusionZones"), Zones) && Zones)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Zones)
        {
            const TSharedPtr<FJsonObject>* Zone = nullptr;
            FVector Min, Max;
            if (Value.IsValid() && Value->TryGetObject(Zone) && Zone && McpReadVector(*Zone, TEXT("min"), Min) && McpReadVector(*Zone, TEXT("max"), Max) && Min.X <= Max.X && Min.Y <= Max.Y)
                Exclusions.Add({ FBox(Min, Max) });
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* ExcludedActors = nullptr;
    if (Payload->TryGetArrayField(TEXT("excludedActors"), ExcludedActors) && ExcludedActors)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ExcludedActors)
        {
            if (!Value.IsValid() || Value->Type != EJson::String) continue;
            const FString ActorLabel = Value->AsString();
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* ExcludedActor = *It;
                if (ExcludedActor && ExcludedActor->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
                {
                    Exclusions.Add({ ExcludedActor->GetComponentsBoundingBox(true) });
                    break;
                }
            }
        }
    }
    double MinSlope = 0.0;
    double MaxSlope = 90.0;
    double MinHeight = -TNumericLimits<double>::Max();
    double MaxHeight = TNumericLimits<double>::Max();
    double SurfaceOffset = 2.0;
    Payload->TryGetNumberField(TEXT("minSlope"), MinSlope);
    Payload->TryGetNumberField(TEXT("maxSlope"), MaxSlope);
    Payload->TryGetNumberField(TEXT("minHeight"), MinHeight);
    Payload->TryGetNumberField(TEXT("maxHeight"), MaxHeight);
    Payload->TryGetNumberField(TEXT("surfaceOffset"), SurfaceOffset);
    if (MinSlope < 0.0 || MaxSlope > 90.0 || MinSlope > MaxSlope || MinHeight > MaxHeight || SurfaceOffset < 0.0)
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("Invalid slope, height, or surfaceOffset constraint."), TEXT("INVALID_ARGUMENT"));
        return true;
    }
    bool bCollisionEnabled = true;
    Payload->TryGetBoolField(TEXT("collisionEnabled"), bCollisionEnabled);
    int32 Seed = 1337;
    Payload->TryGetNumberField(TEXT("seed"), Seed);
    FString FoliageName;
    Payload->TryGetStringField(TEXT("foliageName"), FoliageName);
    if (FoliageName.IsEmpty()) FoliageName = FString::Printf(TEXT("MCP_LandscapeFoliage_%d"), Seed);

    SendProgressUpdate(RequestId, 0.0f, TEXT("Validating deterministic landscape foliage scatter."), true);
    const double StartSeconds = FPlatformTime::Seconds();
    TWeakObjectPtr<UMcpAutomationBridgeSubsystem> WeakThis(this);
    AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, RequestingSocket, World, Landscape, Meshes, Exclusions, ScatterBounds, Seed, FoliageName, DefaultCount, MinSlope, MaxSlope, MinHeight, MaxHeight, SurfaceOffset, bCollisionEnabled, StartSeconds]()
    {
        UMcpAutomationBridgeSubsystem* Subsystem = WeakThis.Get();
        if (!Subsystem || !IsValid(World) || !IsValid(Landscape)) return;
        const FScopedTransaction Transaction(FText::FromString(TEXT("Scatter MCP Landscape Foliage")));
        // Regeneration replaces only the collection with the same tool-owned
        // label. Manual foliage and other generated collections are untouched.
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Existing = *It;
            if (McpHasGeneratedTag(Existing) && Existing->GetActorLabel().Equals(FoliageName, ESearchCase::IgnoreCase))
            {
                Existing->Modify();
                World->DestroyActor(Existing, true);
            }
        }
        FActorSpawnParameters SpawnParams;
        SpawnParams.ObjectFlags |= RF_Transactional;
        AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (!Actor)
        {
            Subsystem->SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create generated foliage actor."), TEXT("SPAWN_FAILED"));
            return;
        }
        Actor->SetActorLabel(FoliageName);
        Actor->Tags.Add(FName(GeneratedFoliageTag));
        const FString NameMetadata = FString(GeneratedFoliageNamePrefix) + FoliageName;
        const FString SeedMetadata = FString(GeneratedFoliageSeedPrefix) + FString::FromInt(Seed);
        Actor->Tags.Add(FName(*NameMetadata));
        Actor->Tags.Add(FName(*SeedMetadata));
        USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("GeneratedFoliageRoot"), RF_Transactional);
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        FRandomStream Random(Seed);
        int32 TotalAdded = 0;
        int32 TotalRejected = 0;
        for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
        {
            const FMcpScatterMesh& Config = Meshes[MeshIndex];
            UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *Config.Path);
            UHierarchicalInstancedStaticMeshComponent* Hism = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transactional);
            Hism->SetStaticMesh(StaticMesh);
            Hism->SetCollisionEnabled(bCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
            Hism->SetupAttachment(Root);
            Hism->RegisterComponent();
            const int32 TargetCount = Config.Count > 0 ? Config.Count :
                (Config.Density > 0.0f ? FMath::RoundToInt(Config.Density) : FMath::Max(1, DefaultCount / Meshes.Num()));
            int32 AddedForMesh = 0;
            for (int32 Attempt = 0; Attempt < TargetCount * 32 && AddedForMesh < TargetCount; ++Attempt)
            {
                const FVector Start(Random.FRandRange(ScatterBounds.Min.X, ScatterBounds.Max.X), Random.FRandRange(ScatterBounds.Min.Y, ScatterBounds.Max.Y), ScatterBounds.Max.Z + 100000.0f);
                if (McpIsExcluded(Start, Exclusions)) { ++TotalRejected; continue; }
                FHitResult Hit;
                if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, 200000.0f), ECC_WorldStatic) || !Cast<ALandscapeProxy>(Hit.GetActor())) { ++TotalRejected; continue; }
                if (McpIsExcluded(Hit.ImpactPoint, Exclusions)) { ++TotalRejected; continue; }
                const double Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0f, 1.0f)));
                if (Slope < MinSlope || Slope > MaxSlope || Hit.ImpactPoint.Z < MinHeight || Hit.ImpactPoint.Z > MaxHeight) { ++TotalRejected; continue; }
                const float Scale = Random.FRandRange(Config.MinScale, Config.MaxScale);
                FTransform Transform(FRotator(0.0f, Random.FRandRange(0.0f, 360.0f), 0.0f), Hit.ImpactPoint + Hit.ImpactNormal * static_cast<float>(SurfaceOffset), FVector(Scale));
                Hism->AddInstance(Transform);
                ++AddedForMesh;
                ++TotalAdded;
            }
        }
        FString SaveError;
        if (!McpSaveLandscapeActorAndWorld(Actor, SaveError))
        {
            Subsystem->SendAutomationError(RequestingSocket, RequestId, SaveError, TEXT("SAVE_FAILED"));
            return;
        }
        Subsystem->SendProgressUpdate(RequestId, 100.0f, TEXT("Deterministic landscape foliage scatter completed."), false);
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetBoolField(TEXT("success"), true);
        Result->SetBoolField(TEXT("usesHISM"), true);
        Result->SetBoolField(TEXT("toolGeneratedOnly"), true);
        Result->SetNumberField(TEXT("seed"), Seed);
        Result->SetNumberField(TEXT("instancesPlaced"), TotalAdded);
        Result->SetNumberField(TEXT("instancesRejected"), TotalRejected);
        Result->SetNumberField(TEXT("excludedAreaViolations"), 0);
        Result->SetNumberField(TEXT("durationMs"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        Result->SetStringField(TEXT("externalPackagePath"), McpExternalOrOwningPackagePath(Actor));
        McpAddGeneratedFoliageSummary(Actor, Result);
        Subsystem->SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Deterministic HISM foliage scattered on landscape."), Result, FString());
    });
    return true;
#else
    SendAutomationResponse(RequestingSocket, RequestId, false, TEXT("Landscape foliage authoring requires an editor build."), nullptr, TEXT("NOT_IMPLEMENTED"));
    return true;
#endif
}
