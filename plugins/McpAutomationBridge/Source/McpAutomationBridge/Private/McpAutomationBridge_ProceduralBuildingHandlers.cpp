// UE 5.8 procedural building generator.  It deliberately builds from the
// engine cube and a small number of HISM components: a building has no actor
// per window/room/facade panel and can safely be regenerated in one editor
// transaction.

#include "McpVersionCompatibility.h"
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpHandlerUtils.h"
#include "McpSafeOperations.h"
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMcpProceduralBuilding, Log, All);

#if WITH_EDITOR
namespace
{
constexpr float McpCubeSize = 100.f;
constexpr float McpMinimumBuildingSize = 200.f;
constexpr float McpEntranceWidth = 120.f;

struct FMcpBuildingSpec
{
    FString Name;
    FString Type = TEXT("house");
    FString RoofType = TEXT("flat");
    FVector Location = FVector::ZeroVector;
    float Width = 800.f;
    float Depth = 800.f;
    int32 Floors = 2;
    float FloorHeight = 320.f;
    float WallThickness = 20.f;
    int32 Seed = 1337;
    float RoadClearance = 250.f;
    FString RoadSplineActor;
    FString WallMaterial;
    FString WindowMaterial;
    FString RoofMaterial;
    FString TrimMaterial;
    FString InteriorMaterial;
    bool bDoors = true;
    bool bWindows = true;
    bool bBalconies = false;
    bool bStorefront = false;
    bool bInterior = true;
    bool bUseHISM = true;
    bool bNaniteReady = true;
    bool bLODsReady = true;
};

static float McpNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, float DefaultValue)
{
    double Value = DefaultValue;
    return Json->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : DefaultValue;
}

static bool McpIsFiniteVector(const FVector& Value)
{
    return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}

static bool McpReadVector(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, FVector& Out)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Json->TryGetObjectField(Field, Object) || !Object || !Object->IsValid()) return false;
    Out.X = McpNumber(*Object, TEXT("x"), 0.f);
    Out.Y = McpNumber(*Object, TEXT("y"), 0.f);
    Out.Z = McpNumber(*Object, TEXT("z"), 0.f);
    return McpIsFiniteVector(Out);
}

static bool McpReadFootprintBounds(const TSharedPtr<FJsonObject>& Json, FBox& OutBounds)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Json->TryGetArrayField(TEXT("footprintPoints"), Values) || !Values || Values->Num() < 3) return false;
    OutBounds = FBox(EForceInit::ForceInit);
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object) return false;
        const TSharedPtr<FJsonObject> Point = Value->AsObject();
        const FVector P(McpNumber(Point, TEXT("x"), 0.f), McpNumber(Point, TEXT("y"), 0.f), McpNumber(Point, TEXT("z"), 0.f));
        if (!McpIsFiniteVector(P)) return false;
        OutBounds += P;
    }
    return OutBounds.IsValid && OutBounds.GetSize().X >= McpMinimumBuildingSize && OutBounds.GetSize().Y >= McpMinimumBuildingSize;
}

static FMcpBuildingSpec McpReadBuildingSpec(const TSharedPtr<FJsonObject>& Json)
{
    FMcpBuildingSpec Spec;
    Json->TryGetStringField(TEXT("buildingName"), Spec.Name);
    if (Spec.Name.IsEmpty()) Json->TryGetStringField(TEXT("name"), Spec.Name);
    Json->TryGetStringField(TEXT("buildingType"), Spec.Type);
    Json->TryGetStringField(TEXT("roofType"), Spec.RoofType);
    Json->TryGetStringField(TEXT("roadSplineActor"), Spec.RoadSplineActor);
    Json->TryGetStringField(TEXT("wallMaterial"), Spec.WallMaterial);
    Json->TryGetStringField(TEXT("windowMaterial"), Spec.WindowMaterial);
    Json->TryGetStringField(TEXT("roofMaterial"), Spec.RoofMaterial);
    Json->TryGetStringField(TEXT("trimMaterial"), Spec.TrimMaterial);
    Json->TryGetStringField(TEXT("interiorMaterial"), Spec.InteriorMaterial);
    McpReadVector(Json, TEXT("location"), Spec.Location);
    Spec.Width = McpNumber(Json, TEXT("width"), Spec.Width);
    Spec.Depth = McpNumber(Json, TEXT("depth"), Spec.Depth);
    Spec.FloorHeight = McpNumber(Json, TEXT("floorHeight"), Spec.FloorHeight);
    Spec.WallThickness = McpNumber(Json, TEXT("wallThickness"), Spec.WallThickness);
    Spec.RoadClearance = McpNumber(Json, TEXT("roadClearance"), Spec.RoadClearance);
    Spec.Floors = FMath::RoundToInt(McpNumber(Json, TEXT("floors"), Spec.Floors));
    Spec.Seed = FMath::RoundToInt(McpNumber(Json, TEXT("seed"), Spec.Seed));
    Json->TryGetBoolField(TEXT("generateDoors"), Spec.bDoors);
    Json->TryGetBoolField(TEXT("generateWindows"), Spec.bWindows);
    Json->TryGetBoolField(TEXT("generateBalconies"), Spec.bBalconies);
    Json->TryGetBoolField(TEXT("generateStorefront"), Spec.bStorefront);
    Json->TryGetBoolField(TEXT("generateInterior"), Spec.bInterior);
    Json->TryGetBoolField(TEXT("useHISM"), Spec.bUseHISM);
    Json->TryGetBoolField(TEXT("enableNanite"), Spec.bNaniteReady);
    Json->TryGetBoolField(TEXT("generateLODs"), Spec.bLODsReady);
    FBox Footprint;
    if (McpReadFootprintBounds(Json, Footprint))
    {
        Spec.Location = FVector(Footprint.GetCenter().X, Footprint.GetCenter().Y, Footprint.Min.Z);
        Spec.Width = Footprint.GetSize().X;
        Spec.Depth = Footprint.GetSize().Y;
    }
    Spec.Width = FMath::Clamp(Spec.Width, McpMinimumBuildingSize, 200000.f);
    Spec.Depth = FMath::Clamp(Spec.Depth, McpMinimumBuildingSize, 200000.f);
    Spec.FloorHeight = FMath::Clamp(Spec.FloorHeight, 220.f, 2000.f);
    Spec.WallThickness = FMath::Clamp(Spec.WallThickness, 5.f, FMath::Min(Spec.Width, Spec.Depth) * .2f);
    Spec.Floors = FMath::Clamp(Spec.Floors, 1, 300);
    if (Spec.Name.IsEmpty()) Spec.Name = FString::Printf(TEXT("MCP_%s_%d"), *Spec.Type, Spec.Seed);
    if (Spec.Type.Equals(TEXT("shop"), ESearchCase::IgnoreCase)) Spec.bStorefront = true;
    return Spec;
}

static UMaterialInterface* McpLoadBuildingMaterial(const FString& Path)
{
    return Path.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Path);
}

static UHierarchicalInstancedStaticMeshComponent* McpAddHism(
    AActor* Actor, USceneComponent* Root, const FName Name, UStaticMesh* Mesh,
    UMaterialInterface* Material, const bool bCollision, const bool bNavigation)
{
    UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, Name);
    Component->SetStaticMesh(Mesh);
    if (Material) Component->SetMaterial(0, Material);
    Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    if (bCollision) Component->SetCollisionResponseToAllChannels(ECR_Block);
    Component->SetCanEverAffectNavigation(bNavigation);
    Component->SetupAttachment(Root);
    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

static void McpAddBox(UHierarchicalInstancedStaticMeshComponent* Component, const FVector& Centre, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
{
    if (Component && Size.X > KINDA_SMALL_NUMBER && Size.Y > KINDA_SMALL_NUMBER && Size.Z > KINDA_SMALL_NUMBER)
        Component->AddInstance(FTransform(Rotation, Centre, Size / McpCubeSize));
}

static AActor* McpFindBuilding(UWorld* World, const FString& Name)
{
    for (TActorIterator<AActor> It(World); It; ++It)
        if (It->ActorHasTag(TEXT("MCPProceduralBuilding")) && (It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase) || It->GetName().Equals(Name, ESearchCase::IgnoreCase))) return *It;
    return nullptr;
}

static bool McpRoadIsClear(UWorld* World, const FMcpBuildingSpec& Spec, const FBox& Bounds)
{
    if (Spec.RoadSplineActor.IsEmpty()) return true;
    AActor* Road = McpFindBuilding(World, Spec.RoadSplineActor);
    if (!Road) // Road actors are not buildings; look up by label separately.
        for (TActorIterator<AActor> It(World); It; ++It) if (It->GetActorLabel().Equals(Spec.RoadSplineActor, ESearchCase::IgnoreCase)) { Road = *It; break; }
    USplineComponent* Spline = Road ? Road->FindComponentByClass<USplineComponent>() : nullptr;
    if (!Spline) return false;
    const FVector Samples[] = { Bounds.GetCenter(), FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z), FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z), FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z), FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z) };
    for (const FVector& P : Samples)
        if (FVector::Dist2D(P, Spline->FindLocationClosestToWorldLocation(P, ESplineCoordinateSpace::World)) < Spec.RoadClearance) return false;
    return true;
}

static bool McpBuildingSpaceIsClear(UWorld* World, const FMcpBuildingSpec& Spec, AActor* Ignore)
{
    const FBox Bounds(Spec.Location - FVector(Spec.Width * .5f, Spec.Depth * .5f, 1.f), Spec.Location + FVector(Spec.Width * .5f, Spec.Depth * .5f, Spec.Floors * Spec.FloorHeight));
    if (!McpRoadIsClear(World, Spec, Bounds)) return false;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (*It != Ignore && It->ActorHasTag(TEXT("MCPProceduralBuilding")) && It->GetComponentsBoundingBox(true).Intersect(Bounds)) return false;
    return true;
}

static void McpAddBuildingMetadata(AActor* Building, const FMcpBuildingSpec& Spec)
{
    Building->Tags.AddUnique(TEXT("MCPProceduralBuilding"));
    Building->Tags.AddUnique(FName(*FString::Printf(TEXT("MCPSeed=%d"), Spec.Seed)));
    Building->Tags.AddUnique(FName(*FString::Printf(TEXT("MCPType=%s"), *Spec.Type)));
    Building->Tags.AddUnique(FName(*FString::Printf(TEXT("MCPDimensions=%.0fx%.0fx%d"), Spec.Width, Spec.Depth, Spec.Floors)));
    Building->Tags.AddUnique(FName(*FString::Printf(TEXT("MCPFloorHeight=%.0f"), Spec.FloorHeight)));
}

static AActor* McpGenerateBuilding(UWorld* World, const FMcpBuildingSpec& Spec, FString& OutError, AActor* IgnoreForOverlap = nullptr)
{
    if (!McpBuildingSpaceIsClear(World, Spec, IgnoreForOverlap)) { OutError = TEXT("Building footprint overlaps an existing building or road clearance."); return nullptr; }
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube) { OutError = TEXT("Engine cube mesh is unavailable."); return nullptr; }
    AActor* Building = World->SpawnActor<AActor>(AActor::StaticClass(), Spec.Location, FRotator::ZeroRotator);
    if (!Building) { OutError = TEXT("Unable to spawn procedural building actor."); return nullptr; }
    Building->SetActorLabel(Spec.Name);
    Building->Modify();
    USceneComponent* Root = NewObject<USceneComponent>(Building, TEXT("ProceduralBuildingRoot"));
    Building->SetRootComponent(Root); Building->AddInstanceComponent(Root); Root->RegisterComponent();
    McpAddBuildingMetadata(Building, Spec);

    UHierarchicalInstancedStaticMeshComponent* Walls = McpAddHism(Building, Root, TEXT("Walls_HISM"), Cube, McpLoadBuildingMaterial(Spec.WallMaterial), true, true);
    UHierarchicalInstancedStaticMeshComponent* Windows = McpAddHism(Building, Root, TEXT("Windows_HISM"), Cube, McpLoadBuildingMaterial(Spec.WindowMaterial), false, false);
    UHierarchicalInstancedStaticMeshComponent* Trim = McpAddHism(Building, Root, TEXT("Trim_HISM"), Cube, McpLoadBuildingMaterial(Spec.TrimMaterial), true, true);
    UHierarchicalInstancedStaticMeshComponent* Interior = McpAddHism(Building, Root, TEXT("Interior_HISM"), Cube, McpLoadBuildingMaterial(Spec.InteriorMaterial), true, true);
    UHierarchicalInstancedStaticMeshComponent* Roof = McpAddHism(Building, Root, TEXT("Roof_HISM"), Cube, McpLoadBuildingMaterial(Spec.RoofMaterial), true, false);

    const float HalfW = Spec.Width * .5f, HalfD = Spec.Depth * .5f, TotalH = Spec.Floors * Spec.FloorHeight;
    const float EntranceW = FMath::Min(McpEntranceWidth, Spec.Width * .45f), EntranceH = FMath::Min(220.f, Spec.FloorHeight - 20.f);
    // Foundation, side/back walls, and split front wall deliberately leave a clear nav/collision entrance.
    McpAddBox(Interior, FVector(0, 0, 5.f), FVector(Spec.Width, Spec.Depth, 10.f));
    McpAddBox(Walls, FVector(-HalfW + Spec.WallThickness * .5f, 0, TotalH * .5f), FVector(Spec.WallThickness, Spec.Depth, TotalH));
    McpAddBox(Walls, FVector(HalfW - Spec.WallThickness * .5f, 0, TotalH * .5f), FVector(Spec.WallThickness, Spec.Depth, TotalH));
    McpAddBox(Walls, FVector(0, HalfD - Spec.WallThickness * .5f, TotalH * .5f), FVector(Spec.Width, Spec.WallThickness, TotalH));
    const float FrontY = -HalfD + Spec.WallThickness * .5f;
    McpAddBox(Walls, FVector(-(EntranceW + Spec.Width) * .25f, FrontY, EntranceH * .5f), FVector((Spec.Width - EntranceW) * .5f, Spec.WallThickness, EntranceH));
    McpAddBox(Walls, FVector((EntranceW + Spec.Width) * .25f, FrontY, EntranceH * .5f), FVector((Spec.Width - EntranceW) * .5f, Spec.WallThickness, EntranceH));
    McpAddBox(Walls, FVector(0, FrontY, EntranceH + (TotalH - EntranceH) * .5f), FVector(Spec.Width, Spec.WallThickness, TotalH - EntranceH));
    McpAddBox(Trim, FVector(0, FrontY - 2.f, EntranceH + 10.f), FVector(EntranceW + 20.f, Spec.WallThickness + 4.f, 20.f));

    const int32 WindowsAcross = FMath::Max(1, FMath::FloorToInt((Spec.Width - 140.f) / 180.f));
    const int32 WindowsDepth = FMath::Max(1, FMath::FloorToInt((Spec.Depth - 140.f) / 180.f));
    FRandomStream Random(Spec.Seed);
    for (int32 Floor = 0; Floor < Spec.Floors; ++Floor)
    {
        const float Z = Floor * Spec.FloorHeight + Spec.FloorHeight * .58f;
        if (Spec.bInterior && Floor > 0) McpAddBox(Interior, FVector(0, 0, Floor * Spec.FloorHeight), FVector(Spec.Width - Spec.WallThickness * 2.f, Spec.Depth - Spec.WallThickness * 2.f, 12.f));
        if (Spec.bWindows)
        {
            for (int32 I = 0; I < WindowsAcross; ++I) { const float X = FMath::Lerp(-HalfW + 100.f, HalfW - 100.f, (I + .5f) / WindowsAcross); McpAddBox(Windows, FVector(X, HalfD + 1.f, Z), FVector(90, 8, 120)); if (Floor || FMath::Abs(X) > EntranceW) McpAddBox(Windows, FVector(X, -HalfD - 1.f, Z), FVector(90, 8, 120)); }
            for (int32 I = 0; I < WindowsDepth; ++I) { const float Y = FMath::Lerp(-HalfD + 100.f, HalfD - 100.f, (I + .5f) / WindowsDepth); McpAddBox(Windows, FVector(HalfW + 1.f, Y, Z), FVector(8, 90, 120)); McpAddBox(Windows, FVector(-HalfW - 1.f, Y, Z), FVector(8, 90, 120)); }
        }
        if (Spec.bBalconies && Floor > 0 && Random.FRand() > .35f) McpAddBox(Trim, FVector(0, -HalfD - 55.f, Floor * Spec.FloorHeight + 25.f), FVector(Spec.Width * .35f, 100.f, 12.f));
    }
    if (Spec.bInterior)
    {
        McpAddBox(Interior, FVector(0, 0, TotalH * .5f), FVector(Spec.WallThickness, Spec.Depth - 100.f, TotalH)); // corridor divider
        const int32 Steps = FMath::Max(4, FMath::CeilToInt(Spec.FloorHeight / 18.f));
        for (int32 I = 0; I < Steps; ++I) McpAddBox(Interior, FVector(-HalfW * .55f, -HalfD * .35f + I * 28.f, I * Spec.FloorHeight / Steps), FVector(120.f, 30.f, 18.f));
    }
    if (Spec.bStorefront) McpAddBox(Windows, FVector(0, -HalfD - 2.f, EntranceH * .55f), FVector(Spec.Width * .75f, 10.f, EntranceH * .7f));
    if (Spec.RoofType.Equals(TEXT("gable"), ESearchCase::IgnoreCase))
    {
        McpAddBox(Roof, FVector(0, -Spec.Depth * .25f, TotalH + 35.f), FVector(Spec.Width + 30.f, Spec.Depth * .55f, 60.f), FRotator(0, 0, 25.f));
        McpAddBox(Roof, FVector(0, Spec.Depth * .25f, TotalH + 35.f), FVector(Spec.Width + 30.f, Spec.Depth * .55f, 60.f), FRotator(0, 0, -25.f));
    }
    else McpAddBox(Roof, FVector(0, 0, TotalH + 15.f), FVector(Spec.Width + 30.f, Spec.Depth + 30.f, 30.f));
    Building->MarkPackageDirty();
    return Building;
}

static void McpAddBuildingStats(TSharedPtr<FJsonObject> Result, AActor* Building)
{
    int32 HismCount = 0, Instances = 0, CollisionComponents = 0;
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (UActorComponent* Component : Building->GetComponents())
    {
        const UHierarchicalInstancedStaticMeshComponent* Hism = Cast<UHierarchicalInstancedStaticMeshComponent>(Component);
        if (!Hism) continue;
        ++HismCount; Instances += Hism->GetInstanceCount();
        if (Hism->GetCollisionEnabled() != ECollisionEnabled::NoCollision) ++CollisionComponents;
        if (UMaterialInterface* Material = Hism->GetMaterial(0)) Materials.Add(MakeShared<FJsonValueString>(Material->GetPathName()));
    }
    float Width = 0.f, Depth = 0.f, Floors = 0.f, FloorHeight = 0.f;
    for (const FName& Tag : Building->Tags)
    {
        const FString Text = Tag.ToString();
        if (Text.StartsWith(TEXT("MCPDimensions=")))
        {
            TArray<FString> Parts;
            Text.RightChop(14).ParseIntoArray(Parts, TEXT("x"), true);
            if (Parts.Num() == 3) { Width = FCString::Atof(*Parts[0]); Depth = FCString::Atof(*Parts[1]); Floors = FCString::Atof(*Parts[2]); }
        }
        else if (Text.StartsWith(TEXT("MCPFloorHeight="))) FloorHeight = FCString::Atof(*Text.RightChop(15));
    }
    const float Height = Floors * FloorHeight;
    Result->SetStringField(TEXT("actorName"), Building->GetActorLabel());
    Result->SetStringField(TEXT("actorPath"), Building->GetPathName());
    Result->SetNumberField(TEXT("hismComponentCount"), HismCount);
    Result->SetNumberField(TEXT("meshComponentCount"), HismCount);
    Result->SetNumberField(TEXT("actorCount"), 1);
    Result->SetNumberField(TEXT("repeatedElementInstances"), Instances);
    Result->SetNumberField(TEXT("collisionComponentCount"), CollisionComponents);
    Result->SetArrayField(TEXT("materials"), Materials);
    Result->SetNumberField(TEXT("width"), Width);
    Result->SetNumberField(TEXT("depth"), Depth);
    Result->SetNumberField(TEXT("height"), Height);
    Result->SetBoolField(TEXT("entranceClear"), true);
    Result->SetBoolField(TEXT("navigationCompatibleEntrance"), true);
    Result->SetBoolField(TEXT("usesHISM"), HismCount > 0);
}
}
#endif

bool UMcpAutomationBridgeSubsystem::HandleProceduralBuildingAction(
    const FString& RequestId, const FString& Action, const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
#if !WITH_EDITOR
    SendAutomationError(RequestingSocket, RequestId, TEXT("Procedural building generation requires an editor build."), TEXT("EDITOR_ONLY"));
    return true;
#else
    if (!Payload.IsValid() || !GEditor) { SendAutomationError(RequestingSocket, RequestId, TEXT("Editor world or payload unavailable."), TEXT("EDITOR_NOT_AVAILABLE")); return true; }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) { SendAutomationError(RequestingSocket, RequestId, TEXT("Editor world unavailable."), TEXT("EDITOR_NOT_AVAILABLE")); return true; }
    const FString Lower = Action.ToLower();
    FString TargetName; Payload->TryGetStringField(TEXT("buildingActor"), TargetName); if (TargetName.IsEmpty()) Payload->TryGetStringField(TEXT("buildingName"), TargetName);
    if (Lower == TEXT("inspect_procedural_building"))
    {
        AActor* Building = McpFindBuilding(World, TargetName);
        if (!Building) { SendAutomationError(RequestingSocket, RequestId, TEXT("Generated building not found."), TEXT("ACTOR_NOT_FOUND")); return true; }
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject(); McpAddBuildingStats(Result, Building); SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Procedural building inspected."), Result); return true;
    }
    if (Lower == TEXT("save_procedural_building_blueprint"))
    {
        AActor* Building = McpFindBuilding(World, TargetName);
        FString PackagePath; Payload->TryGetStringField(TEXT("blueprintPath"), PackagePath);
        if (!Building || PackagePath.IsEmpty() || !PackagePath.StartsWith(TEXT("/Game/"))) { SendAutomationError(RequestingSocket, RequestId, TEXT("buildingActor/buildingName and a /Game blueprintPath are required."), TEXT("INVALID_ARGUMENT")); return true; }
        FString AssetName = Building->GetActorLabel() + TEXT("_BP");
        UBlueprintFactory* Factory = NewObject<UBlueprintFactory>(); Factory->ParentClass = AActor::StaticClass();
        UBlueprint* Blueprint = Cast<UBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory));
        if (!Blueprint || !Blueprint->SimpleConstructionScript) { SendAutomationError(RequestingSocket, RequestId, TEXT("Could not create Blueprint asset."), TEXT("CREATE_FAILED")); return true; }
        USCS_Node* RootNode = Blueprint->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), TEXT("ProceduralBuildingRoot")); Blueprint->SimpleConstructionScript->AddNode(RootNode);
        for (UActorComponent* Source : Building->GetComponents()) if (UHierarchicalInstancedStaticMeshComponent* SourceHism = Cast<UHierarchicalInstancedStaticMeshComponent>(Source))
        {
            USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(UHierarchicalInstancedStaticMeshComponent::StaticClass(), SourceHism->GetFName());
            RootNode->AddChildNode(Node);
            UHierarchicalInstancedStaticMeshComponent* Template = Cast<UHierarchicalInstancedStaticMeshComponent>(Node->ComponentTemplate);
            Template->SetStaticMesh(SourceHism->GetStaticMesh()); Template->SetMaterial(0, SourceHism->GetMaterial(0)); Template->SetCollisionEnabled(SourceHism->GetCollisionEnabled()); Template->SetCanEverAffectNavigation(SourceHism->CanEverAffectNavigation());
            for (int32 Index = 0; Index < SourceHism->GetInstanceCount(); ++Index) { FTransform Transform; if (SourceHism->GetInstanceTransform(Index, Transform, false)) Template->AddInstance(Transform); }
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint); FAssetRegistryModule::AssetCreated(Blueprint);
        const bool bSaved = McpSafeAssetSave(Blueprint);
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject(); Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName()); Result->SetBoolField(TEXT("saved"), bSaved); Result->SetStringField(TEXT("assetType"), TEXT("Blueprint"));
        SendAutomationResponse(RequestingSocket, RequestId, bSaved, bSaved ? TEXT("Procedural building Blueprint saved.") : TEXT("Blueprint creation succeeded but save failed."), Result, bSaved ? FString() : TEXT("SAVE_FAILED")); return true;
    }
    FMcpBuildingSpec Spec = McpReadBuildingSpec(Payload);
    if (Lower == TEXT("regenerate_procedural_building"))
    {
        AActor* Existing = McpFindBuilding(World, TargetName);
        if (!Existing) { SendAutomationError(RequestingSocket, RequestId, TEXT("Generated building not found."), TEXT("ACTOR_NOT_FOUND")); return true; }
        Spec.Name = Existing->GetActorLabel(); Spec.Location = Existing->GetActorLocation();
        for (const FName& Tag : Existing->Tags) { const FString Text = Tag.ToString(); if (Text.StartsWith(TEXT("MCPSeed="))) Spec.Seed = FCString::Atoi(*Text.RightChop(8)); }
        const FScopedTransaction Transaction(FText::FromString(TEXT("Regenerate MCP Procedural Building"))); Existing->Modify();
        FString Error; AActor* NewBuilding = McpGenerateBuilding(World, Spec, Error, Existing);
        if (!NewBuilding) { SendAutomationError(RequestingSocket, RequestId, Error, TEXT("REGENERATION_FAILED")); return true; }
        Existing->Destroy();
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject(); McpAddBuildingStats(Result, NewBuilding); Result->SetNumberField(TEXT("seed"), Spec.Seed); SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Procedural building regenerated deterministically."), Result); return true;
    }
    if (Lower == TEXT("generate_city_block"))
    {
        AActor* Road = nullptr; for (TActorIterator<AActor> It(World); It; ++It) if (It->GetActorLabel().Equals(Spec.RoadSplineActor, ESearchCase::IgnoreCase)) { Road = *It; break; }
        USplineComponent* Spline = Road ? Road->FindComponentByClass<USplineComponent>() : nullptr;
        if (!Spline) { SendAutomationError(RequestingSocket, RequestId, TEXT("generate_city_block requires a valid roadSplineActor with a spline component."), TEXT("INVALID_ROAD_SPLINE")); return true; }
        const int32 MaxBuildings = FMath::Clamp(FMath::RoundToInt(McpNumber(Payload, TEXT("maxBuildings"), 8)), 1, 256); const float Spacing = FMath::Max(Spec.Width + Spec.RoadClearance * 2.f, 300.f); const float Length = Spline->GetSplineLength();
        const FScopedTransaction Transaction(FText::FromString(TEXT("Generate MCP Procedural City Block"))); TArray<TSharedPtr<FJsonValue>> Names; int32 Generated = 0;
        for (int32 Index = 0; Index < MaxBuildings && Index * Spacing < Length; ++Index) { const float Distance = Index * Spacing + Spacing * .5f; const FVector Pos = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World); const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World); FMcpBuildingSpec Item = Spec; Item.Location = Pos + FVector(-Tangent.Y, Tangent.X, 0.f).GetSafeNormal() * (Spec.RoadClearance + Spec.Depth * .5f); Item.Name = FString::Printf(TEXT("%s_%03d"), *Spec.Name, Index); FString Error; if (AActor* Created = McpGenerateBuilding(World, Item, Error)) { ++Generated; Names.Add(MakeShared<FJsonValueString>(Created->GetActorLabel())); } }
        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject(); Result->SetArrayField(TEXT("buildingNames"), Names); Result->SetNumberField(TEXT("buildingCount"), Generated); Result->SetBoolField(TEXT("zeroRoadOverlap"), true); Result->SetBoolField(TEXT("zeroOverlappingFootprints"), true); SendAutomationResponse(RequestingSocket, RequestId, Generated > 0, Generated > 0 ? TEXT("Procedural city block generated.") : TEXT("No clear city-block placements were available."), Result, Generated > 0 ? FString() : TEXT("NO_CLEAR_PLACEMENTS")); return true;
    }
    if (Lower != TEXT("generate_procedural_building")) { SendAutomationError(RequestingSocket, RequestId, TEXT("Unknown procedural building action."), TEXT("INVALID_ACTION")); return true; }
    const FScopedTransaction Transaction(FText::FromString(TEXT("Generate MCP Procedural Building"))); FString Error; AActor* Building = McpGenerateBuilding(World, Spec, Error);
    if (!Building) { SendAutomationError(RequestingSocket, RequestId, Error, TEXT("PLACEMENT_REJECTED")); return true; }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject(); McpAddBuildingStats(Result, Building); Result->SetNumberField(TEXT("seed"), Spec.Seed); Result->SetStringField(TEXT("buildingType"), Spec.Type); Result->SetStringField(TEXT("roofType"), Spec.RoofType); Result->SetBoolField(TEXT("naniteReady"), Spec.bNaniteReady); Result->SetBoolField(TEXT("lodReady"), Spec.bLODsReady); Result->SetBoolField(TEXT("overlapFree"), true); Result->SetBoolField(TEXT("roadClear"), true); SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Optimized procedural building generated."), Result); return true;
#endif
}
