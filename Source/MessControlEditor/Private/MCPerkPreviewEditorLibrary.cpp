#include "MCPerkPreviewEditorLibrary.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXRecipeComposer.h"
#include "MCVFXActionWorkspace.h"
#include "MCVFXRecipePreset.h"
#include "MCActiveSkillAction.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCPerkTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraEmitterInstance.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace
{
    TSharedRef<FJsonObject> VectorJson(const FVector& V)
    {
        auto O=MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("x"),V.X);O->SetNumberField(TEXT("y"),V.Y);O->SetNumberField(TEXT("z"),V.Z);
        return O;
    }

    bool ValidateSavedVisualSource(const UObject* Object,const TCHAR* Label,FString& Error)
    {
        if(!IsValid(Object) || Object->HasAnyFlags(RF_Transient) || Object->IsEditorOnly()
            || Object->GetClass()->GetOutermost()->GetName()==TEXT("/Script/MessControlEditor")
            || Object->GetOutermost()==GetTransientPackage()
            || !FPackageName::DoesPackageExist(Object->GetOutermost()->GetName())
            || Object->GetOutermost()->IsDirty()) {
            Error=FString::Printf(TEXT("%s должен быть сохранённым runtime-ассетом без несохранённых изменений; временный preview не назначается игре."),Label);
            return false;
        }
        return true;
    }

    bool ValidateSavedProfileResources(const UMCPerkVFXProfile* Profile,FString& Error)
    {
        if(!Profile || !Profile->Validate(Error))return false;
        for(const auto& Pair:Profile->Slots) {
            const auto& Slot=Pair.Value;
            if(Slot.Mode==EMCPerkVFXSlotMode::Replace) {
                auto* System=Slot.System.LoadSynchronous();
                if(!ValidateSavedVisualSource(System,TEXT("Niagara System"),Error)
                    || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error))
                {Error=Pair.Key.ToString()+TEXT(": ")+Error;return false;}
            }
            else if(Slot.Mode==EMCPerkVFXSlotMode::Composition) {
                auto* Composition=Slot.Composition.LoadSynchronous();
                if(!ValidateSavedVisualSource(Composition,TEXT("Композиция VFX"),Error))
                {Error=Pair.Key.ToString()+TEXT(": ")+Error;return false;}
                if(Composition->IsA<UMCVFXRecipePreset>()) {
                    Error=Pair.Key.ToString()+TEXT(": авторский пресет редактора нельзя назначить runtime-слоту. Сохраните отдельную runtime-композицию.");return false;
                }
                if(!Composition->Validate(Error)){Error=Pair.Key.ToString()+TEXT(": ")+Error;return false;}
                for(const auto& Layer:Composition->Layers) {
                    // A disabled blank layer is harmless. Any retained resource
                    // reference must still resolve to a persistent runtime asset.
                    if(!Layer.bEnabled && Layer.System.IsNull())continue;
                    auto* System=Layer.System.LoadSynchronous();
                    if(!ValidateSavedVisualSource(System,TEXT("Niagara слоя композиции"),Error)
                        || !UMCVFXComposition::ValidateParameters(Layer,System,Error)
                        || (Layer.bEnabled && !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error)))
                    {Error=Pair.Key.ToString()+TEXT(" / ")+Layer.Name.ToString()+TEXT(": ")+Error;return false;}
                }
            }
        }
        return true;
    }
}

void UMCPerkPreviewEditorLibrary::OpenPerkPreview() { MCPerkActionPreviewPanel::Open(); }
void UMCPerkPreviewEditorLibrary::OpenVFXComposer() {MCVFXActionWorkspace::Open();}
void UMCPerkPreviewEditorLibrary::OpenVFXElements() {MCVFXRecipeComposer::Open();}
FString UMCPerkPreviewEditorLibrary::GetActionWorkspaceSnapshotJson() {return MCVFXActionWorkspace::GetSnapshotJson();}
bool UMCPerkPreviewEditorLibrary::ImportActionEventRecipe(FName ActionID,FName Stage,UMCVFXComposition* Source)
{
    if(!Source || Source->GetClass()!=UMCVFXRecipePreset::StaticClass() || Source->HasAnyFlags(RF_Transient)
        || !Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || !FPackageName::DoesPackageExist(Source->GetOutermost()->GetName()) || Source->GetOutermost()->IsDirty()) {
        UE_LOG(LogTemp,Warning,TEXT("Composer event import requires a clean saved UMCVFXRecipePreset master; no draft was changed."));return false;
    }
    FString Error;
    if(!MCVFXActionWorkspace::ImportEventRecipe(ActionID,Stage,Source,Error)) {
        UE_LOG(LogTemp,Warning,TEXT("Composer event import refused: %s"),*Error);return false;
    }
    return true;
}
bool UMCPerkPreviewEditorLibrary::OpenComposerForRocketStage(FName Stage)
{
    if(Stage!=TEXT("Start")&&Stage!=TEXT("Loop")&&Stage!=TEXT("Impact")&&Stage!=TEXT("End"))return false;
    FString Error;if(!MCPerkActionPreviewPanel::EnsureContext(TEXT("Consumable_Rocket"),Error))return false;
    auto* Profile=MCPerkActionPreviewPanel::GetWorkingProfile();if(!Profile)return false;
    MCVFXRecipeComposer::OpenForSlot(TEXT("Consumable_Rocket"),Stage,Profile);return true;
}
UMCVFXComposition* UMCPerkPreviewEditorLibrary::GetWorkingVFXRecipe() {return MCVFXRecipeComposer::GetWorkingRecipe();}
bool UMCPerkPreviewEditorLibrary::ComposerAddSystem(UNiagaraSystem* System) {return MCVFXRecipeComposer::AddSystem(System);}
bool UMCPerkPreviewEditorLibrary::ComposerLoadRecipe(UMCVFXComposition* Source) {return MCVFXRecipeComposer::LoadComposition(Source);}
UMCVFXComposition* UMCPerkPreviewEditorLibrary::SaveWorkingVFXRecipeAs(const FString& ObjectPath,FString& OutError)
{return MCVFXRecipeComposer::SaveWorkingRecipeAs(ObjectPath,OutError);}
UNiagaraSystem* UMCPerkPreviewEditorLibrary::ExportWorkingVFXRecipe(const FString& PackageName,FString& OutError)
{return MCVFXRecipeComposer::ExportWorkingRecipe(PackageName,OutError);}
bool UMCPerkPreviewEditorLibrary::ComposerApplyToContext(FString& OutError)
{return MCVFXRecipeComposer::ApplyToContext(OutError);}
FString UMCPerkPreviewEditorLibrary::GetVFXComposerSnapshotJson()
{
    auto Data=MakeShared<FJsonObject>();auto* Recipe=MCVFXRecipeComposer::GetWorkingRecipe();
    Data->SetBoolField(TEXT("tool_open"),Recipe!=nullptr);
    if(Recipe)
    {
        FString Error;Data->SetBoolField(TEXT("layout_valid"),Recipe->ValidateLayout(Error));
        Data->SetStringField(TEXT("layout_error"),Error);Data->SetStringField(TEXT("name"),Recipe->PresetName);
        Data->SetNumberField(TEXT("duration"),Recipe->DurationSeconds);Data->SetNumberField(TEXT("tail"),Recipe->TailSeconds);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for(int32 I=0;I<Recipe->Layers.Num();++I)
        {
            auto N=MakeShared<FJsonObject>();const auto& L=Recipe->Layers[I];
            N->SetStringField(TEXT("name"),L.Name.ToString());N->SetStringField(TEXT("system"),L.System.ToString());
            N->SetBoolField(TEXT("enabled"),L.bEnabled);N->SetBoolField(TEXT("solo"),L.bSolo);
            N->SetNumberField(TEXT("delay"),L.StartSeconds);N->SetNumberField(TEXT("emission"),L.EmissionSeconds);
            if(Recipe->Nodes.IsValidIndex(I))
            {
                const auto& Layout=Recipe->Nodes[I];N->SetStringField(TEXT("id"),Layout.ID.ToString());
                N->SetStringField(TEXT("group"),Layout.Group.ToString());N->SetNumberField(TEXT("x"),Layout.Position.X);N->SetNumberField(TEXT("y"),Layout.Position.Y);
            }
            Nodes.Add(MakeShared<FJsonValueObject>(N));
        }
        Data->SetArrayField(TEXT("nodes"),Nodes);Data->SetNumberField(TEXT("group_count"),Recipe->Groups.Num());
    }
    FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
bool UMCPerkPreviewEditorLibrary::RunRocketScenario(FName Scenario)
{return RunActionScenario(TEXT("Consumable_Rocket"),Scenario);}
bool UMCPerkPreviewEditorLibrary::RunActionScenario(FName ActionID,FName Scenario)
{
    EMCPerkPreviewScenario Mode;
    if(Scenario==TEXT("Target")) Mode=EMCPerkPreviewScenario::Target;
    else if(Scenario==TEXT("NearWall")) Mode=EMCPerkPreviewScenario::NearWall;
    else if(Scenario==TEXT("Timeout")) Mode=EMCPerkPreviewScenario::Timeout;
    else return false;
    FString Error;
    return MCPerkActionPreviewPanel::EnsureContext(ActionID,Error) && MCPerkActionPreviewPanel::RunScenario(Mode);
}
void UMCPerkPreviewEditorLibrary::ResetPerkPreview() { MCPerkActionPreviewPanel::ResetPreview(); }
void UMCPerkPreviewEditorLibrary::PausePerkPreview(bool bPaused)
{
    if(auto Runner=MCPerkActionPreviewPanel::GetActiveRunner()) Runner->SetPaused(bPaused);
}
UMCPerkVFXProfile* UMCPerkPreviewEditorLibrary::GetWorkingVFXProfile()
{ return MCPerkActionPreviewPanel::GetWorkingProfile(); }

UMCPerkVFXProfile* UMCPerkPreviewEditorLibrary::SaveWorkingProfileAs(const FString& ObjectPath,FString& OutError)
{
    OutError.Reset();auto* Working=GetWorkingVFXProfile();
    if(!Working || !ValidateSavedProfileResources(Working,OutError)) {if(OutError.IsEmpty()) OutError=TEXT("Окно перка не открыто.");return nullptr;}
    const FString PackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
    if(!FPackageName::IsValidObjectPath(ObjectPath) || !PackageName.StartsWith(TEXT("/Game/"))
        || FPackageName::DoesPackageExist(PackageName) || FindObject<UPackage>(nullptr,*PackageName))
    {OutError=TEXT("Выберите новое имя ассета в /Game/: существующие пакеты не перезаписываются.");return nullptr;}
    UPackage* Package=CreatePackage(*PackageName);
    auto* Saved=DuplicateObject<UMCPerkVFXProfile>(Working,Package,*FPackageName::ObjectPathToObjectName(ObjectPath));
    Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
    if(!UEditorLoadingAndSavingUtils::SavePackages({Package},true))
    {OutError=TEXT("Не удалось сохранить новую DA; рабочая копия сохранена в окне.");return nullptr;}
    return Saved;
}

FString UMCPerkPreviewEditorLibrary::AssignSavedProfileToRocket(UMCPerkVFXProfile* Profile)
{return AssignSavedProfileToPerk(TEXT("Consumable_Rocket"),Profile);}
FString UMCPerkPreviewEditorLibrary::AssignSavedProfileToPerk(FName PerkID,UMCPerkVFXProfile* Profile)
{
    const auto* Entry=MCPerkPreviewAdapters::Find(PerkID);
    if(!Entry || !Entry->bSupportsProfileOverride)return TEXT("Для этого действия ещё нет реальной привязки VFX-профиля; игра не изменена.");
    if(!Profile || Profile->HasAnyFlags(RF_Transient) || !Profile->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || !FPackageName::DoesPackageExist(Profile->GetOutermost()->GetName()) || Profile->GetOutermost()->IsDirty())
        return TEXT("Сначала сохраните новую DA профиля; несохранённая рабочая копия не назначается игре.");
    if(GEditor && GEditor->PlayWorld) return TEXT("Завершите Play перед назначением профиля игре.");
    FString Error;
    if(!ValidateSavedProfileResources(Profile,Error)) return TEXT("Профиль невалиден: ")+Error;
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    if(!Table || Table->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) return TEXT("Каноническая DT_Perks недоступна.");
    if(Table->GetOutermost()->IsDirty()) return TEXT("В DT_Perks есть несохранённые изменения. Сначала сохраните или отмените их отдельно.");
    const FName RowID=Entry->SourceRowID.IsNone()?PerkID:Entry->SourceRowID;
    auto* Row=Table->FindRow<FMCPerkDefinition>(RowID,TEXT("AssignPerkVFXProfile"),false);
    if(!Row) return TEXT("Строка перка не найдена: ")+RowID.ToString();
    if(!Row->bEnabled)return TEXT("Строка перка отключена; профиль не назначен: ")+RowID.ToString();
    if(Row->VFXProfile.Get()==Profile) return TEXT("Этот профиль уже назначен действию.");
    const FScopedTransaction Transaction(NSLOCTEXT("MCPerkPreview","AssignProfile","Назначить VFX-профиль действию"));
    Table->Modify();Row->VFXProfile=Profile;Table->HandleDataTableChanged(RowID);Table->MarkPackageDirty();
    if(!UEditorLoadingAndSavingUtils::SavePackages({Table->GetOutermost()},true))
        return TEXT("Профиль назначен в памяти, но DT_Perks не удалось сохранить. Правка оставлена для Undo/сохранения.");
    return TEXT("Сохранено: ")+RowID.ToString()+TEXT(" → ")+Profile->GetPathName();
}

FString UMCPerkPreviewEditorLibrary::GetPerkPreviewSnapshotJson()
{
    auto Data=MakeShared<FJsonObject>();
    auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    Data->SetBoolField(TEXT("tool_open"),MCVFXActionWorkspace::GetActiveSession().IsValid());
    Data->SetBoolField(TEXT("context_ready"),Runner.IsValid());
    Data->SetBoolField(TEXT("pie"),GEditor && GEditor->PlayWorld);
    UWorld* EditorWorld=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    Data->SetStringField(TEXT("editor_map"),GetPathNameSafe(EditorWorld));
    if(Runner.IsValid())
    {
        UWorld* World=Runner->GetWorld();auto* Hero=Runner->GetHero();auto* Action=Runner->GetAction();
        Data->SetStringField(TEXT("preview_world"),GetPathNameSafe(World));
        Data->SetStringField(TEXT("preview_action_id"),Runner->GetPerkID().ToString());
        Data->SetStringField(TEXT("context_action_id"),MCPerkActionPreviewPanel::GetContextPerkID().ToString());
        Data->SetBoolField(TEXT("transient_preview_world"),World && World->GetOutermost()==GetTransientPackage());
        Data->SetStringField(TEXT("world_type"),World && World->WorldType==EWorldType::Game?TEXT("Game"):TEXT("Other"));
        Data->SetStringField(TEXT("game_instance_class"),World && World->GetGameInstance()?World->GetGameInstance()->GetClass()->GetPathName():TEXT("None"));
        Data->SetStringField(TEXT("game_mode_class"),World && World->GetAuthGameMode()?World->GetAuthGameMode()->GetClass()->GetPathName():TEXT("None"));
        const auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
        Data->SetStringField(TEXT("controller_class"),PC?PC->GetClass()->GetPathName():TEXT("None"));
        Data->SetBoolField(TEXT("has_local_player"),PC && PC->GetLocalPlayer()!=nullptr);
        Data->SetBoolField(TEXT("controller_is_local"),PC && PC->IsLocalController());
        Data->SetBoolField(TEXT("base_game_instance"),World && World->GetGameInstance() && World->GetGameInstance()->GetClass()==UGameInstance::StaticClass());
        Data->SetBoolField(TEXT("audio_disabled"),World && !World->AllowAudioPlayback());
        if(auto* GS=World?World->GetGameState<AMCGameState>():nullptr) {
            Data->SetStringField(TEXT("game_state_class"),GS->GetClass()->GetPathName());Data->SetNumberField(TEXT("player_count"),GS->PlayerArray.Num());
            Data->SetBoolField(TEXT("working_phase"),GS->Phase==EMCShiftPhase::Working && !GS->bLobbyWaiting);
        }
        Data->SetBoolField(TEXT("world_is_separate"),World && World!=EditorWorld && (!GEditor || World!=GEditor->PlayWorld));
        Data->SetStringField(TEXT("status"),Runner->GetStatus());
        Data->SetStringField(TEXT("phase"),Runner->GetPhaseName());
        Data->SetNumberField(TEXT("seconds"),Runner->GetElapsed());
        Data->SetBoolField(TEXT("paused"),Runner->IsPaused());
        Data->SetBoolField(TEXT("running"),Runner->IsRunning());
        const auto Clock=Runner->GetClockStats();auto ClockData=MakeShared<FJsonObject>();
        ClockData->SetStringField(TEXT("owner"),TEXT("editor_shared_service"));
        ClockData->SetNumberField(TEXT("tick_calls"),double(Clock.TickCalls));ClockData->SetNumberField(TEXT("skipped_same_frame_calls"),double(Clock.SkippedSameFrameCalls));
        ClockData->SetNumberField(TEXT("world_steps"),double(Clock.WorldSteps));ClockData->SetNumberField(TEXT("input_seconds"),Clock.InputSeconds);
        ClockData->SetNumberField(TEXT("advanced_seconds"),Clock.AdvancedSeconds);ClockData->SetNumberField(TEXT("dropped_catchup_seconds"),Clock.DroppedCatchupSeconds);
        ClockData->SetNumberField(TEXT("last_input_delta"),Clock.LastInputDelta);ClockData->SetNumberField(TEXT("last_advanced_delta"),Clock.LastAdvancedDelta);
        ClockData->SetNumberField(TEXT("engine_frame"),double(GFrameCounter));ClockData->SetNumberField(TEXT("world_seconds"),World?World->GetTimeSeconds():0.);
        Data->SetObjectField(TEXT("clock"),ClockData);
        Data->SetStringField(TEXT("hero"),GetPathNameSafe(Hero));
        Data->SetStringField(TEXT("action"),GetPathNameSafe(Action));
        if(Hero)
        {
            Data->SetObjectField(TEXT("hero_location"),VectorJson(Hero->GetActorLocation()));
            auto* Mesh=Hero->GetMesh();
            Data->SetStringField(TEXT("anim_instance"),Mesh?GetPathNameSafe(Mesh->GetAnimInstance()):TEXT("None"));
            if(Mesh && Action)
                for(int32 I=0;I<2;++I)
                {
                    const bool bLeft=I==0;FTransform Target;
                    auto Hand=MakeShared<FJsonObject>();
                    const bool bTarget=Action->GetPresentationHandTarget(bLeft,Target);
                    const FName Bone=Hero->RigBone(bLeft?TEXT("hand_l"):TEXT("hand_r"));
                    const FTransform Actual=Mesh->GetSocketTransform(Bone);
                    Hand->SetStringField(TEXT("bone"),Bone.ToString());
                    Hand->SetBoolField(TEXT("has_target"),bTarget);
                    Hand->SetObjectField(TEXT("evaluated_position"),VectorJson(Actual.GetLocation()));
                    if(bTarget)
                    {
                        Hand->SetObjectField(TEXT("target_position"),VectorJson(Target.GetLocation()));
                        Hand->SetNumberField(TEXT("distance_cm"),FVector::Distance(Target.GetLocation(),Actual.GetLocation()));
                        Hand->SetNumberField(TEXT("rotation_error_deg"),FMath::RadiansToDegrees(Target.GetRotation().AngularDistance(Actual.GetRotation())));
                    }
                    Data->SetObjectField(bLeft?TEXT("left_hand"):TEXT("right_hand"),Hand);
                }
        }
        if(Action)
        {
            Data->SetBoolField(TEXT("prop_visible"),Action->PropMesh && Action->PropMesh->IsVisible());
            Data->SetBoolField(TEXT("committed"),Action->IsCommitted());
            Data->SetBoolField(TEXT("finished"),Action->IsFinished());
            Data->SetStringField(TEXT("loop_system"),Action->LoopComponent?GetPathNameSafe(Action->LoopComponent->GetAsset()):TEXT("None"));
            Data->SetStringField(TEXT("prepared_impact_system"),GetPathNameSafe(Action->GetPreparedVFXSystem(TEXT("Impact"))));
            TArray<TSharedPtr<FJsonValue>> PreparedStages;
            for(FName Stage:UMCPerkVFXProfile::GetSupportedStages()) {
                auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("stage"),Stage.ToString());
                P->SetStringField(TEXT("composition"),GetPathNameSafe(Action->GetPreparedVFXComposition(Stage)));
                TArray<UNiagaraSystem*> Systems;Action->GetPreparedVFXSystems(Stage,Systems);
                TArray<TSharedPtr<FJsonValue>> Names;for(auto* System:Systems)Names.Add(MakeShared<FJsonValueString>(GetPathNameSafe(System)));
                P->SetArrayField(TEXT("systems"),Names);PreparedStages.Add(MakeShared<FJsonValueObject>(P));
            }
            Data->SetArrayField(TEXT("prepared_vfx_stages"),PreparedStages);
            if(auto* Rocket=Cast<AMCRocketSkillAction>(Action))
            {
                Data->SetNumberField(TEXT("detonations"),Rocket->DetonationCount);
                Data->SetBoolField(TEXT("launcher_held"),Rocket->IsLauncherHeld());
                Data->SetObjectField(TEXT("muzzle"),VectorJson(Rocket->GetLauncherMuzzlePoint()));
                Data->SetObjectField(TEXT("muzzle_forward"),VectorJson(Rocket->GetLauncherForward()));
            }
        }
        TArray<TSharedPtr<FJsonValue>> Events;
        for(const auto& Event:Runner->GetEvents())
        {
            auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("stage"),Event.Stage.ToString());E->SetNumberField(TEXT("seconds"),Event.Seconds);
            Events.Add(MakeShared<FJsonValueObject>(E));
        }
        Data->SetArrayField(TEXT("events"),Events);
        TArray<TSharedPtr<FJsonValue>> Compositions;int32 RunningCompositions=0,LiveLayers=0,PendingLayers=0;
        if(World)for(TActorIterator<AMCPerkVFXCompositionPlayer> It(World);It;++It)if(IsValid(*It)) {
            auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("player"),It->GetPathName());
            C->SetBoolField(TEXT("running"),It->IsRunning());C->SetBoolField(TEXT("stopping"),It->IsStopping());
            C->SetNumberField(TEXT("playback_seconds"),It->GetPlaybackTime());
            C->SetNumberField(TEXT("live_layers"),It->GetLiveLayerCount());C->SetNumberField(TEXT("pending_layers"),It->GetPendingLayerCount());
            Compositions.Add(MakeShared<FJsonValueObject>(C));RunningCompositions+=It->IsRunning()?1:0;
            LiveLayers+=It->GetLiveLayerCount();PendingLayers+=It->GetPendingLayerCount();
        }
        Data->SetArrayField(TEXT("composition_players"),Compositions);Data->SetNumberField(TEXT("running_composition_players"),RunningCompositions);
        Data->SetNumberField(TEXT("composition_live_layers"),LiveLayers);Data->SetNumberField(TEXT("composition_pending_layers"),PendingLayers);
        TArray<TSharedPtr<FJsonValue>> Niagara;
        for(TObjectIterator<UNiagaraComponent> It;It;++It)
        {
            auto* C=*It;if(!IsValid(C)||C->IsTemplate()||C->GetWorld()!=World||!C->GetAsset())continue;
            auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("component"),C->GetPathName());
            N->SetStringField(TEXT("system"),GetPathNameSafe(C->GetAsset()));
            N->SetBoolField(TEXT("active"),C->IsActive());N->SetBoolField(TEXT("visible"),C->IsVisible());
            N->SetBoolField(TEXT("registered"),C->IsRegistered());
            N->SetObjectField(TEXT("location"),VectorJson(C->GetComponentLocation()));
            N->SetObjectField(TEXT("scale"),VectorJson(C->GetComponentScale()));
            // Explicit editor diagnostics only. Synchronize before reading CPU
            // emitter instances; never inspect their concurrent buffers unsafely.
            if(auto Controller=C->GetSystemInstanceController())
            {
                Controller->WaitForConcurrentTickAndFinalize();
                if(auto* Instance=Controller->GetSystemInstance_Unsafe())
                {
                    TArray<TSharedPtr<FJsonValue>> Counts;int32 Total=0;
                    for(const auto& Emitter:Instance->GetEmitters())
                    {const int32 Count=Emitter->GetNumParticles();Total+=Count;Counts.Add(MakeShared<FJsonValueNumber>(Count));}
                    N->SetArrayField(TEXT("cpu_particle_counts"),Counts);N->SetNumberField(TEXT("cpu_particles"),Total);
                }
            }
            for(const FName Name:{FName(TEXT("User.Radius")),FName(TEXT("User.Duration")),FName(TEXT("User.Intensity"))})
            {bool Valid=false;const float Value=C->GetVariableFloat(Name,Valid);if(Valid)N->SetNumberField(Name.ToString(),Value);}
            Niagara.Add(MakeShared<FJsonValueObject>(N));
        }
        Data->SetArrayField(TEXT("niagara_components"),Niagara);
        int32 ActorCount=0,ActionCount=0;
        if(World) for(TActorIterator<AActor> It(World);It;++It) { ++ActorCount;if(It->IsA<AMCActiveSkillAction>()) ++ActionCount; }
        Data->SetNumberField(TEXT("actor_count"),ActorCount);Data->SetNumberField(TEXT("action_count"),ActionCount);
    }
    FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
