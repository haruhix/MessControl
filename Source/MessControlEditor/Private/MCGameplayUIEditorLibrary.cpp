#include "MCGameplayUIEditorLibrary.h"

#include "MCGameplayUIPreview.h"
#include "MCMainMenuPreview.h"
#include "MCGameplayUIWidget.h"
#include "MCGameplayHUD.h"
#include "MCPlayerController.h"
#include "EngineUtils.h"
#include "MCMainMenuWidget.h"
#include "MCVoiceSettingsWidget.h"
#include "MCHubWidget.h"
#include "MCHubWardrobeWidget.h"
#include "MCHubShopWidget.h"
#include "MCUIDataLibrary.h"
#include "MCThroatVortex.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCNutRainEvent.h"
#include "MCNutBoss.h"
#include "MCSingleDayDirector.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "Editor.h"
#include "UObject/UnrealType.h"
#include "MCToothStatusComponent.h"
#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "MCGameDirectorProfile.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
UMCInventoryComponent* EquipmentReviewInventory(AMCToothCharacter* Hero)
{
    if(!IsInGameThread() || !GEditor || !IsValid(Hero) || !Hero->HasAuthority()) return nullptr;
    UWorld* World=Hero->GetWorld();
    if(!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()!=NM_Standalone
        || World->bIsTearingDown || GEditor->PlayWorld!=World || !World->GetFirstPlayerController()
        || World->GetFirstPlayerController()->GetPawn()!=Hero || !IsValid(Hero->Inventory)
        || Hero->Inventory->GetOwner()!=Hero) return nullptr;
    return Hero->Inventory;
}
}

UMCEquipmentProfile* UMCGameplayUIEditorLibrary::GetPIEEquipmentProfile(AMCToothCharacter* Hero)
{
    auto* Inventory=EquipmentReviewInventory(Hero);
    const auto* Property=FindFProperty<FObjectPropertyBase>(UMCInventoryComponent::StaticClass(),TEXT("Settings"));
    return Inventory && Property?Cast<UMCEquipmentProfile>(Property->GetObjectPropertyValue_InContainer(Inventory)):nullptr;
}

bool UMCGameplayUIEditorLibrary::SetPIEEquipmentProfile(AMCToothCharacter* Hero, UMCEquipmentProfile* Profile)
{
    auto* Inventory=EquipmentReviewInventory(Hero);
    const auto* Property=FindFProperty<FObjectPropertyBase>(UMCInventoryComponent::StaticClass(),TEXT("Settings"));
    if(!Inventory || !Property || !IsValid(Profile) || !FMath::IsFinite(Profile->BrushCleaningRadius)
        || Profile->BrushCleaningRadius<1.f || Profile->BrushCleaningRadius>120.f) return false;
    Property->SetObjectPropertyValue_InContainer(Inventory,Profile);
    return GetPIEEquipmentProfile(Hero)==Profile;
}

bool UMCGameplayUIEditorLibrary::TogglePIEPauseMenu(UWorld* World)
{
    if(!World || World->WorldType!=EWorldType::PIE || World->bIsTearingDown) return false;
    AMCPlayerController* LocalController=nullptr;
    for(TActorIterator<AMCPlayerController> It(World);It;++It) if(It->HasLocalPresentationOwner()) {
        if(LocalController) return false;
        LocalController=*It;
    }
    if(!LocalController) return false;
    LocalController->TogglePauseMenu();
    return true;
}
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/ScrollBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TextureCompiler.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SWindow.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
void RegisterMissingDesignerGuids(UWidgetBlueprint* Blueprint)
{
    // Match the Designer's add-widget path. The compiler only seeds every GUID
    // for an entirely empty map; appending to an existing WBP needs explicit IDs.
    Blueprint->WidgetTree->ForEachWidget([Blueprint](UWidget* Widget) {
        if(!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            Blueprint->OnVariableAdded(Widget->GetFName());
    });
}

bool HasDirtyDesignerPackages()
{
    TArray<UPackage*> Dirty;FEditorFileUtils::GetDirtyContentPackages(Dirty);
    for(const auto* Package:Dirty)
        if(Package && Package->GetName().StartsWith(TEXT("/Game/Gameplay/UI/"))) {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_SNAPSHOT Unsaved UI package preserved: %s"),*Package->GetName());
            return true;
        }
    return false;
}

/** Per-operation snapshots; artist MICs and unrelated/dirty packages are never overwritten. */
struct FDesignerMaterialSnapshots
{
    UWidgetBlueprint* Blueprint;
    UEditorAssetSubsystem* Assets;
    const UObject* NamingRoot;
    TMap<UMaterialInstanceDynamic*,UMaterialInstanceConstant*> BySource;
    TArray<TStrongObjectPtr<UMaterialInstanceConstant>> Pending;
    bool bFailed=false;

    FDesignerMaterialSnapshots(UWidgetBlueprint* InBlueprint,UEditorAssetSubsystem* InAssets,const UObject* InNamingRoot=nullptr)
        :Blueprint(InBlueprint),Assets(InAssets),NamingRoot(InNamingRoot?InNamingRoot:InBlueprint->WidgetTree.Get()) {}

    bool Brush(FSlateBrush& Value,const UWidget* Widget,const TCHAR* Role)
    {
        auto* Dynamic=Cast<UMaterialInstanceDynamic>(Value.GetResourceObject());
        if(!Dynamic) return true;
        if(bFailed) return false;
        if(auto* Existing=BySource.Find(Dynamic)) {Value.SetResourceObject(*Existing);return true;}
        UMaterialInterface* Parent=Dynamic->Parent;
        while(auto* ParentMID=Cast<UMaterialInstanceDynamic>(Parent)) Parent=ParentMID->Parent;
        if(!Parent || !Parent->IsAsset() || Parent->GetPackage()->IsDirty()
            || !Parent->GetPathName().StartsWith(TEXT("/Game/Gameplay/UI/Materials/")) || !Parent->IsUIMaterial()) {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_SNAPSHOT Unsupported or unsaved material parent on %s/%s; no lossy fallback"),*Widget->GetName(),Role);
            bFailed=true;return false;
        }
        // A uniform snapshot must not serialize a transient render target or another
        // runtime-only resource into a saved UI asset. Phase1 surfaces use no textures.
        TArray<FMaterialParameterInfo> Infos;TArray<FGuid> IDs;
        Dynamic->GetAllTextureParameterInfo(Infos,IDs);
        for(const auto& Info:Infos) {
            UTexture* Texture=nullptr;
            if(Dynamic->GetTextureParameterValue(FHashedMaterialParameterInfo(Info),Texture) && Texture
                && (!Texture->IsAsset() || Texture->GetPackage()->IsDirty())) {
                UE_LOG(LogTemp,Error,TEXT("MC_UI_SNAPSHOT Runtime/unsaved texture on %s/%s: %s"),*Widget->GetName(),Role,*Texture->GetPathName());
                bFailed=true;return false;
            }
        }
        const FString Identity=ObjectTools::SanitizeObjectName(Widget->GetPathName(NamingRoot));
        const FString Name=FString::Printf(TEXT("MI_%s_%s"),*Identity,Role);
        const FString PackagePath=TEXT("/Game/Gameplay/UI/Materials/DesignerSnapshots/")+Blueprint->GetName()+TEXT("/")+Name;
        const FString ObjectPath=PackagePath+TEXT(".")+Name;
        if(FindPackage(nullptr,*PackagePath) || FindObject<UObject>(nullptr,*ObjectPath) || Assets->DoesAssetExist(PackagePath)) {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_SNAPSHOT Existing snapshot collision preserved; inspect before retry: %s"),*PackagePath);
            bFailed=true;return false;
        }
        auto* Package=CreatePackage(*PackagePath);
        auto* Material=NewObject<UMaterialInstanceConstant>(Package,FName(Name),RF_Public|RF_Standalone|RF_Transactional);
        Pending.Emplace(Material);
        Material->SetParentEditorOnly(Parent);
        // The engine copies effective scalar/vector/texture values through the
        // MID parent chain. Keeping only GetMaterial() would lose the palette.
        Material->CopyMaterialUniformParametersEditorOnly(Dynamic,false);
        Material->PostEditChange();Material->MarkPackageDirty();
        BySource.Add(Dynamic,Material);Value.SetResourceObject(Material);
        return true;
    }

    bool Save()
    {
        if(bFailed) return false;
        for(const auto& Material:Pending) {
            FAssetRegistryModule::AssetCreated(Material.Get());
            if(!Assets->SaveLoadedAsset(Material.Get(),false)) {
                UE_LOG(LogTemp,Error,TEXT("MC_UI_SNAPSHOT Cannot save owned snapshot: %s"),*Material->GetPathName());
                return false;
            }
            UE_LOG(LogTemp,Display,TEXT("MC_UI_SNAPSHOT Saved editable material parameters: %s"),*Material->GetPathName());
        }
        return true;
    }
};

bool SanitizeAppendedDesignerWidget(UWidget* Widget,UObject* SourceOwner,FDesignerMaterialSnapshots& Snapshots)
{
    Widget->ClearFlags(RF_Transient);Widget->SetFlags(RF_Transactional);Widget->bIsVariable=false;
    if(auto* Button=Cast<UButton>(Widget)) {
        Button->OnClicked.RemoveAll(SourceOwner);
        auto Style=Button->GetStyle();
        if(!Snapshots.Brush(Style.Normal,Widget,TEXT("Normal")) || !Snapshots.Brush(Style.Hovered,Widget,TEXT("Hovered"))
            || !Snapshots.Brush(Style.Pressed,Widget,TEXT("Pressed")) || !Snapshots.Brush(Style.Disabled,Widget,TEXT("Disabled"))) return false;
        Button->SetStyle(Style);
    }
    if(auto* Border=Cast<UBorder>(Widget)) {auto Value=Border->Background;if(!Snapshots.Brush(Value,Widget,TEXT("Background"))) return false;Border->SetBrush(Value);}
    if(auto* Image=Cast<UImage>(Widget)) {auto Value=Image->GetBrush();if(!Snapshots.Brush(Value,Widget,TEXT("Image"))) return false;Image->SetBrush(Value);}
    if(auto* Bar=Cast<UProgressBar>(Widget)) {
        auto Style=Bar->GetWidgetStyle();
        if(!Snapshots.Brush(Style.BackgroundImage,Widget,TEXT("Background")) || !Snapshots.Brush(Style.FillImage,Widget,TEXT("Fill"))
            || !Snapshots.Brush(Style.MarqueeImage,Widget,TEXT("Marquee"))) return false;
        Bar->SetWidgetStyle(Style);
    }
    if(auto* Picker=Cast<UComboBoxString>(Widget);Picker && Picker->OnGenerateWidgetEvent.GetUObject()==SourceOwner)
        Picker->OnGenerateWidgetEvent.Unbind();
    return true;
}
}

void UMCGameplayUIEditorLibrary::OpenMainMenuPreview() {MCMainMenuPreview::Open();}
void UMCGameplayUIEditorLibrary::CloseMainMenuPreview() {MCMainMenuPreview::Close();}
void UMCGameplayUIEditorLibrary::RefreshMainMenuPreview() {MCMainMenuPreview::Refresh();}
void UMCGameplayUIEditorLibrary::SetMainMenuPreviewState(FName State,const FString& Culture) {MCMainMenuPreview::SetState(State,Culture);}
UMCMainMenuWidget* UMCGameplayUIEditorLibrary::GetMainMenuPreviewWidget() {return MCMainMenuPreview::GetPreviewWidget();}
bool UMCGameplayUIEditorLibrary::IsMainMenuPreviewActive() {return MCMainMenuPreview::IsActive();}

bool UMCGameplayUIEditorLibrary::ReadPIEThroatVortex(UWorld* World,AMCThroatVortex* Vortex,
    TArray<AMCFoodActor*>& OutSources,double& OutStartedAt,float& OutDuration)
{
    OutSources.Reset();OutStartedAt=0;OutDuration=0;
    if(!IsInGameThread() || !GEditor || !IsValid(World) || !IsValid(Vortex)
        || World->WorldType!=EWorldType::PIE || Vortex->GetWorld()!=World || Vortex->IsActorBeingDestroyed()) return false;
    const FWorldContext* Context=GEditor->GetWorldContextFromWorld(World);
    if(!Context || Context->WorldType!=EWorldType::PIE) return false;
    OutSources.Reserve(Vortex->FoodSources.Num());
    for(const auto& Food:Vortex->FoodSources) OutSources.Add(Food.Get());
    OutStartedAt=Vortex->StartedAt;OutDuration=Vortex->Duration;
    return true;
}

AMCNutBoss* UMCGameplayUIEditorLibrary::SpawnPIEMageForSummon(UWorld* World,AMCTongue* Tongue,
    AMCNutRainEvent* Event,FVector NearFloor,int32 Seed)
{
    if(!IsInGameThread() || !GEditor || !IsValid(World) || World->WorldType!=EWorldType::PIE
        || World->GetNetMode()!=NM_Standalone || !IsValid(Tongue) || !IsValid(Event) || NearFloor.ContainsNaN()
        || Tongue->GetWorld()!=World || Event->GetWorld()!=World || Event->IsActorBeingDestroyed()
        || Event->bFailed || Event->Stage!=EMCNutRainStage::Enemies) return nullptr;
    const FWorldContext* Context=GEditor->GetWorldContextFromWorld(World);
    if(!Context || Context->WorldType!=EWorldType::PIE || GEditor->PlayWorld!=World) return nullptr;
    for(TActorIterator<AMCNutBoss> It(World);It;++It)
        if(It->BossRole==EMCNutBossRole::Mage && It->IsEncounterAlive()) return nullptr;
    TArray<AMCFoodActor*> Available;Event->GetAvailableRainNuts(Available);
    if(Available.IsEmpty()) return nullptr;
    const float Radius=Event->Settings.Boss.BodyRadiusForRole(EMCNutBossRole::Mage);
    FHitResult Floor;
    if(!Tongue->GameplaySpawnFootprint(NearFloor,Radius+20,Floor)) return nullptr;
    const FTransform Pose(Floor.ImpactPoint+FVector::UpVector*(Radius+4));
    auto* Mage=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,Event,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Mage) return nullptr;
    Mage->ConfigureEncounter(Tongue,Event,EMCNutBossRole::Mage,Event->Settings.Boss,1,Seed);
    FTransform AirbornePose=Pose;AirbornePose.SetLocation(Mage->GetActorLocation());
    Mage->FinishSpawning(AirbornePose);
    Event->Bosses.Add(Mage);Event->RegisterEncounterEnemy(Mage);
    return Mage;
}

bool UMCGameplayUIEditorLibrary::StartPostNutRelaxPreview(AMCPlayerController* PlayerController)
{
    UWorld* World=IsValid(PlayerController)?PlayerController->GetWorld():nullptr;
    if(!IsInGameThread() || !GEditor || !IsValid(World) || World->WorldType!=EWorldType::PIE
        || World->bIsTearingDown || World->GetNetMode()!=NM_Standalone || World->IsPaused()
        || !FParse::Param(FCommandLine::Get(),TEXT("MCHubTransientProfile"))
        || PlayerController->IsActorBeingDestroyed() || !PlayerController->HasAuthority()
        || !PlayerController->IsLocalController() || !PlayerController->HasLocalPresentationOwner()
        || !PlayerController->IsWorldPresentationReady()) return false;
    const FWorldContext* Context=GEditor->GetWorldContextFromWorld(World);
    if(!Context || Context->WorldType!=EWorldType::PIE || GEditor->PlayWorld!=World) return false;
    auto* Player=Cast<AMCToothCharacter>(PlayerController->GetPawn());
    auto* State=World->GetGameState<AMCGameState>();
    auto* Loop=State?State->SingleDayDirector.Get():nullptr;
    if(!IsValid(State) || !IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld()!=World
        || !Player->Status || !Player->Status->IsAlive() || Player->IsArenaArrivalHeld()
        || !State->bSingleDayLoop || State->bLobbyWaiting || State->bTutorialActive
        || State->bDayOneComplete || State->bDevManualEvents || State->Phase!=EMCShiftPhase::Working
        || !IsValid(Loop) || Loop->IsActorBeingDestroyed() || Loop->GetWorld()!=World
        || !Loop->HasAuthority() || Loop->bStageRewardPending || !IsValid(Loop->Mechanics)
        || !IsValid(Loop->Settings) || Loop->IsLegacyTimedFinale()) return false;
    // This fixture is only meaningful for the authored Nuts -> Relax -> Jellies
    // route. It does not manufacture or reorder slots in a custom run.
    const auto& Slots=Loop->Settings->KeyEvents;
    if(Slots.Num()<2 || Slots[0].Kind!=EMCSingleDayKeyEventKind::NutEncounter
        || Slots[1].Kind!=EMCSingleDayKeyEventKind::Jellies) return false;
    UMCDayPlan* Mechanics=Loop->Mechanics;
    UMCGameDirectorProfile* Support=Loop->DirectorProfile;
    // Initialize duplicates settings into the loop. Snapshot outside that outer
    // first, so repeated explicit previews cannot collide with its old settings.
    const FName SnapshotName=MakeUniqueObjectName(GetTransientPackage(),UMCSingleDayProfile::StaticClass(),TEXT("PostNutRelaxPreviewProfile"));
    TStrongObjectPtr<UMCSingleDayProfile> Profile(DuplicateObject<UMCSingleDayProfile>(Loop->Settings,GetTransientPackage(),SnapshotName));
    if(!Profile.IsValid()) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_POST_NUT_RELAX_PREVIEW_SETUP explicit_initial_stage_fixture=1 full_run_or_nut_completion_claim=0 world=%s controller=%s mechanics=%s coffee_to_food=%.2f"),
        *World->GetPathName(),*PlayerController->GetPathName(),*Mechanics->GetPathName(),Profile->AfterNutCoffeeToFoodSeconds);
    // Ordinary lifecycle owns all cleanup and restart work. No timer, food
    // count, completion, player status or economy value is assigned here.
    Loop->Stop();
    Loop->Initialize(Mechanics,Profile.Get(),Support);
    Loop->BeginPostNutMeal();
    return !Loop->bStopped && Loop->Stage==EMCSingleDayStage::PostNutCoffee
        && IsValid(Loop->OpeningCoffee) && Loop->OpeningCoffee->IsActive();
}

FString UMCGameplayUIEditorLibrary::InspectPlayerView(APlayerController* PlayerController)
{
    const UWorld* World=IsValid(PlayerController)?PlayerController->GetWorld():nullptr;
    if(!IsInGameThread() || !GEditor || !IsValid(World) || World->WorldType!=EWorldType::PIE)
        return TEXT("{\"Valid\":false,\"Error\":\"Expected a controller from a live PIE world\"}");
    auto QuotedPath=[](const UObject* Object)->FString {
        if(!IsValid(Object)) return TEXT("null");
        const FString Escaped=Object->GetPathName().Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
            .Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\t"),TEXT("\\t"));
        return FString::Printf(TEXT("\"%s\""),*Escaped);
    };
    TArray<FString> Hidden;
    for(const AActor* Actor:PlayerController->HiddenActors) if(IsValid(Actor)) Hidden.Add(QuotedPath(Actor));
    return FString::Printf(TEXT("{\"Valid\":true,\"PlayerController\":%s,\"World\":%s,\"HiddenActors\":[%s],\"ViewTarget\":%s,\"AutoManage\":%s}"),
        *QuotedPath(PlayerController),*QuotedPath(World),*FString::Join(Hidden,TEXT(",")),
        *QuotedPath(PlayerController->GetViewTarget()),PlayerController->bAutoManageActiveCameraTarget?TEXT("true"):TEXT("false"));
}

bool UMCGameplayUIEditorLibrary::CapturePIEGameWindow(UWorld* World,const FString& OutputFile)
{
    FString Path=FPaths::ConvertRelativePathToFull(OutputFile);FPaths::NormalizeFilename(Path);
    if(!IsInGameThread() || !GEditor || !IsValid(World) || World->WorldType!=EWorldType::PIE
        || !FSlateApplication::IsInitialized() || !FPaths::CollapseRelativeDirectories(Path)
        || !Path.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase)
        || !Path.EndsWith(TEXT(".png"),ESearchCase::IgnoreCase)) return false;
    const FWorldContext* Context=GEditor->GetWorldContextFromWorld(World);
    auto* Viewport=World->GetGameViewport();
    if(!Context || Context->WorldType!=EWorldType::PIE || !Viewport || Viewport->GetWorld()!=World) return false;
    const TSharedPtr<SWindow> Window=Viewport->GetWindow();
    if(!Window.IsValid()) return false;
    TArray<FColor> Pixels;FIntVector Size=FIntVector::ZeroValue;
    // Unlike Shot/FScreenshotRequest, this binds the screenshot to the owned
    // game SWindow. Editor viewport invalidations cannot consume its request.
    if(!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size)
        || Size.X<=0 || Size.Y<=0 || Pixels.Num()<int64(Size.X)*Size.Y) return false;
    TArray64<uint8> PNG;
    if(!FImageUtils::CompressImage(PNG,TEXT("PNG"),FImageView(Pixels.GetData(),Size.X,Size.Y,EGammaSpace::sRGB))) return false;
    return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true) && FFileHelper::SaveArrayToFile(PNG,*Path);
}

bool UMCGameplayUIEditorLibrary::RenderGameplayUIPreview(TSubclassOf<UMCGameplayUIWidget> WidgetClass,
    const FString& OutputFile,FIntPoint DrawSize,const FString& State,const FString& Culture,float Time,float RenderScale)
{
    FString Path=FPaths::ConvertRelativePathToFull(OutputFile);FPaths::NormalizeFilename(Path);
    if (!GEditor || GEditor->PlayWorld || !WidgetClass || !Path.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase)
        || !Path.EndsWith(TEXT(".png"),ESearchCase::IgnoreCase) || DrawSize.X<1 || DrawSize.Y<1 || DrawSize.X>4096 || DrawSize.Y>4096
        || !FMath::IsFinite(RenderScale) || RenderScale<.25f || RenderScale>8.f) return false;
    if(auto* Generated=Cast<UWidgetBlueprintGeneratedClass>(WidgetClass.Get()))
        Generated->UpdateCustomPropertyListForPostConstruction();
    TStrongObjectPtr<UMCGameplayUIWidget> Widget(NewObject<UMCGameplayUIWidget>(GetTransientPackage(),WidgetClass,NAME_None,RF_Transient));
    Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
    Widget->PreviewState=FName(State);Widget->PreviewCulture=Culture;
    const int64 Scenario=StaticEnum<EMCGameplayUIPreviewScenario>()->GetValueByNameString(State);
    if (Scenario!=INDEX_NONE) Widget->PreviewScenario=static_cast<EMCGameplayUIPreviewScenario>(Scenario);
    Widget->Initialize();Widget->BuildPresentationTree();
    const TSharedRef<SWidget> SlateWidget=Widget->TakeWidget();Widget->ApplyEditorPreview(Time);
    // Only materials used by this fixture need to finish before its first draw.
    TArray<UTexture*> FixtureTextures;
    Widget->WidgetTree->ForEachWidgetAndDescendants([&FixtureTextures](UWidget* Item)
    {
        auto CompleteBrush=[](const FSlateBrush& Brush)
        {if(auto* Material=Cast<UMaterialInterface>(Brush.GetResourceObject())) Material->EnsureIsComplete();};
        if(auto* Border=Cast<UBorder>(Item)) CompleteBrush(Border->Background);
        if(auto* Button=Cast<UButton>(Item)) {const auto& Style=Button->GetStyle();CompleteBrush(Style.Normal);CompleteBrush(Style.Hovered);CompleteBrush(Style.Pressed);CompleteBrush(Style.Disabled);}
        if(auto* Image=Cast<UImage>(Item))
        {
            if(auto* Texture=Cast<UTexture>(Image->GetBrush().GetResourceObject())) FixtureTextures.AddUnique(Texture);
            if(auto* Material=Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject())) Material->EnsureIsComplete();
        }
        if(auto* Bar=Cast<UProgressBar>(Item))
            for(const auto* Brush:{&Bar->GetWidgetStyle().FillImage,&Bar->GetWidgetStyle().BackgroundImage})
                if(auto* Material=Cast<UMaterialInterface>(Brush->GetResourceObject())) Material->EnsureIsComplete();
    });
    FTextureCompilingManager::Get().FinishCompilation(FixtureTextures);
    for(auto* Texture:FixtureTextures) Texture->WaitForPendingInitOrStreaming();
    MCGameplayUIPreview::SynchronizeFixtureTree(Widget.Get());
    SlateWidget->SlatePrepass();
    UE_LOG(LogTemp,Display,TEXT("MC_UI_RENDER %s bindings=%d cdoBindings=%d"),*WidgetClass->GetPathName(),Widget->StyleBindings.Num(),WidgetClass->GetDefaultObject<UMCGameplayUIWidget>()->StyleBindings.Num());
    for(const FName Name:{FName(TEXT("TimerValue")),FName(TEXT("ReserveValue")),FName(TEXT("ThemeHeading"))})
        if(auto* Text=Cast<UTextBlock>(Widget->GetWidgetFromName(Name)))
            UE_LOG(LogTemp,Display,TEXT("MC_UI_RENDER_FONT %s valid=%d value=%s"),*Name.ToString(),Text->GetFont().HasValidFont(),*Text->GetText().ToString());
    const float DPIScale=DrawSize.X>=1000 && DrawSize.Y>=600?GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(DrawSize):1.f;
    // Keep the logical layout at a denser native vector rasterization for art
    // review. Existing callers retain scale 1; this never upscales an old PNG.
    const TSharedRef<SWidget> PreviewSurface=SNew(SDPIScaler).DPIScale(DPIScale*RenderScale)[SlateWidget];
    // Render linear colors into a float target, then encode sRGB exactly once.
    // An sRGB target plus Slate gamma correction otherwise brightens PNG previews twice.
    auto* Renderer=new FWidgetRenderer(false,true);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
    Target->ClearColor=FLinearColor::Transparent;
    Target->TargetGamma=1.f;
    Target->InitCustomFormat(DrawSize.X,DrawSize.Y,PF_FloatRGBA,true);
    Target->UpdateResourceImmediate(true);
    Renderer->DrawWidget(Target.Get(),PreviewSurface,FVector2D(DrawSize),0.f,false);
    FlushRenderingCommands();
    // Auto-wrap needs an allotted width before its measured height is final.
    // Warm the actual tree, then export the settled layout.
    // Transient fixtures have no World and do not receive NativeTick. Reapply
    // the fixture with its real paint geometry/DPI so measured HUD panels use
    // the same font scale as the final draw.
    Widget->ApplyEditorPreview(Time);
    MCGameplayUIPreview::SynchronizeFixtureTree(Widget.Get());
    Widget->RefreshPresentationGeometry();
    SlateWidget->Invalidate(EInvalidateWidgetReason::Layout);
    PreviewSurface->SlatePrepass();
    Renderer->DrawWidget(Target.Get(),PreviewSurface,FVector2D(DrawSize),0.f,false);
    FlushRenderingCommands();
    TArray<FLinearColor> LinearPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    TArray64<uint8> Pixels;
    const bool bRead=Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(LinearPixels,ReadFlags);
    TArray<FColor> RawPixels;
    if(bRead)
    {
        RawPixels.Reserve(LinearPixels.Num());
        for(const auto& Color:LinearPixels) RawPixels.Add(Color.ToFColorSRGB());
    }
    const bool bEncoded=bRead && FImageUtils::CompressImage(Pixels,TEXT("PNG"),
        FImageView(RawPixels.GetData(),DrawSize.X,DrawSize.Y,EGammaSpace::sRGB));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const bool bSaved=bEncoded && FFileHelper::SaveArrayToFile(Pixels,*Path);
    BeginCleanup(Renderer);
    return bSaved;
}
void UMCGameplayUIEditorLibrary::EnsureGameplayUIPreviewRegistered()
{
    MCGameplayUIPreview::EnsureRegistered();
}

void UMCGameplayUIEditorLibrary::OpenGameplayUITheme(UMCGameplayUITheme* Theme)
{
    MCGameplayUIPreview::EnsureRegistered();
    MCGameplayUIPreview::OpenThemeEditor(Theme);
}

UWidgetBlueprint* UMCGameplayUIEditorLibrary::CreateEditableGameplayWidget(const FString& AssetPath,TSubclassOf<UMCGameplayUIWidget> ParentClass)
{
    if (!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !ParentClass || HasDirtyDesignerPackages()
        || ParentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || !AssetPath.StartsWith(TEXT("/Game/Gameplay/UI/")) || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Requires idle editor, concrete gameplay widget class and /Game/Gameplay/UI/ package path: %s"),*AssetPath);
        return nullptr;
    }
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (!Assets) return nullptr;
    const FString ObjectPath=AssetPath+TEXT(".")+FPackageName::GetLongPackageAssetName(AssetPath);
    UObject* LoadedExisting=FindObject<UObject>(nullptr,*ObjectPath);
    if (LoadedExisting || Assets->DoesAssetExist(AssetPath))
    {
        auto* Existing=Cast<UWidgetBlueprint>(LoadedExisting?LoadedExisting:Assets->LoadAsset(AssetPath));
        if (!Existing || !Existing->ParentClass || !Existing->ParentClass->IsChildOf(ParentClass))
        {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Existing unrelated asset preserved: %s"),*AssetPath);
            return nullptr;
        }
        UE_LOG(LogTemp,Display,TEXT("MC_UI_AUTHOR Existing artist tree preserved: %s"),*AssetPath);
        return Existing;
    }

    // A design-time source instance supplies the initial editable tree, without running gameplay initialization.
    // Blueprint compilation can collect garbage. The source owns presentation
    // data copied after compilation, so keep it alive through the entire save.
    TStrongObjectPtr<UMCGameplayUIWidget> Source(NewObject<UMCGameplayUIWidget>(GetTransientPackage(),ParentClass,NAME_None,RF_Transient));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Source->Initialize();
    Source->BuildPresentationTree();
    if (!Source->WidgetTree || !Source->WidgetTree->RootWidget)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Parent produced no presentation tree: %s"),*ParentClass->GetPathName());
        return nullptr;
    }

    UPackage* Package=CreatePackage(*AssetPath);
    auto* Blueprint=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass,Package,
        FName(*FPackageName::GetLongPackageAssetName(AssetPath)),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("MCGameplayUIAuthoring")));
    if (!Blueprint) return nullptr;
    TStrongObjectPtr<UWidgetBlueprint> BlueprintGuard(Blueprint);
    if (Blueprint->WidgetTree)
        Blueprint->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors | REN_NonTransactional);
    Blueprint->WidgetTree=DuplicateObject<UWidgetTree>(Source->WidgetTree,Blueprint,TEXT("WidgetTree"));
    Blueprint->WidgetTree->ClearFlags(RF_Transient);
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets);
    Blueprint->WidgetTree->ForEachWidget([SourceWidget=Source.Get(),&Snapshots](UWidget* Widget)
    {
        if(!SanitizeAppendedDesignerWidget(Widget,SourceWidget,Snapshots)) Snapshots.bFailed=true;
    });
    if(Snapshots.bFailed) return nullptr;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Blueprint compilation failed; no package saved: %s"),*AssetPath);
        return nullptr;
    }
    if (auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject()))
    {
        Defaults->StyleBindings=Source->StyleBindings;
        Defaults->TextBindings=Source->TextBindings;
        Defaults->TranslationTable=Source->TranslationTable;
        // Native defaults changed after compilation: refresh the optimized
        // initializer so newly created instances receive those defaults too.
        CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    }
    if(!Snapshots.Save()) return nullptr;
    FAssetRegistryModule::AssetCreated(Blueprint);
    Blueprint->MarkPackageDirty();
    if (!Assets->SaveLoadedAsset(Blueprint,false))
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Could not save new gameplay widget: %s"),*AssetPath);
        return nullptr;
    }
    UE_LOG(LogTemp,Display,TEXT("MC_UI_AUTHOR Saved editable gameplay widget: %s"),*AssetPath);
    return Blueprint;
}

bool UMCGameplayUIEditorLibrary::RefreshHubWardrobePalettePresentation(UWidgetBlueprint* Blueprint)
{
    if(!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint) || HasDirtyDesignerPackages()
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_HubWardrobe.WBP_HubWardrobe")
        || Blueprint->ParentClass!=UMCHubWardrobeWidget::StaticClass() || !Blueprint->WidgetTree) return false;
    if(Blueprint->WidgetTree->FindWidget(TEXT("WardrobeColorPalette"))) return true;
    // Only the known generated RGB layout may be replaced. A later artist tree
    // without these controls requires its own explicit migration.
    for(const TCHAR* Name:{TEXT("WardrobeRSlider"),TEXT("WardrobeGSlider"),TEXT("WardrobeBSlider")})
        if(!Blueprint->WidgetTree->FindWidget(Name)) return false;
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    TStrongObjectPtr<UMCHubWardrobeWidget> Source(NewObject<UMCHubWardrobeWidget>(GetTransientPackage()));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();Source->BuildPresentationTree();
    if(!Source->WidgetTree || !Source->WidgetTree->FindWidget(TEXT("WardrobeColorPalette"))) return false;
    TStrongObjectPtr<UWidgetBlueprint> Guard(Blueprint);
    TStrongObjectPtr<UWidgetTree> PreviousTree(Blueprint->WidgetTree.Get());
    Blueprint->Modify();
    Blueprint->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors | REN_NonTransactional);
    Blueprint->WidgetTree=DuplicateObject<UWidgetTree>(Source->WidgetTree,Blueprint,TEXT("WidgetTree"));
    Blueprint->WidgetTree->ClearFlags(RF_Transient);Blueprint->WidgetTree->SetFlags(RF_Transactional);
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets);
    Blueprint->WidgetTree->ForEachWidget([SourceWidget=Source.Get(),OldTree=PreviousTree.Get(),&Snapshots](UWidget* Widget) {
        // Keep the existing editable MIC snapshots and artist button styles.
        // Creating a second snapshot under the same package would collide.
        if(auto* Old=OldTree->FindWidget(Widget->GetFName())) {
            if(auto* Border=Cast<UBorder>(Widget)) if(auto* OldBorder=Cast<UBorder>(Old)) Border->SetBrush(OldBorder->Background);
            if(auto* Button=Cast<UButton>(Widget)) if(auto* OldButton=Cast<UButton>(Old)) Button->SetStyle(OldButton->GetStyle());
            if(auto* Image=Cast<UImage>(Widget)) if(auto* OldImage=Cast<UImage>(Old)) Image->SetBrush(OldImage->GetBrush());
        }
        if(!SanitizeAppendedDesignerWidget(Widget,SourceWidget,Snapshots)) Snapshots.bFailed=true;
    });
    if(Snapshots.bFailed) return false;
    RegisterMissingDesignerGuids(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) return false;
    auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) return false;
    Defaults->bUseNativeLayout=false;
    Defaults->StyleBindings=Source->StyleBindings;Defaults->TextBindings=Source->TextBindings;
    Defaults->TranslationTable=Source->TranslationTable;
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    if(!Snapshots.Save()) return false;
    Blueprint->MarkPackageDirty();
    return Assets->SaveLoadedAsset(Blueprint,false);
}

bool UMCGameplayUIEditorLibrary::RefreshHubShopCatalogPresentation(UWidgetBlueprint* Blueprint)
{
    if(!IsInGameThread() || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint)
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_HubShop.WBP_HubShop")
        || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCHubShopWidget::StaticClass())
        || Blueprint->GetPackage()->IsDirty() || !Blueprint->WidgetTree || HasDirtyDesignerPackages()) return false;
    const FName Required[]={TEXT("ShopCatalogHost"),TEXT("ShopCatalogScroll"),TEXT("ShopCatalogGrid"),
        TEXT("ShopBalance"),TEXT("ShopSelectedName"),TEXT("ShopSelectedDescription"),TEXT("ShopTransactionMessage"),TEXT("ShopBuy")};
    if(Blueprint->WidgetTree->FindWidget(TEXT("ShopCatalogGrid"))) {
        for(FName Name:Required) if(!Blueprint->WidgetTree->FindWidget(Name)) return false;
        return true;
    }
    auto* Content=Blueprint->WidgetTree->FindWidget<UVerticalBox>(TEXT("ShopContent"));
    auto* PreviousGrid=Blueprint->WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopEmptySlots"));
    if(!Content || !PreviousGrid || PreviousGrid->GetParent()!=Content) return false;
    for(FName Name:Required) if(Blueprint->WidgetTree->FindWidget(Name)) return false;
    TSet<FName> RemovedPlaceholderNames;
    RemovedPlaceholderNames.Add(PreviousGrid->GetFName());
    UWidgetTree::ForWidgetAndChildren(PreviousGrid,[&RemovedPlaceholderNames](UWidget* Widget) {
        RemovedPlaceholderNames.Add(Widget->GetFName());
    });
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    auto* SavedDefaults=Cast<UMCHubShopWidget>(Blueprint->GeneratedClass?Blueprint->GeneratedClass->GetDefaultObject():nullptr);
    if(!SavedDefaults) return false;

    TStrongObjectPtr<UWidgetBlueprint> Guard(Blueprint);
    TStrongObjectPtr<UMCHubShopWidget> Source(NewObject<UMCHubShopWidget>(GetTransientPackage()));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();
    Source->Theme=SavedDefaults->Theme;Source->TranslationTable=SavedDefaults->TranslationTable;
    Source->StyleBindings=SavedDefaults->StyleBindings;Source->TextBindings=SavedDefaults->TextBindings;
    Source->bUseNativeLayout=SavedDefaults->bUseNativeLayout;
    if(Source->WidgetTree) Source->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    // Start with the ACTUAL saved tree, never the native default layout. Only
    // EnsureCatalogPresentation's known ShopEmptySlots migration may change it.
    Source->WidgetTree=DuplicateObject<UWidgetTree>(Blueprint->WidgetTree,Source.Get(),TEXT("WidgetTree"));
    if(!Source->WidgetTree) return false;
    Source->BuildPresentationTree();
    for(FName Name:Required) if(!Source->WidgetTree->FindWidget(Name)) return false;
    if(Source->WidgetTree->FindWidget(TEXT("ShopEmptySlots"))) return false;

    TStrongObjectPtr<UWidgetTree> Prepared(DuplicateObject<UWidgetTree>(Source->WidgetTree,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetTree::StaticClass(),TEXT("ShopCatalogDesignerSeed"))));
    if(!Prepared.IsValid()) return false;
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets,Prepared.Get());
    Prepared->ForEachWidget([SourceWidget=Source.Get(),OldTree=Blueprint->WidgetTree.Get(),&Snapshots](UWidget* Widget) {
        const auto* Previous=OldTree->FindWidget(Widget->GetFName());
        const bool bWasVariable=Previous && Previous->bIsVariable;
        if(!SanitizeAppendedDesignerWidget(Widget,SourceWidget,Snapshots)) Snapshots.bFailed=true;
        // Existing artist variables/graph bindings are part of the saved shell.
        if(Previous) Widget->bIsVariable=bWasVariable;
    });
    if(Snapshots.bFailed) return false;

    TStrongObjectPtr<UWidgetTree> PreviousTree(Blueprint->WidgetTree.Get());
    Blueprint->Modify();
    PreviousTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    Blueprint->WidgetTree=DuplicateObject<UWidgetTree>(Prepared.Get(),Blueprint,TEXT("WidgetTree"));
    Blueprint->WidgetTree->ClearFlags(RF_Transient);Blueprint->WidgetTree->SetFlags(RF_Transactional);
    // Match the Designer's delete-widget bookkeeping. Only names from the
    // removed placeholder subtree are eligible; keep every surviving widget's
    // GUID and all artist graphs/layout/animation metadata intact.
    const TArray<UWidget*> SourceWidgets=Blueprint->GetAllSourceWidgets();
    for(FName Name:RemovedPlaceholderNames) {
        const bool bStillPresent=SourceWidgets.ContainsByPredicate([Name](const UWidget* Widget) {return Widget->GetFName()==Name;});
        if(!bStillPresent && Blueprint->WidgetVariableNameToGuidMap.Contains(Name)) Blueprint->OnVariableRemoved(Name);
    }
    RegisterMissingDesignerGuids(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) return false;
    auto* Defaults=Cast<UMCHubShopWidget>(Blueprint->GeneratedClass->GetDefaultObject());if(!Defaults) return false;
    Defaults->StyleBindings=Source->StyleBindings;Defaults->TextBindings=Source->TextBindings;
    Defaults->TranslationTable=Source->TranslationTable;
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    if(!Snapshots.Save()) return false;
    Blueprint->MarkPackageDirty();return Assets->SaveLoadedAsset(Blueprint,false);
}

bool UMCGameplayUIEditorLibrary::AddMissingGameplayHUDPresentation(UWidgetBlueprint* Blueprint)
{
    if(!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint) || HasDirtyDesignerPackages()
        || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCGameplayHUD::StaticClass())
        || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Gameplay/UI/"))
        || Blueprint->GetPackage()->IsDirty() || !Blueprint->WidgetTree) return false;
    auto* TargetCanvas=Blueprint->WidgetTree->FindWidget<UCanvasPanel>(TEXT("HUDRoot"));
    if(!TargetCanvas) return false;
    const FName Names[]={TEXT("DirectorMonitor"),TEXT("DirectorCandidates"),TEXT("DirectorLogButton"),TEXT("BoosterWidget"),TEXT("GameplayAlerts")};
    TArray<FName> Missing;
    for(FName Name:Names) if(!Blueprint->WidgetTree->FindWidget(Name)) Missing.Add(Name);
    if(Missing.IsEmpty()) return true;

    TStrongObjectPtr<UMCGameplayHUD> Source(NewObject<UMCGameplayHUD>(GetTransientPackage()));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Source->Initialize();Source->BuildPresentationTree();
    // Duplicate the whole tree first: UMG children are outered to the tree,
    // so duplicating a single panel alone would retain source-owned children.
    TStrongObjectPtr<UWidgetTree> Copy(DuplicateObject<UWidgetTree>(Source->WidgetTree,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetTree::StaticClass(),TEXT("HUDDesignerSeed"))));
    if(!Copy.IsValid()) return false;
    TArray<UWidget*> Roots;
    for(FName Name:Missing) {
        UWidget* Root=Copy->FindWidget(Name);
        if(!Root || !Cast<UCanvasPanelSlot>(Root->Slot)) return false;
        bool Safe=StaticFindObjectFast(UObject::StaticClass(),Blueprint->WidgetTree,Root->GetFName())==nullptr;
        UWidgetTree::ForWidgetAndChildren(Root,[&](UWidget* Widget) {
            if(StaticFindObjectFast(UObject::StaticClass(),Blueprint->WidgetTree,Widget->GetFName())) Safe=false;
        });
        if(!Safe) return false;
        Roots.Add(Root);
    }
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets);
    Blueprint->Modify();Blueprint->WidgetTree->Modify();TargetCanvas->Modify();
    for(UWidget* Root:Roots) {
        const auto* SourceSlot=CastChecked<UCanvasPanelSlot>(Root->Slot);
        const FAnchorData Layout=SourceSlot->GetLayout();
        const bool AutoSize=SourceSlot->GetAutoSize();const int32 ZOrder=SourceSlot->GetZOrder();
        TArray<UWidget*> Children={Root};
        UWidgetTree::ForWidgetAndChildren(Root,[&](UWidget* Widget) {Children.Add(Widget);});
        Root->RemoveFromParent();
        for(UWidget* Widget:Children) {
            if(!UWidgetTree::TryMoveWidgetToNewTree(Widget,Blueprint->WidgetTree)) return false;
            if(!SanitizeAppendedDesignerWidget(Widget,Source.Get(),Snapshots)) return false;
            if(auto* UserWidget=Cast<UUserWidget>(Widget);UserWidget && UserWidget->WidgetTree)
                UserWidget->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Child) {
                    if(!SanitizeAppendedDesignerWidget(Child,Source.Get(),Snapshots)) Snapshots.bFailed=true;
                });
            if(Snapshots.bFailed) return false;
        }
        auto* Slot=TargetCanvas->AddChildToCanvas(Root);
        Slot->SetLayout(Layout);Slot->SetAutoSize(AutoSize);Slot->SetZOrder(ZOrder);
    }
    RegisterMissingDesignerGuids(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error) return false;
    return Snapshots.Save() && Assets->SaveLoadedAsset(Blueprint,false);
}

bool UMCGameplayUIEditorLibrary::RepairEditableHubMaterialSnapshots(UWidgetBlueprint* Blueprint)
{
    if(!IsInGameThread() || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint)
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_Hub.WBP_Hub")
        || Blueprint->ParentClass!=UMCHubWidget::StaticClass() || Blueprint->GetPackage()->IsDirty()
        || !Blueprint->WidgetTree || HasDirtyDesignerPackages()) return false;
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    TStrongObjectPtr<UMCHubWidget> Source(NewObject<UMCHubWidget>(GetTransientPackage()));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();Source->BuildPresentationTree();
    if(!Source->WidgetTree || !Source->WidgetTree->RootWidget) return false;
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets);
    TArray<TFunction<void()>> ApplyRepairs;
    auto Repair=[&](const FSlateBrush& Native,FSlateBrush& Saved,const UWidget* Widget,const TCHAR* Role)
    {
        auto* Dynamic=Cast<UMaterialInstanceDynamic>(Native.GetResourceObject());
        // Only the exact lossy migration signature is eligible. A designer's
        // MIC, alternate material, tint, image size and rounded brush stay intact.
        if(!Dynamic || Saved.GetResourceObject()!=Dynamic->GetMaterial()) return false;
        FSlateBrush Captured=Native;
        if(!Snapshots.Brush(Captured,Widget,Role)) return false;
        Saved.SetResourceObject(Captured.GetResourceObject());return true;
    };
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        auto* Native=Source->WidgetTree->FindWidget(Widget->GetFName());if(!Native) return;
        if(auto* Button=Cast<UButton>(Widget)) {
            const auto* NativeButton=Cast<UButton>(Native);if(!NativeButton) return;
            auto Style=Button->GetStyle();const auto& NativeStyle=NativeButton->GetStyle();
            bool Changed=Repair(NativeStyle.Normal,Style.Normal,Widget,TEXT("Normal"));
            Changed|=Repair(NativeStyle.Hovered,Style.Hovered,Widget,TEXT("Hovered"));
            Changed|=Repair(NativeStyle.Pressed,Style.Pressed,Widget,TEXT("Pressed"));
            Changed|=Repair(NativeStyle.Disabled,Style.Disabled,Widget,TEXT("Disabled"));
            if(Changed) ApplyRepairs.Add([Button,Style] {Button->Modify();Button->SetStyle(Style);});
        }
        if(auto* Border=Cast<UBorder>(Widget)) {
            const auto* NativeBorder=Cast<UBorder>(Native);if(!NativeBorder) return;
            auto Brush=Border->Background;
            if(Repair(NativeBorder->Background,Brush,Widget,TEXT("Background")))
                ApplyRepairs.Add([Border,Brush] {Border->Modify();Border->SetBrush(Brush);});
        }
        if(auto* Image=Cast<UImage>(Widget)) {
            const auto* NativeImage=Cast<UImage>(Native);if(!NativeImage) return;
            auto Brush=Image->GetBrush();
            if(Repair(NativeImage->GetBrush(),Brush,Widget,TEXT("Image")))
                ApplyRepairs.Add([Image,Brush] {Image->Modify();Image->SetBrush(Brush);});
        }
    });
    if(Snapshots.bFailed) return false;
    const auto* ExistingDefaults=Blueprint->GeneratedClass?Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject()):nullptr;
    if(!ExistingDefaults) return false;
    TArray<FMCUIStyleBinding> MissingBindings;
    for(const auto& Binding:Source->StyleBindings)
        if(!ExistingDefaults->StyleBindings.ContainsByPredicate([&Binding](const FMCUIStyleBinding& Existing)
            {return Existing.WidgetName==Binding.WidgetName && Existing.Target==Binding.Target;})) MissingBindings.Add(Binding);
    if(ApplyRepairs.IsEmpty() && MissingBindings.IsEmpty()) return true;
    // No target mutation occurs until every snapshot and collision check passes.
    if(!Snapshots.Save()) return false;
    Blueprint->Modify();Blueprint->WidgetTree->Modify();
    for(auto& Apply:ApplyRepairs) Apply();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error) return false;
    auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) return false;
    Defaults->Modify();Defaults->StyleBindings.Append(MissingBindings);
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    UE_LOG(LogTemp,Display,TEXT("MC_UI_SNAPSHOT Repaired %d Hub controls and added %d missing palette bindings; existing bindings preserved"),ApplyRepairs.Num(),MissingBindings.Num());
    return Assets->SaveLoadedAsset(Blueprint,false);
}

bool UMCGameplayUIEditorLibrary::RefreshMainMenuSettingsPresentation(UWidgetBlueprint* Blueprint)
{
    if(!IsInGameThread() || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint)
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu")
        || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCMainMenuWidget::StaticClass())
        || Blueprint->GetPackage()->IsDirty() || !Blueprint->WidgetTree || HasDirtyDesignerPackages()) return false;
    auto* Page=Blueprint->WidgetTree->FindWidget<UVerticalBox>(TEXT("MenuSettings_Page"));
    if(!Page) return false;
    const FName Marker(TEXT("MenuSettings_Sections"));
    if(auto* Existing=Blueprint->WidgetTree->FindWidget(Marker)) {
        int32 Index=INDEX_NONE;
        return Cast<UWidgetSwitcher>(Existing) && UWidgetTree::FindWidgetChild(Page,Marker,Index)==Existing;
    }
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    TStrongObjectPtr<UMCMainMenuWidget> Source(NewObject<UMCMainMenuWidget>(GetTransientPackage()));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();Source->BuildPresentationTree();
    TStrongObjectPtr<UWidgetTree> Copy(DuplicateObject<UWidgetTree>(Source->WidgetTree,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetTree::StaticClass(),TEXT("MainMenuSettingsSeed"))));
    auto* SourcePage=Copy->FindWidget<UVerticalBox>(TEXT("MenuSettings_Page"));
    if(!SourcePage || !Copy->FindWidget<UWidgetSwitcher>(Marker)) return false;
    const TCHAR* Tabs[]={TEXT("MenuSettings_TabGraphics"),TEXT("MenuSettings_TabAudio"),TEXT("MenuSettings_TabLanguage"),TEXT("MenuSettings_TabControls")};
    for(const TCHAR* Tab:Tabs) if(!Copy->FindWidget(Tab)) return false;

    struct FChild
    {
        UWidget* Root;
        FMargin Padding;
        FSlateChildSize Size;
        EHorizontalAlignment Horizontal;
        EVerticalAlignment Vertical;
    };
    TArray<FChild> OldChildren,NewChildren;
    TArray<UWidget*> OldWidgets,NewWidgets;
    auto Collect=[](UVerticalBox* From,TArray<FChild>& Children,TArray<UWidget*>& Widgets)
    {
        for(UWidget* Root:From->GetAllChildren()) {
            const auto* Slot=Cast<UVerticalBoxSlot>(Root->Slot);if(!Slot) return false;
            Children.Add({Root,Slot->GetPadding(),Slot->GetSize(),Slot->GetHorizontalAlignment(),Slot->GetVerticalAlignment()});
            Widgets.AddUnique(Root);
            UWidgetTree::ForWidgetAndChildren(Root,[&Widgets](UWidget* Widget) {Widgets.AddUnique(Widget);});
        }
        return true;
    };
    if(!Collect(Page,OldChildren,OldWidgets) || !Collect(SourcePage,NewChildren,NewWidgets) || NewChildren.IsEmpty()) return false;
    for(UWidget* Widget:OldWidgets) if(Widget->GetOuter()!=Blueprint->WidgetTree) return false;
    // Native anonymous wrappers share class-counter names with other pages.
    // Namespace only the copied, unbound wrappers before any target preflight.
    // Explicit presenter/binding names always retain their exact identity.
    for(UWidget* Widget:NewWidgets) {
        if(Widget->GetOuter()!=Copy.Get()) return false;
        const FName OldName=Widget->GetFName();
        if(OldName.ToString().StartsWith(TEXT("MenuSettings_"))) continue;
        if(Source->TextBindings.Contains(OldName) || Source->StyleBindings.ContainsByPredicate(
            [OldName](const FMCUIStyleBinding& Binding) {return Binding.WidgetName==OldName;})) {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_SETTINGS Anonymous wrapper has a binding; preserve its name: %s"),*OldName.ToString());return false;
        }
        const FString NewName=TEXT("MenuSettings_Seed_")+OldName.ToString();
        if(StaticFindObjectFast(UObject::StaticClass(),Copy.Get(),FName(NewName))
            || !Widget->Rename(*NewName,Copy.Get(),REN_DontCreateRedirectors|REN_NonTransactional)) return false;
    }
    for(UWidget* Widget:NewWidgets) {
        if(Widget->GetOuter()!=Copy.Get()) return false;
        auto* Existing=StaticFindObjectFast(UObject::StaticClass(),Blueprint->WidgetTree,Widget->GetFName());
        if(Existing && !OldWidgets.Contains(Cast<UWidget>(Existing))) {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_SETTINGS Unrelated named object preserved: %s"),*Existing->GetPathName());return false;
        }
    }
    // Snapshot resources and sanitize only the new subtree while the target is
    // untouched. Name/material collisions therefore fail before page mutation.
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets,Copy.Get());
    for(UWidget* Widget:NewWidgets) if(!SanitizeAppendedDesignerWidget(Widget,Source.Get(),Snapshots)) return false;
    TStrongObjectPtr<UWidgetTree> Retired(NewObject<UWidgetTree>(GetTransientPackage(),NAME_None,RF_Transient));
    auto* Holder=Retired->ConstructWidget<UVerticalBox>();Retired->RootWidget=Holder;
    auto Attach=[](UVerticalBox* Parent,const TArray<FChild>& Children)
    {
        for(const auto& Child:Children) {
            Child.Root->RemoveFromParent();
            auto* Slot=Parent->AddChildToVerticalBox(Child.Root);
            Slot->SetPadding(Child.Padding);Slot->SetSize(Child.Size);
            Slot->SetHorizontalAlignment(Child.Horizontal);Slot->SetVerticalAlignment(Child.Vertical);
        }
    };
    const auto PreviousGuids=Blueprint->WidgetVariableNameToGuidMap;
    auto RollBack=[&]
    {
        Page->ClearChildren();
        for(UWidget* Widget:NewWidgets)
            if(Widget->GetOuter()==Blueprint->WidgetTree) UWidgetTree::TryMoveWidgetToNewTree(Widget,Copy.Get());
        Attach(SourcePage,NewChildren);
        for(UWidget* Widget:OldWidgets)
            if(Widget->GetOuter()==Retired.Get()) UWidgetTree::TryMoveWidgetToNewTree(Widget,Blueprint->WidgetTree);
        Attach(Page,OldChildren);
        Blueprint->WidgetVariableNameToGuidMap=PreviousGuids;
        UE_LOG(LogTemp,Error,TEXT("MC_UI_SETTINGS Restored original Settings children after failed migration; no WBP saved"));
    };
    Blueprint->Modify();Blueprint->WidgetTree->Modify();Page->Modify();
    Page->ClearChildren();
    // Retire the old root AND descendants before reusing stable widget names.
    for(UWidget* Widget:OldWidgets)
        if(!UWidgetTree::TryMoveWidgetToNewTree(Widget,Retired.Get())) {RollBack();return false;}
    for(const auto& Child:OldChildren) Holder->AddChild(Child.Root);
    for(const auto& Child:NewChildren) Child.Root->RemoveFromParent();
    for(UWidget* Widget:NewWidgets)
        if(!UWidgetTree::TryMoveWidgetToNewTree(Widget,Blueprint->WidgetTree)) {RollBack();return false;}
    Attach(Page,NewChildren);
    RegisterMissingDesignerGuids(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) {RollBack();return false;}
    auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) {RollBack();return false;}
    Defaults->Modify();
    for(const auto& Entry:Source->TextBindings)
        if(Entry.Key.ToString().StartsWith(TEXT("MenuSettings_")) && !Defaults->TextBindings.Contains(Entry.Key))
            Defaults->TextBindings.Add(Entry.Key,Entry.Value);
    for(const auto& Binding:Source->StyleBindings)
        if(Binding.WidgetName.ToString().StartsWith(TEXT("MenuSettings_"))
            && !Defaults->StyleBindings.ContainsByPredicate([&Binding](const FMCUIStyleBinding& Existing)
                {return Existing.WidgetName==Binding.WidgetName && Existing.Target==Binding.Target;})) Defaults->StyleBindings.Add(Binding);
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    if(!Snapshots.Save()) {RollBack();return false;}
    UE_LOG(LogTemp,Display,TEXT("MC_UI_SETTINGS Replaced Settings children only; retained page, slot and all other menu pages"));
    return Assets->SaveLoadedAsset(Blueprint,false);
}

bool UMCGameplayUIEditorLibrary::AddMainMenuVoicePresentation(UWidgetBlueprint* Blueprint)
{
    if(!IsInGameThread() || !GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Blueprint)
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu")
        || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UMCMainMenuWidget::StaticClass())
        || Blueprint->GetPackage()->IsDirty() || !Blueprint->WidgetTree || HasDirtyDesignerPackages()) return false;
    TStrongObjectPtr<UWidgetBlueprint> BlueprintGuard(Blueprint);
    UWidgetTree* Tree=Blueprint->WidgetTree;
    auto* Tabs=Tree->FindWidget<UHorizontalBox>(TEXT("MenuSettings_Tabs"));
    auto* Sections=Tree->FindWidget<UWidgetSwitcher>(TEXT("MenuSettings_Sections"));
    auto* PreviousDefaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass?Blueprint->GeneratedClass->GetDefaultObject():nullptr);
    if(!Tabs || !Sections || !PreviousDefaults) return false;
    const FName TabName(TEXT("MenuSettings_TabVoice")),SectionName(TEXT("MenuSettings_VoiceSection")),PanelName(TEXT("MenuSettings_VoicePanel"));
    auto* ExistingTab=Tree->FindWidget(TabName);auto* ExistingSection=Tree->FindWidget(SectionName);auto* ExistingPanel=Tree->FindWidget(PanelName);
    if(ExistingTab || ExistingSection || ExistingPanel) {
        int32 Index=INDEX_NONE;
        // Partial/custom trees are never completed by guessing at their intent.
        return Cast<UMCMainMenuActionButton>(ExistingTab) && ExistingTab->GetParent()==Tabs
            && Cast<UScrollBox>(ExistingSection) && Sections->GetChildIndex(ExistingSection)==4
            && Cast<UMCVoiceSettingsWidget>(ExistingPanel)
            && UWidgetTree::FindWidgetChild(Cast<UPanelWidget>(ExistingSection),PanelName,Index)==ExistingPanel;
    }
    if(Tabs->GetChildrenCount()!=4 || Sections->GetChildrenCount()!=4) return false;
    const TCHAR* Names[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Language"),TEXT("Controls")};
    for(int32 I=0;I<4;++I) {
        const auto* Tab=Tree->FindWidget(FName(*(FString(TEXT("MenuSettings_Tab"))+Names[I])));
        const auto* Section=Tree->FindWidget(FName(*(FString(TEXT("MenuSettings_"))+Names[I]+TEXT("Section"))));
        if(!Tab || Tabs->GetChildAt(I)!=Tab || !Section || Sections->GetChildAt(I)!=Section) return false;
    }
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    const auto PreviousStyle=PreviousDefaults->StyleBindings;const auto PreviousText=PreviousDefaults->TextBindings;
    TStrongObjectPtr<UMCMainMenuWidget> Source(NewObject<UMCMainMenuWidget>(GetTransientPackage()));
    Source->Theme=PreviousDefaults->Theme;Source->TranslationTable=PreviousDefaults->TranslationTable;
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();Source->BuildPresentationTree();
    TStrongObjectPtr<UWidgetTree> Copy(DuplicateObject<UWidgetTree>(Source->WidgetTree,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetTree::StaticClass(),TEXT("MainMenuVoiceSeed"))));
    auto* Tab=Copy->FindWidget<UMCMainMenuActionButton>(TabName);
    auto* Section=Copy->FindWidget<UScrollBox>(SectionName);
    auto* Voice=Copy->FindWidget<UMCVoiceSettingsWidget>(PanelName);
    // WidgetTree is DuplicateTransient on UUserWidget. This detached seed copy
    // is outside engine widget-tree initialization, so initialize its nested
    // presenter explicitly before validating and moving the new branch.
    if(Voice) {Voice->SetDesignerFlags(EWidgetDesignFlags::Designing);Voice->Initialize();Voice->BuildPresentationTree();}
    if(!Tab || !Section || !Voice || !Voice->WidgetTree || !Voice->WidgetTree->RootWidget) return false;
    const auto* TabSlot=Cast<UHorizontalBoxSlot>(Tab->Slot);if(!TabSlot) return false;
    const auto Padding=TabSlot->GetPadding();const auto Size=TabSlot->GetSize();
    const auto Horizontal=TabSlot->GetHorizontalAlignment();const auto Vertical=TabSlot->GetVerticalAlignment();
    TArray<UWidget*> NewWidgets;
    for(UWidget* Root:{static_cast<UWidget*>(Tab),static_cast<UWidget*>(Section)}) {
        NewWidgets.AddUnique(Root);
        UWidgetTree::ForWidgetAndChildren(Root,[&NewWidgets](UWidget* Child) {NewWidgets.AddUnique(Child);});
    }
    for(UWidget* Widget:NewWidgets) {
        if(Widget->GetOuter()!=Copy.Get()) return false;
        const FName Name=Widget->GetFName();
        if(!Name.ToString().StartsWith(TEXT("MenuSettings_"))) {
            if(Source->TextBindings.Contains(Name) || Source->StyleBindings.ContainsByPredicate(
                [Name](const FMCUIStyleBinding& Binding) {return Binding.WidgetName==Name;})) return false;
            const FString NewName=TEXT("MenuSettings_VoiceSeed_")+Name.ToString();
            if(StaticFindObjectFast(UObject::StaticClass(),Copy.Get(),FName(NewName))
                || !Widget->Rename(*NewName,Copy.Get(),REN_DontCreateRedirectors|REN_NonTransactional)) return false;
        }
        if(StaticFindObjectFast(UObject::StaticClass(),Tree,Widget->GetFName())) return false;
    }
    FDesignerMaterialSnapshots Snapshots(Blueprint,Assets,Copy.Get());
    for(UWidget* Widget:NewWidgets) {
        if(!SanitizeAppendedDesignerWidget(Widget,Source.Get(),Snapshots)) return false;
        if(auto* Nested=Cast<UUserWidget>(Widget);Nested && Nested->WidgetTree) {
            Nested->WidgetTree->ClearFlags(RF_Transient);Nested->WidgetTree->SetFlags(RF_Transactional);
            Nested->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Child) {
                if(!SanitizeAppendedDesignerWidget(Child,Source.Get(),Snapshots)) Snapshots.bFailed=true;
            });
            if(Snapshots.bFailed) return false;
        }
    }
    const auto PreviousGuids=Blueprint->WidgetVariableNameToGuidMap;
    const int32 Active=Sections->GetActiveWidgetIndex();
    auto RollBack=[&] {
        Tab->RemoveFromParent();Section->RemoveFromParent();
        for(UWidget* Widget:NewWidgets) if(Widget->GetOuter()==Tree) UWidgetTree::TryMoveWidgetToNewTree(Widget,Copy.Get());
        Sections->SetActiveWidgetIndex(Active);Blueprint->WidgetVariableNameToGuidMap=PreviousGuids;
        if(auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass?Blueprint->GeneratedClass->GetDefaultObject():nullptr)) {
            Defaults->StyleBindings=PreviousStyle;Defaults->TextBindings=PreviousText;
            CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
        }
        UE_LOG(LogTemp,Error,TEXT("MC_UI_VOICE Restored original four-tab tree after failed append; no WBP saved"));
    };
    Blueprint->Modify();Tree->Modify();Tabs->Modify();Sections->Modify();
    Tab->RemoveFromParent();Section->RemoveFromParent();
    for(UWidget* Widget:NewWidgets) if(!UWidgetTree::TryMoveWidgetToNewTree(Widget,Tree)) {RollBack();return false;}
    auto* NewSlot=Tabs->AddChildToHorizontalBox(Tab);NewSlot->SetPadding(Padding);NewSlot->SetSize(Size);
    NewSlot->SetHorizontalAlignment(Horizontal);NewSlot->SetVerticalAlignment(Vertical);
    Sections->AddChild(Section);Sections->SetActiveWidgetIndex(Active);
    RegisterMissingDesignerGuids(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) {RollBack();return false;}
    auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) {RollBack();return false;}
    Defaults->Modify();
    for(const auto& Entry:Source->TextBindings)
        if(NewWidgets.Contains(Tree->FindWidget(Entry.Key)) && !Defaults->TextBindings.Contains(Entry.Key)) Defaults->TextBindings.Add(Entry.Key,Entry.Value);
    for(const auto& Binding:Source->StyleBindings)
        if(NewWidgets.Contains(Tree->FindWidget(Binding.WidgetName)) && !Defaults->StyleBindings.ContainsByPredicate(
            [&Binding](const FMCUIStyleBinding& Existing) {return Existing.WidgetName==Binding.WidgetName && Existing.Target==Binding.Target;})) Defaults->StyleBindings.Add(Binding);
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    if(!Snapshots.Save()) {RollBack();return false;}
    if(!Assets->SaveLoadedAsset(Blueprint,false)) {RollBack();return false;}
    UE_LOG(LogTemp,Display,TEXT("MC_UI_VOICE Added Voice tab and section; retained all existing controls and materials"));
    return true;
}

bool UMCGameplayUIEditorLibrary::UseAuthoredMainMenu(UWidgetBlueprint* Host,TSubclassOf<UMCMainMenuWidget> MenuClass)
{
    if(!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !IsValid(Host) || !MenuClass
        || Host->GetPathName()!=TEXT("/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D")
        || Host->GetPackage()->IsDirty()) return false;
    TArray<UEdGraph*> Graphs;Host->GetAllGraphs(Graphs);
    TArray<UEdGraphPin*> Pins;bool AlreadyAuthored=false;
    for(UEdGraph* Graph:Graphs) for(UEdGraphNode* Node:Graph->Nodes)
        if(Node && Node->GetClass()->GetFName()==TEXT("K2Node_CreateWidget"))
            if(auto* Pin=Node->FindPin(TEXT("Class"));Pin && Pin->LinkedTo.IsEmpty()) {
                if(Pin->DefaultObject==UMCMainMenuWidget::StaticClass()) Pins.Add(Pin);
                else if(Pin->DefaultObject==MenuClass.Get()) AlreadyAuthored=true;
            }
    if(Pins.IsEmpty()) return AlreadyAuthored;
    Host->Modify();
    for(UEdGraphPin* Pin:Pins) {
        Pin->GetOwningNode()->Modify();
        Pin->GetSchema()->TrySetDefaultObject(*Pin,MenuClass.Get());
        if(Pin->DefaultObject!=MenuClass.Get()) return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Host);
    FKismetEditorUtilities::CompileBlueprint(Host);
    if(Host->Status==BS_Error) return false;
    return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(Host,false);
}

bool UMCGameplayUIEditorLibrary::ValidateGameplayUILocalization(TArray<FString>& OutReport)
{
    OutReport.Reset();
    auto& I18N=FInternationalization::Get();
    const FString LanguageBefore=I18N.GetCurrentLanguage()->GetName();
    const FString LocaleBefore=I18N.GetCurrentLocale()->GetName();
    int32 Passed=0,Failed=0;
    auto Check=[&](const FString& Label,const FString& Actual,const FString& Expected)
    {
        const bool bPass=Actual==Expected;
        if(bPass) ++Passed;else ++Failed;
        OutReport.Add(FString::Printf(TEXT("%s %s: expected [%s], got [%s]"),bPass?TEXT("PASS"):TEXT("FAIL"),*Label,*Expected,*Actual));
    };
    auto Format=[&](const TCHAR* Pattern,const FFormatNamedArguments& Arguments,const TCHAR* Culture)
    {
        return UMCUIDataLibrary::FormatTextForCulture(FText::AsCultureInvariant(Pattern),Arguments,Culture).ToString();
    };
    const FText GlobalPattern=FText::AsCultureInvariant(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"));
    const FFormatNamedArguments GlobalArguments{{TEXT("N"),int64(2)}};
    const FString EnginePluralBefore=FText::Format(GlobalPattern,GlobalArguments).ToString();
    const FText GlobalOrdinal=FText::AsCultureInvariant(TEXT("{N}|ordinal(one=one,two=two,few=few,other=other)"));
    const FString EngineOrdinalBefore=FText::Format(GlobalOrdinal,GlobalArguments).ToString();
    struct FCase {double Value;const TCHAR* RU;const TCHAR* EN;const TCHAR* ENOrdinal;};
    const FCase Cases[]=
    {
        {0,TEXT("many"),TEXT("other"),TEXT("other")},
        {1,TEXT("one"),TEXT("one"),TEXT("one")},
        {2,TEXT("few"),TEXT("other"),TEXT("two")},
        {5,TEXT("many"),TEXT("other"),TEXT("other")},
        {11,TEXT("many"),TEXT("other"),TEXT("other")},
        {21,TEXT("one"),TEXT("other"),TEXT("one")},
        {1.5,TEXT("other"),TEXT("other"),TEXT("other")}
    };
    for(const auto& Case:Cases)
    {
        const FFormatArgumentValue Number=Case.Value==1.5?FFormatArgumentValue(Case.Value):FFormatArgumentValue(int64(Case.Value));
        const FFormatNamedArguments Arguments{{TEXT("N"),Number}};
        const FString Label=FString::Printf(TEXT("%g"),Case.Value);
        Check(TEXT("RU cardinal ")+Label,Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),Arguments,TEXT("ru")),Case.RU);
        Check(TEXT("EN cardinal ")+Label,Format(TEXT("{N}|plural(one=one,other=other)"),Arguments,TEXT("en")),Case.EN);
        Check(TEXT("RU ordinal ")+Label,Format(TEXT("{N}|ordinal(other=other)"),Arguments,TEXT("ru")),TEXT("other"));
        Check(TEXT("EN ordinal ")+Label,Format(TEXT("{N}|ordinal(one=one,two=two,few=few,other=other)"),Arguments,TEXT("en")),Case.ENOrdinal);
    }
    Check(TEXT("RU decimal"),UMCUIDataLibrary::DecimalForCulture(1.5,1,TEXT("ru")).ToString(),TEXT("1,5"));
    Check(TEXT("EN decimal"),UMCUIDataLibrary::DecimalForCulture(1.5,1,TEXT("en")).ToString(),TEXT("1.5"));
    Check(TEXT("RU numeric argument"),Format(TEXT("{N}"),FFormatNamedArguments{{TEXT("N"),1.5}},TEXT("ru")),TEXT("1,5"));
    Check(TEXT("EN numeric argument"),Format(TEXT("{N}"),FFormatNamedArguments{{TEXT("N"),1.5}},TEXT("en")),TEXT("1.5"));
    const FText NumericText=UMCUIDataLibrary::NumberForCulture(21,TEXT("en"));
    Check(TEXT("Historic numeric argument"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),NumericText}},TEXT("ru")),TEXT("one"));
    Check(TEXT("Nested format argument"),Format(TEXT("{N}|plural(one=\"{Who}: один\",few=\"{Who}: несколько\",many=\"{Who}: много\",other=\"{Who}: часть\")"),
        FFormatNamedArguments{{TEXT("N"),int64(2)},{TEXT("Who"),FText::AsCultureInvariant(TEXT("Зуб"))}},TEXT("ru")),TEXT("Зуб: несколько"));
    Check(TEXT("Nested plural/ordinal"),Format(TEXT("{N}|plural(one=\"{M}|ordinal(one=first,two=second,few=third,other=rest)\",other=\"none\")"),
        FFormatNamedArguments{{TEXT("N"),int64(1)},{TEXT("M"),int64(2)}},TEXT("en")),TEXT("second"));
    Check(TEXT("Escaped modifier"),Format(TEXT("{N}`|plural(one=ONE,other=OTHER)"),FFormatNamedArguments{{TEXT("N"),int64(1)}},TEXT("en")),TEXT("1|plural(one=ONE,other=OTHER)"));
    Check(TEXT("Escaped argument"),Format(TEXT("`{N`} {N}"),FFormatNamedArguments{{TEXT("N"),int64(2)}},TEXT("en")),TEXT("{N} 2"));
    Check(TEXT("Literal modifier-like text"),Format(TEXT("literal |plural("),FFormatNamedArguments(),TEXT("en")),TEXT("literal |plural("));
    Check(TEXT("Regional culture"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),int64(21)}},TEXT("ru-RU")),TEXT("one"));
    Check(TEXT("Invalid culture falls back to RU"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),int64(2)}},TEXT("invalid-ui-culture")),TEXT("few"));
    Check(TEXT("Editor language unchanged"),I18N.GetCurrentLanguage()->GetName(),LanguageBefore);
    Check(TEXT("Editor locale unchanged"),I18N.GetCurrentLocale()->GetName(),LocaleBefore);
    Check(TEXT("Engine plural unchanged"),FText::Format(GlobalPattern,GlobalArguments).ToString(),EnginePluralBefore);
    Check(TEXT("Engine ordinal unchanged"),FText::Format(GlobalOrdinal,GlobalArguments).ToString(),EngineOrdinalBefore);
    OutReport.Insert(FString::Printf(TEXT("Gameplay UI localization: %d passed, %d failed; language=%s locale=%s"),Passed,Failed,*LanguageBefore,*LocaleBefore),0);
    UE_LOG(LogTemp,Display,TEXT("MC_UI_LOCALIZATION %s"),*OutReport[0]);
    // Python hides OutReport when the bool return is false, so retain every actionable comparison in the log.
    for(const FString& Entry:OutReport)
        if(Entry.StartsWith(TEXT("FAIL "),ESearchCase::CaseSensitive))
            UE_LOG(LogTemp,Warning,TEXT("MC_UI_LOCALIZATION %s"),*Entry);
    return Failed==0;
}

bool UMCGameplayUIEditorLibrary::ConfigureBossNameCJKFallback(UFont* NameFont,UFontFace* FallbackFace)
{
    if(!IsValid(NameFont) || !IsValid(FallbackFace) || NameFont->FontCacheType!=EFontCacheType::Runtime
        || (GEditor && GEditor->PlayWorld) || NameFont->GetInternalCompositeFont().DefaultTypeface.Fonts.IsEmpty())
    {
        UE_LOG(LogTemp,Warning,TEXT("MC_BOSS_CJK_FALLBACK Invalid font/face, empty default typeface, or active PIE"));
        return false;
    }
    // The empty culture list is deliberate: an explicit PreviewCulture does
    // not change global culture, and these scripts need a real glyph face in
    // every preview/editor locale rather than the diagnostic last-resort font.
    static const FName EntryName(TEXT("MC_Boss_CJK_Fallback"));
    FProperty* Property=FindFProperty<FProperty>(UFont::StaticClass(),UFont::GetInternalCompositeFontPropertyName());
    NameFont->Modify();NameFont->PreEditChange(Property);
    auto& Composite=NameFont->GetMutableInternalCompositeFont();
    FCompositeSubFont* SubFont=Composite.SubTypefaces.FindByPredicate([](const FCompositeSubFont& Entry){return Entry.EditorName==EntryName;});
    if(!SubFont) SubFont=&Composite.SubTypefaces.AddDefaulted_GetRef();
    SubFont->EditorName=EntryName;SubFont->Cultures.Reset();SubFont->ScalingFactor=1.f;
    SubFont->Typeface.Fonts.Reset();
    auto& FaceEntry=SubFont->Typeface.Fonts.AddDefaulted_GetRef();FaceEntry.Name=TEXT("Default");FaceEntry.Font=FFontData(FallbackFace);
    SubFont->CharacterRanges.Reset();
    // BMP ranges covered by the bundled Droid Sans Fallback. Latin/Cyrillic,
    // numeric values and punctuation outside these ranges retain the default.
    const int32 Ranges[][2]={{0x1100,0x11FF},{0x2E80,0x2FFF},{0x3000,0x31FF},{0x3200,0x33FF},
        {0x3400,0x4DBF},{0x4E00,0x9FFF},{0xA960,0xA97F},{0xAC00,0xD7AF},{0xD7B0,0xD7FF},
        {0xF900,0xFAFF},{0xFE10,0xFE1F},{0xFE30,0xFE4F},{0xFF00,0xFFEF}};
    for(const auto& Range:Ranges) SubFont->CharacterRanges.Add(FInt32Range::Inclusive(Range[0],Range[1]));
    Composite.MakeDirty();
    FPropertyChangedEvent Changed(Property,EPropertyChangeType::ValueSet);NameFont->PostEditChangeProperty(Changed);
    NameFont->MarkPackageDirty();
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().GetRenderer()->FlushFontCache(TEXT("MC boss CJK sub-typeface configured"));
    UE_LOG(LogTemp,Display,TEXT("MC_BOSS_CJK_FALLBACK %s face=%s ranges=%d default_faces=%d"),
        *NameFont->GetPathName(),*FallbackFace->GetPathName(),SubFont->CharacterRanges.Num(),Composite.DefaultTypeface.Fonts.Num());
    return true;
}
