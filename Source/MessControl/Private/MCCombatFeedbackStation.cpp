#include "MCCombatFeedbackStation.h"
#include "MCCombatFeedbackPanel.h"
#include "MCBossCharacter.h"
#include "MCNutBoss.h"
#include "MCVFXLabBossStation.h"

#include "MCDayPlan.h"
#include "MCGameMode.h"
#include "MCInventoryComponent.h"
#include "MCNutRainProfile.h"
#include "MCPlaytestBotController.h"
#include "MCPlayerState.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpectatorPawn.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "TimerManager.h"
#include "Widgets/SWidget.h"

namespace
{
const FName LegacyTag(TEXT("MC.LegacyCombatFeedback"));
const FName SubjectTag(TEXT("MC_VFXLabSubject"));
const FName OverviewTag(TEXT("MCCombatFeedbackOverview"));
float SafeDamage(float Value) {return FMath::IsFinite(Value)?FMath::Clamp(Value,1.f,1000000.f):25.f;}
}

void AMCCombatFeedbackNut::Tick(float DeltaSeconds)
{
    AActor::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    const auto* GS=GetWorld()->GetGameState();
    TickPresentation(GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds());
#endif
}

AMCCombatFeedbackStation::AMCCombatFeedbackStation()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickInterval=.05f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ComparisonOrigin")));
    Readout=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ComparisonStatus"));
    Readout->SetupAttachment(RootComponent); Readout->SetRelativeLocation(FVector(0,0,235));
    Readout->SetHorizontalAlignment(EHTA_Center); Readout->SetWorldSize(22);
    Readout->SetCollisionEnabled(ECollisionEnabled::NoCollision); Readout->SetCastShadow(false);
    SubjectClass=FSoftObjectPath(TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    NutProfile=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
}

void AMCCombatFeedbackStation::BeginPlay()
{
    Super::BeginPlay(); SetActorTickEnabled(false);
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    SetStatus(TEXT("Development comparison disabled"));
#else
    if(HasAuthority() && GetWorld()->IsGameWorld()) {
        SetStatus(TEXT("Preparing real damage fixtures"));
        // Wait for the saved floor's BeginPlay to finish constructing its support.
        NextCycleAt=.25; bRunning=bAutoRun; SetActorTickEnabled(true);
    }
#endif
}

void AMCCombatFeedbackStation::SetStatus(const FString& Text)
{
    Status=Text;
    Readout->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"),bLegacyFeedback?TEXT("OLD"):TEXT("NEW"),*Text)));
    Readout->SetTextRenderColor(bLegacyFeedback?FColor(190,205,220):FColor(120,240,185));
}

bool AMCCombatFeedbackStation::GroundPoint(FVector Offset,float Radius,FVector& OutPoint) const
{
    if(!IsValid(Floor) || Floor->GetWorld()!=GetWorld()) return false;
    FHitResult Hit;
    if(!Floor->InteriorSurfacePoint(GetActorTransform().TransformPosition(Offset),Radius,Hit)) return false;
    OutPoint=Hit.ImpactPoint; return true;
}

bool AMCCombatFeedbackStation::SpawnFixtures()
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    return false;
#else
    if(ScenarioIndex>0) {
        const FTransform Pose=GetActorTransform();
        BossFixture=GetWorld()->SpawnActorDeferred<AMCVFXLabBossStation>(AMCVFXLabBossStation::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!BossFixture) {SetStatus(TEXT("Boss station spawn failed"));return false;}
        BossFixture->bComparisonMode=true; BossFixture->bLegacyFeedback=bLegacyFeedback;
        BossFixture->bAutoRun=false; BossFixture->ComparisonSeed=137;
        BossFixture->Floor=Floor; BossFixture->SubjectClass=SubjectClass; BossFixture->NutProfile=NutProfile;
        BossFixture->FinishSpawning(Pose); BossFixture->Label->SetHiddenInGame(true);
        if(!BossFixture->PrepareComparison(EMCVFXLabBossKind(ScenarioIndex-1))) {
            SetStatus(BossFixture->GetComparisonStatusText());return false;
        }
        Subject=BossFixture->GetComparisonSubject(); Enemy=Cast<AMCNutEnemy>(BossFixture->GetComparisonEnemy());
        if(IsValid(Subject)) {
            Subject->ApplyPlayerColor(FLinearColor(.24f,.65f,1));
            if(Subject->Inventory) Subject->Inventory->ServerSelect(EMCToolSlot::Knife);
        }
        SetStatus(TEXT("Boss prepared; F3 for manual damage or a real attack"));return true;
    }
    UClass* PawnClass=SubjectClass.LoadSynchronous();
    if(!PawnClass || !PawnClass->IsChildOf(AMCToothCharacter::StaticClass())) {SetStatus(TEXT("Missing player Blueprint"));return false;}
    const auto* Profile=NutProfile.LoadSynchronous();
    EnemySettings=Profile?Profile->Settings.Enemy:FMCNutEnemySettings();
    // Remove the per-actor ambient hop phase so both lanes start from the same pose.
    EnemySettings.MaxHealth=100; EnemySettings.HopHeight=0; EnemySettings.Sanitize();
    ResolvedEnemyMesh=EnemyMesh.LoadSynchronous();
    if(!ResolvedEnemyMesh && Profile) {
        if(auto* Menu=Profile->Menu.LoadSynchronous())
            if(const auto* Row=Menu->FindRow<FMCFoodRow>(Profile->NutRow,TEXT("Combat comparison")))
                if(!Row->WholeMeshes.IsEmpty()) ResolvedEnemyMesh=Row->WholeMeshes[0].LoadSynchronous();
        if(!ResolvedEnemyMesh) ResolvedEnemyMesh=Profile->Settings.Boss.WholeMesh.LoadSynchronous();
    }
    if(!ResolvedEnemyMesh) {SetStatus(TEXT("Missing saved nut mesh/profile"));return false;}
    const double Height=ResolvedEnemyMesh->GetBounds().BoxExtent.Z*2;
    if(Height<=KINDA_SMALL_NUMBER) {SetStatus(TEXT("Nut mesh has no height"));return false;}
    EnemyScale=FVector(EnemySettings.BodyRadius*2/Height);
    FVector SubjectGround,EnemyGround;
    if(!GroundPoint(SubjectOffset,45,SubjectGround) || !GroundPoint(EnemyOffset,EnemySettings.BodyRadius,EnemyGround)) {
        SetStatus(TEXT("Fixtures outside Floor"));return false;
    }
    FActorSpawnParameters Params; Params.Owner=this; Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Bot=GetWorld()->SpawnActor<AMCPlaytestBotController>(AMCPlaytestBotController::StaticClass(),FTransform::Identity,Params);
    auto* Identity=Bot?Bot->GetPlayerState<APlayerState>():nullptr;
    if(!Identity) {SetStatus(TEXT("Subject controller has no PlayerState"));ClearFixtures();return false;}
    Bot->Configure(EMCPlaytestBotSkill::Regular,0,137);
    if(auto* PlayerIdentity=Cast<AMCPlayerState>(Identity)) {
        PlayerIdentity->PlayerColor=FLinearColor(.24f,.65f,1); PlayerIdentity->ForceNetUpdate();
    }
    Identity->SetIsSpectator(false); Identity->SetIsOnlyASpectator(false); Identity->SetIsABot(true);
    Identity->SetPlayerName(bLegacyFeedback?TEXT("Old impact subject"):TEXT("New impact subject"));
    const auto* Defaults=PawnClass->GetDefaultObject<AMCToothCharacter>();
    const FVector Facing=(EnemyGround-SubjectGround).GetSafeNormal2D(SMALL_NUMBER,GetActorForwardVector());
    SubjectPose=FTransform(Facing.Rotation(),SubjectGround+FVector(0,0,Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4));
    Subject=GetWorld()->SpawnActorDeferred<AMCToothCharacter>(PawnClass,SubjectPose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Subject) {SetStatus(TEXT("Subject spawn failed"));ClearFixtures();return false;}
    Subject->AutoPossessAI=EAutoPossessAI::Disabled; Subject->Tags.AddUnique(SubjectTag);
    if(bLegacyFeedback) Subject->Tags.AddUnique(LegacyTag);
    Subject->FinishSpawning(SubjectPose); Bot->Possess(Subject); Bot->SetLabControlled(true);
    Bot->SetActorTickEnabled(false);
    if(!AMCGameMode::IsGameplayParticipant(Bot)) {SetStatus(TEXT("Subject is not a gameplay participant"));ClearFixtures();return false;}
    Subject->ApplyPlayerColor(FLinearColor(.24f,.65f,1)); Subject->Status->Initialize(100);
    Subject->Inventory->ServerSelect(EMCToolSlot::Knife);
    const FTransform EnemyPose((-Facing).Rotation(),EnemyGround+FVector(0,0,EnemySettings.BodyRadius+4));
    auto* Nut=GetWorld()->SpawnActorDeferred<AMCCombatFeedbackNut>(AMCCombatFeedbackNut::StaticClass(),EnemyPose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Nut) {SetStatus(TEXT("Enemy spawn failed"));ClearFixtures();return false;}
    Enemy=Nut; Enemy->Tags.AddUnique(SubjectTag); if(bLegacyFeedback) Enemy->Tags.AddUnique(LegacyTag);
    Enemy->ConfigureEnemy(Floor,ResolvedEnemyMesh,EnemyScale,EnemySettings); Enemy->FinishSpawning(EnemyPose);
    SetStatus(TEXT("Ready: identical damage and timing"));return true;
#endif
}

bool AMCCombatFeedbackStation::StartCycle()
{
    if(!IsValid(Subject) || !IsValid(Enemy) || !Subject->Status->IsAlive() || !Enemy->IsEncounterAlive()) {
        ClearFixtures(); if(!SpawnFixtures()) return false;
    }
    Subject->Status->Initialize(100);
    if(Subject->GetController()==Bot) {
        Subject->CancelGameplayInput(); Subject->GetCharacterMovement()->StopMovementImmediately();
        Subject->TeleportTo(SubjectPose.GetLocation(),SubjectPose.Rotator());
    }
    Enemy->ConfigureEnemy(Floor,ResolvedEnemyMesh,EnemyScale,EnemySettings);
    CycleStartedAt=GetWorld()->GetTimeSeconds(); NextCycleAt=CycleStartedAt+RepeatSeconds;
    bCycleActive=true; bEnemyStruck=bPlayerStruck=false; LastEnemyDamage=LastPlayerDamage=0; ++Cycles;
    SetStatus(TEXT("Ready: enemy hit, then player hit"));return true;
}

bool AMCCombatFeedbackStation::StrikeEnemy()
{
    return ApplyOutgoing(EnemyDamage);
}

bool AMCCombatFeedbackStation::StrikePlayer()
{
    return ApplyIncoming(PlayerDamage);
}

AActor* AMCCombatFeedbackStation::GetComparisonEnemy() const
{
    return IsValid(BossFixture)?BossFixture->GetComparisonEnemy():Enemy.Get();
}

FString AMCCombatFeedbackStation::GetComparisonStatusText() const
{
    if(IsValid(BossFixture)) return BossFixture->GetComparisonStatusText();
    const float HP=IsValid(Subject) && Subject->Status?Subject->Status->State.Health:0;
    return FString::Printf(TEXT("Player HP %.0f/100 | Enemy HP %.0f/100\nOutgoing HP %.1f | Incoming HP %.1f\n%s"),
        HP,IsValid(Enemy)?Enemy->Health:0,LastEnemyDamage,LastPlayerDamage,*Status);
}

bool AMCCombatFeedbackStation::ApplyOutgoing(float Damage)
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    return false;
#else
    AActor* Target=GetComparisonEnemy();
    if(!HasAuthority() || !IsValid(Subject) || !IsValid(Target)) return false;
    const float ShieldBefore=Cast<AMCNutBoss>(Target)?Cast<AMCNutBoss>(Target)->ShieldHealth:0;
    LastEnemyDamage=IsValid(BossFixture)?BossFixture->ApplyComparisonOutgoing(SafeDamage(Damage)):
        Enemy->ReceiveToolDamage(SafeDamage(Damage),Subject);
    const float ShieldAfter=Cast<AMCNutBoss>(Target)?Cast<AMCNutBoss>(Target)->ShieldHealth:0;
    if(LastEnemyDamage>0) {
        FVector Point=Target->GetActorLocation();
        if(auto* Nut=Cast<AMCNutEnemy>(Target)) Point=Nut->GetToolTargetPoint(Subject->GetActorLocation());
        else if(auto* Boss=Cast<AMCBossCharacter>(Target)) Point=Boss->GetMeleeTargetPoint(Subject->GetActorLocation());
        Subject->MulticastHitSound(Point,uint8(EMCToolSlot::Knife),FMath::Clamp(LastEnemyDamage/60.f,.25f,1.f));
    }
    SetStatus(FString::Printf(TEXT("Outgoing: HP %.1f / shield %.1f"),LastEnemyDamage,FMath::Max(0.f,ShieldBefore-ShieldAfter)));
    return LastEnemyDamage>0 || ShieldBefore>ShieldAfter;
#endif
}

bool AMCCombatFeedbackStation::ApplyIncoming(float Damage)
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
    return false;
#else
    AActor* Source=GetComparisonEnemy();
    if(!HasAuthority() || !IsValid(Subject) || !IsValid(Source) || !Subject->Status) return false;
    if(IsValid(BossFixture)) {LastPlayerDamage=BossFixture->ApplyComparisonIncoming(SafeDamage(Damage));return LastPlayerDamage>0;}
    const float Before=Subject->Status->State.Health;
    const FVector Direction=(Subject->GetActorLocation()-Source->GetActorLocation()).GetSafeNormal2D();
    if(!Subject->Status->Damage(SafeDamage(Damage),Direction,Source)) {SetStatus(TEXT("Player damage rejected"));return false;}
    LastPlayerDamage=Before-Subject->Status->State.Health;
    SetStatus(FString::Printf(TEXT("Enemy %.0f / player %.0f actual damage"),LastEnemyDamage,LastPlayerDamage));return LastPlayerDamage>0;
#endif
}

bool AMCCombatFeedbackStation::RunSelectedCase()
{
    if(!HasAuthority()) return false;
    if(IsValid(BossFixture)) return BossFixture->RunComparisonAction();
    SetRunning(false); return StrikeEnemy() && StrikePlayer();
}

void AMCCombatFeedbackStation::SelectScenario(int32 Index)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    ClearFixtures(); ScenarioIndex=FMath::Clamp(Index,0,1+int32(uint8(EMCVFXLabBossKind::Phase3AreaAttack)));
    const float Lane=(bLegacyFeedback?1.f:-1.f)*(ScenarioIndex>0?2000.f:425.f);
    SetActorLocation(FVector(0,Lane,0));
    if(IsValid(Floor)) {
        Floor->SetActorLocation(FVector(0,Lane,-20));
        Floor->SetActorScale3D(ScenarioIndex>0?FVector(18,24,.4):FVector(9,7,.4)); Floor->RebuildSurface();
    }
    const FName TitleTag(*(FString(TEXT("CombatFeedback"))+(bLegacyFeedback?TEXT("OLD"):TEXT("NEW"))+TEXT("Title")));
    const FName SubtitleTag(*(FString(TEXT("CombatFeedback"))+(bLegacyFeedback?TEXT("OLD"):TEXT("NEW"))+TEXT("Subtitle")));
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        if(It->ActorHasTag(TitleTag)) It->SetActorLocation(FVector(-260,Lane,565));
        if(It->ActorHasTag(SubtitleTag)) {
            It->SetActorLocation(FVector(-260,Lane,475));
            if(auto* Text=It->FindComponentByClass<UTextRenderComponent>()) Text->SetText(FText::FromString(TEXT("F3: DAMAGE / SHIELD / REAL BOSS ATTACK")));
        }
    }
    bRunning=false; Reset();
#endif
}

void AMCCombatFeedbackStation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(ScenarioIndex>0) {
        if(!IsValid(BossFixture) && Now>=NextCycleAt && !SpawnFixtures()) {SetActorTickEnabled(false);return;}
        if(IsValid(BossFixture)) {
            Subject=BossFixture->GetComparisonSubject(); Enemy=Cast<AMCNutEnemy>(BossFixture->GetComparisonEnemy());
            SetStatus(BossFixture->GetComparisonStatusText());
        }
        return;
    }
    if(!IsValid(Subject) || !IsValid(Enemy)) {
        if(Now<NextCycleAt) return;
        ClearFixtures();
        if(!SpawnFixtures()) {bRunning=false;SetActorTickEnabled(false);return;}
        if(!bRunning) {SetActorTickEnabled(false);return;}
    }
    if(!bRunning) return;
    RepeatSeconds=FMath::IsFinite(RepeatSeconds)?FMath::Clamp(RepeatSeconds,3.f,30.f):4.f;
    if(!bCycleActive || Now>=NextCycleAt) if(!StartCycle()) {bRunning=false;SetActorTickEnabled(false);return;}
    if(!bEnemyStruck && Now-CycleStartedAt>=.55) {bEnemyStruck=true;StrikeEnemy();}
    if(!bPlayerStruck && Now-CycleStartedAt>=1.65) {bPlayerStruck=true;StrikePlayer();}
#endif
}

void AMCCombatFeedbackStation::SetRunning(bool bEnabled)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !HasActorBegunPlay() || !GetWorld()->IsGameWorld()) return;
    if(ScenarioIndex>0) {SetStatus(TEXT("Boss cases run manually from F3; reset to repeat"));return;}
    bRunning=bEnabled; bCycleActive=false; NextCycleAt=GetWorld()->GetTimeSeconds();
    SetActorTickEnabled(bEnabled || !IsValid(Subject));
    SetStatus(bEnabled?TEXT("Repeating real damage"):TEXT("Paused: subjects available for manual play"));
#endif
}

void AMCCombatFeedbackStation::ResumeBotControl()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(IsValid(BossFixture)) {BossFixture->ResumeComparisonBot();return;}
    if(HasAuthority() && IsValid(Bot) && IsValid(Subject) && !Subject->GetController()) {
        Bot->Possess(Subject); Bot->SetLabControlled(true); Bot->SetActorTickEnabled(false);
    }
#endif
}

void AMCCombatFeedbackStation::ClearFixtures()
{
    if(IsValid(BossFixture)) {BossFixture->Destroy();BossFixture=nullptr;Subject=nullptr;Enemy=nullptr;bCycleActive=false;return;}
    if(IsValid(Subject)) Subject->CancelGameplayInput();
    if(IsValid(Bot)) {
        auto* Identity=Bot->GetPlayerState<APlayerState>(); Bot->UnPossess(); Bot->Destroy();
        if(IsValid(Identity)) Identity->Destroy();
    }
    if(IsValid(Enemy)) Enemy->Destroy(); if(IsValid(Subject)) Subject->Destroy();
    Bot=nullptr; Enemy=nullptr; Subject=nullptr; bCycleActive=false;
}

void AMCCombatFeedbackStation::Reset()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    ClearFixtures(); Cycles=0; LastEnemyDamage=LastPlayerDamage=0;
    NextCycleAt=GetWorld()->GetTimeSeconds(); SetActorTickEnabled(true); SetStatus(TEXT("Resetting comparison fixtures"));
#endif
}

void AMCCombatFeedbackStation::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false; SetActorTickEnabled(false); if(HasAuthority()) ClearFixtures(); Super::EndPlay(Reason);
}

void AMCCombatFeedbackController::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(IsLocalController()) GetWorldTimerManager().SetTimerForNextTick(this,&AMCCombatFeedbackController::ViewOverview);
#endif
}

bool AMCCombatFeedbackController::InputKey(const FInputKeyEventArgs& Params)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(Params.Key==EKeys::F3) {if(Params.Event==IE_Pressed) ToggleTestPanel();return true;}
    if(Params.Key==EKeys::Escape && IsTestPanelOpen()) {if(Params.Event==IE_Pressed) CloseTestPanel();return true;}
    if(IsTestPanelOpen()) return true;
    // Handle comparison commands before the pawn's tool keys and restart action.
    // Space continues through ordinary Enhanced Input while a player is possessed.
    if(Params.Key==EKeys::One || Params.Key==EKeys::Two || Params.Key==EKeys::Zero || Params.Key==EKeys::R
        || (Params.Key==EKeys::SpaceBar && !Cast<AMCToothCharacter>(GetPawn()))) {
        if(Params.Event==IE_Pressed) {
            if(Params.Key==EKeys::One) ViewLegacy();
            else if(Params.Key==EKeys::Two) ViewCurrent();
            else if(Params.Key==EKeys::Zero) ViewOverview();
            else if(Params.Key==EKeys::R) ResetStations();
            else ToggleRepeating();
        }
        return true;
    }
#endif
    return Super::InputKey(Params);
}

void AMCCombatFeedbackController::ViewSubject(bool bLegacy)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) if(It->bLegacyFeedback==bLegacy && IsValid(It->Subject)) {
        if(auto* Spectator=Cast<ASpectatorPawn>(GetPawn())) OverviewPawn=Spectator;
        ReleaseSubject();
        Possess(It->Subject); SetViewTargetWithBlend(It->Subject,.15f);return;
    }
#endif
}
void AMCCombatFeedbackController::ReleaseSubject()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    auto* Previous=Cast<AMCToothCharacter>(GetPawn());
    if(!Previous) return;
    Previous->CancelGameplayInput(); UnPossess();
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It)
        if(It->Subject==Previous) {It->ResumeBotControl();break;}
#endif
}
void AMCCombatFeedbackController::ViewLegacy() {ViewSubject(true);}
void AMCCombatFeedbackController::ViewCurrent() {ViewSubject(false);}
void AMCCombatFeedbackController::ViewOverview()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    if(auto* Spectator=Cast<ASpectatorPawn>(GetPawn())) OverviewPawn=Spectator;
    ReleaseSubject();
    if(IsValid(OverviewPawn) && GetPawn()!=OverviewPawn) Possess(OverviewPawn);
    for(TActorIterator<ACameraActor> It(GetWorld());It;++It) if(It->ActorHasTag(OverviewTag)) {
        const bool Boss=SelectedScenario>0;
        It->SetActorLocation(Boss?FVector(1800,325,1100):FVector(1350,0,750));
        It->SetActorRotation((FVector(0,Boss?325:0,Boss?180:95)-It->GetActorLocation()).Rotation());
        It->GetCameraComponent()->OrthoWidth=Boss?6000:2000;
        SetViewTargetWithBlend(*It,.15f);return;
    }
    if(IsValid(OverviewPawn)) SetViewTarget(OverviewPawn);
#endif
}
void AMCCombatFeedbackController::ResetStations()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    ViewOverview(); for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) It->Reset();
#endif
}
void AMCCombatFeedbackController::ToggleRepeating()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority() || Cast<AMCToothCharacter>(GetPawn())) return;
    bool AnyRunning=false;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) AnyRunning|=It->bRunning;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) It->SetRunning(!AnyRunning);
#endif
}

bool AMCCombatFeedbackController::IsTestPanelOpen() const {return TestPanel.IsValid();}
void AMCCombatFeedbackController::ToggleTestPanel()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(IsTestPanelOpen()) {CloseTestPanel();return;}
    if(!IsLocalController() || !GetWorld()) return;
    auto* Viewport=GetWorld()->GetGameViewport(); if(!Viewport) return;
    TestPanel=MCBuildCombatFeedbackPanel(TWeakObjectPtr<AMCCombatFeedbackController>(this)); PanelViewport=Viewport;
    Viewport->AddViewportWidgetContent(TestPanel.ToSharedRef(),100);
    if(auto* Tooth=Cast<AMCToothCharacter>(GetPawn())) Tooth->CancelGameplayInput();
    if(APawn* ControlledPawn=GetPawn()) {
        ControlledPawn->ConsumeMovementInputVector();
        if(auto* Movement=ControlledPawn->GetMovementComponent()) Movement->StopMovementImmediately();
    }
    SetIgnoreMoveInput(true);SetIgnoreLookInput(true);SetShowMouseCursor(true);
    FInputModeGameAndUI Mode;Mode.SetWidgetToFocus(TestPanel);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);
#endif
}
void AMCCombatFeedbackController::CloseTestPanel()
{
    if(!TestPanel.IsValid()) return;
    if(auto* Viewport=PanelViewport.Get()) Viewport->RemoveViewportWidgetContent(TestPanel.ToSharedRef());
    TestPanel.Reset();PanelViewport.Reset();SetIgnoreMoveInput(false);SetIgnoreLookInput(false);SetShowMouseCursor(false);
    SetInputMode(FInputModeGameOnly());
}
void AMCCombatFeedbackController::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseTestPanel();Super::EndPlay(Reason);
}
void AMCCombatFeedbackController::SelectScenario(int32 Index)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!HasAuthority()) return;
    ViewOverview();SelectedScenario=FMath::Clamp(Index,0,1+int32(uint8(EMCVFXLabBossKind::Phase3AreaAttack)));
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) It->SelectScenario(SelectedScenario);
    ViewOverview();
#endif
}
FString AMCCombatFeedbackController::GetComparisonStatusText() const
{
    FString Result;
    for(const bool Legacy:{true,false}) for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It)
        if(It->bLegacyFeedback==Legacy) Result+=FString::Printf(TEXT("%s\n%s\n\n"),Legacy?TEXT("OLD"):TEXT("NEW"),*It->GetComparisonStatusText());
    return Result;
}
FString AMCCombatFeedbackController::GetScenarioTitle() const
{
    return MCCombatFeedbackScenarioTitle(SelectedScenario);
}
void AMCCombatFeedbackController::StrikeOutgoing(float Damage)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) {It->SetRunning(false);It->ApplyOutgoing(Damage);}
}
void AMCCombatFeedbackController::StrikeIncoming(float Damage)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) {It->SetRunning(false);It->ApplyIncoming(Damage);}
}
void AMCCombatFeedbackController::FinishEnemy() {StrikeOutgoing(1000000);}
void AMCCombatFeedbackController::FinishPlayer() {StrikeIncoming(1000000);}
bool AMCCombatFeedbackController::CanShieldTest() const {return SelectedScenario>=1 && SelectedScenario<=8;}
void AMCCombatFeedbackController::StrikeShield(float Damage) {if(CanShieldTest()) StrikeOutgoing(Damage);}
void AMCCombatFeedbackController::RunSelectedCase()
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCCombatFeedbackStation> It(GetWorld());It;++It) It->RunSelectedCase();
}

void AMCCombatFeedbackHUD::DrawHUD()
{
    Super::DrawHUD();
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    if(!Canvas) return;
    const FString Controls=TEXT("F3: damage / boss test panel   1: OLD player   2: NEW player   0: overview   R: reset\nSpace in overview: pause/resume basic enemy. Player view uses ordinary movement and attacks.");
    DrawRect(FLinearColor(0,0,0,.55f),20,18,FMath::Min(920.f,Canvas->ClipX-40),62);
    DrawText(Controls,FLinearColor::White,32,28,nullptr,1.05f,false);
#endif
}

AMCCombatFeedbackGameMode::AMCCombatFeedbackGameMode()
{
    DefaultPawnClass=ASpectatorPawn::StaticClass(); SpectatorClass=ASpectatorPawn::StaticClass();
    PlayerControllerClass=AMCCombatFeedbackController::StaticClass(); HUDClass=AMCCombatFeedbackHUD::StaticClass();
    PlayerStateClass=AMCPlayerState::StaticClass();
}
