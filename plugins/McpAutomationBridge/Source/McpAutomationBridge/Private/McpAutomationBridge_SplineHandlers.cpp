// =============================================================================
// McpAutomationBridge_SplineHandlers.cpp
// =============================================================================
// Spline System Handlers for MCP Automation Bridge
//
// HANDLERS IMPLEMENTED:
// ---------------------
// Section 1: Spline Component Operations
//   - HandleSplineAction             : Main dispatcher for spline_* actions
//   - create_spline_component         : Add USplineComponent to Blueprint via SCS
//   - add_spline_point                : Add point to existing spline
//   - add_spline_points               : Batch add multiple points
//   - clear_spline_points             : Remove all points from spline
//   - set_spline_point_position       : Set position of specific point
//   - set_spline_point_tangent        : Set tangent of specific point
//
// Section 2: Spline Mesh Operations
//   - create_spline_mesh_component    : Add USplineMeshComponent to Blueprint
//   - add_spline_mesh                 : Create spline mesh along spline path
//   - configure_spline_mesh           : Configure spline mesh properties
//
// Section 3: Utility Functions
//   - get_spline_info                 : Get spline component details
//   - get_spline_length               : Get total spline length
//   - get_spline_point_count          : Get number of points
//
// PAYLOAD/RESPONSE FORMATS:
// -------------------------
// create_spline_component:
//   Payload: { "blueprintPath": string, "componentName"?: string }
//   Response: { "success": bool, "componentName": string, "blueprintPath": string }
//
// add_spline_point:
//   Payload: { "actorName": string, "componentName"?: string,
//              "location": {x,y,z}, "tangent"?: {x,y,z},
//              "pointType"?: "Curve"|"Linear"|"Constant }
//   Response: { "success": bool, "pointIndex": int }
//
// create_spline_mesh:
//   Payload: { "blueprintPath": string, "splineComponentName": string,
//              "staticMesh": string, "material"?: string }
//   Response: { "success": bool, "meshCount": int }
//
// VERSION COMPATIBILITY:
// ----------------------
// UE 5.0-5.7: All handlers supported
// - USplineComponent and USplineMeshComponent APIs stable across versions
// - SCS (Simple Construction Script) required for component templates
//
// Copyright (c) 2024 MCP Automation Bridge Contributors
// =============================================================================

#include "McpVersionCompatibility.h"  // MUST be first
#include "McpHandlerUtils.h"

#include "Dom/JsonObject.h"
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpBridgeWebSocket.h"
#include "Misc/EngineVersionComparison.h"

// =============================================================================
// Editor-Only Includes
// =============================================================================
#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"

// -----------------------------------------------------------------------------
// Spline System Includes
// -----------------------------------------------------------------------------
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "ScopedTransaction.h"
#endif

// =============================================================================
// Logging Category
// =============================================================================
DEFINE_LOG_CATEGORY_STATIC(LogMcpSplineHandlers, Log, All);

#if WITH_EDITOR

// Helper to get string field from JSON
static FString GetJsonStringFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, const FString& Default = TEXT(""))
{
    if (!Payload.IsValid()) return Default;
    FString Value;
    if (Payload->TryGetStringField(FieldName, Value))
    {
        return Value;
    }
    return Default;
}

// Helper to get number field from JSON
static double GetJsonNumberFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, double Default = 0.0)
{
    if (!Payload.IsValid()) return Default;
    double Value;
    if (Payload->TryGetNumberField(FieldName, Value))
    {
        return Value;
    }
    return Default;
}

// Helper to get bool field from JSON
static bool GetJsonBoolFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, bool Default = false)
{
    if (!Payload.IsValid()) return Default;
    bool Value;
    if (Payload->TryGetBoolField(FieldName, Value))
    {
        return Value;
    }
    return Default;
}

// Helper to get int field from JSON
static int32 GetJsonIntFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, int32 Default = 0)
{
    if (!Payload.IsValid()) return Default;
    double Value;
    if (Payload->TryGetNumberField(FieldName, Value))
    {
        return static_cast<int32>(Value);
    }
    return Default;
}

// Helper to get FVector from JSON object field
static FVector GetJsonVectorFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, const FVector& Default = FVector::ZeroVector)
{
    if (!Payload.IsValid()) return Default;
    const TSharedPtr<FJsonObject>* VecObj;
    if (Payload->TryGetObjectField(FieldName, VecObj) && VecObj->IsValid())
    {
        return FVector(
            GetJsonNumberFieldSpline(*VecObj, TEXT("x"), Default.X),
            GetJsonNumberFieldSpline(*VecObj, TEXT("y"), Default.Y),
            GetJsonNumberFieldSpline(*VecObj, TEXT("z"), Default.Z)
        );
    }
    return Default;
}

// Helper to get FRotator from JSON object field
static FRotator GetJsonRotatorFieldSpline(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, const FRotator& Default = FRotator::ZeroRotator)
{
    if (!Payload.IsValid()) return Default;
    const TSharedPtr<FJsonObject>* RotObj;
    if (Payload->TryGetObjectField(FieldName, RotObj) && RotObj->IsValid())
    {
        return FRotator(
            GetJsonNumberFieldSpline(*RotObj, TEXT("pitch"), Default.Pitch),
            GetJsonNumberFieldSpline(*RotObj, TEXT("yaw"), Default.Yaw),
            GetJsonNumberFieldSpline(*RotObj, TEXT("roll"), Default.Roll)
        );
    }
    return Default;
}

// Helper to find actor by name
static AActor* FindActorByName(UWorld* World, const FString& ActorName)
{
    if (!World || ActorName.IsEmpty()) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
        {
            return *It;
        }
    }
    return nullptr;
}

// Helper to find spline component on actor
static USplineComponent* FindSplineComponent(AActor* Actor, const FString& ComponentName = TEXT(""))
{
    if (!Actor) return nullptr;

    TArray<USplineComponent*> SplineComponents;
    Actor->GetComponents<USplineComponent>(SplineComponents);

    if (SplineComponents.Num() == 0) return nullptr;

    if (!ComponentName.IsEmpty())
    {
        for (USplineComponent* Comp : SplineComponents)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                return Comp;
            }
        }
        return nullptr;
    }

    return SplineComponents[0];
}

// Helper to parse spline point type (case-insensitive)
static ESplinePointType::Type ParseSplinePointType(const FString& TypeStr)
{
    FString LowerStr = TypeStr.ToLower();
    if (LowerStr == TEXT("linear")) return ESplinePointType::Linear;
    if (LowerStr == TEXT("curve")) return ESplinePointType::Curve;
    if (LowerStr == TEXT("constant")) return ESplinePointType::Constant;
    if (LowerStr == TEXT("curveclamped")) return ESplinePointType::CurveClamped;
    if (LowerStr == TEXT("curvecustomtangent")) return ESplinePointType::CurveCustomTangent;
    return ESplinePointType::Curve; // Default
}

// Helper to convert spline point type to string
static FString SplinePointTypeToString(ESplinePointType::Type Type)
{
    switch (Type)
    {
        case ESplinePointType::Linear: return TEXT("Linear");
        case ESplinePointType::Curve: return TEXT("Curve");
        case ESplinePointType::Constant: return TEXT("Constant");
        case ESplinePointType::CurveClamped: return TEXT("CurveClamped");
        case ESplinePointType::CurveCustomTangent: return TEXT("CurveCustomTangent");
        default: return TEXT("Unknown");
    }
}

static FString MakeSplineConfigTagPrefix(const FString& Key)
{
    return FString::Printf(TEXT("MCP.Spline.%s="), *Key);
}

static void SetSplineConfigValue(AActor* Target, const FString& Key, const FString& Value)
{
    if (!Target) return;

    const FString Prefix = MakeSplineConfigTagPrefix(Key);
    for (int32 Index = Target->Tags.Num() - 1; Index >= 0; --Index)
    {
        if (Target->Tags[Index].ToString().StartsWith(Prefix))
        {
            Target->Tags.RemoveAt(Index);
        }
    }

    Target->Modify();
    Target->Tags.Add(FName(*(Prefix + Value)));
    Target->MarkPackageDirty();
}

static bool TryGetSplineConfigValue(AActor* Target, const FString& Key, FString& OutValue)
{
    if (!Target) return false;

    const FString Prefix = MakeSplineConfigTagPrefix(Key);
    for (const FName& Tag : Target->Tags)
    {
        const FString TagString = Tag.ToString();
        if (TagString.StartsWith(Prefix))
        {
            OutValue = TagString.RightChop(Prefix.Len());
            return true;
        }
    }

    return false;
}

static AActor* ResolveSplineConfigTarget(UWorld* World, const FString& ActorName)
{
    if (!World) return nullptr;

    if (!ActorName.TrimStartAndEnd().IsEmpty())
    {
        return FindActorByName(World, ActorName.TrimStartAndEnd());
    }

    return World->GetWorldSettings();
}

static FString GetSplineConfigTargetName(AActor* Target)
{
    if (!Target) return TEXT("");
    return Target->GetActorLabel().IsEmpty() ? Target->GetName() : Target->GetActorLabel();
}

static bool GetConfiguredSplineBool(AActor* Actor, UWorld* World, const FString& Key, bool DefaultValue)
{
    FString Value;
    if (TryGetSplineConfigValue(Actor, Key, Value) || TryGetSplineConfigValue(World ? World->GetWorldSettings() : nullptr, Key, Value))
    {
        return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
    }

    return DefaultValue;
}

static double GetConfiguredSplineNumber(AActor* Actor, UWorld* World, const FString& Key, double DefaultValue)
{
    FString Value;
    if (TryGetSplineConfigValue(Actor, Key, Value) || TryGetSplineConfigValue(World ? World->GetWorldSettings() : nullptr, Key, Value))
    {
        return FCString::Atod(*Value);
    }

    return DefaultValue;
}

static FString BoolToSplineConfigString(bool bValue)
{
    return bValue ? TEXT("true") : TEXT("false");
}

struct FMcpSplinePointInput
{
    FVector Location = FVector::ZeroVector;
    FVector ArriveTangent = FVector::ZeroVector;
    FVector LeaveTangent = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale = FVector::OneVector;
    float Roll = 0.0f;
    ESplinePointType::Type PointType = ESplinePointType::Curve;
    bool bHasArriveTangent = false;
    bool bHasLeaveTangent = false;
    bool bHasPointType = false;
    bool bHasRotation = false;
    bool bHasScale = false;
    bool bHasRoll = false;
};

static bool TryGetSplineCoordinateSpace(
    const TSharedPtr<FJsonObject>& Payload,
    ESplineCoordinateSpace::Type& OutSpace,
    FString& OutError)
{
    const FString Value = GetJsonStringFieldSpline(Payload, TEXT("coordinateSpace"), TEXT("Local"));
    if (Value.Equals(TEXT("Local"), ESearchCase::IgnoreCase))
    {
        OutSpace = ESplineCoordinateSpace::Local;
        return true;
    }
    if (Value.Equals(TEXT("World"), ESearchCase::IgnoreCase))
    {
        OutSpace = ESplineCoordinateSpace::World;
        return true;
    }

    OutError = TEXT("coordinateSpace must be Local or World");
    return false;
}

static bool TryGetStrictSplineVector(
    const TSharedPtr<FJsonObject>& Object,
    FVector& OutVector,
    FString& OutError)
{
    if (!Object.IsValid())
    {
        OutError = TEXT("spline point vector must be an object");
        return false;
    }

    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    if (!Object->TryGetNumberField(TEXT("x"), X) ||
        !Object->TryGetNumberField(TEXT("y"), Y) ||
        !Object->TryGetNumberField(TEXT("z"), Z) ||
        !FMath::IsFinite(static_cast<float>(X)) ||
        !FMath::IsFinite(static_cast<float>(Y)) ||
        !FMath::IsFinite(static_cast<float>(Z)))
    {
        OutError = TEXT("spline point vectors require finite numeric x, y and z fields");
        return false;
    }

    OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
    return true;
}

static bool TryGetStrictSplineRotator(
    const TSharedPtr<FJsonObject>& Object,
    FRotator& OutRotation,
    FString& OutError)
{
    if (!Object.IsValid())
    {
        OutError = TEXT("spline point rotation must be an object");
        return false;
    }

    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
    if (!Object->TryGetNumberField(TEXT("pitch"), Pitch) ||
        !Object->TryGetNumberField(TEXT("yaw"), Yaw) ||
        !Object->TryGetNumberField(TEXT("roll"), Roll) ||
        !FMath::IsFinite(static_cast<float>(Pitch)) ||
        !FMath::IsFinite(static_cast<float>(Yaw)) ||
        !FMath::IsFinite(static_cast<float>(Roll)))
    {
        OutError = TEXT("spline point rotations require finite numeric pitch, yaw and roll fields");
        return false;
    }

    OutRotation = FRotator(static_cast<float>(Pitch), static_cast<float>(Yaw), static_cast<float>(Roll));
    return true;
}

static bool TryGetRoutePointObject(
    const TSharedPtr<FJsonObject>& PointObject,
    FMcpSplinePointInput& OutPoint,
    FString& OutError)
{
    const TSharedPtr<FJsonObject>* LocationObject = nullptr;
    if (PointObject->TryGetObjectField(TEXT("location"), LocationObject) && LocationObject && LocationObject->IsValid())
    {
        if (!TryGetStrictSplineVector(*LocationObject, OutPoint.Location, OutError)) return false;
    }
    else if (PointObject->TryGetObjectField(TEXT("position"), LocationObject) && LocationObject && LocationObject->IsValid())
    {
        if (!TryGetStrictSplineVector(*LocationObject, OutPoint.Location, OutError)) return false;
    }
    else if (!TryGetStrictSplineVector(PointObject, OutPoint.Location, OutError))
    {
        return false;
    }

    FString TypeString;
    if (PointObject->TryGetStringField(TEXT("pointType"), TypeString) ||
        PointObject->TryGetStringField(TEXT("type"), TypeString))
    {
        if (!TypeString.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
            !TypeString.Equals(TEXT("Curve"), ESearchCase::IgnoreCase) &&
            !TypeString.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
            !TypeString.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
        {
            OutError = TEXT("point type must be Linear, Curve, Constant or CurveClamped");
            return false;
        }
        OutPoint.PointType = ParseSplinePointType(TypeString);
        OutPoint.bHasPointType = true;
    }

    const TSharedPtr<FJsonObject>* VectorObject = nullptr;
    if (PointObject->TryGetObjectField(TEXT("arriveTangent"), VectorObject) && VectorObject && VectorObject->IsValid())
    {
        if (!TryGetStrictSplineVector(*VectorObject, OutPoint.ArriveTangent, OutError)) return false;
        OutPoint.bHasArriveTangent = true;
    }
    if (PointObject->TryGetObjectField(TEXT("leaveTangent"), VectorObject) && VectorObject && VectorObject->IsValid())
    {
        if (!TryGetStrictSplineVector(*VectorObject, OutPoint.LeaveTangent, OutError)) return false;
        OutPoint.bHasLeaveTangent = true;
    }
    if (PointObject->TryGetObjectField(TEXT("rotation"), VectorObject) ||
        PointObject->TryGetObjectField(TEXT("pointRotation"), VectorObject))
    {
        if (!VectorObject || !VectorObject->IsValid() ||
            !TryGetStrictSplineRotator(*VectorObject, OutPoint.Rotation, OutError)) return false;
        OutPoint.bHasRotation = true;
    }
    if (PointObject->TryGetObjectField(TEXT("scale"), VectorObject) ||
        PointObject->TryGetObjectField(TEXT("pointScale"), VectorObject))
    {
        if (!VectorObject || !VectorObject->IsValid() ||
            !TryGetStrictSplineVector(*VectorObject, OutPoint.Scale, OutError)) return false;
        if (OutPoint.Scale.X <= 0.0f || OutPoint.Scale.Y <= 0.0f || OutPoint.Scale.Z <= 0.0f)
        {
            OutError = TEXT("spline point scale values must be greater than zero");
            return false;
        }
        OutPoint.bHasScale = true;
    }

    double Roll = 0.0;
    if (PointObject->TryGetNumberField(TEXT("roll"), Roll))
    {
        if (!FMath::IsFinite(static_cast<float>(Roll)))
        {
            OutError = TEXT("spline point roll must be finite");
            return false;
        }
        OutPoint.Roll = static_cast<float>(Roll);
        OutPoint.bHasRoll = true;
    }

    return true;
}

static bool TryParseSplineRoute(
    const TSharedPtr<FJsonObject>& Payload,
    bool bClosedLoop,
    TArray<FMcpSplinePointInput>& OutPoints,
    ESplineCoordinateSpace::Type& OutSpace,
    bool& bHasRoute,
    FString& OutError)
{
    bHasRoute = false;
    const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
    if (Payload->HasField(TEXT("points")))
    {
        bHasRoute = true;
        if (!Payload->TryGetArrayField(TEXT("points"), PointsArray) || !PointsArray)
        {
            OutError = TEXT("points must be an array");
            return false;
        }
    }
    else if (Payload->HasField(TEXT("routePoints")))
    {
        bHasRoute = true;
        if (!Payload->TryGetArrayField(TEXT("routePoints"), PointsArray) || !PointsArray)
        {
            OutError = TEXT("routePoints must be an array");
            return false;
        }
    }
    else if (Payload->HasField(TEXT("initialPoints")))
    {
        bHasRoute = true;
        if (!Payload->TryGetArrayField(TEXT("initialPoints"), PointsArray) || !PointsArray)
        {
            OutError = TEXT("initialPoints must be an array");
            return false;
        }
    }

    if (!TryGetSplineCoordinateSpace(Payload, OutSpace, OutError)) return false;
    if (!bHasRoute) return true;
    if (PointsArray->Num() < 2 || (bClosedLoop && PointsArray->Num() < 2))
    {
        OutError = TEXT("a spline route requires at least two points");
        return false;
    }

    OutPoints.Reserve(PointsArray->Num());
    for (int32 Index = 0; Index < PointsArray->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* PointObject = nullptr;
        if (!(*PointsArray)[Index].IsValid() || !(*PointsArray)[Index]->TryGetObject(PointObject) || !PointObject || !PointObject->IsValid())
        {
            OutError = FString::Printf(TEXT("route point %d must be an object"), Index);
            return false;
        }

        FMcpSplinePointInput Point;
        if (!TryGetRoutePointObject(*PointObject, Point, OutError))
        {
            OutError = FString::Printf(TEXT("route point %d: %s"), Index, *OutError);
            return false;
        }
        bool bDuplicatePoint = false;
        for (const FMcpSplinePointInput& ExistingPoint : OutPoints)
        {
            if (FVector::DistSquared(ExistingPoint.Location, Point.Location) <= KINDA_SMALL_NUMBER)
            {
                bDuplicatePoint = true;
                break;
            }
        }
        if (bDuplicatePoint)
        {
            OutError = FString::Printf(TEXT("route point %d duplicates an earlier point or forms a zero-length segment"), Index);
            return false;
        }
        OutPoints.Add(Point);
    }
    if (bClosedLoop && FVector::DistSquared(OutPoints[0].Location, OutPoints.Last().Location) <= KINDA_SMALL_NUMBER)
    {
        OutError = TEXT("a closed-loop route must not repeat its first point as the last point");
        return false;
    }
    return true;
}

static void ApplySplinePointInput(
    USplineComponent* SplineComp,
    int32 Index,
    const FMcpSplinePointInput& Point,
    ESplineCoordinateSpace::Type CoordinateSpace,
    bool bUpdateSpline)
{
    SplineComp->SetSplinePointType(Index, Point.PointType, false);
    if (Point.bHasArriveTangent && Point.bHasLeaveTangent)
    {
        SplineComp->SetTangentsAtSplinePoint(Index, Point.ArriveTangent, Point.LeaveTangent, CoordinateSpace, false);
    }
    else if (Point.bHasArriveTangent || Point.bHasLeaveTangent)
    {
        SplineComp->SetTangentAtSplinePoint(Index,
            Point.bHasArriveTangent ? Point.ArriveTangent : Point.LeaveTangent,
            CoordinateSpace, false);
    }
    if (Point.bHasRotation || Point.bHasRoll)
    {
        FRotator Rotation = Point.Rotation;
        if (Point.bHasRoll && !Point.bHasRotation) Rotation.Roll = Point.Roll;
        SplineComp->SetRotationAtSplinePoint(Index, Rotation, CoordinateSpace, false);
    }
    if (Point.bHasScale) SplineComp->SetScaleAtSplinePoint(Index, Point.Scale, false);
    if (bUpdateSpline) SplineComp->UpdateSpline();
}

static bool HasDuplicateSplinePoint(
    USplineComponent* SplineComp,
    const FVector& Position,
    ESplineCoordinateSpace::Type CoordinateSpace,
    int32 IgnoreIndex = INDEX_NONE)
{
    if (!SplineComp) return false;
    for (int32 Index = 0; Index < SplineComp->GetNumberOfSplinePoints(); ++Index)
    {
        if (Index != IgnoreIndex &&
            FVector::DistSquared(SplineComp->GetLocationAtSplinePoint(Index, CoordinateSpace), Position) <= KINDA_SMALL_NUMBER)
        {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Spline Creation Handlers
// ============================================================================

static bool HandleCreateSplineActor(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"), TEXT("SplineActor"));
    FVector Location = GetJsonVectorFieldSpline(Payload, TEXT("location"));
    FRotator Rotation = GetJsonRotatorFieldSpline(Payload, TEXT("rotation"));
    bool bClosedLoop = GetJsonBoolFieldSpline(Payload, TEXT("bClosedLoop"), false);
    FString SplineType = GetJsonStringFieldSpline(Payload, TEXT("splineType"), TEXT("Curve"));
    TArray<FMcpSplinePointInput> RoutePoints;
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;
    bool bHasRoute = false;
    FString RouteError;
    if (!TryParseSplineRoute(Payload, bClosedLoop, RoutePoints, CoordinateSpace, bHasRoute, RouteError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, RouteError, nullptr, TEXT("INVALID_ROUTE"));
        return true;
    }
    if (!SplineType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("splineType must be Linear, Curve, Constant or CurveClamped"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    // Spawn a new actor with a spline component
    // Use NameMode::Requested to auto-generate unique name if collision occurs
    // This prevents the Fatal Error: "Cannot generate unique name for 'SplineActor'"
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, SpawnParams);
    if (!NewActor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to spawn spline actor"), nullptr, TEXT("SPAWN_FAILED"));
        return true;
    }

    NewActor->SetActorLabel(*ActorName);

    // Create and attach spline component
    USplineComponent* SplineComp = NewObject<USplineComponent>(NewActor, TEXT("SplineComponent"));
    if (!SplineComp)
    {
        NewActor->Destroy();
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to create spline component"), nullptr, TEXT("COMPONENT_FAILED"));
        return true;
    }

    SplineComp->RegisterComponent();
    NewActor->AddInstanceComponent(SplineComp);
    // Note: Do not call AttachToComponent before SetRootComponent - the actor has no root yet
    NewActor->SetRootComponent(SplineComp);

    // Configure spline
    const FScopedTransaction Transaction(FText::FromString(TEXT("Create MCP Spline Actor")));
    NewActor->Modify();
    SplineComp->Modify();
    SplineComp->SetClosedLoop(bClosedLoop);

    ESplinePointType::Type PointType = ParseSplinePointType(SplineType);

    if (!SplineType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("splineType must be Linear, Curve, Constant or CurveClamped"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (bHasRoute)
    {
        SplineComp->ClearSplinePoints(false);
        for (int32 i = 0; i < RoutePoints.Num(); ++i)
        {
            const FMcpSplinePointInput& Point = RoutePoints[i];
            SplineComp->AddSplinePoint(Point.Location, CoordinateSpace, false);
            FMcpSplinePointInput AppliedPoint = Point;
            if (!AppliedPoint.bHasPointType) AppliedPoint.PointType = PointType;
            ApplySplinePointInput(SplineComp, i, AppliedPoint, CoordinateSpace, false);
        }
    }
    else
    {
        for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
        {
            SplineComp->SetSplinePointType(i, PointType, false);
        }
    }
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("actorName"), NewActor->GetActorLabel());
    Result->SetStringField(TEXT("actorPath"), NewActor->GetPathName());
    Result->SetNumberField(TEXT("pointCount"), SplineComp->GetNumberOfSplinePoints());
    Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());
    Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
    Result->SetStringField(TEXT("coordinateSpace"), CoordinateSpace == ESplineCoordinateSpace::World ? TEXT("World") : TEXT("Local"));

    // Add verification data
    McpHandlerUtils::AddVerification(Result, NewActor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Spline actor '%s' created with %d points"), *ActorName, SplineComp->GetNumberOfSplinePoints()), Result);
    return true;
}

static bool HandleAddSplinePoint(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FVector Position = GetJsonVectorFieldSpline(Payload, TEXT("position"));
    int32 Index = GetJsonIntFieldSpline(Payload, TEXT("index"), -1);
    FString PointType = GetJsonStringFieldSpline(Payload, TEXT("pointType"), TEXT("Curve"));
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;
    FString CoordinateError;
    if (!TryGetSplineCoordinateSpace(Payload, CoordinateSpace, CoordinateError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    const TSharedPtr<FJsonObject>* PositionObject = nullptr;
    FString PositionError;
    if ((!Payload->TryGetObjectField(TEXT("position"), PositionObject) &&
         !Payload->TryGetObjectField(TEXT("location"), PositionObject)) || !PositionObject ||
        !TryGetStrictSplineVector(*PositionObject, Position, PositionError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            PositionError.IsEmpty() ? TEXT("position with finite x, y and z is required") : PositionError,
            nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    if (!PointType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
        !PointType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase) &&
        !PointType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
        !PointType.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("pointType must be Linear, Curve, Constant or CurveClamped"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    if (Index < -1 || Index > SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid insertion index: %d"), Index), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }
    if (HasDuplicateSplinePoint(SplineComp, Position, CoordinateSpace))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Spline points must be unique and cannot create zero-length segments"), nullptr, TEXT("DUPLICATE_POINT"));
        return true;
    }

    // Add point at specified index or at end
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add MCP Spline Point")));
    Actor->Modify();
    SplineComp->Modify();
    if (Index < 0 || Index == SplineComp->GetNumberOfSplinePoints())
    {
        SplineComp->AddSplinePoint(Position, CoordinateSpace, false);
        Index = SplineComp->GetNumberOfSplinePoints() - 1;
    }
    else
    {
        SplineComp->AddSplinePointAtIndex(Position, Index, CoordinateSpace, false);
    }

    SplineComp->SetSplinePointType(Index, ParseSplinePointType(PointType), true);
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), Index);
    Result->SetNumberField(TEXT("totalPoints"), SplineComp->GetNumberOfSplinePoints());

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Added spline point at index %d"), Index), Result);
    return true;
}

static bool HandleRemoveSplinePoint(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), 0);
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    FString CoordinateError;
    if (!TryGetSplineCoordinateSpace(Payload, CoordinateSpace, CoordinateError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }

    if (SplineComp->GetNumberOfSplinePoints() <= 2)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("A spline must retain at least two points"), nullptr, TEXT("INSUFFICIENT_POINTS"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Remove MCP Spline Point")));
    Actor->Modify();
    SplineComp->Modify();
    SplineComp->RemoveSplinePoint(PointIndex, false);
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("removedIndex"), PointIndex);
    Result->SetNumberField(TEXT("remainingPoints"), SplineComp->GetNumberOfSplinePoints());

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Removed spline point at index %d"), PointIndex), Result);
    return true;
}

static bool HandleSetSplinePointPosition(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), 0);
    FVector Position = GetJsonVectorFieldSpline(Payload, TEXT("position"));
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    FString CoordinateError;
    if (!TryGetSplineCoordinateSpace(Payload, CoordinateSpace, CoordinateError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    const TSharedPtr<FJsonObject>* PositionObject = nullptr;
    FString PositionError;
    if ((!Payload->TryGetObjectField(TEXT("position"), PositionObject) &&
         !Payload->TryGetObjectField(TEXT("location"), PositionObject)) || !PositionObject ||
        !TryGetStrictSplineVector(*PositionObject, Position, PositionError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            PositionError.IsEmpty() ? TEXT("position with finite x, y and z is required") : PositionError,
            nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }

    if (HasDuplicateSplinePoint(SplineComp, Position, CoordinateSpace, PointIndex))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Spline points must be unique and cannot create a zero-length segment"), nullptr, TEXT("DUPLICATE_POINT"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Update MCP Spline Point")));
    Actor->Modify();
    SplineComp->Modify();
    SplineComp->SetLocationAtSplinePoint(PointIndex, Position, CoordinateSpace, false);
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), PointIndex);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Set position for spline point %d"), PointIndex), Result);
    return true;
}

static bool HandleSetSplinePointTangents(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), 0);
    FVector ArriveTangent = GetJsonVectorFieldSpline(Payload, TEXT("arriveTangent"));
    FVector LeaveTangent = GetJsonVectorFieldSpline(Payload, TEXT("leaveTangent"));
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    FString CoordinateError;
    if (!TryGetSplineCoordinateSpace(Payload, CoordinateSpace, CoordinateError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    const bool bHasArrive = Payload->HasField(TEXT("arriveTangent"));
    const bool bHasLeave = Payload->HasField(TEXT("leaveTangent"));
    const TSharedPtr<FJsonObject>* ArriveObject = nullptr;
    const TSharedPtr<FJsonObject>* LeaveObject = nullptr;
    FString TangentError;
    if (bHasArrive && (!Payload->TryGetObjectField(TEXT("arriveTangent"), ArriveObject) || !ArriveObject ||
        !TryGetStrictSplineVector(*ArriveObject, ArriveTangent, TangentError)))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TangentError.IsEmpty() ? TEXT("arriveTangent is invalid") : TangentError,
            nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    if (bHasLeave && (!Payload->TryGetObjectField(TEXT("leaveTangent"), LeaveObject) || !LeaveObject ||
        !TryGetStrictSplineVector(*LeaveObject, LeaveTangent, TangentError)))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TangentError.IsEmpty() ? TEXT("leaveTangent is invalid") : TangentError,
            nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    if (!bHasArrive && !bHasLeave)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("arriveTangent or leaveTangent is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Configure MCP Spline Tangents")));
    Actor->Modify();
    SplineComp->Modify();
    if (bHasArrive && bHasLeave)
    {
        SplineComp->SetTangentsAtSplinePoint(PointIndex, ArriveTangent, LeaveTangent, CoordinateSpace, false);
    }
    else
    {
        SplineComp->SetTangentAtSplinePoint(PointIndex, bHasArrive ? ArriveTangent : LeaveTangent, CoordinateSpace, false);
    }
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), PointIndex);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Set tangents for spline point %d"), PointIndex), Result);
    return true;
}

static bool HandleSetSplinePointRotation(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), 0);
    FRotator Rotation = GetJsonRotatorFieldSpline(Payload, TEXT("pointRotation"));
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    FString CoordinateError;
    if (!TryGetSplineCoordinateSpace(Payload, CoordinateSpace, CoordinateError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Set MCP Spline Point Rotation")));
    Actor->Modify();
    SplineComp->Modify();
    SplineComp->SetRotationAtSplinePoint(PointIndex, Rotation, CoordinateSpace, false);
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), PointIndex);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Set rotation for spline point %d"), PointIndex), Result);
    return true;
}

static bool HandleSetSplinePointScale(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), 0);
    FVector Scale = GetJsonVectorFieldSpline(Payload, TEXT("pointScale"), FVector::OneVector);

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }

    if (Scale.X <= 0.0f || Scale.Y <= 0.0f || Scale.Z <= 0.0f)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("pointScale values must be greater than zero"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Set MCP Spline Point Scale")));
    Actor->Modify();
    SplineComp->Modify();
    SplineComp->SetScaleAtSplinePoint(PointIndex, Scale, false);
    SplineComp->UpdateSpline();

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), PointIndex);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Set scale for spline point %d"), PointIndex), Result);
    return true;
}

static bool HandleSetSplineType(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FString SplineType = GetJsonStringFieldSpline(Payload, TEXT("splineType"), TEXT("Curve"));
    int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), -1);

    if (SplineType != TEXT("Linear") && SplineType != TEXT("Curve") &&
        SplineType != TEXT("Constant") && SplineType != TEXT("CurveClamped"))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("splineType must be Linear, Curve, Constant, or CurveClamped"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Set MCP Spline Point Type")));
    Actor->Modify();
    SplineComp->Modify();

    ESplinePointType::Type PointType = ParseSplinePointType(SplineType);

    if (PointIndex >= 0)
    {
        // Set for specific point
        if (PointIndex >= SplineComp->GetNumberOfSplinePoints())
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Invalid point index: %d"), PointIndex), nullptr, TEXT("INVALID_INDEX"));
            return true;
        }
        SplineComp->SetSplinePointType(PointIndex, PointType, false);
    }
    else
    {
        // Set for all points
        for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); i++)
        {
            SplineComp->SetSplinePointType(i, PointType, false);
        }
    }

    SplineComp->UpdateSpline();
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("splineType"), SplineType);
    Result->SetNumberField(TEXT("pointsAffected"), PointIndex >= 0 ? 1 : SplineComp->GetNumberOfSplinePoints());

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Set spline type to %s"), *SplineType), Result);
    return true;
}

// ============================================================================
// Spline Mesh Handlers
// ============================================================================

static bool HandleCreateSplineMeshComponent(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString BlueprintPath = GetJsonStringFieldSpline(Payload, TEXT("blueprintPath"));
    FString ComponentName = GetJsonStringFieldSpline(Payload, TEXT("componentName"), TEXT("SplineMesh"));
    FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));
    FString ForwardAxis = GetJsonStringFieldSpline(Payload, TEXT("forwardAxis"), TEXT("X"));

    if (BlueprintPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("blueprintPath is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    // SECURITY: Validate blueprintPath to prevent directory traversal and arbitrary file access
    FString SafeBlueprintPath = SanitizeProjectRelativePath(BlueprintPath);
    if (SafeBlueprintPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid or unsafe blueprintPath: %s. Path must be relative to project (e.g., /Game/...)"), *BlueprintPath),
            nullptr, TEXT("SECURITY_VIOLATION"));
        return true;
    }

    // SECURITY: Validate meshPath if provided
    FString SafeMeshPath;
    if (!MeshPath.IsEmpty())
    {
        SafeMeshPath = SanitizeProjectRelativePath(MeshPath);
        if (SafeMeshPath.IsEmpty())
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Invalid or unsafe meshPath: %s. Path must be relative to project (e.g., /Game/...)"), *MeshPath),
                nullptr, TEXT("SECURITY_VIOLATION"));
            return true;
        }
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *SafeBlueprintPath);
    if (!Blueprint)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Blueprint has no SimpleConstructionScript"), nullptr, TEXT("INVALID_BP"));
        return true;
    }

    // Check if component already exists
    for (USCS_Node* Node : SCS->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Component '%s' already exists"), *ComponentName), nullptr, TEXT("ALREADY_EXISTS"));
            return true;
        }
    }

    // Create the SCS node for SplineMeshComponent
    USCS_Node* NewNode = SCS->CreateNode(USplineMeshComponent::StaticClass(), *ComponentName);
    if (!NewNode)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to create SCS node"), nullptr, TEXT("CREATE_FAILED"));
        return true;
    }

    // Configure the component template
    USplineMeshComponent* MeshComp = Cast<USplineMeshComponent>(NewNode->ComponentTemplate);
    if (MeshComp)
    {
        // Set mesh if provided (use sanitized path)
        if (!SafeMeshPath.IsEmpty())
        {
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *SafeMeshPath);
            if (!Mesh)
            {
                Self->SendAutomationResponse(Socket, RequestId, false,
                    FString::Printf(TEXT("Mesh not found: %s"), *SafeMeshPath), nullptr, TEXT("MESH_NOT_FOUND"));
                return true;
            }
            MeshComp->SetStaticMesh(Mesh);
        }

        // Set forward axis
        ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
        if (ForwardAxis == TEXT("Y")) Axis = ESplineMeshAxis::Y;
        else if (ForwardAxis == TEXT("Z")) Axis = ESplineMeshAxis::Z;
        MeshComp->SetForwardAxis(Axis);

        // Ensure material is valid - use fallback if engine default is missing
        // This prevents "DefaultMaterial not available" warnings on custom engine builds
        if (MeshComp->GetMaterial(0) == nullptr)
        {
            UMaterialInterface* FallbackMaterial = McpLoadMaterialWithFallback(TEXT(""), true);
            if (FallbackMaterial)
            {
                MeshComp->SetMaterial(0, FallbackMaterial);
            }
        }
    }

    // Add node to SCS
    SCS->AddNode(NewNode);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    if (GetJsonBoolFieldSpline(Payload, TEXT("save"), false))
    {
        McpSafeAssetSave(Blueprint);
    }

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("componentName"), ComponentName);
    Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);

    // Add verification data
    Result->SetBoolField(TEXT("existsAfter"), true);
    // Use action prefix format expected by TS message-handler.ts enforceActionMatch()
    Result->SetStringField(TEXT("action"), TEXT("manage_splines:component_added"));

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("SplineMeshComponent '%s' added to Blueprint"), *ComponentName), Result);
    return true;
}

static bool HandleSetSplineMeshAsset(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FString ComponentName = GetJsonStringFieldSpline(Payload, TEXT("componentName"));
    FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));

    if (ActorName.IsEmpty() || MeshPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName and meshPath are required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    // SECURITY: Validate meshPath to prevent directory traversal and arbitrary file access
    FString SafeMeshPath = SanitizeProjectRelativePath(MeshPath);
    if (SafeMeshPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid or unsafe meshPath: %s. Path must be relative to project (e.g., /Game/...)"), *MeshPath),
            nullptr, TEXT("SECURITY_VIOLATION"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    // Find SplineMeshComponent
    TArray<USplineMeshComponent*> MeshComponents;
    Actor->GetComponents<USplineMeshComponent>(MeshComponents);

    USplineMeshComponent* TargetComp = nullptr;
    if (!ComponentName.IsEmpty())
    {
        for (USplineMeshComponent* Comp : MeshComponents)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                TargetComp = Comp;
                break;
            }
        }
    }
    else if (MeshComponents.Num() > 0)
    {
        TargetComp = MeshComponents[0];
    }

    if (!TargetComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No SplineMeshComponent found on actor"), nullptr, TEXT("NO_COMPONENT"));
        return true;
    }

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *SafeMeshPath);
    if (!Mesh)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Mesh not found: %s"), *SafeMeshPath), nullptr, TEXT("MESH_NOT_FOUND"));
        return true;
    }

    TargetComp->SetStaticMesh(Mesh);
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("actorName"), ActorName);
    Result->SetStringField(TEXT("meshPath"), SafeMeshPath);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Spline mesh asset set"), Result);
    return true;
}

static bool HandleConfigureSplineMeshAxis(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FString ComponentName = GetJsonStringFieldSpline(Payload, TEXT("componentName"));
    FString ForwardAxis = GetJsonStringFieldSpline(Payload, TEXT("forwardAxis"), TEXT("X"));

    if (ActorName.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName is required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    TArray<USplineMeshComponent*> MeshComponents;
    Actor->GetComponents<USplineMeshComponent>(MeshComponents);

    USplineMeshComponent* TargetComp = nullptr;
    if (!ComponentName.IsEmpty())
    {
        for (USplineMeshComponent* Comp : MeshComponents)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                TargetComp = Comp;
                break;
            }
        }
    }
    else if (MeshComponents.Num() > 0)
    {
        TargetComp = MeshComponents[0];
    }

    if (!TargetComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No SplineMeshComponent found on actor"), nullptr, TEXT("NO_COMPONENT"));
        return true;
    }

    ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
    if (ForwardAxis == TEXT("Y")) Axis = ESplineMeshAxis::Y;
    else if (ForwardAxis == TEXT("Z")) Axis = ESplineMeshAxis::Z;

    TargetComp->SetForwardAxis(Axis);
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("forwardAxis"), ForwardAxis);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Spline mesh forward axis set to %s"), *ForwardAxis), Result);
    return true;
}

static bool HandleSetSplineMeshMaterial(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FString ComponentName = GetJsonStringFieldSpline(Payload, TEXT("componentName"));
    FString MaterialPath = GetJsonStringFieldSpline(Payload, TEXT("materialPath"));
    int32 MaterialIndex = GetJsonIntFieldSpline(Payload, TEXT("materialIndex"), 0);

    if (ActorName.IsEmpty() || MaterialPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("actorName and materialPath are required"), nullptr, TEXT("MISSING_PARAM"));
        return true;
    }

    // SECURITY: Validate materialPath to prevent directory traversal and arbitrary file access
    FString SafeMaterialPath = SanitizeProjectRelativePath(MaterialPath);
    if (SafeMaterialPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid or unsafe materialPath: %s. Path must be relative to project (e.g., /Game/...)"), *MaterialPath),
            nullptr, TEXT("SECURITY_VIOLATION"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    TArray<USplineMeshComponent*> MeshComponents;
    Actor->GetComponents<USplineMeshComponent>(MeshComponents);

    USplineMeshComponent* TargetComp = nullptr;
    if (!ComponentName.IsEmpty())
    {
        for (USplineMeshComponent* Comp : MeshComponents)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                TargetComp = Comp;
                break;
            }
        }
    }
    else if (MeshComponents.Num() > 0)
    {
        TargetComp = MeshComponents[0];
    }

    if (!TargetComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No SplineMeshComponent found on actor"), nullptr, TEXT("NO_COMPONENT"));
        return true;
    }

    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *SafeMaterialPath);
    if (!Material)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Material not found: %s"), *SafeMaterialPath), nullptr, TEXT("MATERIAL_NOT_FOUND"));
        return true;
    }

    TargetComp->SetMaterial(MaterialIndex, Material);
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("materialPath"), SafeMaterialPath);
    Result->SetNumberField(TEXT("materialIndex"), MaterialIndex);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);
    AddComponentVerification(Result, TargetComp);

    Self->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Spline mesh material set"), Result);
    return true;
}

static bool HandleCreateSplineMeshActor(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"), TEXT("SplineMeshActor"));
    FString ComponentName = GetJsonStringFieldSpline(Payload, TEXT("componentName"), TEXT("SplineMesh"));
    FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));
    FString ForwardAxis = GetJsonStringFieldSpline(Payload, TEXT("forwardAxis"), TEXT("X"));
    FVector Location = GetJsonVectorFieldSpline(Payload, TEXT("location"));
    FRotator Rotation = GetJsonRotatorFieldSpline(Payload, TEXT("rotation"));

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    // SECURITY: Validate meshPath if provided
    FString SafeMeshPath;
    if (!MeshPath.IsEmpty())
    {
        SafeMeshPath = SanitizeProjectRelativePath(MeshPath);
        if (SafeMeshPath.IsEmpty())
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Invalid or unsafe meshPath: %s. Path must be relative to project (e.g., /Game/...)"), *MeshPath),
                nullptr, TEXT("SECURITY_VIOLATION"));
            return true;
        }
    }

    // Spawn actor with unique name handling
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, SpawnParams);
    if (!NewActor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to spawn spline mesh actor"), nullptr, TEXT("SPAWN_FAILED"));
        return true;
    }

    NewActor->SetActorLabel(*ActorName);

    // Create SplineMeshComponent and attach to actor
    USplineMeshComponent* SplineMeshComp = NewObject<USplineMeshComponent>(NewActor, *ComponentName);
    if (!SplineMeshComp)
    {
        NewActor->Destroy();
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to create SplineMeshComponent"), nullptr, TEXT("COMPONENT_FAILED"));
        return true;
    }

    SplineMeshComp->RegisterComponent();
    NewActor->AddInstanceComponent(SplineMeshComp);
    NewActor->SetRootComponent(SplineMeshComp);

    // Set mesh if provided
    if (!SafeMeshPath.IsEmpty())
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *SafeMeshPath);
        if (!Mesh)
        {
            // Clean up the partially created actor
            NewActor->Destroy();
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Mesh not found: %s"), *SafeMeshPath), nullptr, TEXT("MESH_NOT_FOUND"));
            return true;
        }
        SplineMeshComp->SetStaticMesh(Mesh);
    }

    // Ensure material is valid - use fallback if engine default is missing
    // This prevents "DefaultMaterial not available" warnings on custom engine builds
    if (SplineMeshComp->GetMaterial(0) == nullptr)
    {
        UMaterialInterface* FallbackMaterial = McpLoadMaterialWithFallback(TEXT(""), true);
        if (FallbackMaterial)
        {
            SplineMeshComp->SetMaterial(0, FallbackMaterial);
        }
    }

    // Set forward axis
    ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
    if (ForwardAxis == TEXT("Y")) Axis = ESplineMeshAxis::Y;
    else if (ForwardAxis == TEXT("Z")) Axis = ESplineMeshAxis::Z;
    SplineMeshComp->SetForwardAxis(Axis);

    // Set default start/end positions for a simple spline mesh
    SplineMeshComp->SetStartAndEnd(FVector::ZeroVector, FVector(100, 0, 0),
                                    FVector(500, 0, 0), FVector(-100, 0, 0));

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("actorName"), NewActor->GetActorLabel());
    Result->SetStringField(TEXT("actorPath"), NewActor->GetPathName());
    Result->SetStringField(TEXT("componentName"), ComponentName);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, NewActor);
    AddComponentVerification(Result, SplineMeshComp);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("SplineMeshActor '%s' created with component '%s'"), *ActorName, *ComponentName), Result);
    return true;
}

// ============================================================================
// Mesh Scattering Handlers
// ============================================================================

static bool HandleScatterMeshesAlongSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));
    bool bAlignToSpline = GetJsonBoolFieldSpline(Payload, TEXT("alignToSpline"), true);

    // Sanitize mesh path
    FString SafeMeshPath = SanitizeProjectRelativePath(MeshPath);
    if (SafeMeshPath.IsEmpty())
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Invalid or unsafe meshPath: %s. Path must be relative to project (e.g., /Game/...)"), *MeshPath),
            nullptr, TEXT("SECURITY_VIOLATION"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    AActor* Actor = FindActorByName(World, ActorName);
    if (!Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
        return true;
    }

    const bool bHasSpacing = Payload.IsValid() && Payload->HasField(TEXT("spacing"));
    const bool bHasUseRandomOffset = Payload.IsValid() && Payload->HasField(TEXT("useRandomOffset"));
    const bool bHasRandomOffsetRange = Payload.IsValid() && Payload->HasField(TEXT("randomOffsetRange"));
    const bool bHasRandomizeScale = Payload.IsValid() && Payload->HasField(TEXT("randomizeScale"));
    const bool bHasMinScale = Payload.IsValid() && Payload->HasField(TEXT("minScale"));
    const bool bHasMaxScale = Payload.IsValid() && Payload->HasField(TEXT("maxScale"));
    const bool bHasRandomizeRotation = Payload.IsValid() && Payload->HasField(TEXT("randomizeRotation"));
    const bool bHasRotationRange = Payload.IsValid() && Payload->HasField(TEXT("rotationRange"));

    double Spacing = bHasSpacing
        ? GetJsonNumberFieldSpline(Payload, TEXT("spacing"), 100.0)
        : GetConfiguredSplineNumber(Actor, World, TEXT("meshSpacing"), 100.0);
    const bool bUseRandomOffset = bHasUseRandomOffset
        ? GetJsonBoolFieldSpline(Payload, TEXT("useRandomOffset"), false)
        : GetConfiguredSplineBool(Actor, World, TEXT("useRandomOffset"), false);
    const double RandomOffsetRange = bHasRandomOffsetRange
        ? GetJsonNumberFieldSpline(Payload, TEXT("randomOffsetRange"), 0.0)
        : GetConfiguredSplineNumber(Actor, World, TEXT("randomOffsetRange"), 0.0);
    const bool bRandomizeScale = bHasRandomizeScale
        ? GetJsonBoolFieldSpline(Payload, TEXT("randomizeScale"), false)
        : GetConfiguredSplineBool(Actor, World, TEXT("randomizeScale"), false);
    const double MinScale = bHasMinScale
        ? GetJsonNumberFieldSpline(Payload, TEXT("minScale"), 0.8)
        : GetConfiguredSplineNumber(Actor, World, TEXT("minScale"), 0.8);
    const double MaxScale = bHasMaxScale
        ? GetJsonNumberFieldSpline(Payload, TEXT("maxScale"), 1.2)
        : GetConfiguredSplineNumber(Actor, World, TEXT("maxScale"), 1.2);
    const bool bRandomizeRotation = bHasRandomizeRotation
        ? GetJsonBoolFieldSpline(Payload, TEXT("randomizeRotation"), false)
        : GetConfiguredSplineBool(Actor, World, TEXT("randomizeRotation"), false);
    const double RotationRange = bHasRotationRange
        ? GetJsonNumberFieldSpline(Payload, TEXT("rotationRange"), 360.0)
        : GetConfiguredSplineNumber(Actor, World, TEXT("rotationRange"), 360.0);

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *SafeMeshPath);
    if (!Mesh)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Mesh not found: %s"), *SafeMeshPath), nullptr, TEXT("MESH_NOT_FOUND"));
        return true;
    }

    // Validate spacing/randomization before creating components.
    if (Spacing <= 0.0)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("spacing must be greater than 0"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    if (RandomOffsetRange < 0.0 || MinScale <= 0.0 || MaxScale <= 0.0 || MinScale > MaxScale || RotationRange < 0.0)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Invalid spline mesh randomization configuration"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    float SplineLength = SplineComp->GetSplineLength();
    int32 MeshCount = FMath::FloorToInt(SplineLength / Spacing);

    TArray<FString> CreatedMeshes;

    for (int32 i = 0; i <= MeshCount; i++)
    {
        float Distance = static_cast<float>(i * Spacing);
        if (bUseRandomOffset && RandomOffsetRange > 0.0)
        {
            Distance += FMath::FRandRange(static_cast<float>(-RandomOffsetRange), static_cast<float>(RandomOffsetRange));
            Distance = FMath::Clamp(Distance, 0.0f, SplineLength);
        }
        FVector Location = SplineComp->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        FRotator Rotation = bAlignToSpline
            ? SplineComp->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World)
            : FRotator::ZeroRotator;

        if (bRandomizeRotation && RotationRange > 0.0)
        {
            Rotation.Yaw += FMath::FRandRange(static_cast<float>(-RotationRange), static_cast<float>(RotationRange));
        }

        // Create a static mesh component for each instance
        UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(Actor);
        if (MeshComp)
        {
            MeshComp->SetStaticMesh(Mesh);
            MeshComp->SetWorldLocationAndRotation(Location, Rotation);
            if (bRandomizeScale)
            {
                const float UniformScale = FMath::FRandRange(static_cast<float>(MinScale), static_cast<float>(MaxScale));
                MeshComp->SetWorldScale3D(FVector(UniformScale));
            }
            MeshComp->RegisterComponent();
            Actor->AddInstanceComponent(MeshComp);
            MeshComp->AttachToComponent(SplineComp, FAttachmentTransformRules::KeepWorldTransform);
            CreatedMeshes.Add(MeshComp->GetName());
        }
    }

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("meshesCreated"), CreatedMeshes.Num());
    Result->SetNumberField(TEXT("splineLength"), SplineLength);
    Result->SetNumberField(TEXT("spacing"), Spacing);
    Result->SetBoolField(TEXT("useRandomOffset"), bUseRandomOffset);
    Result->SetNumberField(TEXT("randomOffsetRange"), RandomOffsetRange);
    Result->SetBoolField(TEXT("randomizeScale"), bRandomizeScale);
    Result->SetNumberField(TEXT("minScale"), MinScale);
    Result->SetNumberField(TEXT("maxScale"), MaxScale);
    Result->SetBoolField(TEXT("randomizeRotation"), bRandomizeRotation);
    Result->SetNumberField(TEXT("rotationRange"), RotationRange);

    // Add verification data
    McpHandlerUtils::AddVerification(Result, Actor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("Scattered %d meshes along spline"), CreatedMeshes.Num()), Result);
    return true;
}

static bool HandleConfigureMeshSpacing(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    AActor* Target = ResolveSplineConfigTarget(World, ActorName);
    if (!Target)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Spline configuration target not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    const double Spacing = GetJsonNumberFieldSpline(Payload, TEXT("spacing"), 100.0);
    const bool bUseRandomOffset = GetJsonBoolFieldSpline(Payload, TEXT("useRandomOffset"), false);
    const double RandomOffsetRange = GetJsonNumberFieldSpline(Payload, TEXT("randomOffsetRange"), 0.0);

    if (Spacing <= 0.0 || RandomOffsetRange < 0.0)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("spacing must be greater than 0 and randomOffsetRange must be non-negative"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    SetSplineConfigValue(Target, TEXT("meshSpacing"), FString::SanitizeFloat(Spacing));
    SetSplineConfigValue(Target, TEXT("useRandomOffset"), BoolToSplineConfigString(bUseRandomOffset));
    SetSplineConfigValue(Target, TEXT("randomOffsetRange"), FString::SanitizeFloat(RandomOffsetRange));
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("targetName"), GetSplineConfigTargetName(Target));
    Result->SetStringField(TEXT("targetPath"), Target->GetPathName());
    Result->SetBoolField(TEXT("stored"), true);
    Result->SetNumberField(TEXT("spacing"), Spacing);
    Result->SetBoolField(TEXT("useRandomOffset"), bUseRandomOffset);
    Result->SetNumberField(TEXT("randomOffsetRange"), RandomOffsetRange);

    Self->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Mesh spacing configuration stored on Unreal spline target"), Result);
    return true;
}

static bool HandleConfigureMeshRandomization(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    AActor* Target = ResolveSplineConfigTarget(World, ActorName);
    if (!Target)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            FString::Printf(TEXT("Spline configuration target not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    const bool bRandomizeScale = GetJsonBoolFieldSpline(Payload, TEXT("randomizeScale"), false);
    const double MinScale = GetJsonNumberFieldSpline(Payload, TEXT("minScale"), 0.8);
    const double MaxScale = GetJsonNumberFieldSpline(Payload, TEXT("maxScale"), 1.2);
    const bool bRandomizeRotation = GetJsonBoolFieldSpline(Payload, TEXT("randomizeRotation"), false);
    const double RotationRange = GetJsonNumberFieldSpline(Payload, TEXT("rotationRange"), 360.0);

    if (MinScale <= 0.0 || MaxScale <= 0.0 || MinScale > MaxScale || RotationRange < 0.0)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Scale values must be positive, minScale must not exceed maxScale, and rotationRange must be non-negative"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    SetSplineConfigValue(Target, TEXT("randomizeScale"), BoolToSplineConfigString(bRandomizeScale));
    SetSplineConfigValue(Target, TEXT("minScale"), FString::SanitizeFloat(MinScale));
    SetSplineConfigValue(Target, TEXT("maxScale"), FString::SanitizeFloat(MaxScale));
    SetSplineConfigValue(Target, TEXT("randomizeRotation"), BoolToSplineConfigString(bRandomizeRotation));
    SetSplineConfigValue(Target, TEXT("rotationRange"), FString::SanitizeFloat(RotationRange));
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("targetName"), GetSplineConfigTargetName(Target));
    Result->SetStringField(TEXT("targetPath"), Target->GetPathName());
    Result->SetBoolField(TEXT("stored"), true);
    Result->SetBoolField(TEXT("randomizeScale"), bRandomizeScale);
    Result->SetNumberField(TEXT("minScale"), MinScale);
    Result->SetNumberField(TEXT("maxScale"), MaxScale);
    Result->SetBoolField(TEXT("randomizeRotation"), bRandomizeRotation);
    Result->SetNumberField(TEXT("rotationRange"), RotationRange);

    Self->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Mesh randomization configuration stored on Unreal spline target"), Result);
    return true;
}

// ============================================================================
// Quick Template Handlers
// ============================================================================

static void GetGeneratedSplineMeshComponents(AActor* Actor, TArray<USplineMeshComponent*>& OutComponents)
{
    OutComponents.Reset();
    if (!Actor) return;

    TArray<USplineMeshComponent*> MeshComponents;
    Actor->GetComponents<USplineMeshComponent>(MeshComponents);
    for (USplineMeshComponent* Component : MeshComponents)
    {
        if (Component && Component->ComponentTags.Contains(FName(TEXT("MCP.GeneratedSplineSegment"))))
        {
            OutComponents.Add(Component);
        }
    }
}

static int32 ClearGeneratedSplineMeshComponents(AActor* Actor)
{
    if (!Actor) return 0;

    TArray<USplineMeshComponent*> Generated;
    GetGeneratedSplineMeshComponents(Actor, Generated);
    for (USplineMeshComponent* Component : Generated)
    {
        if (Component)
        {
            Actor->RemoveInstanceComponent(Component);
            Component->DestroyComponent();
        }
    }
    return Generated.Num();
}

static bool GenerateSplineMeshSegments(
    AActor* Actor,
    USplineComponent* SplineComp,
    UStaticMesh* Mesh,
    UMaterialInterface* Material,
    ESplineMeshAxis::Type ForwardAxis,
    bool bCollisionEnabled,
    TArray<USplineMeshComponent*>& OutComponents,
    FString& OutError)
{
    if (!Actor || !SplineComp || !Mesh)
    {
        OutError = TEXT("actor, spline component and mesh are required");
        return false;
    }
    const int32 PointCount = SplineComp->GetNumberOfSplinePoints();
    const int32 SegmentCount = SplineComp->IsClosedLoop() ? PointCount : PointCount - 1;
    if (PointCount < 2 || SegmentCount < 1)
    {
        OutError = TEXT("at least two non-duplicate spline points are required");
        return false;
    }

    ClearGeneratedSplineMeshComponents(Actor);
    OutComponents.Reset();
    for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
    {
        const int32 StartIndex = SegmentIndex;
        const int32 EndIndex = (SegmentIndex + 1) % PointCount;
        const FVector Start = SplineComp->GetLocationAtSplinePoint(StartIndex, ESplineCoordinateSpace::Local);
        const FVector End = SplineComp->GetLocationAtSplinePoint(EndIndex, ESplineCoordinateSpace::Local);
        if (FVector::DistSquared(Start, End) <= KINDA_SMALL_NUMBER)
        {
            OutError = FString::Printf(TEXT("spline segment %d has zero length"), SegmentIndex);
            ClearGeneratedSplineMeshComponents(Actor);
            return false;
        }

        const FString ComponentName = FString::Printf(TEXT("MCP_SplineSegment_%03d"), SegmentIndex);
        USplineMeshComponent* Component = NewObject<USplineMeshComponent>(Actor, *ComponentName);
        if (!Component)
        {
            OutError = FString::Printf(TEXT("failed to create spline mesh segment %d"), SegmentIndex);
            ClearGeneratedSplineMeshComponents(Actor);
            return false;
        }
        Component->ComponentTags.Add(FName(TEXT("MCP.GeneratedSplineSegment")));
        Component->ComponentTags.Add(FName(*FString::Printf(TEXT("MCP.SplineSegmentIndex=%d"), SegmentIndex)));
        Component->SetStaticMesh(Mesh);
        Component->SetForwardAxis(ForwardAxis);
        Component->SetCollisionEnabled(bCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if (Material) Component->SetMaterial(0, Material);
        Component->SetStartAndEnd(
            Start,
            SplineComp->GetTangentAtSplinePoint(StartIndex, ESplineCoordinateSpace::Local),
            End,
            SplineComp->GetTangentAtSplinePoint(EndIndex, ESplineCoordinateSpace::Local));
        const FVector StartScale = SplineComp->GetScaleAtSplinePoint(StartIndex);
        const FVector EndScale = SplineComp->GetScaleAtSplinePoint(EndIndex);
        Component->SetStartScale(FVector2D(StartScale.X, StartScale.Y));
        Component->SetEndScale(FVector2D(EndScale.X, EndScale.Y));
        Component->SetStartRoll(FMath::DegreesToRadians(SplineComp->GetRollAtSplinePoint(StartIndex, ESplineCoordinateSpace::Local)), false);
        Component->SetEndRoll(FMath::DegreesToRadians(SplineComp->GetRollAtSplinePoint(EndIndex, ESplineCoordinateSpace::Local)), false);
        Component->RegisterComponent();
        Actor->AddInstanceComponent(Component);
        Component->AttachToComponent(SplineComp, FAttachmentTransformRules::KeepRelativeTransform);
        Component->UpdateMesh();
        OutComponents.Add(Component);
    }
    return true;
}

static ESplineMeshAxis::Type ParseSplineMeshAxis(const FString& AxisString)
{
    if (AxisString.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) return ESplineMeshAxis::Y;
    if (AxisString.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) return ESplineMeshAxis::Z;
    return ESplineMeshAxis::X;
}

static bool ResolveSplineMeshSettings(
    const TSharedPtr<FJsonObject>& Payload,
    UStaticMesh*& OutMesh,
    UMaterialInterface*& OutMaterial,
    ESplineMeshAxis::Type& OutAxis,
    bool& OutCollisionEnabled,
    FString& OutSafeMeshPath,
    FString& OutSafeMaterialPath,
    FString& OutError)
{
    const FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));
    if (MeshPath.IsEmpty())
    {
        OutError = TEXT("meshPath is required");
        return false;
    }
    OutSafeMeshPath = SanitizeProjectRelativePath(MeshPath);
    if (OutSafeMeshPath.IsEmpty())
    {
        OutError = TEXT("meshPath is invalid or unsafe");
        return false;
    }
    OutMesh = LoadObject<UStaticMesh>(nullptr, *OutSafeMeshPath);
    if (!OutMesh)
    {
        OutError = FString::Printf(TEXT("mesh not found: %s"), *OutSafeMeshPath);
        return false;
    }

    OutSafeMaterialPath.Reset();
    OutMaterial = nullptr;
    const FString MaterialPath = GetJsonStringFieldSpline(Payload, TEXT("materialPath"));
    if (!MaterialPath.IsEmpty())
    {
        OutSafeMaterialPath = SanitizeProjectRelativePath(MaterialPath);
        if (OutSafeMaterialPath.IsEmpty())
        {
            OutError = TEXT("materialPath is invalid or unsafe");
            return false;
        }
        OutMaterial = LoadObject<UMaterialInterface>(nullptr, *OutSafeMaterialPath);
        if (!OutMaterial)
        {
            OutError = FString::Printf(TEXT("material not found: %s"), *OutSafeMaterialPath);
            return false;
        }
    }
    OutAxis = ParseSplineMeshAxis(GetJsonStringFieldSpline(Payload, TEXT("forwardAxis"), TEXT("X")));
    OutCollisionEnabled = GetJsonBoolFieldSpline(Payload, TEXT("collisionEnabled"), true);
    return true;
}

static bool HandleGenerateSplineMeshSegments(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }
    AActor* Actor = FindActorByName(World, ActorName);
    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!Actor || !SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("Spline actor or component not found"), nullptr, TEXT("NOT_FOUND"));
        return true;
    }

    UStaticMesh* Mesh = nullptr;
    UMaterialInterface* Material = nullptr;
    ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
    bool bCollisionEnabled = true;
    FString SafeMeshPath;
    FString SafeMaterialPath;
    FString Error;
    if (!ResolveSplineMeshSettings(Payload, Mesh, Material, Axis, bCollisionEnabled,
        SafeMeshPath, SafeMaterialPath, Error))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, Error, nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Generate MCP Spline Mesh Segments")));
    Actor->Modify();
    SplineComp->Modify();
    TArray<USplineMeshComponent*> Components;
    if (!GenerateSplineMeshSegments(Actor, SplineComp, Mesh, Material, Axis, bCollisionEnabled, Components, Error))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, Error, nullptr, TEXT("GENERATE_FAILED"));
        return true;
    }
    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
    Result->SetStringField(TEXT("meshPath"), SafeMeshPath);
    Result->SetStringField(TEXT("materialPath"), SafeMaterialPath);
    Result->SetNumberField(TEXT("segmentCount"), Components.Num());
    Result->SetBoolField(TEXT("collisionEnabled"), bCollisionEnabled);
    TArray<TSharedPtr<FJsonValue>> SegmentArray;
    const int32 PointCount = SplineComp->GetNumberOfSplinePoints();
    for (int32 SegmentIndex = 0; SegmentIndex < Components.Num(); ++SegmentIndex)
    {
        const int32 EndIndex = (SegmentIndex + 1) % PointCount;
        const FVector StartWorld = SplineComp->GetLocationAtSplinePoint(SegmentIndex, ESplineCoordinateSpace::World);
        const FVector EndWorld = SplineComp->GetLocationAtSplinePoint(EndIndex, ESplineCoordinateSpace::World);
        TSharedPtr<FJsonObject> Segment = McpHandlerUtils::CreateResultObject();
        Segment->SetStringField(TEXT("componentName"), Components[SegmentIndex]->GetName());
        TSharedPtr<FJsonObject> Start = McpHandlerUtils::CreateResultObject();
        Start->SetNumberField(TEXT("x"), StartWorld.X); Start->SetNumberField(TEXT("y"), StartWorld.Y); Start->SetNumberField(TEXT("z"), StartWorld.Z);
        TSharedPtr<FJsonObject> End = McpHandlerUtils::CreateResultObject();
        End->SetNumberField(TEXT("x"), EndWorld.X); End->SetNumberField(TEXT("y"), EndWorld.Y); End->SetNumberField(TEXT("z"), EndWorld.Z);
        Segment->SetObjectField(TEXT("startPosition"), Start);
        Segment->SetObjectField(TEXT("endPosition"), End);
        Segment->SetBoolField(TEXT("collisionEnabled"), Components[SegmentIndex]->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
        SegmentArray.Add(MakeShared<FJsonValueObject>(Segment));
    }
    Result->SetArrayField(TEXT("generatedComponents"), SegmentArray);
    Self->SendAutomationResponse(Socket, RequestId, true, TEXT("Spline mesh segments generated"), Result);
    return true;
}

static bool HandleClearGeneratedSplineSegments(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    AActor* Actor = FindActorByName(World, ActorName);
    if (!World || !Actor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("Spline actor not found"), nullptr, TEXT("NOT_FOUND"));
        return true;
    }
    const FScopedTransaction Transaction(FText::FromString(TEXT("Clear MCP Generated Spline Segments")));
    Actor->Modify();
    const int32 ClearedCount = ClearGeneratedSplineMeshComponents(Actor);
    World->MarkPackageDirty();
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("clearedCount"), ClearedCount);
    Result->SetNumberField(TEXT("segmentCount"), 0);
    Self->SendAutomationResponse(Socket, RequestId, true, TEXT("Generated spline segments cleared"), Result);
    return true;
}

static bool HandleCreateTemplateSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket,
    const FString& TemplateName,
    const FString& DefaultMeshPath)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"), TemplateName + TEXT("_Spline"));
    FVector Location = GetJsonVectorFieldSpline(Payload, TEXT("location"));
    double Width = GetJsonNumberFieldSpline(Payload, TEXT("width"), 400.0);
    FString MaterialPath = GetJsonStringFieldSpline(Payload, TEXT("materialPath"));
    const bool bClosedLoop = GetJsonBoolFieldSpline(Payload, TEXT("bClosedLoop"), false);
    const FString SplineType = GetJsonStringFieldSpline(Payload, TEXT("splineType"), TEXT("Curve"));
    TArray<FMcpSplinePointInput> RoutePoints;
    ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::Local;
    bool bHasRoute = false;
    FString RouteError;
    if (!TryParseSplineRoute(Payload, bClosedLoop, RoutePoints, CoordinateSpace, bHasRoute, RouteError))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, RouteError, nullptr, TEXT("INVALID_ROUTE"));
        return true;
    }
    if (!SplineType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase) &&
        !SplineType.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("splineType must be Linear, Curve, Constant or CurveClamped"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    // Spawn actor with spline
    // Use NameMode::Requested to auto-generate unique name if collision occurs
    // This prevents the Fatal Error: "Cannot generate unique name for 'SplineActor'"
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
    if (!NewActor)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to spawn spline actor"), nullptr, TEXT("SPAWN_FAILED"));
        return true;
    }

    NewActor->SetActorLabel(*ActorName);

    // Create spline component
    USplineComponent* SplineComp = NewObject<USplineComponent>(NewActor, TEXT("SplineComponent"));
    if (!SplineComp)
    {
        NewActor->Destroy();
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("Failed to create spline component"), nullptr, TEXT("COMPONENT_FAILED"));
        return true;
    }

    SplineComp->RegisterComponent();
    NewActor->AddInstanceComponent(SplineComp);
    NewActor->SetRootComponent(SplineComp);

    const FScopedTransaction Transaction(FText::FromString(TEXT("Create MCP Spline Template")));
    NewActor->Modify();
    SplineComp->Modify();
    SplineComp->SetClosedLoop(bClosedLoop);
    const ESplinePointType::Type DefaultPointType = ParseSplinePointType(SplineType);
    if (bHasRoute)
    {
        SplineComp->ClearSplinePoints(false);
        for (int32 Index = 0; Index < RoutePoints.Num(); ++Index)
        {
            SplineComp->AddSplinePoint(RoutePoints[Index].Location, CoordinateSpace, false);
            FMcpSplinePointInput Point = RoutePoints[Index];
            if (!Point.bHasPointType) Point.PointType = DefaultPointType;
            ApplySplinePointInput(SplineComp, Index, Point, CoordinateSpace, false);
        }
    }
    else
    {
        // Preserve the historical template defaults only when the caller did
        // not supply a route. A supplied route is always authoritative.
        SplineComp->ClearSplinePoints(false);
        SplineComp->AddSplinePoint(FVector(0, 0, 0), ESplineCoordinateSpace::Local, false);
        SplineComp->AddSplinePoint(FVector(500, 0, 0), ESplineCoordinateSpace::Local, false);
        SplineComp->AddSplinePoint(FVector(1000, 200, 0), ESplineCoordinateSpace::Local, false);
        SplineComp->AddSplinePoint(FVector(1500, 200, 0), ESplineCoordinateSpace::Local, false);
        for (int32 Index = 0; Index < SplineComp->GetNumberOfSplinePoints(); ++Index)
        {
            SplineComp->SetSplinePointType(Index, DefaultPointType, false);
        }
    }
    SplineComp->UpdateSpline();

    int32 GeneratedSegmentCount = 0;
    const FString MeshPath = GetJsonStringFieldSpline(Payload, TEXT("meshPath"));
    if (!MeshPath.IsEmpty())
    {
        UStaticMesh* Mesh = nullptr;
        UMaterialInterface* Material = nullptr;
        ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
        bool bCollisionEnabled = true;
        FString SafeMeshPath;
        FString SafeMaterialPath;
        FString MeshError;
        if (!ResolveSplineMeshSettings(Payload, Mesh, Material, Axis, bCollisionEnabled,
            SafeMeshPath, SafeMaterialPath, MeshError))
        {
            NewActor->Destroy();
            Self->SendAutomationResponse(Socket, RequestId, false, MeshError, nullptr, TEXT("INVALID_MESH"));
            return true;
        }
        TArray<USplineMeshComponent*> Generated;
        if (!GenerateSplineMeshSegments(NewActor, SplineComp, Mesh, Material, Axis,
            bCollisionEnabled, Generated, MeshError))
        {
            NewActor->Destroy();
            Self->SendAutomationResponse(Socket, RequestId, false, MeshError, nullptr, TEXT("GENERATE_FAILED"));
            return true;
        }
        GeneratedSegmentCount = Generated.Num();
    }

    World->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("actorName"), NewActor->GetActorLabel());
    Result->SetStringField(TEXT("templateType"), TemplateName);
    Result->SetNumberField(TEXT("pointCount"), SplineComp->GetNumberOfSplinePoints());
    Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());
    Result->SetNumberField(TEXT("segmentCount"), GeneratedSegmentCount);
    Result->SetBoolField(TEXT("closedLoop"), bClosedLoop);
    Result->SetStringField(TEXT("coordinateSpace"), CoordinateSpace == ESplineCoordinateSpace::World ? TEXT("World") : TEXT("Local"));

    // Add verification data
    McpHandlerUtils::AddVerification(Result, NewActor);

    Self->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("%s spline '%s' created"), *TemplateName, *ActorName), Result);
    return true;
}

static bool HandleCreateRoadSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Road"), TEXT(""));
}

static bool HandleCreateRiverSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("River"), TEXT(""));
}

static bool HandleCreateFenceSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Fence"), TEXT(""));
}

static bool HandleCreateWallSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Wall"), TEXT(""));
}

static bool HandleCreateCableSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Cable"), TEXT(""));
}

static bool HandleCreatePipeSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Pipe"), TEXT(""));
}

static bool HandleCreatePathSpline(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    return HandleCreateTemplateSpline(Self, RequestId, Payload, Socket, TEXT("Path"), TEXT(""));
}

// ============================================================================
// Utility Handlers
// ============================================================================

static bool HandleFindSplineActors(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }
    const FString Filter = GetJsonStringFieldSpline(Payload, TEXT("filter"));
    TArray<TSharedPtr<FJsonValue>> Actors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        TArray<USplineComponent*> Splines;
        Actor->GetComponents<USplineComponent>(Splines);
        if (Splines.Num() == 0 || (!Filter.IsEmpty() && !Actor->GetActorLabel().Contains(Filter))) continue;
        TSharedPtr<FJsonObject> ActorObj = McpHandlerUtils::CreateResultObject();
        ActorObj->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
        ActorObj->SetStringField(TEXT("actorPath"), Actor->GetPathName());
        ActorObj->SetNumberField(TEXT("splineComponentCount"), Splines.Num());
        ActorObj->SetNumberField(TEXT("pointCount"), Splines[0] ? Splines[0]->GetNumberOfSplinePoints() : 0);
        ActorObj->SetBoolField(TEXT("closedLoop"), Splines[0] && Splines[0]->IsClosedLoop());
        TArray<USplineMeshComponent*> Generated;
        GetGeneratedSplineMeshComponents(Actor, Generated);
        ActorObj->SetNumberField(TEXT("segmentCount"), Generated.Num());
        Actors.Add(MakeShared<FJsonValueObject>(ActorObj));
    }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetArrayField(TEXT("actors"), Actors);
    Result->SetNumberField(TEXT("count"), Actors.Num());
    Self->SendAutomationResponse(Socket, RequestId, true, TEXT("Spline actors found"), Result);
    return true;
}

static bool HandleFindSplineComponents(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }
    TArray<AActor*> Actors;
    if (!ActorName.IsEmpty())
    {
        AActor* Actor = FindActorByName(World, ActorName);
        if (!Actor)
        {
            Self->SendAutomationResponse(Socket, RequestId, false, TEXT("Spline actor not found"), nullptr, TEXT("NOT_FOUND"));
            return true;
        }
        Actors.Add(Actor);
    }
    else
    {
        for (TActorIterator<AActor> It(World); It; ++It) Actors.Add(*It);
    }

    TArray<TSharedPtr<FJsonValue>> Components;
    for (AActor* Actor : Actors)
    {
        TArray<USplineComponent*> Splines;
        Actor->GetComponents<USplineComponent>(Splines);
        for (USplineComponent* Spline : Splines)
        {
            if (!Spline) continue;
            TSharedPtr<FJsonObject> ComponentObj = McpHandlerUtils::CreateResultObject();
            ComponentObj->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
            ComponentObj->SetStringField(TEXT("componentName"), Spline->GetName());
            ComponentObj->SetStringField(TEXT("componentPath"), Spline->GetPathName());
            ComponentObj->SetNumberField(TEXT("pointCount"), Spline->GetNumberOfSplinePoints());
            ComponentObj->SetBoolField(TEXT("closedLoop"), Spline->IsClosedLoop());
            Components.Add(MakeShared<FJsonValueObject>(ComponentObj));
        }
    }
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    if (!ActorName.IsEmpty()) Result->SetStringField(TEXT("actorName"), ActorName);
    Result->SetArrayField(TEXT("components"), Components);
    Result->SetNumberField(TEXT("count"), Components.Num());
    Self->SendAutomationResponse(Socket, RequestId, true, TEXT("Spline components found"), Result);
    return true;
}

static bool HandleSetSplinePointRoll(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    const FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));
    const int32 PointIndex = GetJsonIntFieldSpline(Payload, TEXT("pointIndex"), -1);
    const double Roll = GetJsonNumberFieldSpline(Payload, TEXT("roll"), 0.0);
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    AActor* Actor = FindActorByName(World, ActorName);
    USplineComponent* SplineComp = FindSplineComponent(Actor);
    if (!World || !Actor || !SplineComp)
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("Spline actor or component not found"), nullptr, TEXT("NOT_FOUND"));
        return true;
    }
    if (PointIndex < 0 || PointIndex >= SplineComp->GetNumberOfSplinePoints())
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("Invalid spline point index"), nullptr, TEXT("INVALID_INDEX"));
        return true;
    }
    if (!FMath::IsFinite(Roll))
    {
        Self->SendAutomationResponse(Socket, RequestId, false, TEXT("roll must be finite degrees"), nullptr, TEXT("INVALID_PARAM"));
        return true;
    }
    const FScopedTransaction Transaction(FText::FromString(TEXT("Set MCP Spline Point Roll")));
    Actor->Modify();
    SplineComp->Modify();
    FRotator Rotation = SplineComp->GetRotationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local);
    Rotation.Roll = static_cast<float>(Roll);
    SplineComp->SetRotationAtSplinePoint(PointIndex, Rotation, ESplineCoordinateSpace::Local, false);
    SplineComp->UpdateSpline();
    World->MarkPackageDirty();
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetNumberField(TEXT("pointIndex"), PointIndex);
    Result->SetNumberField(TEXT("roll"), Roll);
    Self->SendAutomationResponse(Socket, RequestId, true, TEXT("Spline point roll set"), Result);
    return true;
}

static bool HandleGetSplinesInfo(
    UMcpAutomationBridgeSubsystem* Self,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
    FString ActorName = GetJsonStringFieldSpline(Payload, TEXT("actorName"));

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Self->SendAutomationResponse(Socket, RequestId, false,
            TEXT("No editor world available"), nullptr, TEXT("NO_WORLD"));
        return true;
    }

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();

    if (!ActorName.IsEmpty())
    {
        // Get info for specific actor
        AActor* Actor = FindActorByName(World, ActorName);
        if (!Actor)
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                FString::Printf(TEXT("Actor not found: %s"), *ActorName), nullptr, TEXT("NOT_FOUND"));
            return true;
        }

        USplineComponent* SplineComp = FindSplineComponent(Actor);
        if (!SplineComp)
        {
            Self->SendAutomationResponse(Socket, RequestId, false,
                TEXT("No spline component found on actor"), nullptr, TEXT("NO_SPLINE"));
            return true;
        }

        Result->SetStringField(TEXT("actorName"), ActorName);
        Result->SetNumberField(TEXT("pointCount"), SplineComp->GetNumberOfSplinePoints());
        Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());
        Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());

        ESplineCoordinateSpace::Type ReadSpace = ESplineCoordinateSpace::Local;
        FString CoordinateError;
        if (!TryGetSplineCoordinateSpace(Payload, ReadSpace, CoordinateError))
        {
            Self->SendAutomationResponse(Socket, RequestId, false, CoordinateError, nullptr, TEXT("INVALID_PARAM"));
            return true;
        }
        Result->SetStringField(TEXT("coordinateSpace"), ReadSpace == ESplineCoordinateSpace::World ? TEXT("World") : TEXT("Local"));

        // Add point details
        TArray<TSharedPtr<FJsonValue>> PointsArray;
        for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); i++)
        {
            TSharedPtr<FJsonObject> PointObj = McpHandlerUtils::CreateResultObject();
            FVector LocalLoc = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
            FVector WorldLoc = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
            FVector Loc = SplineComp->GetLocationAtSplinePoint(i, ReadSpace);
            FVector Tangent = SplineComp->GetTangentAtSplinePoint(i, ReadSpace);
            FVector ArriveTangent = SplineComp->GetArriveTangentAtSplinePoint(i, ReadSpace);
            FVector LeaveTangent = SplineComp->GetLeaveTangentAtSplinePoint(i, ReadSpace);
            FRotator Rot = SplineComp->GetRotationAtSplinePoint(i, ReadSpace);
            FVector Scale = SplineComp->GetScaleAtSplinePoint(i);

            PointObj->SetNumberField(TEXT("index"), i);

            TSharedPtr<FJsonObject> LocObj = McpHandlerUtils::CreateResultObject();
            LocObj->SetNumberField(TEXT("x"), Loc.X);
            LocObj->SetNumberField(TEXT("y"), Loc.Y);
            LocObj->SetNumberField(TEXT("z"), Loc.Z);
            PointObj->SetObjectField(TEXT("location"), LocObj);

            TSharedPtr<FJsonObject> LocalObj = McpHandlerUtils::CreateResultObject();
            LocalObj->SetNumberField(TEXT("x"), LocalLoc.X);
            LocalObj->SetNumberField(TEXT("y"), LocalLoc.Y);
            LocalObj->SetNumberField(TEXT("z"), LocalLoc.Z);
            PointObj->SetObjectField(TEXT("localLocation"), LocalObj);

            TSharedPtr<FJsonObject> WorldObj = McpHandlerUtils::CreateResultObject();
            WorldObj->SetNumberField(TEXT("x"), WorldLoc.X);
            WorldObj->SetNumberField(TEXT("y"), WorldLoc.Y);
            WorldObj->SetNumberField(TEXT("z"), WorldLoc.Z);
            PointObj->SetObjectField(TEXT("worldLocation"), WorldObj);

            TSharedPtr<FJsonObject> TangentObj = McpHandlerUtils::CreateResultObject();
            TangentObj->SetNumberField(TEXT("x"), Tangent.X);
            TangentObj->SetNumberField(TEXT("y"), Tangent.Y);
            TangentObj->SetNumberField(TEXT("z"), Tangent.Z);
            PointObj->SetObjectField(TEXT("tangent"), TangentObj);

            TSharedPtr<FJsonObject> ArriveObj = McpHandlerUtils::CreateResultObject();
            ArriveObj->SetNumberField(TEXT("x"), ArriveTangent.X);
            ArriveObj->SetNumberField(TEXT("y"), ArriveTangent.Y);
            ArriveObj->SetNumberField(TEXT("z"), ArriveTangent.Z);
            PointObj->SetObjectField(TEXT("arriveTangent"), ArriveObj);
            TSharedPtr<FJsonObject> LeaveObj = McpHandlerUtils::CreateResultObject();
            LeaveObj->SetNumberField(TEXT("x"), LeaveTangent.X);
            LeaveObj->SetNumberField(TEXT("y"), LeaveTangent.Y);
            LeaveObj->SetNumberField(TEXT("z"), LeaveTangent.Z);
            PointObj->SetObjectField(TEXT("leaveTangent"), LeaveObj);

            TSharedPtr<FJsonObject> RotationObj = McpHandlerUtils::CreateResultObject();
            RotationObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
            RotationObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
            RotationObj->SetNumberField(TEXT("roll"), Rot.Roll);
            PointObj->SetObjectField(TEXT("rotation"), RotationObj);

            TSharedPtr<FJsonObject> ScaleObj = McpHandlerUtils::CreateResultObject();
            ScaleObj->SetNumberField(TEXT("x"), Scale.X);
            ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
            ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
            PointObj->SetObjectField(TEXT("scale"), ScaleObj);
            PointObj->SetNumberField(TEXT("roll"), SplineComp->GetRollAtSplinePoint(i, ReadSpace));

            PointObj->SetStringField(TEXT("type"), SplinePointTypeToString(SplineComp->GetSplinePointType(i)));

            PointsArray.Add(MakeShared<FJsonValueObject>(PointObj));
        }
        Result->SetArrayField(TEXT("points"), PointsArray);

        TArray<USplineMeshComponent*> Generated;
        GetGeneratedSplineMeshComponents(Actor, Generated);
        Result->SetNumberField(TEXT("segmentCount"), Generated.Num());
        TArray<TSharedPtr<FJsonValue>> GeneratedArray;
        for (USplineMeshComponent* Component : Generated)
        {
            if (!Component) continue;
            TSharedPtr<FJsonObject> ComponentObj = McpHandlerUtils::CreateResultObject();
            ComponentObj->SetStringField(TEXT("componentName"), Component->GetName());
            ComponentObj->SetStringField(TEXT("meshPath"), Component->GetStaticMesh() ? Component->GetStaticMesh()->GetPathName() : TEXT(""));
            ComponentObj->SetStringField(TEXT("forwardAxis"), Component->GetForwardAxis() == ESplineMeshAxis::Y ? TEXT("Y") : Component->GetForwardAxis() == ESplineMeshAxis::Z ? TEXT("Z") : TEXT("X"));
            ComponentObj->SetBoolField(TEXT("collisionEnabled"), Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
            GeneratedArray.Add(MakeShared<FJsonValueObject>(ComponentObj));
        }
        Result->SetArrayField(TEXT("generatedComponents"), GeneratedArray);
    }
    else
    {
        // List all actors with spline components
        TArray<TSharedPtr<FJsonValue>> SplinesArray;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            TArray<USplineComponent*> SplineComponents;
            Actor->GetComponents<USplineComponent>(SplineComponents);

            if (SplineComponents.Num() > 0)
            {
                TSharedPtr<FJsonObject> ActorObj = McpHandlerUtils::CreateResultObject();
                ActorObj->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
                ActorObj->SetNumberField(TEXT("splineComponentCount"), SplineComponents.Num());

                if (SplineComponents[0])
                {
                    ActorObj->SetNumberField(TEXT("pointCount"), SplineComponents[0]->GetNumberOfSplinePoints());
                    ActorObj->SetNumberField(TEXT("splineLength"), SplineComponents[0]->GetSplineLength());
                }

                SplinesArray.Add(MakeShared<FJsonValueObject>(ActorObj));
            }
        }
        Result->SetArrayField(TEXT("splines"), SplinesArray);
        Result->SetNumberField(TEXT("totalSplineActors"), SplinesArray.Num());
    }

    Self->SendAutomationResponse(Socket, RequestId, true,
        TEXT("Spline info retrieved"), Result);
    return true;
}

#endif // WITH_EDITOR

// ============================================================================
// Main Dispatcher
// ============================================================================

bool UMcpAutomationBridgeSubsystem::HandleManageSplinesAction(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket)
{
#if WITH_EDITOR
    FString SubAction = GetJsonStringFieldSpline(Payload, TEXT("subAction"), TEXT(""));

    UE_LOG(LogMcpSplineHandlers, Verbose, TEXT("HandleManageSplinesAction: SubAction=%s"), *SubAction);

    // Spline Creation
    if (SubAction == TEXT("create_spline_actor"))
        return HandleCreateSplineActor(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("add_spline_point"))
        return HandleAddSplinePoint(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("insert_spline_point"))
        return HandleAddSplinePoint(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("remove_spline_point"))
        return HandleRemoveSplinePoint(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_point_position"))
        return HandleSetSplinePointPosition(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("update_spline_point"))
        return HandleSetSplinePointPosition(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_point_tangents"))
        return HandleSetSplinePointTangents(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_point_rotation"))
        return HandleSetSplinePointRotation(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_point_scale"))
        return HandleSetSplinePointScale(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_point_roll"))
        return HandleSetSplinePointRoll(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_type"))
        return HandleSetSplineType(this, RequestId, Payload, Socket);

    // Spline Mesh
    if (SubAction == TEXT("create_spline_mesh_component"))
        return HandleCreateSplineMeshComponent(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_spline_mesh_actor"))
        return HandleCreateSplineMeshActor(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_mesh_asset"))
        return HandleSetSplineMeshAsset(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("configure_spline_mesh_axis"))
        return HandleConfigureSplineMeshAxis(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("set_spline_mesh_material"))
        return HandleSetSplineMeshMaterial(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("generate_spline_mesh_segments") ||
        SubAction == TEXT("rebuild_spline_mesh_segments"))
        return HandleGenerateSplineMeshSegments(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("clear_generated_spline_segments"))
        return HandleClearGeneratedSplineSegments(this, RequestId, Payload, Socket);

    // Mesh Scattering
    if (SubAction == TEXT("scatter_meshes_along_spline"))
        return HandleScatterMeshesAlongSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("configure_mesh_spacing"))
        return HandleConfigureMeshSpacing(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("configure_mesh_randomization"))
        return HandleConfigureMeshRandomization(this, RequestId, Payload, Socket);

    // Quick Templates
    if (SubAction == TEXT("create_road_spline"))
        return HandleCreateRoadSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_river_spline"))
        return HandleCreateRiverSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_fence_spline"))
        return HandleCreateFenceSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_wall_spline"))
        return HandleCreateWallSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_cable_spline"))
        return HandleCreateCableSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_pipe_spline"))
        return HandleCreatePipeSpline(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("create_path_spline"))
        return HandleCreatePathSpline(this, RequestId, Payload, Socket);

    // Utility
    if (SubAction == TEXT("find_spline_actors"))
        return HandleFindSplineActors(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("find_spline_components"))
        return HandleFindSplineComponents(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("inspect_spline_points"))
        return HandleGetSplinesInfo(this, RequestId, Payload, Socket);
    if (SubAction == TEXT("get_splines_info"))
        return HandleGetSplinesInfo(this, RequestId, Payload, Socket);

    // Unknown action
    SendAutomationResponse(Socket, RequestId, false,
        FString::Printf(TEXT("Unknown spline subAction: %s"), *SubAction), nullptr, TEXT("UNKNOWN_ACTION"));
    return true;
#else
    SendAutomationResponse(Socket, RequestId, false,
        TEXT("Spline operations require editor build"), nullptr, TEXT("EDITOR_ONLY"));
    return true;
#endif
}
