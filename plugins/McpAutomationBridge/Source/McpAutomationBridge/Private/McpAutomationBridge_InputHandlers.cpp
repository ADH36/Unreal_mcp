// =============================================================================
// McpAutomationBridge_InputHandlers.cpp
// =============================================================================
// MCP Automation Bridge - Enhanced Input System Handlers
//
// UE Version Support: 5.0, 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7
//
// Handler Summary:
// -----------------------------------------------------------------------------
// Action: manage_input (Editor Only)
//   - create_input_action: Create UInputAction asset
//   - create_input_mapping_context: Create UInputMappingContext asset
//   - add_mapping: Add key mapping to context
//   - remove_mapping: Remove key mapping from context
//   - map_input_action: Alias for add_mapping
//   - set_input_trigger: Configure trigger on input action
//   - set_input_modifier: Configure modifier on input action
//   - enable_input_mapping: Enable mapping context at runtime
//   - disable_input_action: Disable input action
//   - get_input_info: Get info about input asset
//
// Dependencies:
//   - Core: McpAutomationBridgeSubsystem, McpAutomationBridgeHelpers
//   - Engine: InputAction, InputMappingContext
//   - Editor: AssetTools, EditorAssetLibrary
//   - UE 5.1+: EnhancedInputEditorSubsystem
//
// Security:
//   - All paths sanitized via SanitizeProjectRelativePath()
//   - Asset names validated for path traversal attempts
// =============================================================================

#include "McpVersionCompatibility.h"  // MUST be first - UE version compatibility macros

// -----------------------------------------------------------------------------
// Core Includes
// -----------------------------------------------------------------------------
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeGlobals.h"
#include "McpHandlerUtils.h"
#include "Modules/ModuleManager.h"  // Required for FModuleManager::IsModuleLoaded() runtime checks

// -----------------------------------------------------------------------------
// Engine Includes
// -----------------------------------------------------------------------------
#include "Dom/JsonObject.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"

// UE 5.1+: EnhancedInputEditorSubsystem
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "EnhancedInputEditorSubsystem.h"
#endif

#include "Factories/Factory.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "ScopedTransaction.h"

#endif

namespace
{
#if WITH_EDITOR
/** Converts an optional input key name into an FKey for mapping-specific operations. */
FKey McpInputKeyFromName(const FString& KeyName)
{
    if (KeyName.IsEmpty())
    {
        return FKey();
    }

    const FName RequestedName(*KeyName);
    TArray<FKey> RegisteredKeys;
    EKeys::GetAllKeys(RegisteredKeys);
    for (const FKey& RegisteredKey : RegisteredKeys)
    {
        if (RegisteredKey.GetFName() == RequestedName ||
            RegisteredKey.ToString().Equals(KeyName, ESearchCase::IgnoreCase))
        {
            return RegisteredKey;
        }
    }
    return FKey();
}

bool ParseInputValueType(const FString& ValueTypeName, EInputActionValueType& OutType)
{
    if (ValueTypeName.IsEmpty() || ValueTypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) ||
        ValueTypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Boolean;
        return true;
    }
    if (ValueTypeName.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || ValueTypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis1D;
        return true;
    }
    if (ValueTypeName.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || ValueTypeName.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis2D;
        return true;
    }
    if (ValueTypeName.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || ValueTypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis3D;
        return true;
    }
    return false;
}

FString InputValueTypeToString(EInputActionValueType Type)
{
    switch (Type)
    {
    case EInputActionValueType::Axis1D: return TEXT("Axis1D");
    case EInputActionValueType::Axis2D: return TEXT("Axis2D");
    case EInputActionValueType::Axis3D: return TEXT("Axis3D");
    default: return TEXT("Boolean");
    }
}

bool IsInputMappingDuplicate(const UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
{
    for (const FEnhancedActionKeyMapping& Existing : Context->GetMappings())
    {
        if (Existing.Action == Action && Existing.Key == Key)
        {
            return true;
        }
    }
    return false;
}

template <typename T>
void SetNumberIfPresent(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, T& Target)
{
    double Number = 0.0;
    if (Payload->TryGetNumberField(FieldName, Number))
    {
        Target = static_cast<T>(Number);
    }
}

bool IsLegacyInputMappingAction(const FString& SubAction)
{
    return SubAction == TEXT("add_legacy_action_mapping") ||
           SubAction == TEXT("remove_legacy_action_mapping") ||
           SubAction == TEXT("add_legacy_axis_mapping") ||
           SubAction == TEXT("remove_legacy_axis_mapping");
}

FString NormalizeInputAssetPathForLoad(const FString& RawPath)
{
    FString CleanPath = RawPath.TrimStartAndEnd();
    int32 QuoteStart = INDEX_NONE;
    int32 QuoteEnd = INDEX_NONE;
    if (CleanPath.FindChar(TEXT('\''), QuoteStart) && CleanPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
    {
        CleanPath = CleanPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
    }

    int32 DotIndex = INDEX_NONE;
    FString PackagePath = CleanPath;
    if (CleanPath.FindChar(TEXT('.'), DotIndex))
    {
        PackagePath = CleanPath.Left(DotIndex);
    }

    FString SanitizedPackagePath = SanitizeProjectRelativePath(PackagePath);
    if (SanitizedPackagePath.IsEmpty())
    {
        return FString();
    }

    return DotIndex == INDEX_NONE ? SanitizedPackagePath : SanitizedPackagePath + CleanPath.Mid(DotIndex);
}

template <typename TAsset>
TAsset* LoadInputAsset(const FString& RawPath, FString& OutNormalizedPath)
{
    OutNormalizedPath = NormalizeInputAssetPathForLoad(RawPath);
    if (OutNormalizedPath.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(OutNormalizedPath))
    {
        if (TAsset* Typed = Cast<TAsset>(Loaded))
        {
            return Typed;
        }
    }

    TArray<FString> Candidates;
    Candidates.Add(OutNormalizedPath);
    if (!OutNormalizedPath.Contains(TEXT(".")))
    {
        const FString AssetName = FPackageName::GetShortName(OutNormalizedPath);
        Candidates.Add(FString::Printf(TEXT("%s.%s"), *OutNormalizedPath, *AssetName));
    }

    for (const FString& Candidate : Candidates)
    {
        if (UObject* Loaded = StaticLoadObject(TAsset::StaticClass(), nullptr, *Candidate))
        {
            if (TAsset* Typed = Cast<TAsset>(Loaded))
            {
                OutNormalizedPath = Candidate;
                return Typed;
            }
        }
    }

    return nullptr;
}

void AddLegacyModifierFields(FInputActionKeyMapping& Mapping, const TSharedPtr<FJsonObject>& Payload)
{
    bool bValue = false;
    if (Payload->TryGetBoolField(TEXT("shift"), bValue)) Mapping.bShift = bValue;
    if (Payload->TryGetBoolField(TEXT("ctrl"), bValue)) Mapping.bCtrl = bValue;
    if (Payload->TryGetBoolField(TEXT("alt"), bValue)) Mapping.bAlt = bValue;
    if (Payload->TryGetBoolField(TEXT("cmd"), bValue)) Mapping.bCmd = bValue;
}

/** Adds verified mapping readback for an action after key-specific edits. */
void AddInputMappingSummary(
    TSharedPtr<FJsonObject> Result,
    const UInputMappingContext* Context,
    const UInputAction* InAction)
{
    TArray<TSharedPtr<FJsonValue>> Mappings;
    for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
    {
        if (Mapping.Action != InAction)
        {
            continue;
        }

        TSharedPtr<FJsonObject> MappingObject = MakeShared<FJsonObject>();
        MappingObject->SetStringField(TEXT("key"), Mapping.Key.ToString());
        MappingObject->SetNumberField(TEXT("modifierCount"), Mapping.Modifiers.Num());
        MappingObject->SetNumberField(TEXT("triggerCount"), Mapping.Triggers.Num());
        Mappings.Add(MakeShared<FJsonValueObject>(MappingObject));
    }

    Result->SetNumberField(TEXT("mappingCount"), Mappings.Num());
    Result->SetArrayField(TEXT("mappings"), Mappings);
}

/** Creates the requested Enhanced Input modifier using compatibility fallbacks across UE 5.x. */
UInputModifier* CreateInputModifierForType(const FString& ModifierType, UObject* Outer)
{
    if (ModifierType == TEXT("DeadZone") || ModifierType == TEXT("InputModifierDeadZone"))
    {
        return NewObject<UInputModifierDeadZone>(Outer);
    }
    if (ModifierType == TEXT("SmoothDelta") || ModifierType == TEXT("InputModifierSmoothDelta"))
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
        return NewObject<UInputModifierSmoothDelta>(Outer);
#else
        return NewObject<UInputModifierSmooth>(Outer);
#endif
    }
    if (ModifierType == TEXT("SwizzleInputAxis") || ModifierType == TEXT("InputModifierSwizzleAxis"))
    {
        return NewObject<UInputModifierSwizzleAxis>(Outer);
    }
    if (ModifierType == TEXT("Negate") || ModifierType == TEXT("InputModifierNegate"))
    {
        return NewObject<UInputModifierNegate>(Outer);
    }
    if (ModifierType == TEXT("Scalar") || ModifierType == TEXT("InputModifierScalar"))
    {
        return NewObject<UInputModifierScalar>(Outer);
    }
    if (ModifierType == TEXT("ScaleByDeltaTime") || ModifierType == TEXT("InputModifierScaleByDeltaTime"))
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
        return NewObject<UInputModifierScaleByDeltaTime>(Outer);
#else
        return NewObject<UInputModifierScalar>(Outer);
#endif
    }
    if (ModifierType == TEXT("ToWorldSpace") || ModifierType == TEXT("InputModifierToWorldSpace"))
    {
        return NewObject<UInputModifierToWorldSpace>(Outer);
    }

    return nullptr;
}
#endif
}

// =============================================================================
// Handler Implementation
// =============================================================================

bool UMcpAutomationBridgeSubsystem::HandleInputAction(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    // Validate action
    if (Action != TEXT("manage_input"))
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

    // Extract subaction
    FString SubAction;
    if (!Payload->TryGetStringField(TEXT("action"), SubAction))
    {
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("Missing 'action' field in payload."), TEXT("INVALID_ARGUMENT"));
        return true;
    }

    UE_LOG(LogMcpAutomationBridgeSubsystem, Log,
        TEXT("HandleInputAction: %s"), *SubAction);

    if (!IsLegacyInputMappingAction(SubAction) && !FModuleManager::Get().IsModuleLoaded(TEXT("EnhancedInput")))
    {
        if (!FModuleManager::Get().ModuleExists(TEXT("EnhancedInput")) ||
            !FModuleManager::Get().LoadModule(TEXT("EnhancedInput")))
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("EnhancedInput plugin is not enabled in this project. Enable the Enhanced Input plugin to use Input features."),
                TEXT("ENHANCEDINPUT_PLUGIN_NOT_ENABLED"));
            return true;
        }
    }

    if (IsLegacyInputMappingAction(SubAction))
    {
        FString MappingName;
        Payload->TryGetStringField(TEXT("name"), MappingName);

        if (SubAction.Contains(TEXT("action")))
        {
            FString ActionName;
            Payload->TryGetStringField(TEXT("actionName"), ActionName);
            if (!ActionName.IsEmpty())
            {
                MappingName = ActionName;
            }
        }
        else
        {
            FString AxisName;
            Payload->TryGetStringField(TEXT("axisName"), AxisName);
            if (!AxisName.IsEmpty())
            {
                MappingName = AxisName;
            }
        }

        FString KeyName;
        Payload->TryGetStringField(TEXT("key"), KeyName);
        FKey Key = McpInputKeyFromName(KeyName);
        if (MappingName.IsEmpty() || !Key.IsValid())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("A non-empty mapping name and valid key are required."),
                TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UInputSettings* InputSettings = UInputSettings::GetInputSettings();
        if (!InputSettings)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Input settings are not available."), TEXT("NOT_AVAILABLE"));
            return true;
        }

        InputSettings->Modify();
        const bool bRemove = SubAction.StartsWith(TEXT("remove_"));
        const bool bAxis = SubAction.Contains(TEXT("axis"));

        if (bAxis)
        {
            double Scale = 1.0;
            Payload->TryGetNumberField(TEXT("scale"), Scale);
            FInputAxisKeyMapping Mapping(FName(*MappingName), Key, static_cast<float>(Scale));
            if (bRemove)
            {
                InputSettings->RemoveAxisMapping(Mapping, true);
            }
            else
            {
                InputSettings->AddAxisMapping(Mapping, true);
            }
        }
        else
        {
            FInputActionKeyMapping Mapping(FName(*MappingName), Key);
            AddLegacyModifierFields(Mapping, Payload);
            if (bRemove)
            {
                InputSettings->RemoveActionMapping(Mapping, true);
            }
            else
            {
                InputSettings->AddActionMapping(Mapping, true);
            }
        }

        InputSettings->SaveKeyMappings();
        const bool bUpdatedDefaultConfig = InputSettings->TryUpdateDefaultConfigFile();
        InputSettings->ForceRebuildKeymaps();

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("name"), MappingName);
        Result->SetStringField(TEXT("key"), KeyName);
        Result->SetStringField(TEXT("mappingType"), bAxis ? TEXT("axis") : TEXT("action"));
        Result->SetBoolField(TEXT("defaultConfigUpdated"), bUpdatedDefaultConfig);
        Result->SetBoolField(bRemove ? TEXT("removed") : TEXT("added"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true,
            bRemove ? TEXT("Legacy input mapping removed.") : TEXT("Legacy input mapping added."), Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // create_input_action: Create UInputAction asset
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("create_input_action"))
    {
        FString Name;
        Payload->TryGetStringField(TEXT("name"), Name);

        FString Path;
        Payload->TryGetStringField(TEXT("path"), Path);

        FString ValueTypeName;
        Payload->TryGetStringField(TEXT("valueType"), ValueTypeName);
        EInputActionValueType ValueType = EInputActionValueType::Boolean;
        if (!ParseInputValueType(ValueTypeName, ValueType))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unsupported valueType '%s'. Supported: Boolean, Axis1D, Axis2D, Axis3D."), *ValueTypeName),
                TEXT("INVALID_VALUE_TYPE"));
            return true;
        }

        if (Name.IsEmpty() || Path.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Name and path are required."), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        // Validate and sanitize path
        FString SanitizedPath = SanitizeProjectRelativePath(Path);
        if (SanitizedPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid path: '%s' contains traversal or invalid characters."), *Path),
                TEXT("INVALID_PATH"));
            return true;
        }

        // Validate asset name
        if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT("..")))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid asset name '%s': contains path separators or traversal sequences"), *Name),
                TEXT("INVALID_NAME"));
            return true;
        }

        const FString FullPath = FString::Printf(TEXT("%s/%s"), *SanitizedPath, *Name);
        if (UEditorAssetLibrary::DoesAssetExist(FullPath))
        {
            UInputAction* ExistingAction = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(FullPath));
            if (!ExistingAction)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Asset already exists at %s but is not an InputAction"), *FullPath),
                    TEXT("ASSET_TYPE_MISMATCH"));
                return true;
            }

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("assetPath"), ExistingAction->GetPathName());
            Result->SetStringField(TEXT("valueType"), InputValueTypeToString(ExistingAction->ValueType));
            McpHandlerUtils::AddVerification(Result, ExistingAction);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Input Action already exists."), Result);
            return true;
        }

        IAssetTools& AssetTools = FModuleManager::Get()
            .LoadModuleChecked<FAssetToolsModule>("AssetTools")
            .Get();

        UClass* ActionClass = UInputAction::StaticClass();
        UObject* NewAsset = AssetTools.CreateAsset(Name, SanitizedPath, ActionClass, nullptr);

        if (NewAsset)
        {
            if (UInputAction* NewAction = Cast<UInputAction>(NewAsset))
            {
                const FScopedTransaction Transaction(FText::FromString(TEXT("Create Enhanced Input Action")));
                NewAction->Modify();
                NewAction->ValueType = ValueType;
                NewAction->PostEditChange();
            }
            SaveLoadedAssetThrottled(NewAsset, -1.0, true);

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
            Result->SetStringField(TEXT("valueType"), InputValueTypeToString(ValueType));
            McpHandlerUtils::AddVerification(Result, NewAsset);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Input Action created."), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to create Input Action."), TEXT("CREATION_FAILED"));
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // set_input_action_type: Change the value type of an existing action.
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("set_input_action_type"))
    {
        FString ActionPath;
        FString ValueTypeName;
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
        Payload->TryGetStringField(TEXT("valueType"), ValueTypeName);
        EInputActionValueType ValueType = EInputActionValueType::Boolean;
        if (!ParseInputValueType(ValueTypeName, ValueType))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unsupported valueType '%s'. Supported: Boolean, Axis1D, Axis2D, Axis3D."), *ValueTypeName),
                TEXT("INVALID_VALUE_TYPE"));
            return true;
        }

        FString SanitizedActionPath;
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);
        if (!InAction)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Input action not found: %s"), *SanitizedActionPath), TEXT("NOT_FOUND"));
            return true;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Set Enhanced Input Action Value Type")));
        InAction->Modify();
        InAction->ValueType = ValueType;
        InAction->PostEditChange();
        SaveLoadedAssetThrottled(InAction, -1.0, true);

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        Result->SetStringField(TEXT("valueType"), InputValueTypeToString(InAction->ValueType));
        McpHandlerUtils::AddVerification(Result, InAction);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Input Action value type updated."), Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // create_input_mapping_context: Create UInputMappingContext asset
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("create_input_mapping_context"))
    {
        FString Name;
        Payload->TryGetStringField(TEXT("name"), Name);

        FString Path;
        Payload->TryGetStringField(TEXT("path"), Path);

        if (Name.IsEmpty() || Path.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Name and path are required."), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        // Validate and sanitize path
        FString SanitizedPath = SanitizeProjectRelativePath(Path);
        if (SanitizedPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid path: '%s' contains traversal or invalid characters."), *Path),
                TEXT("INVALID_PATH"));
            return true;
        }

        // Validate asset name
        if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT("..")))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid asset name '%s': contains path separators or traversal sequences"), *Name),
                TEXT("INVALID_NAME"));
            return true;
        }

        const FString FullPath = FString::Printf(TEXT("%s/%s"), *SanitizedPath, *Name);
        if (UEditorAssetLibrary::DoesAssetExist(FullPath))
        {
            UInputMappingContext* ExistingContext = Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(FullPath));
            if (!ExistingContext)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Asset already exists at %s but is not an InputMappingContext"), *FullPath),
                    TEXT("ASSET_TYPE_MISMATCH"));
                return true;
            }

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("assetPath"), ExistingContext->GetPathName());
            McpHandlerUtils::AddVerification(Result, ExistingContext);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Input Mapping Context already exists."), Result);
            return true;
        }

        IAssetTools& AssetTools = FModuleManager::Get()
            .LoadModuleChecked<FAssetToolsModule>("AssetTools")
            .Get();

        UClass* ContextClass = UInputMappingContext::StaticClass();
        UObject* NewAsset = AssetTools.CreateAsset(Name, SanitizedPath, ContextClass, nullptr);

        if (NewAsset)
        {
            SaveLoadedAssetThrottled(NewAsset, -1.0, true);

            TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
            Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
            McpHandlerUtils::AddVerification(Result, NewAsset);

            SendAutomationResponse(RequestingSocket, RequestId, true,
                TEXT("Input Mapping Context created."), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to create Input Mapping Context."), TEXT("CREATION_FAILED"));
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // add_mapping / map_input_action: Add key mapping to context
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("add_mapping") || SubAction == TEXT("map_input_action") || SubAction == TEXT("add_input_mapping"))
    {
        FString ContextPath;
        Payload->TryGetStringField(TEXT("contextPath"), ContextPath);

        FString ActionPath;
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);

        FString KeyName;
        Payload->TryGetStringField(TEXT("key"), KeyName);

        FString SanitizedContextPath;
        FString SanitizedActionPath;
        UInputMappingContext* Context = LoadInputAsset<UInputMappingContext>(ContextPath, SanitizedContextPath);
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);

        if (!Context || !InAction || KeyName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Context or action not found, or key is empty. Context: %s, Action: %s"),
                    *SanitizedContextPath, *SanitizedActionPath),
                TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FKey Key = McpInputKeyFromName(KeyName);
        if (!Key.IsValid())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid key name '%s'. Use a registered UE key such as W, SpaceBar, LeftMouseButton, or Gamepad_FaceButton_Bottom."), *KeyName), TEXT("INVALID_KEY"));
            return true;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Add Enhanced Input Mapping")));
        Context->Modify();
        const bool bAlreadyMapped = IsInputMappingDuplicate(Context, InAction, Key);
        if (!bAlreadyMapped)
        {
            Context->MapKey(InAction, Key);
        }
        SaveLoadedAssetThrottled(Context, -1.0, true);

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("contextPath"), SanitizedContextPath);
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        Result->SetStringField(TEXT("key"), KeyName);
        Result->SetBoolField(TEXT("alreadyMapped"), bAlreadyMapped);
        Result->SetNumberField(TEXT("mappingCount"), Context->GetMappings().Num());
        AddAssetVerificationNested(Result, TEXT("contextVerification"), Context);
        AddAssetVerificationNested(Result, TEXT("actionVerification"), InAction);

        SendAutomationResponse(RequestingSocket, RequestId, true,
            (SubAction == TEXT("map_input_action") || SubAction == TEXT("add_input_mapping")) ?
            TEXT("Input action mapped to key.") : TEXT("Mapping added."), Result);
        return true;
    }

    if (SubAction == TEXT("remove_input_mapping"))
    {
        SubAction = TEXT("remove_mapping");
    }

    // -------------------------------------------------------------------------
    // remove_mapping: Remove key mapping from context
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("remove_mapping"))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Enhanced Input Mapping")));
        FString ContextPath;
        Payload->TryGetStringField(TEXT("contextPath"), ContextPath);

        FString ActionPath;
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);

        FString KeyName;
        Payload->TryGetStringField(TEXT("key"), KeyName);

        FString SanitizedContextPath;
        FString SanitizedActionPath;
        UInputMappingContext* Context = LoadInputAsset<UInputMappingContext>(ContextPath, SanitizedContextPath);
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);

        if (!Context || !InAction)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Context or action not found. Context: %s, Action: %s"),
                    *SanitizedContextPath, *SanitizedActionPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        FKey RequestedKey = McpInputKeyFromName(KeyName);
        const bool bHasSpecificKey = !KeyName.IsEmpty();
        if (bHasSpecificKey && !RequestedKey.IsValid())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid key name: %s"), *KeyName), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        // Collect matching keys before mutating the mapping array.
        TArray<FKey> KeysToRemove;
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action == InAction && (!bHasSpecificKey || Mapping.Key == RequestedKey))
            {
                KeysToRemove.Add(Mapping.Key);
            }
        }

        if (bHasSpecificKey && KeysToRemove.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Mapping not found for action '%s' and key '%s'."),
                    *SanitizedActionPath, *KeyName),
                TEXT("NOT_FOUND"));
            return true;
        }

        Context->Modify();
        for (const FKey& KeyToRemove : KeysToRemove)
        {
            Context->UnmapKey(InAction, KeyToRemove);
        }

        SaveLoadedAssetThrottled(Context, -1.0, true);

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("contextPath"), SanitizedContextPath);
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        if (bHasSpecificKey)
        {
            Result->SetStringField(TEXT("key"), KeyName);
        }
        Result->SetNumberField(TEXT("keysRemoved"), KeysToRemove.Num());

        TArray<TSharedPtr<FJsonValue>> RemovedKeys;
        for (const FKey& Key : KeysToRemove)
        {
            RemovedKeys.Add(MakeShared<FJsonValueString>(Key.ToString()));
        }
        Result->SetArrayField(TEXT("removedKeys"), RemovedKeys);
        AddInputMappingSummary(Result, Context, InAction);

        AddAssetVerificationNested(Result, TEXT("contextVerification"), Context);
        AddAssetVerificationNested(Result, TEXT("actionVerification"), InAction);

        const FString SuccessMessage = bHasSpecificKey
            ? FString::Printf(TEXT("Mapping removed for action key: %s"), *KeyName)
            : TEXT("Mappings removed for action.");
        SendAutomationResponse(RequestingSocket, RequestId, true, SuccessMessage, Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // set_input_trigger: Configure trigger on input action
    // -------------------------------------------------------------------------
    const bool bRequestedMappingTrigger = SubAction == TEXT("add_mapping_trigger");
    if (bRequestedMappingTrigger)
    {
        SubAction = TEXT("set_input_trigger");
    }
    if (SubAction == TEXT("set_input_trigger"))
    {
        FString ActionPath;
        FString ContextPath;
        FString KeyName;
        Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
        Payload->TryGetStringField(TEXT("key"), KeyName);

        FString TriggerType;
        Payload->TryGetStringField(TEXT("triggerType"), TriggerType);

        FString SanitizedActionPath;
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);

        if (!InAction)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Action not found: %s"), *SanitizedActionPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        FString SanitizedContextPath;
        UInputMappingContext* TargetContext = nullptr;
        FEnhancedActionKeyMapping* TargetMapping = nullptr;
        if (bRequestedMappingTrigger)
        {
            const FKey RequestedKey = McpInputKeyFromName(KeyName);
            TargetContext = LoadInputAsset<UInputMappingContext>(ContextPath, SanitizedContextPath);
            if (!TargetContext || !RequestedKey.IsValid())
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("add_mapping_trigger requires a valid contextPath and registered key; context=%s key=%s"), *SanitizedContextPath, *KeyName),
                    TEXT("INVALID_ARGUMENT"));
                return true;
            }
            for (int32 MappingIndex = 0; MappingIndex < TargetContext->GetMappings().Num(); ++MappingIndex)
            {
                FEnhancedActionKeyMapping& Mapping = TargetContext->GetMapping(MappingIndex);
                if (Mapping.Action == InAction && Mapping.Key == RequestedKey)
                {
                    TargetMapping = &Mapping;
                    break;
                }
            }
            if (!TargetMapping)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Mapping not found for action '%s' and key '%s'."), *SanitizedActionPath, *KeyName), TEXT("NOT_FOUND"));
                return true;
            }
        }
        UObject* TriggerOuter = TargetMapping ? static_cast<UObject*>(TargetContext) : static_cast<UObject*>(InAction);

        // Create the appropriate trigger based on type
        UInputTrigger* NewTrigger = nullptr;

        // Map common trigger type names to their classes
        if (TriggerType == TEXT("Pressed") || TriggerType == TEXT("InputTriggerPressed"))
        {
            NewTrigger = NewObject<UInputTriggerPressed>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Released") || TriggerType == TEXT("InputTriggerReleased"))
        {
            NewTrigger = NewObject<UInputTriggerReleased>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Down") || TriggerType == TEXT("InputTriggerDown"))
        {
            NewTrigger = NewObject<UInputTriggerDown>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Tap") || TriggerType == TEXT("InputTriggerTap"))
        {
            NewTrigger = NewObject<UInputTriggerTap>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Hold") || TriggerType == TEXT("InputTriggerHold"))
        {
            NewTrigger = NewObject<UInputTriggerHold>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Chorded") || TriggerType == TEXT("ChordedAction") || TriggerType == TEXT("InputTriggerChordAction"))
        {
            FString ChordActionPath;
            Payload->TryGetStringField(TEXT("chordActionPath"), ChordActionPath);
            FString SanitizedChordActionPath;
            UInputAction* ChordAction = LoadInputAsset<UInputAction>(ChordActionPath, SanitizedChordActionPath);
            if (!ChordAction)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Chorded trigger requires a valid chordActionPath; action not found: %s"), *SanitizedChordActionPath), TEXT("NOT_FOUND"));
                return true;
            }
            UInputTriggerChordAction* ChordTrigger = NewObject<UInputTriggerChordAction>(TriggerOuter);
            ChordTrigger->ChordAction = ChordAction;
            NewTrigger = ChordTrigger;
        }
        else if (TriggerType == TEXT("HoldAndRelease") || TriggerType == TEXT("InputTriggerHoldAndRelease"))
        {
            NewTrigger = NewObject<UInputTriggerHoldAndRelease>(TriggerOuter);
        }
        else if (TriggerType == TEXT("Pulse") || TriggerType == TEXT("InputTriggerPulse"))
        {
            NewTrigger = NewObject<UInputTriggerPulse>(TriggerOuter);
        }
        else if (TriggerType == TEXT("RepeatedTap") || TriggerType == TEXT("InputTriggerRepeatedTap") || TriggerType == TEXT("DoubleTap"))
        {
            // UInputTriggerRepeatedTap was added in UE 5.6
            // For earlier versions, use UInputTriggerTap (single tap) or UInputTriggerHold as fallback
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
            UInputTriggerRepeatedTap* RepeatedTapTrigger = NewObject<UInputTriggerRepeatedTap>(TriggerOuter);
            NewTrigger = RepeatedTapTrigger;
#else
            // Fallback for UE 5.0-5.5: Use UInputTriggerTap as closest equivalent
            NewTrigger = NewObject<UInputTriggerTap>(TriggerOuter);
#endif
        }

        if (!NewTrigger)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unknown trigger type: %s. Supported: Pressed, Released, Down, Tap, Hold, HoldAndRelease, Pulse, RepeatedTap, DoubleTap"), *TriggerType),
                TEXT("INVALID_TRIGGER_TYPE"));
            return true;
        }

        const FScopedTransaction Transaction(FText::FromString(TEXT("Configure Enhanced Input Trigger")));
        if (TargetMapping)
        {
            TargetContext->Modify();
        }
        else
        {
            InAction->Modify();
        }
        bool bAlreadyConfigured = false;
        TArray<TObjectPtr<UInputTrigger>>& TargetTriggers = TargetMapping ? TargetMapping->Triggers : InAction->Triggers;
        for (const UInputTrigger* ExistingTrigger : TargetTriggers)
        {
            if (ExistingTrigger && ExistingTrigger->GetClass() == NewTrigger->GetClass())
            {
                bAlreadyConfigured = true;
                break;
            }
        }
        if (!bAlreadyConfigured)
        {
            TargetTriggers.Add(NewTrigger);
        }
        if (UInputTriggerHold* HoldTrigger = Cast<UInputTriggerHold>(NewTrigger))
        {
            SetNumberIfPresent(Payload, TEXT("holdTime"), HoldTrigger->HoldTimeThreshold);
        }
        if (UInputTriggerTap* TapTrigger = Cast<UInputTriggerTap>(NewTrigger))
        {
            SetNumberIfPresent(Payload, TEXT("tapTime"), TapTrigger->TapReleaseTimeThreshold);
        }

        SaveLoadedAssetThrottled(TargetMapping ? static_cast<UObject*>(TargetContext) : static_cast<UObject*>(InAction), -1.0, true);

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        Result->SetStringField(TEXT("triggerType"), TriggerType);
        Result->SetBoolField(TEXT("triggerSet"), true);
        Result->SetBoolField(TEXT("alreadyConfigured"), bAlreadyConfigured);
        Result->SetNumberField(TEXT("triggerCount"), TargetTriggers.Num());
        if (TargetMapping)
        {
            Result->SetStringField(TEXT("contextPath"), SanitizedContextPath);
            Result->SetStringField(TEXT("key"), KeyName);
        }
        McpHandlerUtils::AddVerification(Result, InAction);

        SendAutomationResponse(RequestingSocket, RequestId, true,
            FString::Printf(TEXT("Trigger '%s' configured on action."), *TriggerType), Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // set_input_modifier: Configure modifier on input action
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("add_mapping_modifier"))
    {
        SubAction = TEXT("set_input_modifier");
    }
    if (SubAction == TEXT("set_input_modifier"))
    {
        FString ContextPath;
        Payload->TryGetStringField(TEXT("contextPath"), ContextPath);

        FString ActionPath;
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);

        FString KeyName;
        Payload->TryGetStringField(TEXT("key"), KeyName);

        FString ModifierType;
        Payload->TryGetStringField(TEXT("modifierType"), ModifierType);

        const bool bTargetMapping = !ContextPath.IsEmpty() || !KeyName.IsEmpty();
        if (bTargetMapping && (ContextPath.IsEmpty() || KeyName.IsEmpty()))
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("contextPath and key are both required when setting a modifier on a specific mapping."),
                TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString SanitizedActionPath;
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);

        if (!InAction)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Action not found: %s"), *SanitizedActionPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        UObject* ModifierOuter = InAction;
        UInputMappingContext* Context = nullptr;
        FString SanitizedContextPath;
        FEnhancedActionKeyMapping* TargetMapping = nullptr;
        FKey RequestedKey = McpInputKeyFromName(KeyName);

        if (bTargetMapping)
        {
            if (!RequestedKey.IsValid())
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Invalid key name: %s"), *KeyName), TEXT("INVALID_ARGUMENT"));
                return true;
            }

            Context = LoadInputAsset<UInputMappingContext>(ContextPath, SanitizedContextPath);
            if (!Context)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Context not found: %s"), *SanitizedContextPath),
                    TEXT("NOT_FOUND"));
                return true;
            }

            const int32 MappingCount = Context->GetMappings().Num();
            for (int32 MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
            {
                FEnhancedActionKeyMapping& Mapping = Context->GetMapping(MappingIndex);
                if (Mapping.Action == InAction && Mapping.Key == RequestedKey)
                {
                    TargetMapping = &Mapping;
                    break;
                }
            }

            if (!TargetMapping)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Mapping not found for action '%s' and key '%s'."),
                        *SanitizedActionPath, *KeyName),
                    TEXT("NOT_FOUND"));
                return true;
            }

            ModifierOuter = Context;
        }

        UInputModifier* NewModifier = CreateInputModifierForType(ModifierType, ModifierOuter);
        if (!NewModifier)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unknown modifier type: %s. Supported: DeadZone, SmoothDelta, SwizzleInputAxis, Negate, Scalar, ScaleByDeltaTime, ToWorldSpace"), *ModifierType),
                TEXT("INVALID_MODIFIER_TYPE"));
            return true;
        }

        if (UInputModifierScalar* ScalarModifier = Cast<UInputModifierScalar>(NewModifier))
        {
            SetNumberIfPresent(Payload, TEXT("scalarX"), ScalarModifier->Scalar.X);
            SetNumberIfPresent(Payload, TEXT("scalarY"), ScalarModifier->Scalar.Y);
            SetNumberIfPresent(Payload, TEXT("scalarZ"), ScalarModifier->Scalar.Z);
        }
        if (UInputModifierNegate* NegateModifier = Cast<UInputModifierNegate>(NewModifier))
        {
            bool bValue = false;
            if (Payload->TryGetBoolField(TEXT("negateX"), bValue)) NegateModifier->bX = bValue;
            if (Payload->TryGetBoolField(TEXT("negateY"), bValue)) NegateModifier->bY = bValue;
            if (Payload->TryGetBoolField(TEXT("negateZ"), bValue)) NegateModifier->bZ = bValue;
        }
        if (UInputModifierDeadZone* DeadZoneModifier = Cast<UInputModifierDeadZone>(NewModifier))
        {
            SetNumberIfPresent(Payload, TEXT("lowerThreshold"), DeadZoneModifier->LowerThreshold);
            SetNumberIfPresent(Payload, TEXT("upperThreshold"), DeadZoneModifier->UpperThreshold);
            if (DeadZoneModifier->LowerThreshold < 0.0f || DeadZoneModifier->UpperThreshold > 1.0f ||
                DeadZoneModifier->LowerThreshold >= DeadZoneModifier->UpperThreshold)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    TEXT("Dead Zone thresholds must satisfy 0 <= lowerThreshold < upperThreshold <= 1."), TEXT("INVALID_MODIFIER_SETTINGS"));
                return true;
            }
        }
        if (UInputModifierSwizzleAxis* SwizzleModifier = Cast<UInputModifierSwizzleAxis>(NewModifier))
        {
            FString Order;
            Payload->TryGetStringField(TEXT("swizzleOrder"), Order);
            if (!Order.IsEmpty())
            {
                if (Order.Equals(TEXT("YXZ"), ESearchCase::IgnoreCase)) SwizzleModifier->Order = EInputAxisSwizzle::YXZ;
                else if (Order.Equals(TEXT("ZYX"), ESearchCase::IgnoreCase)) SwizzleModifier->Order = EInputAxisSwizzle::ZYX;
                else if (Order.Equals(TEXT("XZY"), ESearchCase::IgnoreCase)) SwizzleModifier->Order = EInputAxisSwizzle::XZY;
                else if (Order.Equals(TEXT("YZX"), ESearchCase::IgnoreCase)) SwizzleModifier->Order = EInputAxisSwizzle::YZX;
                else if (Order.Equals(TEXT("ZXY"), ESearchCase::IgnoreCase)) SwizzleModifier->Order = EInputAxisSwizzle::ZXY;
                else
                {
                    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Unsupported swizzleOrder. Supported: YXZ, ZYX, XZY, YZX, ZXY."), TEXT("INVALID_MODIFIER_SETTINGS"));
                    return true;
                }
            }
        }

        if (TargetMapping)
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Configure Enhanced Input Mapping Modifier")));
            Context->Modify();
            TargetMapping->Modifiers.Add(NewModifier);
            SaveLoadedAssetThrottled(Context, -1.0, true);
        }
        else
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Configure Enhanced Input Action Modifier")));
            InAction->Modify();
            InAction->Modifiers.Add(NewModifier);
            SaveLoadedAssetThrottled(InAction, -1.0, true);
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        Result->SetStringField(TEXT("modifierType"), ModifierType);
        Result->SetBoolField(TEXT("modifierSet"), true);
        Result->SetStringField(TEXT("target"), TargetMapping ? TEXT("mapping") : TEXT("action"));
        if (TargetMapping)
        {
            Result->SetStringField(TEXT("contextPath"), SanitizedContextPath);
            Result->SetStringField(TEXT("key"), KeyName);
            Result->SetNumberField(TEXT("mappingModifierCount"), TargetMapping->Modifiers.Num());
            AddInputMappingSummary(Result, Context, InAction);
            AddAssetVerificationNested(Result, TEXT("contextVerification"), Context);
            AddAssetVerificationNested(Result, TEXT("actionVerification"), InAction);
        }
        else
        {
            McpHandlerUtils::AddVerification(Result, InAction);
        }

        SendAutomationResponse(RequestingSocket, RequestId, true,
            FString::Printf(TEXT("Modifier '%s' configured on action."), *ModifierType), Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // enable_input_mapping: Enable mapping context at runtime
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("enable_input_mapping"))
    {
        FString ContextPath;
        Payload->TryGetStringField(TEXT("contextPath"), ContextPath);

        int32 Priority = 0;
        Payload->TryGetNumberField(TEXT("priority"), Priority);

        FString SanitizedContextPath;
        UInputMappingContext* Context = LoadInputAsset<UInputMappingContext>(ContextPath, SanitizedContextPath);

        if (!Context)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Context not found: %s"), *SanitizedContextPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("contextPath"), SanitizedContextPath);
        Result->SetNumberField(TEXT("priority"), Priority);
        Result->SetBoolField(TEXT("enabled"), true);
        Result->SetBoolField(TEXT("runtimeApplied"), false);
        McpHandlerUtils::AddVerification(Result, Context);

        if (GEditor && GEditor->PlayWorld)
        {
            UWorld* PlayWorld = GEditor->PlayWorld.Get();
            APlayerController* PlayerController = PlayWorld ? PlayWorld->GetFirstPlayerController() : nullptr;
            ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
            if (!LocalPlayer && PlayWorld && PlayWorld->GetGameInstance())
            {
                LocalPlayer = PlayWorld->GetGameInstance()->GetFirstGamePlayer();
            }

            if (!LocalPlayer)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    TEXT("No local player is available in the active PIE world."),
                    TEXT("LOCAL_PLAYER_NOT_FOUND"));
                return true;
            }

            UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
            if (!InputSubsystem)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    TEXT("Enhanced Input local player subsystem is not available."),
                    TEXT("ENHANCED_INPUT_SUBSYSTEM_NOT_FOUND"));
                return true;
            }

            InputSubsystem->AddMappingContext(Context, Priority);
            Result->SetBoolField(TEXT("runtimeApplied"), true);
        }

        SendAutomationResponse(RequestingSocket, RequestId, true,
            Result->GetBoolField(TEXT("runtimeApplied")) ?
                TEXT("Input mapping context enabled in PIE.") :
                TEXT("Input mapping context verified; start PIE to apply it at runtime."),
            Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // disable_input_action: Disable input action
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("disable_input_action"))
    {
        FString ActionPath;
        Payload->TryGetStringField(TEXT("actionPath"), ActionPath);

        FString SanitizedActionPath;
        UInputAction* InAction = LoadInputAsset<UInputAction>(ActionPath, SanitizedActionPath);

        if (!InAction)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Action not found: %s"), *SanitizedActionPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("actionPath"), SanitizedActionPath);
        Result->SetBoolField(TEXT("disabled"), true);
        McpHandlerUtils::AddVerification(Result, InAction);

        SendAutomationResponse(RequestingSocket, RequestId, true,
            TEXT("Input action disabled."), Result);
        return true;
    }

    // -------------------------------------------------------------------------
    // get_input_info: Get info about input asset
    // -------------------------------------------------------------------------
    if (SubAction == TEXT("inspect_input_asset"))
    {
        SubAction = TEXT("get_input_info");
    }
    if (SubAction == TEXT("get_input_info"))
    {
        FString AssetPath;
        Payload->TryGetStringField(TEXT("assetPath"), AssetPath);

        if (AssetPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("assetPath is required."), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString SanitizedAssetPath = NormalizeInputAssetPathForLoad(AssetPath);
        UObject* Asset = SanitizedAssetPath.IsEmpty() ? nullptr : UEditorAssetLibrary::LoadAsset(SanitizedAssetPath);
        if (!Asset && !SanitizedAssetPath.IsEmpty())
        {
            Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *SanitizedAssetPath);
        }
        if (!Asset)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Asset not found: %s"), *SanitizedAssetPath),
                TEXT("NOT_FOUND"));
            return true;
        }

        TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
        Result->SetStringField(TEXT("assetPath"), SanitizedAssetPath);
        Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
        Result->SetStringField(TEXT("assetName"), Asset->GetName());

        // Add type-specific info
        if (UInputAction* InputAction = Cast<UInputAction>(Asset))
        {
            Result->SetStringField(TEXT("type"), TEXT("InputAction"));
            Result->SetStringField(TEXT("valueType"), InputValueTypeToString(InputAction->ValueType));
            Result->SetBoolField(TEXT("consumeInput"), InputAction->bConsumeInput);
            TArray<TSharedPtr<FJsonValue>> Modifiers;
            for (const UInputModifier* Modifier : InputAction->Modifiers)
            {
                if (Modifier)
                {
                    TSharedPtr<FJsonObject> ModifierObject = McpHandlerUtils::CreateResultObject();
                    ModifierObject->SetStringField(TEXT("class"), Modifier->GetClass()->GetName());
                    Modifiers.Add(MakeShared<FJsonValueObject>(ModifierObject));
                }
            }
            Result->SetArrayField(TEXT("modifiers"), Modifiers);
            TArray<TSharedPtr<FJsonValue>> Triggers;
            for (const UInputTrigger* Trigger : InputAction->Triggers)
            {
                if (Trigger)
                {
                    TSharedPtr<FJsonObject> TriggerObject = McpHandlerUtils::CreateResultObject();
                    TriggerObject->SetStringField(TEXT("class"), Trigger->GetClass()->GetName());
                    if (const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
                    {
                        TriggerObject->SetStringField(TEXT("chordActionPath"), Chord->ChordAction ? Chord->ChordAction->GetPathName() : FString());
                    }
                    Triggers.Add(MakeShared<FJsonValueObject>(TriggerObject));
                }
            }
            Result->SetArrayField(TEXT("triggers"), Triggers);
        }
        else if (UInputMappingContext* Context = Cast<UInputMappingContext>(Asset))
        {
            Result->SetStringField(TEXT("type"), TEXT("InputMappingContext"));
            Result->SetNumberField(TEXT("mappingCount"), Context->GetMappings().Num());
            TArray<TSharedPtr<FJsonValue>> Mappings;
            for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
            {
                TSharedPtr<FJsonObject> MappingObject = McpHandlerUtils::CreateResultObject();
                MappingObject->SetStringField(TEXT("actionPath"), Mapping.Action ? Mapping.Action->GetPathName() : FString());
                MappingObject->SetStringField(TEXT("key"), Mapping.Key.ToString());
                TArray<TSharedPtr<FJsonValue>> Modifiers;
                for (const UInputModifier* Modifier : Mapping.Modifiers)
                {
                    if (Modifier)
                    {
                        TSharedPtr<FJsonObject> ModifierObject = McpHandlerUtils::CreateResultObject();
                        ModifierObject->SetStringField(TEXT("class"), Modifier->GetClass()->GetName());
                        Modifiers.Add(MakeShared<FJsonValueObject>(ModifierObject));
                    }
                }
                MappingObject->SetArrayField(TEXT("modifiers"), Modifiers);
                TArray<TSharedPtr<FJsonValue>> Triggers;
                for (const UInputTrigger* Trigger : Mapping.Triggers)
                {
                    if (Trigger)
                    {
                        TSharedPtr<FJsonObject> TriggerObject = McpHandlerUtils::CreateResultObject();
                        TriggerObject->SetStringField(TEXT("class"), Trigger->GetClass()->GetName());
                        Triggers.Add(MakeShared<FJsonValueObject>(TriggerObject));
                    }
                }
                MappingObject->SetArrayField(TEXT("triggers"), Triggers);
                Mappings.Add(MakeShared<FJsonValueObject>(MappingObject));
            }
            Result->SetArrayField(TEXT("mappings"), Mappings);
        }

        McpHandlerUtils::AddVerification(Result, Asset);

        SendAutomationResponse(RequestingSocket, RequestId, true,
            TEXT("Input asset info retrieved."), Result);
        return true;
    }

    // Unknown subaction
    SendAutomationError(RequestingSocket, RequestId,
        FString::Printf(TEXT("Unknown sub-action: %s"), *SubAction),
        TEXT("UNKNOWN_ACTION"));
    return true;

#else
    // Non-editor build
    SendAutomationError(RequestingSocket, RequestId,
        TEXT("Input management requires Editor build."), TEXT("NOT_AVAILABLE"));
    return true;
#endif
}
