// =============================================================================
// McpAutomationBridge_UiHandlers.cpp
// =============================================================================
// Handler implementations for UI/Widget and Editor control operations.
//
// HANDLERS IMPLEMENTED:
// ---------------------
// system_control / manage_ui:
//   - create_widget: Create UMG widget blueprint
//   - add_widget_child: Add child widget to widget tree
//   - screenshot: Capture viewport screenshot with base64 encoding
//   - play_in_editor: Start PIE session
//   - stop_play: Stop PIE session
//   - save_all: Save all assets
//   - simulate_input: Simulate keyboard input events
//   - create_hud: Create and add widget to viewport
//   - set_widget_text: Set text on TextBlock widgets
//   - set_widget_image: Set image on Image widgets
//   - set_widget_visibility: Toggle widget visibility
//   - remove_widget_from_viewport: Remove widgets from viewport
//
// VERSION COMPATIBILITY:
// ----------------------
// UE 5.0: FImageUtils::CompressImageArray (no ThumbnailCompressImageArray)
// UE 5.1+: FImageUtils::ThumbnailCompressImageArray available
// WidgetBlueprintFactory: Header location varies by UE version
//
// SECURITY:
// ---------
// - Screenshot paths validated and sanitized
// - No arbitrary code execution via widget operations
// =============================================================================

// =============================================================================
// Version Compatibility Header (MUST BE FIRST)
// =============================================================================
#include "McpVersionCompatibility.h"

// =============================================================================
// Core Headers
// =============================================================================
#include "Dom/JsonObject.h"
#include "McpAutomationBridgeGlobals.h"
#include "McpHandlerUtils.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"

// =============================================================================
// Editor-Only Headers
// =============================================================================
#if WITH_EDITOR

// Asset Management
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"

// Widget Support
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "WidgetBlueprint.h"

// Engine & Rendering
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "UnrealClient.h"

// Widget Factory (version-dependent header location)
#if __has_include("Factories/WidgetBlueprintFactory.h")
#include "Factories/WidgetBlueprintFactory.h"
#define MCP_HAS_WIDGET_FACTORY 1
#else
#define MCP_HAS_WIDGET_FACTORY 0
#endif

#endif // WITH_EDITOR

// =============================================================================
// Handler Implementation
// =============================================================================

#if WITH_EDITOR
namespace {
constexpr int32 MaxScreenshotPngBytesForBase64ForMcp = 3 * 1024 * 1024;
constexpr int32 MinScreenshotResolutionForMcp = 256;
constexpr int32 MaxScreenshotResolutionForMcp = 7680;

bool HasVisibleScreenshotPixelsForMcp(const TArray<FColor>& Bitmap, int32& OutNonBlackPixels)
{
  OutNonBlackPixels = 0;
  for (const FColor& Pixel : Bitmap)
  {
    if (Pixel.R > 8 || Pixel.G > 8 || Pixel.B > 8) ++OutNonBlackPixels;
  }
  return OutNonBlackPixels > 0;
}

// Box-filter resample of an RGBA bitmap; footprint < 1px degenerates to nearest-neighbor on upscale.
void ResizeScreenshotBitmapForMcp(const TArray<FColor>& Source, int32 SourceWidth,
                                  int32 SourceHeight, int32 TargetWidth, int32 TargetHeight,
                                  TArray<FColor>& OutBitmap)
{
  OutBitmap.Reset();
  if (SourceWidth <= 0 || SourceHeight <= 0 || TargetWidth <= 0 || TargetHeight <= 0 ||
      Source.Num() < SourceWidth * SourceHeight)
  {
    return;
  }

  OutBitmap.SetNumUninitialized(TargetWidth * TargetHeight);
  const double ScaleX = static_cast<double>(SourceWidth) / static_cast<double>(TargetWidth);
  const double ScaleY = static_cast<double>(SourceHeight) / static_cast<double>(TargetHeight);

  for (int32 TargetY = 0; TargetY < TargetHeight; ++TargetY)
  {
    const double SpanBeginY = TargetY * ScaleY;
    const double SpanEndY = SpanBeginY + ScaleY;
    const int32 FirstSourceY = FMath::Clamp(FMath::FloorToInt32(SpanBeginY), 0, SourceHeight - 1);
    const int32 LastSourceY = FMath::Clamp(FMath::CeilToInt32(SpanEndY) - 1, 0, SourceHeight - 1);

    for (int32 TargetX = 0; TargetX < TargetWidth; ++TargetX)
    {
      const double SpanBeginX = TargetX * ScaleX;
      const double SpanEndX = SpanBeginX + ScaleX;
      const int32 FirstSourceX = FMath::Clamp(FMath::FloorToInt32(SpanBeginX), 0, SourceWidth - 1);
      const int32 LastSourceX = FMath::Clamp(FMath::CeilToInt32(SpanEndX) - 1, 0, SourceWidth - 1);

      double SumR = 0.0, SumG = 0.0, SumB = 0.0, SumWeight = 0.0;
      for (int32 SourceY = FirstSourceY; SourceY <= LastSourceY; ++SourceY)
      {
        const double WeightY = FMath::Min(SpanEndY, SourceY + 1.0) -
                               FMath::Max(SpanBeginY, static_cast<double>(SourceY));
        if (WeightY <= 0.0) continue;
        const FColor* Row = Source.GetData() + SourceY * SourceWidth;
        for (int32 SourceX = FirstSourceX; SourceX <= LastSourceX; ++SourceX)
        {
          const double WeightX = FMath::Min(SpanEndX, SourceX + 1.0) -
                                 FMath::Max(SpanBeginX, static_cast<double>(SourceX));
          if (WeightX <= 0.0) continue;
          const double Weight = WeightX * WeightY;
          SumR += Row[SourceX].R * Weight;
          SumG += Row[SourceX].G * Weight;
          SumB += Row[SourceX].B * Weight;
          SumWeight += Weight;
        }
      }

      FColor& OutPixel = OutBitmap[TargetY * TargetWidth + TargetX];
      if (SumWeight > 0.0)
      {
        OutPixel.R = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt32(SumR / SumWeight + 0.5), 0, 255));
        OutPixel.G = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt32(SumG / SumWeight + 0.5), 0, 255));
        OutPixel.B = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt32(SumB / SumWeight + 0.5), 0, 255));
      }
      else
      {
        OutPixel.R = 0;
        OutPixel.G = 0;
        OutPixel.B = 0;
      }
      OutPixel.A = 255;
    }
  }
}

// Returns true when any resolution request field is present. 0 means unspecified or unparsable.
bool ParseUiScreenshotResolutionForMcp(const TSharedPtr<FJsonObject>& Payload,
                                       int32& OutWidth, int32& OutHeight)
{
  OutWidth = 0;
  OutHeight = 0;
  if (!Payload.IsValid())
  {
    return false;
  }

  FString ResolutionText;
  if (Payload->TryGetStringField(TEXT("resolution"), ResolutionText))
  {
    ResolutionText.ReplaceInline(TEXT("X"), TEXT("x"));
    ResolutionText.ReplaceInline(TEXT("*"), TEXT("x"));
    TArray<FString> Parts;
    ResolutionText.ParseIntoArray(Parts, TEXT("x"), true);
    if (Parts.Num() == 2)
    {
      OutWidth = FCString::Atoi(*Parts[0]);
      OutHeight = FCString::Atoi(*Parts[1]);
    }
    else if (Parts.Num() == 1)
    {
      OutWidth = FCString::Atoi(*Parts[0]);
      OutHeight = OutWidth;
    }
  }
  else
  {
    double ResolutionValue = 0.0;
    if (Payload->TryGetNumberField(TEXT("resolution"), ResolutionValue))
    {
      OutWidth = FMath::FloorToInt32(FMath::Clamp(ResolutionValue, 0.0, static_cast<double>(MAX_int32)) + 0.5);
      OutHeight = OutWidth;
    }
  }

  double WidthValue = 0.0;
  if (Payload->TryGetNumberField(TEXT("width"), WidthValue))
  {
    OutWidth = FMath::FloorToInt32(FMath::Clamp(WidthValue, 0.0, static_cast<double>(MAX_int32)) + 0.5);
  }
  double HeightValue = 0.0;
  if (Payload->TryGetNumberField(TEXT("height"), HeightValue))
  {
    OutHeight = FMath::FloorToInt32(FMath::Clamp(HeightValue, 0.0, static_cast<double>(MAX_int32)) + 0.5);
  }

  return Payload->HasField(TEXT("resolution")) || Payload->HasField(TEXT("width")) ||
         Payload->HasField(TEXT("height"));
}

FString MakeSafeUiScreenshotFilenameForMcp(
    const TSharedPtr<FJsonObject> &Payload) {
  FString Filename;
  if (Payload.IsValid()) {
    Payload->TryGetStringField(TEXT("filename"), Filename);
  }

  if (Filename.IsEmpty()) {
    Filename = FString::Printf(TEXT("Screenshot_%lld"),
                               FDateTime::Now().ToUnixTimestamp());
  }

  Filename = FPaths::GetCleanFilename(Filename);
  if (Filename.Contains(TEXT("..")) || Filename.Contains(TEXT("/")) ||
      Filename.Contains(TEXT("\\"))) {
    Filename = FString::Printf(TEXT("Screenshot_%lld"),
                               FDateTime::Now().ToUnixTimestamp());
  }

  if (!Filename.EndsWith(TEXT(".png"))) {
    Filename += TEXT(".png");
  }

  return Filename;
}

void AddScreenshotMetadataForUiMcp(const TSharedPtr<FJsonObject> &Resp,
                                   const TSharedPtr<FJsonObject> &Payload) {
  if (!Resp.IsValid() || !Payload.IsValid()) {
    return;
  }

  bool bIncludeMetadata = false;
  if (!Payload->TryGetBoolField(TEXT("includeMetadata"), bIncludeMetadata) ||
      !bIncludeMetadata) {
    return;
  }

  const TSharedPtr<FJsonObject> *Metadata = nullptr;
  if (Payload->TryGetObjectField(TEXT("metadata"), Metadata) && Metadata &&
      Metadata->IsValid()) {
    Resp->SetObjectField(TEXT("metadata"), *Metadata);
  }
}

FString MakeScreenshotTooLargeMessageForUiMcp(int32 SizeBytes) {
  return FString::Printf(
      TEXT("Screenshot PNG is too large to return as base64 (%d bytes, max %d bytes). Retry with returnBase64=false or a smaller viewport/window."),
      SizeBytes, MaxScreenshotPngBytesForBase64ForMcp);
}
}
#endif

/**
 * @brief Handles UI widget operations and system control actions.
 *
 * Processes both "system_control" and "manage_ui" actions with various subActions
 * for widget creation, manipulation, screenshots, and PIE control.
 *
 * @param RequestId Identifier for the incoming request.
 * @param Action Action name ("system_control" or "manage_ui").
 * @param Payload JSON object containing "subAction" and action-specific parameters.
 * @param RequestingSocket WebSocket for response delivery.
 * @return true if the action was handled, false otherwise.
 */
bool UMcpAutomationBridgeSubsystem::HandleUiAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString LowerAction = Action.ToLower();
  bool bIsSystemControl =
      LowerAction.Equals(TEXT("system_control"), ESearchCase::IgnoreCase);
  bool bIsManageUi =
      LowerAction.Equals(TEXT("manage_ui"), ESearchCase::IgnoreCase);

  if (!bIsSystemControl && !bIsManageUi) {
    return false;
  }

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId, TEXT("Payload missing."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  // -------------------------------------------------------------------------
  // Extract SubAction
  // -------------------------------------------------------------------------
  FString SubAction;
  if (Payload->HasField(TEXT("subAction"))) {
    SubAction = GetJsonStringField(Payload, TEXT("subAction"));
  } else {
    Payload->TryGetStringField(TEXT("action"), SubAction);
  }
  const FString LowerSub = SubAction.ToLower();

  TSharedPtr<FJsonObject> Resp = McpHandlerUtils::CreateResultObject();
  Resp->SetStringField(TEXT("action"), LowerSub);

  bool bSuccess = false;
  FString Message;
  FString ErrorCode;

#if WITH_EDITOR
  // ===========================================================================
  // SubAction: create_widget
  // ===========================================================================
  if (LowerSub == TEXT("create_widget")) {
#if WITH_EDITOR && MCP_HAS_WIDGET_FACTORY
    FString WidgetName;
    if (!Payload->TryGetStringField(TEXT("name"), WidgetName) ||
        WidgetName.IsEmpty()) {
      Message = TEXT("name field required for create_widget");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      FString SavePath;
      Payload->TryGetStringField(TEXT("savePath"), SavePath);
      if (SavePath.IsEmpty()) {
        SavePath = TEXT("/Game/UI/Widgets");
      }

      FString WidgetType;
      Payload->TryGetStringField(TEXT("widgetType"), WidgetType);

      const FString NormalizedPath = SavePath.TrimStartAndEnd();
      const FString TargetPath =
          FString::Printf(TEXT("%s/%s"), *NormalizedPath, *WidgetName);
      if (UEditorAssetLibrary::DoesAssetExist(TargetPath)) {
        bSuccess = true;
        Message = FString::Printf(TEXT("Widget blueprint already exists at %s"),
                                  *TargetPath);
        Resp->SetStringField(TEXT("widgetPath"), TargetPath);
        Resp->SetBoolField(TEXT("exists"), true);
        if (!WidgetType.IsEmpty()) {
          Resp->SetStringField(TEXT("widgetType"), WidgetType);
        }
        Resp->SetStringField(TEXT("widgetName"), WidgetName);
      } else {
        UWidgetBlueprintFactory *Factory = NewObject<UWidgetBlueprintFactory>();
        if (!Factory) {
          Message = TEXT("Failed to create widget blueprint factory");
          ErrorCode = TEXT("FACTORY_CREATION_FAILED");
          Resp->SetStringField(TEXT("error"), Message);
        } else {
          UObject *NewAsset = Factory->FactoryCreateNew(
              UWidgetBlueprint::StaticClass(),
              UEditorAssetLibrary::DoesAssetExist(NormalizedPath)
                  ? UEditorAssetLibrary::LoadAsset(NormalizedPath)
                  : nullptr,
              FName(*WidgetName), RF_Standalone, nullptr, GWarn);

          UWidgetBlueprint *WidgetBlueprint = Cast<UWidgetBlueprint>(NewAsset);

          if (!WidgetBlueprint) {
            Message = TEXT("Failed to create widget blueprint asset");
            ErrorCode = TEXT("ASSET_CREATION_FAILED");
            Resp->SetStringField(TEXT("error"), Message);
          } else {
            // Force immediate save and registry scan
            SaveLoadedAssetThrottled(WidgetBlueprint, -1.0, true);
            ScanPathSynchronous(WidgetBlueprint->GetOutermost()->GetName());

            bSuccess = true;
            Message = FString::Printf(TEXT("Widget blueprint created at %s"),
                                      *WidgetBlueprint->GetPathName());
            Resp->SetStringField(TEXT("widgetPath"),
                                 WidgetBlueprint->GetPathName());
            Resp->SetStringField(TEXT("widgetName"), WidgetName);
            if (!WidgetType.IsEmpty()) {
              Resp->SetStringField(TEXT("widgetType"), WidgetType);
            }
          }
        }
      }
    }
#else
    Message =
        TEXT("create_widget requires editor build with widget factory support");
    ErrorCode = TEXT("NOT_AVAILABLE");
    Resp->SetStringField(TEXT("error"), Message);
#endif
  }
  // ===========================================================================
  // SubAction: add_widget_child
  // ===========================================================================
  else if (LowerSub == TEXT("add_widget_child")) {
#if WITH_EDITOR && MCP_HAS_WIDGET_FACTORY
    FString WidgetPath;
    if (!Payload->TryGetStringField(TEXT("widgetPath"), WidgetPath) ||
        WidgetPath.IsEmpty()) {
      Message = TEXT("widgetPath required for add_widget_child");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      UWidgetBlueprint *WidgetBP =
          LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
      if (!WidgetBP) {
        Message = FString::Printf(TEXT("Could not find Widget Blueprint at %s"),
                                  *WidgetPath);
        ErrorCode = TEXT("ASSET_NOT_FOUND");
        Resp->SetStringField(TEXT("error"), Message);
      } else {
        FString ChildClassPath;
        if (!Payload->TryGetStringField(TEXT("childClass"), ChildClassPath) ||
            ChildClassPath.IsEmpty()) {
          Message = TEXT("childClass required (e.g. /Script/UMG.Button)");
          ErrorCode = TEXT("INVALID_ARGUMENT");
          Resp->SetStringField(TEXT("error"), Message);
        } else {
          UClass *WidgetClass =
              UEditorAssetLibrary::FindAssetData(ChildClassPath)
                      .GetAsset()
                      .IsValid()
                  ? LoadClass<UObject>(nullptr, *ChildClassPath)
                  : FindObject<UClass>(nullptr, *ChildClassPath);

          // Try partial search for common UMG widgets
          if (!WidgetClass) {
            if (ChildClassPath.Contains(TEXT(".")))
              WidgetClass = FindObject<UClass>(nullptr, *ChildClassPath);
            else
              WidgetClass = FindObject<UClass>(
                  nullptr,
                  *FString::Printf(TEXT("/Script/UMG.%s"), *ChildClassPath));
          }

          if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass())) {
            Message = FString::Printf(
                TEXT("Could not resolve valid UWidget class from '%s'"),
                *ChildClassPath);
            ErrorCode = TEXT("CLASS_NOT_FOUND");
            Resp->SetStringField(TEXT("error"), Message);
          } else {
            FString ParentName;
            Payload->TryGetStringField(TEXT("parentName"), ParentName);

            WidgetBP->Modify();

            UWidget *NewWidget =
                WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass);

            bool bAdded = false;
            bool bIsRoot = false;

            if (ParentName.IsEmpty()) {
              // Try to set as RootWidget if empty
              if (WidgetBP->WidgetTree->RootWidget == nullptr) {
                WidgetBP->WidgetTree->RootWidget = NewWidget;
                bAdded = true;
                bIsRoot = true;
              } else {
                // Try to add to existing root if it's a panel
                UPanelWidget *RootPanel =
                    Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
                if (RootPanel) {
                  RootPanel->AddChild(NewWidget);
                  bAdded = true;
                } else {
                  Message = TEXT("Root widget is not a panel and already "
                                 "exists. Specify parentName.");
                  ErrorCode = TEXT("ROOT_Full");
                }
              }
            } else {
              // Find parent
              UWidget *ParentWidget =
                  WidgetBP->WidgetTree->FindWidget(FName(*ParentName));
              UPanelWidget *ParentPanel = Cast<UPanelWidget>(ParentWidget);
              if (ParentPanel) {
                ParentPanel->AddChild(NewWidget);
                bAdded = true;
              } else {
                Message = FString::Printf(
                    TEXT("Parent '%s' not found or is not a PanelWidget"),
                    *ParentName);
                ErrorCode = TEXT("PARENT_NOT_FOUND");
              }
            }

            if (bAdded) {
              bSuccess = true;
              Message = FString::Printf(TEXT("Added %s to %s"),
                                        *WidgetClass->GetName(),
                                        *WidgetBP->GetName());
              Resp->SetStringField(TEXT("widgetName"), NewWidget->GetName());
              Resp->SetStringField(TEXT("childClass"), WidgetClass->GetName());
            } else {
              if (Message.IsEmpty())
                Message = TEXT("Failed to add widget child.");
              Resp->SetStringField(TEXT("error"), Message);
            }
          }
        }
      }
    }
#else
    Message = TEXT("add_widget_child requires editor build");
    ErrorCode = TEXT("NOT_AVAILABLE");
    Resp->SetStringField(TEXT("error"), Message);
#endif
  }
  // ===========================================================================
  // SubAction: screenshot
  // ===========================================================================
  else if (LowerSub == TEXT("screenshot")) {
    FString Mode;
    Payload->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.TrimStartAndEnd().ToLower();
    if (Mode.IsEmpty() && bIsSystemControl) {
      return HandleControlEditorScreenshot(RequestId, Payload, RequestingSocket);
    }
    if (Mode == TEXT("full_editor_window") || Mode == TEXT("editor_viewport")) {
      return HandleControlEditorScreenshot(RequestId, Payload, RequestingSocket);
    }
    if (!Mode.IsEmpty() && Mode != TEXT("game_viewport")) {
      Message = TEXT("Invalid screenshot mode. Supported modes: editor_viewport, game_viewport, full_editor_window");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
      SendAutomationResponse(RequestingSocket, RequestId, false, Message, Resp, ErrorCode);
      return true;
    }
    Resp->SetStringField(TEXT("captureSource"), TEXT("game_viewport"));

    // Take a screenshot of the viewport and return as base64
    FString RawScreenshotPath;
    Payload->TryGetStringField(TEXT("path"), RawScreenshotPath);

    FString ScreenshotPath;
    if (RawScreenshotPath.IsEmpty()) {
      ScreenshotPath =
          FPaths::ProjectSavedDir() / TEXT("Screenshots/WindowsEditor");
    } else {
      FString SafePath = SanitizeProjectFilePath(RawScreenshotPath);
      if (SafePath.IsEmpty()) {
        Message = FString::Printf(TEXT("Invalid or unsafe screenshot path: %s. Path must be relative to project."), *RawScreenshotPath);
        ErrorCode = TEXT("SECURITY_VIOLATION");
        Resp->SetStringField(TEXT("error"), Message);
        SendAutomationResponse(RequestingSocket, RequestId, false, Message, Resp, ErrorCode);
        return true;
      }

      ScreenshotPath = FPaths::ProjectDir() / SafePath;
      ScreenshotPath = FPaths::ConvertRelativePathToFull(ScreenshotPath);
      FPaths::NormalizeFilename(ScreenshotPath);

      FString NormalizedProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
      FPaths::NormalizeDirectoryName(NormalizedProjectDir);
      if (!NormalizedProjectDir.EndsWith(TEXT("/"))) {
        NormalizedProjectDir += TEXT("/");
      }

      if (!ScreenshotPath.StartsWith(NormalizedProjectDir, ESearchCase::IgnoreCase)) {
        Message = FString::Printf(TEXT("Invalid or unsafe screenshot path: %s. Path escapes project directory."), *RawScreenshotPath);
        ErrorCode = TEXT("SECURITY_VIOLATION");
        Resp->SetStringField(TEXT("error"), Message);
        SendAutomationResponse(RequestingSocket, RequestId, false, Message, Resp, ErrorCode);
        return true;
      }
    }

    const FString Filename = MakeSafeUiScreenshotFilenameForMcp(Payload);

    bool bReturnBase64 = true;
    Payload->TryGetBoolField(TEXT("returnBase64"), bReturnBase64);

    int32 RequestedWidth = 0;
    int32 RequestedHeight = 0;
    const bool bResolutionRequested =
        ParseUiScreenshotResolutionForMcp(Payload, RequestedWidth, RequestedHeight);

    // Get viewport. During PIE, prefer the viewport owned by the PIE world rather
    // than GEngine->GameViewport, which can point at the editor viewport surface
    // and capture editor overlays or a stale editor camera instead of the active
    // game camera.
    UGameViewportClient *ViewportClient = nullptr;
    bool bUsingPieViewport = false;
    if (GEditor && GEditor->PlayWorld) {
      if (UWorld *PlayWorld = GEditor->PlayWorld.Get()) {
        ViewportClient = PlayWorld->GetGameViewport();
        bUsingPieViewport = ViewportClient != nullptr;
      }
    }
    if (!ViewportClient && GEngine) {
      ViewportClient = GEngine->GameViewport;
    }

    if (!ViewportClient) {
      Message = TEXT("No game viewport available");
      ErrorCode = TEXT("NO_VIEWPORT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      FViewport *Viewport = ViewportClient->Viewport;

      if (!Viewport) {
        Message = TEXT("No viewport available");
        ErrorCode = TEXT("NO_VIEWPORT");
        Resp->SetStringField(TEXT("error"), Message);
      } else {
        bool bForcedViewportDraw = false;
        bool bHasPlayerCamera = false;
        FString ActiveViewTargetPath;
        FVector ActiveCameraLocation = FVector::ZeroVector;
        FRotator ActiveCameraRotation = FRotator::ZeroRotator;
        float ActiveCameraFov = 0.0f;

        if (UWorld *ViewportWorld = ViewportClient->GetWorld()) {
          if (APlayerController *PlayerController = ViewportWorld->GetFirstPlayerController()) {
            if (AActor *ViewTarget = PlayerController->GetViewTarget()) {
              ActiveViewTargetPath = ViewTarget->GetPathName();
            }
            if (APlayerCameraManager *CameraManager = PlayerController->PlayerCameraManager) {
              CameraManager->UpdateCamera(0.0f);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
              const FMinimalViewInfo &CameraView = CameraManager->GetCameraCacheView();
#else
              const FMinimalViewInfo CameraView = CameraManager->GetCameraCachePOV();
#endif
              ActiveCameraLocation = CameraView.Location;
              ActiveCameraRotation = CameraView.Rotation;
              ActiveCameraFov = CameraView.FOV;
              bHasPlayerCamera = true;
            }
          }
        }

        int32 WarmupFrames = 3;
        double WarmupFramesValue = 0.0;
        if (Payload->TryGetNumberField(TEXT("warmupFrames"), WarmupFramesValue))
          WarmupFrames = FMath::Clamp(FMath::FloorToInt(WarmupFramesValue), 0, 120);
        int32 ScreenshotDelayMs = 100;
        double ScreenshotDelayValue = 0.0;
        if (Payload->TryGetNumberField(TEXT("screenshotDelayMs"), ScreenshotDelayValue))
          ScreenshotDelayMs = FMath::Clamp(FMath::FloorToInt(ScreenshotDelayValue), 0, 5000);

        for (int32 WarmupIndex = 0; WarmupIndex < WarmupFrames; ++WarmupIndex)
        {
          Viewport->Draw(false);
          FlushRenderingCommands();
          bForcedViewportDraw = true;
        }
        if (ScreenshotDelayMs > 0) FPlatformProcess::Sleep(static_cast<float>(ScreenshotDelayMs) / 1000.0f);

        // Capture after rendering is complete. Retry black frames because the
        // first PIE frame can be a cleared backbuffer before the player camera.
        TArray<FColor> Bitmap;
        FIntVector Size(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y, 0);
        int32 NonBlackPixels = 0;
        int32 CaptureAttempts = 0;
        bool bReadSuccess = false;
        for (CaptureAttempts = 1; CaptureAttempts <= 3; ++CaptureAttempts)
        {
          if (CaptureAttempts > 1)
          {
            Viewport->Draw(false);
            FlushRenderingCommands();
            if (ScreenshotDelayMs > 0) FPlatformProcess::Sleep(static_cast<float>(ScreenshotDelayMs) / 1000.0f);
          }
          Bitmap.Reset();
          bReadSuccess = Viewport->ReadPixels(Bitmap);
          if (bReadSuccess && Bitmap.Num() > 0 && HasVisibleScreenshotPixelsForMcp(Bitmap, NonBlackPixels)) break;
        }

        if (!bReadSuccess || Bitmap.Num() == 0) {
          Message = TEXT("Failed to read viewport pixels");
          ErrorCode = TEXT("CAPTURE_FAILED");
          Resp->SetStringField(TEXT("error"), Message);
        } else if (NonBlackPixels == 0) {
          Message = TEXT("Screenshot capture produced an all-black frame after retries");
          ErrorCode = TEXT("BLACK_FRAME");
          Resp->SetStringField(TEXT("error"), Message);
        } else {
          // Ensure we have the right size
          const int32 Width = Size.X;
          const int32 Height = Size.Y;

          TArray<FColor> ResizedBitmap;
          bool bResized = false;
          int32 EncodeWidth = Width;
          int32 EncodeHeight = Height;
          TArray<TSharedPtr<FJsonValue>> Warnings;
          if (bResolutionRequested) {
            if (RequestedWidth > 0 && RequestedHeight <= 0) {
              RequestedHeight = FMath::Clamp(
                  FMath::FloorToInt32(static_cast<double>(RequestedWidth) * Height / Width + 0.5),
                  1, MaxScreenshotResolutionForMcp);
            } else if (RequestedHeight > 0 && RequestedWidth <= 0) {
              RequestedWidth = FMath::Clamp(
                  FMath::FloorToInt32(static_cast<double>(RequestedHeight) * Width / Height + 0.5),
                  1, MaxScreenshotResolutionForMcp);
            }

            if (RequestedWidth <= 0 || RequestedHeight <= 0) {
              Warnings.Add(MakeShared<FJsonValueString>(
                  TEXT("Could not parse requested resolution; request ignored.")));
            } else if (RequestedWidth < MinScreenshotResolutionForMcp ||
                       RequestedWidth > MaxScreenshotResolutionForMcp ||
                       RequestedHeight < MinScreenshotResolutionForMcp ||
                       RequestedHeight > MaxScreenshotResolutionForMcp) {
              Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                  TEXT("Requested resolution %dx%d outside supported range %d-%d; request ignored."),
                  RequestedWidth, RequestedHeight, MinScreenshotResolutionForMcp,
                  MaxScreenshotResolutionForMcp)));
            } else if (Bitmap.Num() != Width * Height) {
              Warnings.Add(MakeShared<FJsonValueString>(
                  TEXT("Captured bitmap size does not match viewport dimensions; resize skipped.")));
            } else if (RequestedWidth != Width || RequestedHeight != Height) {
              ResizeScreenshotBitmapForMcp(Bitmap, Width, Height, RequestedWidth,
                                           RequestedHeight, ResizedBitmap);
              if (ResizedBitmap.Num() == RequestedWidth * RequestedHeight) {
                bResized = true;
                EncodeWidth = RequestedWidth;
                EncodeHeight = RequestedHeight;
              } else {
                Warnings.Add(MakeShared<FJsonValueString>(
                    TEXT("Screenshot resize failed; using captured dimensions.")));
              }
            }
          }
          const TArray<FColor>& SourceBitmap = bResized ? ResizedBitmap : Bitmap;

          TArray<uint8> PngData;
          IImageWrapperModule &ImageWrapperModule =
              FModuleManager::LoadModuleChecked<IImageWrapperModule>(
                  FName("ImageWrapper"));
          TSharedPtr<IImageWrapper> ImageWrapper =
              ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

          if (ImageWrapper.IsValid()) {
            TArray<uint8> RawData;
            RawData.SetNumUninitialized(EncodeWidth * EncodeHeight * 4);
            for (int32 i = 0; i < SourceBitmap.Num(); ++i) {
              RawData[i * 4 + 0] = SourceBitmap[i].R;
              RawData[i * 4 + 1] = SourceBitmap[i].G;
              RawData[i * 4 + 2] = SourceBitmap[i].B;
              RawData[i * 4 + 3] = 255;
            }

            if (ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), EncodeWidth,
                                     EncodeHeight, ERGBFormat::RGBA, 8)) {
              PngData = ImageWrapper->GetCompressed(100);
            }
          }

          if (PngData.Num() == 0) {
            Message = TEXT("Failed to encode viewport screenshot as PNG");
            ErrorCode = TEXT("CAPTURE_FAILED");
            Resp->SetStringField(TEXT("error"), Message);
            SendAutomationResponse(RequestingSocket, RequestId, false, Message, Resp, ErrorCode);
            return true;
          }

          FString FullPath =
              FPaths::Combine(ScreenshotPath, Filename);
          FPaths::MakeStandardFilename(FullPath);

          // Always save to disk
          IFileManager::Get().MakeDirectory(*ScreenshotPath, true);
          bool bSaved = FFileHelper::SaveArrayToFile(PngData, *FullPath);

          bSuccess = true;
          Message = FString::Printf(TEXT("Screenshot captured (%dx%d)"), Width,
                                    Height);
          if (bResized) {
            Message += FString::Printf(TEXT(", resized to %dx%d"), EncodeWidth,
                                       EncodeHeight);
          }
          Resp->SetStringField(TEXT("screenshotPath"), FullPath);
          Resp->SetStringField(TEXT("filename"), Filename);
          Resp->SetStringField(TEXT("mode"), TEXT("game_viewport"));
          Resp->SetBoolField(TEXT("usingPieViewport"), bUsingPieViewport);
          Resp->SetBoolField(TEXT("forcedViewportDraw"), bForcedViewportDraw);
          Resp->SetNumberField(TEXT("warmupFrames"), WarmupFrames);
          Resp->SetNumberField(TEXT("screenshotDelayMs"), ScreenshotDelayMs);
          Resp->SetNumberField(TEXT("captureAttempts"), CaptureAttempts);
          Resp->SetNumberField(TEXT("nonBlackPixelCount"), NonBlackPixels);
          if (UWorld *ViewportWorld = ViewportClient->GetWorld()) {
            Resp->SetStringField(TEXT("viewportWorld"), ViewportWorld->GetName());
            Resp->SetNumberField(TEXT("viewportWorldType"), static_cast<int32>(ViewportWorld->WorldType));
          }
          if (!ActiveViewTargetPath.IsEmpty()) {
            Resp->SetStringField(TEXT("activeViewTarget"), ActiveViewTargetPath);
          }
          if (bHasPlayerCamera) {
            TSharedPtr<FJsonObject> CameraLocationObj = McpHandlerUtils::CreateResultObject();
            CameraLocationObj->SetNumberField(TEXT("x"), ActiveCameraLocation.X);
            CameraLocationObj->SetNumberField(TEXT("y"), ActiveCameraLocation.Y);
            CameraLocationObj->SetNumberField(TEXT("z"), ActiveCameraLocation.Z);
            Resp->SetObjectField(TEXT("activeCameraLocation"), CameraLocationObj);

            TSharedPtr<FJsonObject> CameraRotationObj = McpHandlerUtils::CreateResultObject();
            CameraRotationObj->SetNumberField(TEXT("pitch"), ActiveCameraRotation.Pitch);
            CameraRotationObj->SetNumberField(TEXT("yaw"), ActiveCameraRotation.Yaw);
            CameraRotationObj->SetNumberField(TEXT("roll"), ActiveCameraRotation.Roll);
            Resp->SetObjectField(TEXT("activeCameraRotation"), CameraRotationObj);
            Resp->SetNumberField(TEXT("activeCameraFov"), ActiveCameraFov);
          }
          Resp->SetBoolField(TEXT("saved"), bSaved);
          Resp->SetNumberField(TEXT("width"), Width);
          Resp->SetNumberField(TEXT("height"), Height);
          Resp->SetBoolField(TEXT("resized"), bResized);
          if (bResized) {
            Resp->SetNumberField(TEXT("resizedWidth"), EncodeWidth);
            Resp->SetNumberField(TEXT("resizedHeight"), EncodeHeight);
          }
          if (bResolutionRequested) {
            Resp->SetNumberField(TEXT("requestedWidth"), RequestedWidth);
            Resp->SetNumberField(TEXT("requestedHeight"), RequestedHeight);
            const bool bResolutionVerified =
                bResized
                    ? (EncodeWidth == RequestedWidth && EncodeHeight == RequestedHeight)
                    : (Width == RequestedWidth && Height == RequestedHeight);
            Resp->SetBoolField(TEXT("resolutionVerified"), bResolutionVerified);
            if (!bResolutionVerified) {
              FString VerificationNote;
              if (Warnings.Num() > 0) {
                VerificationNote = Warnings[0]->AsString();
              } else {
                VerificationNote = FString::Printf(
                    TEXT("Output dimensions %dx%d do not match requested %dx%d."),
                    EncodeWidth, EncodeHeight, RequestedWidth, RequestedHeight);
              }
              Resp->SetStringField(TEXT("verificationNote"), VerificationNote);
            }
          }
          if (Warnings.Num() > 0) {
            Resp->SetArrayField(TEXT("warnings"), Warnings);
          }
          Resp->SetNumberField(TEXT("sizeBytes"), PngData.Num());
          Resp->SetStringField(TEXT("mimeType"), TEXT("image/png"));
          AddScreenshotMetadataForUiMcp(Resp, Payload);

          if (!bSaved && !bReturnBase64) {
            bSuccess = false;
            Message = TEXT("Screenshot captured but failed to save, and returnBase64=false leaves no image output.");
            ErrorCode = TEXT("SAVE_FAILED");
            Resp->SetStringField(TEXT("error"), Message);
          } else if (bReturnBase64 && PngData.Num() > MaxScreenshotPngBytesForBase64ForMcp) {
            bSuccess = false;
            Message = MakeScreenshotTooLargeMessageForUiMcp(PngData.Num());
            ErrorCode = TEXT("IMAGE_TOO_LARGE");
            Resp->SetStringField(TEXT("error"), Message);
          }

          // Return base64 encoded image if requested
          if (bSuccess && bReturnBase64) {
            FString Base64Data = FBase64::Encode(PngData);
            Resp->SetStringField(TEXT("imageBase64"), Base64Data);
          }
        }
      }
    }
  }
  // ===========================================================================
  // SubAction: play_in_editor
  // ===========================================================================
  else if (LowerSub == TEXT("play_in_editor")) {
    // Start play in editor
    if (GEditor && GEditor->PlayWorld) {
      Message = TEXT("Already playing in editor");
      ErrorCode = TEXT("ALREADY_PLAYING");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      // Execute play command
      bool bCommandSuccess = GEditor->Exec(nullptr, TEXT("Play In Editor"));
      if (bCommandSuccess) {
        bSuccess = true;
        Message = TEXT("Started play in editor");
        Resp->SetStringField(TEXT("status"), TEXT("playing"));
      } else {
        Message = TEXT("Failed to start play in editor");
        ErrorCode = TEXT("PLAY_FAILED");
        Resp->SetStringField(TEXT("error"), Message);
      }
    }
  }
  // ===========================================================================
  // SubAction: stop_play
  // ===========================================================================
  else if (LowerSub == TEXT("stop_play")) {
    // Stop play in editor
    if (GEditor && GEditor->PlayWorld) {
      // Execute stop command
      bool bCommandSuccess =
          GEditor->Exec(nullptr, TEXT("Stop Play In Editor"));
      if (bCommandSuccess) {
        bSuccess = true;
        Message = TEXT("Stopped play in editor");
        Resp->SetStringField(TEXT("status"), TEXT("stopped"));
      } else {
        Message = TEXT("Failed to stop play in editor");
        ErrorCode = TEXT("STOP_FAILED");
        Resp->SetStringField(TEXT("error"), Message);
      }
    } else {
      Message = TEXT("Not currently playing in editor");
      ErrorCode = TEXT("NOT_PLAYING");
      Resp->SetStringField(TEXT("error"), Message);
    }
  }
  // ===========================================================================
  // SubAction: save_all
  // ===========================================================================
  else if (LowerSub == TEXT("save_all")) {
    // Save all assets and levels
    bool bCommandSuccess = GEditor->Exec(nullptr, TEXT("Asset Save All"));
    if (bCommandSuccess) {
      bSuccess = true;
      Message = TEXT("Saved all assets");
      Resp->SetStringField(TEXT("status"), TEXT("saved"));
    } else {
      Message = TEXT("Failed to save all assets");
      ErrorCode = TEXT("SAVE_FAILED");
      Resp->SetStringField(TEXT("error"), Message);
    }
  }
  // ===========================================================================
  // SubAction: simulate_input
  // ===========================================================================
  else if (LowerSub == TEXT("simulate_input")) {
    FString KeyName;
    Payload->TryGetStringField(TEXT("keyName"),
                               KeyName); // Changed to keyName to match schema
    if (KeyName.IsEmpty())
      Payload->TryGetStringField(TEXT("key"), KeyName); // Fallback

    FString EventType;
    Payload->TryGetStringField(TEXT("eventType"), EventType);

    FKey Key = FKey(FName(*KeyName));
    if (Key.IsValid()) {
      const uint32 CharacterCode = 0;
      const uint32 KeyCode = 0;
      const bool bIsRepeat = false;
      FModifierKeysState ModifierState;

      if (EventType == TEXT("KeyDown")) {
        FKeyEvent KeyEvent(Key, ModifierState,
                           FSlateApplication::Get().GetUserIndexForKeyboard(),
                           bIsRepeat, CharacterCode, KeyCode);
        FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
      } else if (EventType == TEXT("KeyUp")) {
        FKeyEvent KeyEvent(Key, ModifierState,
                           FSlateApplication::Get().GetUserIndexForKeyboard(),
                           bIsRepeat, CharacterCode, KeyCode);
        FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
      } else {
        // Press and Release
        FKeyEvent KeyDownEvent(
            Key, ModifierState,
            FSlateApplication::Get().GetUserIndexForKeyboard(), bIsRepeat,
            CharacterCode, KeyCode);
        FSlateApplication::Get().ProcessKeyDownEvent(KeyDownEvent);

        FKeyEvent KeyUpEvent(Key, ModifierState,
                             FSlateApplication::Get().GetUserIndexForKeyboard(),
                             bIsRepeat, CharacterCode, KeyCode);
        FSlateApplication::Get().ProcessKeyUpEvent(KeyUpEvent);
      }

      bSuccess = true;
      Message = FString::Printf(TEXT("Simulated input for key: %s"), *KeyName);
    } else {
      Message = FString::Printf(TEXT("Invalid key name: %s"), *KeyName);
      ErrorCode = TEXT("INVALID_KEY");
      Resp->SetStringField(TEXT("error"), Message);
    }
  }
  // ===========================================================================
  // SubAction: create_hud
  // ===========================================================================
  else if (LowerSub == TEXT("create_hud")) {
    FString WidgetPath;
    Payload->TryGetStringField(TEXT("widgetPath"), WidgetPath);
    UClass *WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetPath);
    if (WidgetClass && GEngine && GEngine->GameViewport) {
      UWorld *World = GEngine->GameViewport->GetWorld();
      if (World) {
        UUserWidget *Widget = CreateWidget<UUserWidget>(World, WidgetClass);
        if (Widget) {
          Widget->AddToViewport();
          bSuccess = true;
          Message = TEXT("HUD created and added to viewport");
          Resp->SetStringField(TEXT("widgetName"), Widget->GetName());
        } else {
          Message = TEXT("Failed to create widget");
          ErrorCode = TEXT("CREATE_FAILED");
        }
      } else {
        Message = TEXT("No world context found (is PIE running?)");
        ErrorCode = TEXT("NO_WORLD");
      }
    } else {
      Message =
          FString::Printf(TEXT("Failed to load widget class: %s"), *WidgetPath);
      ErrorCode = TEXT("CLASS_NOT_FOUND");
    }
  }
  // ===========================================================================
  // SubAction: set_widget_text
  // ===========================================================================
  else if (LowerSub == TEXT("set_widget_text")) {
    FString Key, Value;
    Payload->TryGetStringField(TEXT("key"), Key);
    Payload->TryGetStringField(TEXT("value"), Value);

    bool bFound = false;
    // Iterate all widgets to find one matching Key (Name)
    TArray<UUserWidget *> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
        GEditor->GetEditorWorldContext().World(), Widgets,
        UUserWidget::StaticClass(), false);
    // Also try Game Viewport world if Editor World is not right context (PIE)
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld()) {
      UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
          GEngine->GameViewport->GetWorld(), Widgets,
          UUserWidget::StaticClass(), false);
    }

    for (UUserWidget *Widget : Widgets) {
      // Search inside this widget for a TextBlock named Key
      UWidget *Child = Widget->GetWidgetFromName(FName(*Key));
      if (UTextBlock *TextBlock = Cast<UTextBlock>(Child)) {
        TextBlock->SetText(FText::FromString(Value));
        bFound = true;
        bSuccess = true;
        Message =
            FString::Printf(TEXT("Set text on '%s' to '%s'"), *Key, *Value);
        break;
      }
      // Also check if the widget ITSELF is the one (though UserWidget !=
      // TextBlock usually)
      if (Widget->GetName() == Key) {
        // Can't set text on UserWidget directly unless it implements interface?
        // Assuming Key refers to child widget name usually
      }
    }

    if (!bFound) {
      // Fallback: Use TObjectIterator to find ANY UTextBlock with that name,
      // risky but covers cases
      for (TObjectIterator<UTextBlock> It; It; ++It) {
        if (It->GetName() == Key && It->GetWorld()) {
          It->SetText(FText::FromString(Value));
          bFound = true;
          bSuccess = true;
          Message = FString::Printf(TEXT("Set text on global '%s'"), *Key);
          break;
        }
      }
    }

    if (!bFound) {
      Message = FString::Printf(TEXT("Widget/TextBlock '%s' not found"), *Key);
      ErrorCode = TEXT("WIDGET_NOT_FOUND");
    }
  }
  // ===========================================================================
  // SubAction: set_widget_image
  // ===========================================================================
  else if (LowerSub == TEXT("set_widget_image")) {
    FString Key, TexturePath;
    Payload->TryGetStringField(TEXT("key"), Key);
    Payload->TryGetStringField(TEXT("texturePath"), TexturePath);
    UTexture2D *Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
    if (Texture) {
      bool bFound = false;
      for (TObjectIterator<UImage> It; It; ++It) {
        if (It->GetName() == Key && It->GetWorld()) {
          It->SetBrushFromTexture(Texture);
          bFound = true;
          bSuccess = true;
          Message = FString::Printf(TEXT("Set image on '%s'"), *Key);
          break;
        }
      }
      if (!bFound) {
        Message = FString::Printf(TEXT("Image widget '%s' not found"), *Key);
        ErrorCode = TEXT("WIDGET_NOT_FOUND");
      }
    } else {
      Message = TEXT("Failed to load texture");
      ErrorCode = TEXT("ASSET_NOT_FOUND");
    }
  }
  // ===========================================================================
  // SubAction: set_widget_visibility
  // ===========================================================================
  else if (LowerSub == TEXT("set_widget_visibility")) {
    FString Key;
    bool bVisible = true;
    Payload->TryGetStringField(TEXT("key"), Key);
    Payload->TryGetBoolField(TEXT("visible"), bVisible);

    bool bFound = false;
    // Try UserWidgets
    for (TObjectIterator<UUserWidget> It; It; ++It) {
      if (It->GetName() == Key && It->GetWorld()) {
        It->SetVisibility(bVisible ? ESlateVisibility::Visible
                                   : ESlateVisibility::Collapsed);
        bFound = true;
        bSuccess = true;
        break;
      }
    }
    // If not found, try generic UWidget
    if (!bFound) {
      for (TObjectIterator<UWidget> It; It; ++It) {
        if (It->GetName() == Key && It->GetWorld()) {
          It->SetVisibility(bVisible ? ESlateVisibility::Visible
                                     : ESlateVisibility::Collapsed);
          bFound = true;
          bSuccess = true;
          break;
        }
      }
    }

      if (bFound) {
        Message = FString::Printf(TEXT("Set visibility on '%s' to %s"), *Key,
                                  bVisible ? TEXT("Visible") : TEXT("Collapsed"));
      } else {
        Message = FString::Printf(TEXT("Widget '%s' not found"), *Key);
        ErrorCode = TEXT("WIDGET_NOT_FOUND");
      }
  }
  // ===========================================================================
  // SubAction: get_project_settings
  // ===========================================================================
  else if (LowerSub == TEXT("get_project_settings")) {
    FString Section;
    Payload->TryGetStringField(TEXT("section"), Section);
    Payload->TryGetStringField(TEXT("category"), Section);  // Accept both

    TSharedPtr<FJsonObject> SettingsObj = MakeShared<FJsonObject>();

    // Get common project settings
    if (GEngine) {
      // Engine settings
      SettingsObj->SetStringField(TEXT("engineVersion"), FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));

      // Project name
      FString ProjectName = FApp::GetProjectName();
      SettingsObj->SetStringField(TEXT("projectName"), ProjectName);

      // Project directory
      FString ProjectDir = FPaths::ProjectDir();
      SettingsObj->SetStringField(TEXT("projectDir"), ProjectDir);

      // Game engine settings via config
      FString ResolutionX, ResolutionY;
      GConfig->GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), ResolutionX, GGameUserSettingsIni);
      GConfig->GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeY"), ResolutionY, GGameUserSettingsIni);
      if (!ResolutionX.IsEmpty() && !ResolutionY.IsEmpty()) {
        TSharedPtr<FJsonObject> ResObj = MakeShared<FJsonObject>();
        ResObj->SetStringField(TEXT("width"), ResolutionX);
        ResObj->SetStringField(TEXT("height"), ResolutionY);
        SettingsObj->SetObjectField(TEXT("resolution"), ResObj);
      }

      // Fullscreen mode
      FString FullscreenMode;
      GConfig->GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("LastConfirmedFullscreenMode"), FullscreenMode, GGameUserSettingsIni);
      if (!FullscreenMode.IsEmpty()) {
        SettingsObj->SetStringField(TEXT("fullscreenMode"), FullscreenMode);
      }
    }

    Resp->SetObjectField(TEXT("settings"), SettingsObj);
    bSuccess = true;
    Message = TEXT("Project settings retrieved");
  }
  // ===========================================================================
  // SubAction: set_project_setting
  // ===========================================================================
  else if (LowerSub == TEXT("set_project_setting")) {
    FString Section, Key, Value;
    Payload->TryGetStringField(TEXT("section"), Section);
    Payload->TryGetStringField(TEXT("key"), Key);
    Payload->TryGetStringField(TEXT("value"), Value);

    if (Section.IsEmpty() || Key.IsEmpty()) {
      Message = TEXT("section and key are required for set_project_setting");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      // Try to set the config value
      // First, normalize section format (ensure it starts with /Script/ if it looks like a UE section)
      FString NormalizedSection = Section;
      if (!NormalizedSection.StartsWith(TEXT("/")) && !NormalizedSection.StartsWith(TEXT("["))) {
        NormalizedSection = FString::Printf(TEXT("/Script/%s"), *Section);
      }

      // Set the value in the appropriate config file
      // For project settings, use DefaultEngine.ini
      FString ConfigFile = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");

      // Use GConfig to set the value
      GConfig->SetString(*NormalizedSection, *Key, *Value, ConfigFile);
      GConfig->Flush(false, ConfigFile);

      Resp->SetStringField(TEXT("section"), NormalizedSection);
      Resp->SetStringField(TEXT("key"), Key);
      Resp->SetStringField(TEXT("value"), Value);
      bSuccess = true;
      Message = FString::Printf(TEXT("Set %s.%s = %s"), *NormalizedSection, *Key, *Value);
    }
  }
  // ===========================================================================
  // SubAction: remove_widget_from_viewport
  // ===========================================================================
  else if (LowerSub == TEXT("remove_widget_from_viewport")) {
    FString Key;
    Payload->TryGetStringField(TEXT("key"),
                               Key); // If empty, remove all? OR specific

    if (Key.IsEmpty()) {
      // Remove all user widgets?
      TArray<UUserWidget *> TempWidgets;
      UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
          GEditor->GetEditorWorldContext().World(), TempWidgets,
          UUserWidget::StaticClass(), true);
      // Implementation:
      if (GEngine && GEngine->GameViewport &&
          GEngine->GameViewport->GetWorld()) {
        TArray<UUserWidget *> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
            GEngine->GameViewport->GetWorld(), Widgets,
            UUserWidget::StaticClass(), true);
        for (UUserWidget *W : Widgets) {
          W->RemoveFromParent();
        }
        bSuccess = true;
        Message = TEXT("Removed all widgets");
      }
    } else {
      bool bFound = false;
      for (TObjectIterator<UUserWidget> It; It; ++It) {
        if (It->GetName() == Key && It->GetWorld()) {
          It->RemoveFromParent();
          bFound = true;
          bSuccess = true;
          break;
        }
      }
      if (bFound) {
        Message = FString::Printf(TEXT("Removed widget '%s'"), *Key);
      } else {
        Message = FString::Printf(TEXT("Widget '%s' not found"), *Key);
        ErrorCode = TEXT("WIDGET_NOT_FOUND");
      }
    }
  }
  // ===========================================================================
  // Unknown SubAction
  // ===========================================================================
  else {
    Message = FString::Printf(
        TEXT("System control action '%s' not implemented"), *LowerSub);
    ErrorCode = TEXT("NOT_IMPLEMENTED");
    Resp->SetStringField(TEXT("error"), Message);
  }

#else
  Message = TEXT("System control actions require editor build.");
  ErrorCode = TEXT("NOT_IMPLEMENTED");
  Resp->SetStringField(TEXT("error"), Message);
#endif

  Resp->SetBoolField(TEXT("success"), bSuccess);
  if (Message.IsEmpty()) {
    Message = bSuccess ? TEXT("System control action completed")
                       : TEXT("System control action failed");
  }

  SendAutomationResponse(RequestingSocket, RequestId, bSuccess, Message, Resp,
                         ErrorCode);
  return true;
}
