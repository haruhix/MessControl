#include "MCRoguelikePreview.h"
#include "MCBossCharacter.h"
#include "MCGameMode.h"
#include "MCPerkComponent.h"
#include "MCPerkPickup.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCPerkChoiceWidget.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCExpressionComponent.h"
#include "MCGazeComponent.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCTongue.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

AMCRoguelikePreview::AMCRoguelikePreview() { PrimaryActorTick.bCanEverTick=false; }

void AMCRoguelikePreview::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    Destroy();
#else
    if (!HasAuthority()) { Destroy(); return; }
    bChestSocialReview=FParse::Param(FCommandLine::Get(),TEXT("MCChestSocialReview"));
    bBossPhase3Review=FParse::Param(FCommandLine::Get(),TEXT("MCBossPhase3Review"));
    FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),ExpectedPlayers);
    ExpectedPlayers=FMath::Clamp(ExpectedPlayers,1,8);
    StartedAt=StageStartedAt=GetWorld()->GetTimeSeconds();
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("RogueReview")),true);
    GetWorldTimerManager().SetTimer(StepTimer,this,&AMCRoguelikePreview::Step,.1f,true);
    UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW Started; expected players=%d boss_phase3_review=%d"),ExpectedPlayers,bBossPhase3Review);
#endif
}

void AMCRoguelikePreview::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(StepTimer);
    Super::EndPlay(Reason);
}

bool AMCRoguelikePreview::PlacePlayer(FVector FloorPoint)
{
    if (!IsValid(Hero)) return false;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    return Hero->TeleportTo(FloorPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),Hero->GetActorRotation());
}

void AMCRoguelikePreview::Photograph(const TCHAR* Filename,FVector Focus)
{
    auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if (!PC || GetNetMode()==NM_DedicatedServer) return;
    if (bBossPhase3Review)
    {
        // Retain the fixed, bounds-derived camera for every clip. The timed
        // recorder owns screenshot requests when a video is being captured.
        FString VideoName;
        if (!FParse::Value(FCommandLine::Get(),TEXT("MCVideo="),VideoName))
        {
            const FString Folder=FPaths::ProjectSavedDir()/TEXT("RogueReview/BossPhase3");
            IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/Filename,true,false);
        }
        return;
    }
    if (!Camera) Camera=GetWorld()->SpawnActor<ACameraActor>();
    if (!Camera) return;
    FVector Eye=Focus+FVector(500,-850,550);
    if(bChestSocialReview && Stage>=10) Eye=Focus+SocialForward*330+SocialRight*220+FVector(0,0,70);
    else if(bChestSocialReview && Stage<=3 && IsValid(Chest)) Eye=Focus+Chest->GetActorForwardVector()*390-Chest->GetActorRightVector()*380+FVector(0,0,210);
    else if(Stage<=3 && IsValid(Chest)) Eye=Focus-Chest->GetActorRightVector()*750+Chest->GetActorForwardVector()*250+FVector(0,0,470);
    else if(IsValid(Boss)) Eye=Focus+Boss->GetActorForwardVector()*750-Boss->GetActorRightVector()*300+FVector(0,0,420);
    Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
    Camera->GetCameraComponent()->SetFieldOfView(bChestSocialReview?46.f:60.f);
    PC->SetViewTarget(Camera);
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("RogueReview")/Filename,true,false);
}

void AMCRoguelikePreview::Finish(bool Passed,const FString& Detail)
{
    if (bFinished) return;
    bFinished=true;
    GetWorldTimerManager().ClearTimer(StepTimer);
    UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Detail);
    if (bBossPhase3Review) UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Detail);
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}

bool AMCRoguelikePreview::BeginSocialReview()
{
    if (!IsValid(Chest) || !IsValid(Hero)) return false;
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    if (Mode)
    {
        Mode->SetActorTickEnabled(false);
        if (Mode->DayDirector) Mode->DayDirector->SetActorTickEnabled(false);
    }
    if (auto* State=GetWorld()->GetGameState<AMCGameState>())
    {
        State->Phase=EMCShiftPhase::Working;
        State->PhaseEndsAt=0;
        State->bDevManualEvents=true;
    }
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Dispose();
    AMCTongue* Tongue=nullptr;
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!Tongue) return false;
    SocialForward=Chest->GetActorForwardVector(); SocialRight=Chest->GetActorRightVector();
    const FVector Center=Chest->GetLockpickContact().GetLocation()+SocialForward*500;
    FHitResult ObserverFloor,PartnerFloor;
    if (!Tongue->SurfacePoint(Center-SocialRight*90,ObserverFloor)
        || !Tongue->SurfacePoint(Center+SocialRight*90,PartnerFloor)
        || !PlacePlayer(ObserverFloor.ImpactPoint)) return false;
    const FVector PartnerLocation=PartnerFloor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    SocialPartner=GetWorld()->SpawnActor<AMCToothCharacter>(Hero->GetClass(),PartnerLocation,FRotator::ZeroRotator,Spawn);
    if (!SocialPartner || !SocialPartner->Expression || !SocialPartner->Inventory || !Hero->Expression || !Hero->Gaze) return false;
    SocialPartner->Tags.Add(TEXT("MC_SocialReview"));
    Hero->CancelGameplayInput(); SocialPartner->CancelGameplayInput();
    Hero->SetActorRotation((SocialPartner->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D().Rotation());
    SocialPartner->SetActorRotation((Hero->GetActorLocation()-SocialPartner->GetActorLocation()).GetSafeNormal2D().Rotation());
    Hero->Status->ApplyCoffee(0); SocialPartner->Status->ApplyCoffee(0);
    HeroHealthBefore=Hero->Status->State.Health; PartnerHealthBefore=SocialPartner->Status->State.Health;
    Stage=10; StageStartedAt=GetWorld()->GetTimeSeconds(); bSocialPhotographed=false;
    UE_LOG(LogTemp,Display,TEXT("MC_SOCIAL_REVIEW paired distance=%.1fcm observer_hp=%.0f sender_hp=%.0f"),
        FVector::Dist(Hero->GetActorLocation(),SocialPartner->GetActorLocation()),HeroHealthBefore,PartnerHealthBefore);
    Photograph(TEXT("SocialClean.png"),(Hero->GetActorLocation()+SocialPartner->GetActorLocation())*.5+FVector(0,0,30));
    return true;
}

void AMCRoguelikePreview::StepSocialReview(double Now)
{
    if (!IsValid(SocialPartner) || !SocialPartner->Status->IsAlive() || !Hero->Expression
        || !FMath::IsNearlyEqual(Hero->Status->State.Health,HeroHealthBefore)
        || !FMath::IsNearlyEqual(SocialPartner->Status->State.Health,PartnerHealthBefore))
    { Finish(false,TEXT("Social review lost an avatar or changed health during a cosmetic reaction.")); return; }
    const double Age=Now-StageStartedAt;
    const FVector Focus=(Hero->GetActorLocation()+SocialPartner->GetActorLocation())*.5+FVector(0,0,30);
    if (Stage==10 && Age>=1.4)
    {
        SocialPartner->Status->ApplyCoffee(1);
        Stage=11; StageStartedAt=Now; bSocialPhotographed=false;
        UE_LOG(LogTemp,Display,TEXT("MC_SOCIAL_REVIEW dirty partner; production gaze chooses its target."));
    }
    else if (Stage==11)
    {
        PeakDisgust=FMath::Max(PeakDisgust,Hero->Expression->SocialDisgust);
        if (!bSocialPhotographed && Age>=1.6)
        { bSocialPhotographed=true; Photograph(TEXT("SocialDisgust.png"),Focus); }
        if (Age>=2.4)
        {
            if (Hero->Gaze->Target.Actor!=SocialPartner || PeakDisgust<.1f)
            { Finish(false,FString::Printf(TEXT("Dirty avatar was not noticed through production gaze: target=%s disgust=%.3f"),*GetNameSafe(Hero->Gaze->Target.Actor),PeakDisgust)); return; }
            // Aim slightly left of the receiver's center: the actual can/nozzle
            // is held on the sender's right, rather than on the capsule axis.
            FRotator SprayFacing=(Hero->GetActorLocation()-SocialPartner->GetActorLocation()).GetSafeNormal2D().Rotation();
            SprayFacing.Yaw-=9.5f;
            SocialPartner->SetActorRotation(SprayFacing);
            SocialPartner->Inventory->ServerSelect(EMCToolSlot::Spray);
            SocialPartner->ServerSetPrimary(true);
            Stage=12; StageStartedAt=Now; bSocialPhotographed=false;
            UE_LOG(LogTemp,Display,TEXT("MC_SOCIAL_REVIEW spray held through production input and cone/LOS selection."));
        }
    }
    else if (Stage==12)
    {
        PeakSprayReaction=FMath::Max(PeakSprayReaction,Hero->Expression->SprayReaction);
        if (!bSocialPhotographed && Age>=1.2)
        { bSocialPhotographed=true; Photograph(TEXT("SocialSpray.png"),Focus); }
        if (Age>=3.)
        {
            SocialPartner->ServerSetPrimary(false);
            Stage=13; StageStartedAt=Now; bSocialPhotographed=false;
            UE_LOG(LogTemp,Display,TEXT("MC_SOCIAL_REVIEW spray released; receiver reaction peak=%.3f"),PeakSprayReaction);
        }
    }
    else if (Stage==13)
    {
        if (!bSocialPhotographed && Age>=1.7)
        { bSocialPhotographed=true; Photograph(TEXT("SocialRecovered.png"),Focus); }
        if (Age>=2.4)
        {
            const float Recovered=Hero->Expression->SprayReaction;
            Finish(PeakSprayReaction>.2f && Recovered<.08f,
                FString::Printf(TEXT("chest opening without modal -> three cards -> one choice; natural dirty-player gaze %.3f; production held spray %.3f -> recovered %.3f; HP %.0f/%.0f unchanged. Solo rendered review, network replication not verified."),
                    PeakDisgust,PeakSprayReaction,Recovered,HeroHealthBefore,PartnerHealthBefore));
        }
    }
}

bool AMCRoguelikePreview::SetBossPhase3Camera()
{
    auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr;
    USkeletalMeshComponent* Mesh=Boss?Boss->GetMesh():nullptr;
    if (!PC || !Mesh || !Mesh->GetSkeletalMeshAsset()) return false;
    const FBoxSphereBounds Bounds=Mesh->GetSkeletalMeshAsset()->GetBounds().TransformBy(Mesh->GetComponentTransform());
    if (Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN()
        || !FMath::IsFinite(Bounds.SphereRadius) || Bounds.SphereRadius<20.f || Bounds.SphereRadius>2000.f) return false;
    constexpr float FieldOfView=52.f,Aspect=16.f/9.f;
    const float VerticalHalfAngle=FMath::Atan(FMath::Tan(FMath::DegreesToRadians(FieldOfView*.5f))/Aspect);
    // A sphere around the full authored mesh, with room for hands, kicking and
    // the death pose, stays inside the narrower vertical field of view.
    const float Distance=Bounds.SphereRadius/FMath::Sin(VerticalHalfAngle)*1.22f;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBossPhase3ReviewCamera),false,Boss);
    Query.AddIgnoredActor(Hero);
    for (float Angle:{-25.f,25.f,-45.f,45.f,0.f})
    {
        const FVector Direction=(Boss->GetActorForwardVector().RotateAngleAxis(Angle,FVector::UpVector)+FVector(0,0,.18f)).GetSafeNormal();
        const FVector Eye=Bounds.Origin+Direction*Distance;
        bool Clear=true;
        for (float Height:{-.65f,0.f,.65f})
        {
            FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit,Eye,Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z*Height),ECC_Visibility,Query))
            { Clear=false; break; }
        }
        if (!Clear) continue;
        if (!Camera) Camera=GetWorld()->SpawnActor<ACameraActor>();
        if (!Camera) return false;
        Camera->SetActorLocationAndRotation(Eye,(Bounds.Origin-Eye).Rotation());
        Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
        Camera->GetCameraComponent()->SetAspectRatio(Aspect);
        PC->SetViewTarget(Camera);
        UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CAMERA center=%s extent=%s radius=%.2f distance=%.2f eye=%s fov=%.1f"),
            *Bounds.Origin.ToString(),*Bounds.BoxExtent.ToString(),Bounds.SphereRadius,Distance,*Eye.ToString(),FieldOfView);
        return true;
    }
    return false;
}

void AMCRoguelikePreview::RestoreBossPhase3View()
{
    auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr;
    if (!PC) return;
    PC->bAutoManageActiveCameraTarget=false;
    PC->ResetIgnoreMoveInput(); PC->SetIgnoreMoveInput(true);
    PC->ResetIgnoreLookInput(); PC->SetIgnoreLookInput(true);
    if (IsValid(Camera)) PC->SetViewTarget(Camera);
}

void AMCRoguelikePreview::RequestBossPhase3Action(EMCDevAction Action,int32 Index)
{
    if (auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr)
    {
        PC->RequestDevAction(Action,Index);
        // The normal F3 path updates input mode and can reset the controller
        // guards. This opt-in review owns its camera/guards after that call.
        RestoreBossPhase3View();
    }
}

bool AMCRoguelikePreview::BeginBossPhase3Clip(int32 Index,double Now)
{
    auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr;
    if (!PC || !IsValid(Boss) || !Phase3Clips.IsValidIndex(Index-1) || !Phase3Clips[Index-1]) return false;
    const int32 PreviousSerial=Boss->Runtime.PreviewSerial;
    RequestBossPhase3Action(EMCDevAction::BossPhase3Animation,Index);
    const auto Preview=static_cast<EMCBossAnimationPreview>(Index);
    if (Boss->Runtime.AnimationPreview!=Preview || Boss->Runtime.PreviewSerial!=PreviousSerial+1
        || Boss->Runtime.State!=EMCBossState::Dormant || !FMath::IsFinite(Boss->Runtime.PreviewStartedAt)) return false;
    const UAnimSingleNodeInstance* Instance=Boss->GetMesh()->GetSingleNodeInstance();
    // Native playback is verified after the mesh has evaluated its next frame.
    if (!Instance) return false;
    Phase3ClipIndex=Index;
    Phase3PreviewSerial=Boss->Runtime.PreviewSerial;
    Phase3PreviewStartedAt=Boss->Runtime.PreviewStartedAt;
    Phase3ClipOrigin=Boss->GetActorLocation();
    Phase3ClipDuration=FMath::Max(Phase3Clips[Index-1]->GetPlayLength()+.5f,Index<=2?3.f:1.f);
    Phase3PoseSamples=0; Phase3PeakBoneMotion=0;
    Phase3FirstBonePositions.Reset(); bPhase3Photographed=false;
    StageStartedAt=Now;
    const TCHAR* Names[]={TEXT("Idle"),TEXT("Walk"),TEXT("PunchLeft"),TEXT("PunchRight"),TEXT("Kick"),TEXT("Hurt"),TEXT("Death"),TEXT("Roar")};
    UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CLIP index=%d name=%s start=%.6f length=%.3f hold=%.3f serial=%d stamp=%.6f asset=%s"),
        Index,Names[Index-1],Now-StartedAt,Phase3Clips[Index-1]->GetPlayLength(),Phase3ClipDuration,
        Phase3PreviewSerial,Phase3PreviewStartedAt,*Phase3Clips[Index-1]->GetPathName());
    return true;
}

void AMCRoguelikePreview::StepBossPhase3Review(double Now)
{
    if (Now-StartedAt>100.) { Finish(false,FString::Printf(TEXT("Phase 3 review timed out stage=%d clip=%d."),Stage,Phase3ClipIndex)); return; }
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    auto* State=GetWorld()->GetGameState<AMCGameState>();
    if (!Mode || !State) return;
    if (Stage==0)
    {
        auto* PC=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
        Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
        if (Now-StartedAt<3. || !AMCBossCharacter::IsLivingPlayer(Hero) || !PC || !PC->CanUseDevPanel()) return;
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
        { Finish(false,TEXT("Normal map spawned a boss before the explicit F3 phase 3 review.")); return; }
        Mode->SetActorTickEnabled(false);
        if (Mode->DayDirector) Mode->DayDirector->SetActorTickEnabled(false);
        State->Phase=EMCShiftPhase::Working; State->PhaseEndsAt=0; State->bDevManualEvents=true;
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Dispose();
        Hero->CancelGameplayInput();
        // Stage on the actual tongue/nav surface so the two isolated F3 spawns
        // can choose different collision-free points in the same arena.
        auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld());
        const ANavigationData* HeroNav=Nav?Nav->GetNavDataForProps(Hero->GetNavAgentPropertiesRef(),Hero->GetActorLocation()):nullptr;
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            FHitResult Floor; FNavLocation Point;
            if (HeroNav && It->SurfacePoint(It->Surface->Bounds.Origin,Floor)
                && Nav->ProjectPointToNavigation(Floor.ImpactPoint,Point,FVector(200,200,250),HeroNav)) PlacePlayer(Point.Location);
            break;
        }
        HeroHealthBefore=Hero->Status->State.Health;
        RequestBossPhase3Action(EMCDevAction::BossPractice);
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if (It->ActorHasTag(TEXT("MC_DevBoss")) && !It->ActorHasTag(TEXT("MC_DevBossPhase3"))) { PhaseOneBoss=*It; break; }
        if (!IsValid(PhaseOneBoss) || !PhaseOneBoss->GetResolvedProfile() || !PhaseOneBoss->GetMesh()->GetSkeletalMeshAsset()
            || PhaseOneBoss->Runtime.State!=EMCBossState::Dormant || PhaseOneBoss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle)
        { Finish(false,TEXT("Existing phase 1 F3 action failed to create its dormant idle boss.")); return; }
        PhaseOneTransform=PhaseOneBoss->GetActorTransform();
        PhaseOneHealth=PhaseOneBoss->Runtime.Health; PhaseOnePreviewSerial=PhaseOneBoss->Runtime.PreviewSerial;
        PhaseOneProfilePath=PhaseOneBoss->GetResolvedProfile()->GetPathName();
        PhaseOneMeshPath=PhaseOneBoss->GetMesh()->GetSkeletalMeshAsset()->GetPathName();
        PhaseOneSkeletonPath=GetPathNameSafe(PhaseOneBoss->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton());
        RequestBossPhase3Action(EMCDevAction::BossPhase3Spawn);
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if (It->ActorHasTag(TEXT("MC_DevBoss")) && It->ActorHasTag(TEXT("MC_DevBossPhase3"))) { Boss=*It; break; }
        const UMCBossProfile* Profile=Boss?Boss->GetResolvedProfile():nullptr;
        USkeletalMesh* Mesh=Boss?Boss->GetMesh()->GetSkeletalMeshAsset():nullptr;
        if (!IsValid(Boss) || Boss==PhaseOneBoss || !Profile || !Mesh || !Mesh->GetSkeleton()
            || Boss->GetClass()->GetPathName()!=TEXT("/Game/Gameplay/Boss/Phase3/BP_BossPhase3.BP_BossPhase3_C")
            || Profile->GetPathName()==PhaseOneProfilePath || Mesh->GetPathName()==PhaseOneMeshPath
            || Mesh->GetSkeleton()->GetPathName()==PhaseOneSkeletonPath || !Profile->AnimationClass.IsNull()
            || Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle)
        { Finish(false,TEXT("Phase 3 F3 spawn lacks an isolated class/profile/mesh/skeleton and dormant native idle playback.")); return; }
        Phase3Clips={Profile->IdleAnimation.Get(),Profile->WalkAnimation.Get(),nullptr,nullptr,nullptr,
            Profile->HurtAnimation.Get(),Profile->DeathAnimation.Get(),Profile->RoarAnimation.Get()};
        for (const auto& Attack:Profile->Attacks)
        {
            const int32 Index=Attack.AttackId==TEXT("PunchLeft")?2:Attack.AttackId==TEXT("PunchRight")?3:Attack.AttackId==TEXT("Kick")?4:INDEX_NONE;
            if (Index==INDEX_NONE) continue;
            if (Phase3Clips[Index] || !FMath::IsFinite(Attack.WindupSeconds) || !FMath::IsFinite(Attack.ActiveSeconds)
                || !FMath::IsFinite(Attack.RecoverySeconds) || !FMath::IsFinite(Attack.CooldownSeconds)
                || Attack.WindupSeconds<.05f || Attack.ActiveSeconds<.05f || Attack.RecoverySeconds<.05f
                || Attack.CooldownSeconds<0.f || Attack.WindupSeconds+Attack.ActiveSeconds+Attack.RecoverySeconds>20.f)
            { Finish(false,TEXT("Phase 3 attack slot is duplicated or has invalid timing.")); return; }
            Phase3Clips[Index]=Attack.Animation.Get();
        }
        for (int32 I=0;I<Phase3Clips.Num();++I)
        {
            const UAnimSequence* Clip=Phase3Clips[I];
            if (!Clip || Clip->GetSkeleton()!=Mesh->GetSkeleton() || !FMath::IsFinite(Clip->GetPlayLength())
                || Clip->GetPlayLength()<.25f || Clip->GetPlayLength()>12.f)
            { Finish(false,FString::Printf(TEXT("Phase 3 clip %d is missing, uses another skeleton, or has invalid duration."),I+1)); return; }
        }
        BossStarted=Boss->GetActorLocation();
        const float Health=Boss->Runtime.Health;
        if (Boss->ReceiveBossDamage(10.f,Hero)!=0.f || !FMath::IsNearlyEqual(Boss->Runtime.Health,Health))
        { Finish(false,TEXT("Dormant phase 3 preview accepted combat damage.")); return; }
        if (!SetBossPhase3Camera()) { Finish(false,TEXT("No unobstructed full-body camera could be derived from the phase 3 mesh bounds.")); return; }
        RestoreBossPhase3View();
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
        Hero->SetActorHiddenInGame(true);
        UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_ISOLATION phase1_profile=%s phase1_mesh=%s phase1_skeleton=%s phase3_profile=%s phase3_mesh=%s phase3_skeleton=%s"),
            *PhaseOneProfilePath,*PhaseOneMeshPath,*PhaseOneSkeletonPath,*Profile->GetPathName(),*Mesh->GetPathName(),*Mesh->GetSkeleton()->GetPathName());
        Stage=1; StageStartedAt=Now; return;
    }
    if (!IsValid(Hero) || !Hero->Status || !Hero->Status->IsAlive() || !FMath::IsNearlyEqual(Hero->Status->State.Health,HeroHealthBefore))
    { Finish(false,TEXT("Review player disappeared or took damage.")); return; }
    auto* PC=Cast<AMCPlayerController>(Hero->GetController());
    if (!PC) { Finish(false,TEXT("Review lost its F3 controller.")); return; }
    RestoreBossPhase3View();
    if (Stage<=4 && !IsValid(Boss)) { Finish(false,TEXT("Phase 3 boss disappeared before F3 removal.")); return; }
    const double Age=Now-StageStartedAt;
    if (Stage==1 && Age>=.8)
    {
        if (Boss->Runtime.State!=EMCBossState::Dormant || !Boss->GetVelocity().IsNearlyZero()
            || FVector::Dist(BossStarted,Boss->GetActorLocation())>.1f)
        { Finish(false,TEXT("Phase 3 dormant idle enabled movement or AI.")); return; }
        RequestBossPhase3Action(EMCDevAction::BossPhase3Animation,2);
        if (Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Walk)
        { Finish(false,TEXT("Phase 3 F3 animation command selected the wrong boss.")); return; }
        RequestBossPhase3Action(EMCDevAction::BossPhase3Deactivate);
        if (!IsValid(PhaseOneBoss) || !PhaseOneBoss->GetResolvedProfile() || !PhaseOneBoss->GetMesh()->GetSkeletalMeshAsset()
            || PhaseOneBoss->GetResolvedProfile()->GetPathName()!=PhaseOneProfilePath
            || PhaseOneBoss->GetMesh()->GetSkeletalMeshAsset()->GetPathName()!=PhaseOneMeshPath
            || !PhaseOneBoss->GetActorTransform().Equals(PhaseOneTransform,.1f) || !FMath::IsNearlyEqual(PhaseOneBoss->Runtime.Health,PhaseOneHealth)
            || PhaseOneBoss->Runtime.PreviewSerial!=PhaseOnePreviewSerial || PhaseOneBoss->Runtime.State!=EMCBossState::Dormant
            || PhaseOneBoss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle
            || Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle)
        { Finish(false,TEXT("Phase 3 spawn/animation/deactivate changed phase 1 or failed to restore phase 3 idle.")); return; }
        RequestBossPhase3Action(EMCDevAction::BossRemove);
        if (IsValid(PhaseOneBoss) && !PhaseOneBoss->IsActorBeingDestroyed())
        { Finish(false,TEXT("Phase 1 F3 removal did not remove phase 1.")); return; }
        if (!IsValid(Boss) || Boss->IsActorBeingDestroyed()) { Finish(false,TEXT("Phase 1 F3 removal also removed phase 3.")); return; }
        UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CHECK PASS isolated F3 spawn/animation/deactivate/removal; phase 1 unchanged."));
        Stage=2;
        if (!BeginBossPhase3Clip(1,Now)) Finish(false,TEXT("F3 idle could not start through the owning player controller."));
        return;
    }
    if (Stage==2)
    {
        UAnimSequence* Clip=Phase3Clips[Phase3ClipIndex-1];
        USkeletalMeshComponent* Mesh=Boss->GetMesh();
        UAnimSingleNodeInstance* Instance=Mesh->GetSingleNodeInstance();
        if (Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=static_cast<EMCBossAnimationPreview>(Phase3ClipIndex)
            || Boss->Runtime.PreviewSerial!=Phase3PreviewSerial || Boss->Runtime.PreviewStartedAt!=Phase3PreviewStartedAt
            || !FMath::IsNearlyEqual(Boss->Runtime.Health,Boss->Runtime.MaxHealth) || !Boss->GetVelocity().IsNearlyZero()
            || FVector::Dist(Phase3ClipOrigin,Boss->GetActorLocation())>.1f || !Instance || Instance->GetCurrentAsset()!=Clip)
        {
            Finish(false,FString::Printf(TEXT("F3 clip invariant: clip=%d state=%d preview=%d serial=%d/%d stamp=%.9f/%.9f health=%.3f/%.3f velocity=%s distance=%.5f instance=%s current=%s expected=%s actor=%s origin=%s view=%s camera=%s"),
                Phase3ClipIndex,int32(Boss->Runtime.State),int32(Boss->Runtime.AnimationPreview),Boss->Runtime.PreviewSerial,Phase3PreviewSerial,
                Boss->Runtime.PreviewStartedAt,Phase3PreviewStartedAt,Boss->Runtime.Health,Boss->Runtime.MaxHealth,*Boss->GetVelocity().ToString(),
                FVector::Dist(Phase3ClipOrigin,Boss->GetActorLocation()),*GetNameSafe(Instance),*GetPathNameSafe(Instance?Instance->GetCurrentAsset():nullptr),
                *GetPathNameSafe(Clip),*Boss->GetActorLocation().ToString(),*Phase3ClipOrigin.ToString(),*GetNameSafe(PC->GetViewTarget()),*GetNameSafe(Camera)));
            return;
        }
        const bool Loop=Phase3ClipIndex<=2;
        const double ServerNow=State->GetServerWorldTimeSeconds();
        const float Elapsed=FMath::Max(0.f,float(ServerNow-Phase3PreviewStartedAt));
        const float Expected=Loop?FMath::Fmod(Elapsed,Clip->GetPlayLength()):FMath::Min(Elapsed,Clip->GetPlayLength());
        const float Actual=Instance->GetCurrentTime();
        float TimingError=FMath::Abs(Expected-Actual);
        if (Loop) TimingError=FMath::Min(TimingError,FMath::Abs(Clip->GetPlayLength()-TimingError));
        if (!FMath::IsFinite(Actual) || Actual<0.f || Actual>Clip->GetPlayLength()+.01f || TimingError>.25f)
        { Finish(false,FString::Printf(TEXT("Phase 3 clip %d did not follow its F3 timestamp: expected=%.3f actual=%.3f error=%.3f."),Phase3ClipIndex,Expected,Actual,TimingError)); return; }
        const float TanH=FMath::Tan(FMath::DegreesToRadians(Camera->GetCameraComponent()->FieldOfView*.5f));
        const float TanV=TanH/Camera->GetCameraComponent()->AspectRatio;
        for (int32 Bone=0;Bone<Mesh->GetNumBones();++Bone)
        {
            const FTransform Transform=Mesh->GetBoneTransform(Bone);
            const FVector Position=Transform.GetLocation();
            const FVector View=Camera->GetActorQuat().UnrotateVector(Position-Camera->GetActorLocation());
            if (Transform.ContainsNaN() || View.X<=0.f || FMath::Abs(View.Y/(View.X*TanH))>.94f || FMath::Abs(View.Z/(View.X*TanV))>.91f)
            { Finish(false,FString::Printf(TEXT("Phase 3 clip %d has an invalid/offscreen bone %s; position=%s view=%s."),Phase3ClipIndex,*Mesh->GetBoneName(Bone).ToString(),*Position.ToString(),*View.ToString())); return; }
            if (Phase3PoseSamples==0) Phase3FirstBonePositions.Add(Position);
            else Phase3PeakBoneMotion=FMath::Max(Phase3PeakBoneMotion,float(FVector::Dist(Position,Phase3FirstBonePositions[Bone])));
        }
        ++Phase3PoseSamples;
        if (!bPhase3Photographed && Age>=FMath::Min(Clip->GetPlayLength()*.55f,2.f))
        {
            bPhase3Photographed=true;
            const TCHAR* ClipNames[]={TEXT("Idle"),TEXT("Walk"),TEXT("PunchLeft"),TEXT("PunchRight"),TEXT("Kick"),TEXT("Hurt"),TEXT("Death"),TEXT("Roar")};
            Photograph(*FString::Printf(TEXT("Clip%02d_%s.png"),Phase3ClipIndex,ClipNames[Phase3ClipIndex-1]),Boss->GetActorLocation());
        }
        if (Age>=Phase3ClipDuration)
        {
            if (Phase3PoseSamples<3 || Phase3PeakBoneMotion<.05f)
            { Finish(false,FString::Printf(TEXT("Phase 3 clip %d remained static (%d samples, motion %.4fcm)."),Phase3ClipIndex,Phase3PoseSamples,Phase3PeakBoneMotion)); return; }
            UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CHECK PASS clip=%d samples=%d bone_motion=%.2fcm timing_error=%.3fs whole_pose_visible=1"),
                Phase3ClipIndex,Phase3PoseSamples,Phase3PeakBoneMotion,TimingError);
            if (Phase3ClipIndex<8)
            { if (!BeginBossPhase3Clip(Phase3ClipIndex+1,Now)) Finish(false,TEXT("Next phase 3 clip did not start through F3.")); return; }
            RequestBossPhase3Action(EMCDevAction::BossPhase3Deactivate);
            RequestBossPhase3Action(EMCDevAction::BossPhase3Activate);
            if (Boss->Runtime.State==EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::None || !Boss->GetController())
            { Finish(false,TEXT("Explicit phase 3 F3 AI activation failed.")); return; }
            UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CHECK PASS explicit AI activation state=%d preview=%d."),int32(Boss->Runtime.State),int32(Boss->Runtime.AnimationPreview));
            // Stop in the same review step: activation is tested without allowing
            // an attack timer to damage the observer during an animation reel.
            RequestBossPhase3Action(EMCDevAction::BossPhase3Deactivate);
            if (Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle
                || !FMath::IsNearlyEqual(Boss->Runtime.Health,Boss->Runtime.MaxHealth))
            { Finish(false,TEXT("Explicit phase 3 F3 deactivation failed to restore dormant idle.")); return; }
            Stage=3; StageStartedAt=Now; return;
        }
    }
    if (Stage==3 && Age>=1.)
    {
        if (Boss->Runtime.State!=EMCBossState::Dormant || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("Deactivated phase 3 resumed its AI or movement.")); return; }
        RequestBossPhase3Action(EMCDevAction::BossPhase3Remove);
        if (IsValid(Boss) && !Boss->IsActorBeingDestroyed()) { Finish(false,TEXT("Phase 3 F3 removal failed.")); return; }
        UE_LOG(LogTemp,Display,TEXT("MC_BOSS_PHASE3_CHECK PASS explicit AI activate/deactivate/remove; observer health=%.0f unchanged."),HeroHealthBefore);
        Stage=5; StageStartedAt=Now; return;
    }
    if (Stage==5 && Age>=.2)
    {
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if (!It->IsActorBeingDestroyed()) { Finish(false,TEXT("A test boss remained after isolated F3 removal.")); return; }
        Finish(true,TEXT("Phase 1 unchanged and separately removed; phase 3 owns its class/profile/mesh/skeleton and eight finite clips; actual F3 stamps drive evaluated moving poses, all bones fit the fixed full-body camera; previews dormant and damage-free; explicit AI activate/deactivate/remove passed. Solo review; network replication not exercised."));
    }
}

void AMCRoguelikePreview::Step()
{
    const double Now=GetWorld()->GetTimeSeconds();
    if (bBossPhase3Review) { StepBossPhase3Review(Now); return; }
    if (Now-StartedAt>60.)
    {
        Finish(false,FString::Printf(TEXT("Timeout stage=%d chest=%d boss=%d chase=%d target=%d telegraph=%d attack=%d"),
            Stage,Chest?int32(Chest->Stage):-1,Boss?int32(Boss->Runtime.State):-1,bSawChase,bSawTarget,bSawTelegraph,bSawAttack));
        return;
    }
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    if (!Mode || !Mode->RoguelikeDirector) return;
    if (Stage==0)
    {
        int32 LivingPlayers=0;
        for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* PC=It->Get(); auto* Player=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
            auto* PS=Player?Player->GetPlayerState<AMCPlayerState>():nullptr;
            if (!AMCBossCharacter::IsLivingPlayer(Player) || !PS || !PS->Perks || !PS->Perks->GetPerkTable()) continue;
            ++LivingPlayers; if (!Hero) { Hero=Player; Perks=PS->Perks; }
        }
        if (LivingPlayers<ExpectedPlayers || !Hero) return;
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
        { Finish(false,TEXT("Normal map contains a boss before an explicit F3 spawn.")); return; }
        for (const auto& Perk:Perks->ActivePerks) BeforeStacks+=Perk.Stacks;
        auto* Director=Mode->RoguelikeDirector.Get();
        BeforeSpawned=Director->RewardsSpawned;
        const int32 BeforeRewards=Director->PendingRewards+Director->RewardsSpawned;
        Mode->AwardTask(Hero,EMCScoreTask::Coffee);
        if (Director->PendingRewards+Director->RewardsSpawned!=BeforeRewards)
        { Finish(false,TEXT("An individual score award incorrectly created an objective reward.")); return; }
        Mode->NotifyObjectiveCompleted(TEXT("RoguePreviewObjective"));
        Mode->NotifyObjectiveCompleted(TEXT("RoguePreviewObjective"));
        if (Director->PendingRewards+Director->RewardsSpawned!=BeforeRewards+1)
        { Finish(false,TEXT("Objective completion did not enqueue exactly one deduplicated reward.")); return; }
        Stage=1; StageStartedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW Objective reward enqueued once through GameMode::NotifyObjectiveCompleted."));
        return;
    }
    if (!IsValid(Hero) || !IsValid(Perks) || !Hero->Status->IsAlive())
    { Finish(false,TEXT("Demonstration player died or disappeared before completion.")); return; }
    if (bChestSocialReview && Stage>=10) { StepSocialReview(Now); return; }
    if (Stage==1)
    {
        if (Mode->RoguelikeDirector->RewardsSpawned<=BeforeSpawned) return;
        if (!Chest) for (TActorIterator<AMCRewardChest> It(GetWorld());It;++It)
            if (!It->bPlacedReward && It->GetOwner()==Mode->RoguelikeDirector) { Chest=*It; break; }
        if (!IsValid(Chest) || Chest->Stage!=EMCRewardChestStage::Landed) return;
        FVector Approach=Chest->GetLockpickContact().GetLocation()+Chest->GetActorForwardVector()*140;
        Approach.Z=Chest->LandingPoint.Z;
        if (!PlacePlayer(Approach)) { Finish(false,TEXT("Safe chest approach teleport failed.")); return; }
        Hero->SetActorRotation((-Chest->GetActorForwardVector()).Rotation());
        // Exercise the production E interaction RPC instead of bypassing lockpicking.
        Hero->ServerBeginRewardOpening(Chest);
        const auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (Chest->Stage!=EMCRewardChestStage::Lockpicking || Chest->OpeningPlayer!=Hero
            || Hero->RewardInteraction!=Chest || !PC || !PC->IsRewardInteractionActive()
            || PC->IsRewardMenuOpen() || PC->bShowMouseCursor || !PC->IsMoveInputIgnored())
        { Finish(false,TEXT("Production E interaction did not reserve the chest and begin opening without a modal.")); return; }
        Photograph(TEXT("Chest.png"),Chest->LandingPoint+FVector(0,0,60));
        Stage=2; StageStartedAt=Now; return;
    }
    if (Stage==2)
    {
        if (!IsValid(Chest)) { Finish(false,TEXT("Chest disappeared before opening.")); return; }
        if (Chest->Stage==EMCRewardChestStage::Lockpicking && !bOpeningPhotographed && Now-StageStartedAt>=1.5)
        {
            const auto* PC=Cast<AMCPlayerController>(Hero->GetController());
            if (!PC || PC->IsRewardMenuOpen() || PC->bShowMouseCursor || !PC->IsMoveInputIgnored())
            { Finish(false,TEXT("Opening unexpectedly displayed a modal or released the movement guard.")); return; }
            bOpeningPhotographed=true;
            if(Hero->Brush) {
                const FVector Tip=Hero->Brush->DoesSocketExist(TEXT("LockpickTip"))
                    ?Hero->Brush->GetSocketLocation(TEXT("LockpickTip"))
                    :Hero->Brush->GetComponentTransform().TransformPosition(FVector(-17.64037,0,1.485929));
                UE_LOG(LogTemp,Display,TEXT("MC_CHEST_CONTACT tip_error=%.2fcm visible=%d stage_age=%.2fs"),
                    FVector::Dist(Tip,Chest->GetLockpickContact().GetLocation()),Hero->Brush->IsVisible(),Now-Chest->StageStartedAt);
            }
            Photograph(TEXT("ChestOpening.png"),Chest->LandingPoint+FVector(0,0,60));
        }
        if (Chest->Stage!=EMCRewardChestStage::Open) return;
        const auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (Now-StageStartedAt<4.9 || !bOpeningPhotographed || !PC || !PC->IsRewardMenuOpen()
            || !PC->PerkChoiceWidget || !PC->PerkChoiceWidget->IsShowingChoices())
        { Finish(false,TEXT("Chest skipped its five-second opening or failed to transition into HUD cards.")); return; }
        if (Chest->LootIDs.Num()!=3 || Chest->LootIDs[0]==Chest->LootIDs[1] || Chest->LootIDs[0]==Chest->LootIDs[2] || Chest->LootIDs[1]==Chest->LootIDs[2])
        { Finish(false,TEXT("Chest did not expose three distinct perk IDs.")); return; }
        for (FName ID:Chest->LootIDs)
        {
            FMCPerkDefinition Definition;
            if (!Perks->GetPerkDefinition(ID,Definition) || Definition.Polarity!=Chest->Polarity)
            { Finish(false,TEXT("Chest mixed positive and negative perk definitions.")); return; }
        }
        Photograph(TEXT("Choices.png"),Chest->LandingPoint+FVector(0,-70,60));
        Stage=3; StageStartedAt=Now; return;
    }
    if (Stage==3)
    {
        if (Now-StageStartedAt<1.) return;
        for (TActorIterator<AMCPerkPickup> It(GetWorld());It;++It)
            if (It->RewardChest==Chest) { Finish(false,TEXT("Card reward incorrectly spawned a world pickup.")); return; }
        if (!IsValid(Chest) || Chest->LootIDs.Num()!=3) { Finish(false,TEXT("Choice chest or card IDs disappeared.")); return; }
        auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        if (!PC || !PC->IsRewardMenuOpen()) { Finish(false,TEXT("Card choice modal closed before selection.")); return; }
        ChosenID=Chest->LootIDs[0];
        const int32 PreviousChosenStacks=Perks->GetStacks(ChosenID);
        PC->ChooseRewardPerk(0);
        // A repeated owning-controller RPC must remain harmless even after local UI has closed.
        PC->ServerChooseRewardPerk(Chest,0);
        int32 Stacks=0; for (const auto& Perk:Perks->ActivePerks) Stacks+=Perk.Stacks;
        if (Stacks!=BeforeStacks+1 || Perks->GetStacks(ChosenID)!=PreviousChosenStacks+1)
        { Finish(false,TEXT("Card RPC granted an incorrect stack count or accepted a duplicate.")); return; }
        if (Chest->ClaimedMask!=7 || Chest->Stage!=EMCRewardChestStage::Exhausted || PC->IsRewardMenuOpen() || Hero->RewardInteraction)
        { Finish(false,TEXT("Selection did not consume siblings and restore gameplay input.")); return; }
        if (bChestSocialReview)
        {
            if (!BeginSocialReview()) Finish(false,TEXT("No safe tongue staging point for the social reaction review."));
            return;
        }
        const FText SpawnResult=Mode->ExecuteDevAction(PC,EMCDevAction::BossPractice);
        for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            if (It->ActorHasTag(TEXT("MC_DevBoss"))) { Boss=*It; break; }
        if (!Boss || Boss->Runtime.State!=EMCBossState::Dormant || Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Idle)
        { Finish(false,FString::Printf(TEXT("F3 spawn did not create a dormant test-only boss: %s"),*SpawnResult.ToString())); return; }
        auto* NavSystem=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation NavPoint;
        const FVector Candidate=Boss->GetActorLocation()+Boss->GetActorForwardVector()*650;
        const ANavigationData* BossNav=NavSystem?NavSystem->GetNavDataForProps(Boss->GetNavAgentPropertiesRef(),Boss->GetActorLocation()):nullptr;
        if (!NavSystem || !BossNav || !NavSystem->ProjectPointToNavigation(Candidate,NavPoint,FVector(500,500,600),BossNav) || !PlacePlayer(NavPoint.Location))
        { Finish(false,TEXT("No safe navigable player staging point near the boss.")); return; }
        BossStarted=Boss->GetActorLocation();
        const float Health=Boss->Runtime.Health;
        Boss->ReceiveBossDamage(10.f,Hero);
        if (!FMath::IsNearlyEqual(Boss->Runtime.Health,Health)) { Finish(false,TEXT("Dormant F3 animation preview accepted combat damage.")); return; }
        Stage=4; StageStartedAt=Now;
        Photograph(TEXT("BossIdle.png"),Boss->GetActorLocation()+FVector(0,0,60));
        return;
    }
    if (Stage==4)
    {
        if (Now-StageStartedAt<.8) return;
        if (!IsValid(Boss) || Boss->Runtime.State!=EMCBossState::Dormant || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("F3 presentation preview started moving or activating AI.")); return; }
        auto* PC=Cast<AMCPlayerController>(Hero->GetController());
        // The second explicit F3 command, rather than ordinary gameplay, activates combat.
        const FText AIResult=Mode->ExecuteDevAction(PC,EMCDevAction::BossAI);
        if (Boss->Runtime.State==EMCBossState::Dormant)
        { Finish(false,FString::Printf(TEXT("F3 AI command did not activate the test boss: %s"),*AIResult.ToString())); return; }
        const float Health=Boss->Runtime.Health;
        const float Damage=FMath::Min(10.f,Health*.1f);
        Boss->ReceiveBossDamage(Damage,Hero);
        if (!FMath::IsNearlyEqual(Boss->Runtime.Health,Health-Damage)) { Finish(false,TEXT("Boss authoritative damage failed.")); return; }
        Stage=5; StageStartedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_ROGUE_PREVIEW HUD placeholder %s granted exactly once; F3 boss spawn/AI activated."),*ChosenID.ToString());
        return;
    }
    if (Stage==5)
    {
        if (!IsValid(Boss)) { Finish(false,TEXT("Boss disappeared during demonstration.")); return; }
        bSawTarget|=Boss->Runtime.Target==Hero;
        bSawChase|=Boss->Runtime.State==EMCBossState::Chasing && FVector::DistSquared(BossStarted,Boss->GetActorLocation())>FMath::Square(20.f);
        if (Boss->Runtime.State==EMCBossState::Telegraph && !bSawTelegraph)
        { bSawTelegraph=true; Photograph(TEXT("BossTelegraph.png"),Boss->GetActorLocation()+FVector(0,0,60)); }
        if (Boss->Runtime.State==EMCBossState::Attacking) bSawAttack=true;
        if (bSawTarget && bSawChase && bSawTelegraph && bSawAttack && Now-StageStartedAt>=5.)
        {
            Boss->ReceiveBossDamage(Boss->Runtime.MaxHealth,Hero);
            if (Boss->Runtime.State!=EMCBossState::Dead || Boss->IsBossAlive()) { Finish(false,TEXT("Boss death did not cancel behavior.")); return; }
            Stage=6; StageStartedAt=Now; return;
        }
    }
    if (Stage==6 && Now-StageStartedAt>=1.)
    {
        if (!IsValid(Boss) || Boss->Runtime.State!=EMCBossState::Dead || !Boss->GetVelocity().IsNearlyZero())
        { Finish(false,TEXT("Dead boss resumed behavior or movement.")); return; }
        Photograph(TEXT("BossDeath.png"),Boss->GetActorLocation()+FVector(0,0,40));
        Stage=7; StageStartedAt=Now; return;
    }
    if (Stage==7 && Now-StageStartedAt>=.4)
    {
        Finish(true,TEXT("no ordinary-map boss; objective->safe falling chest->E/five-second opening without a modal->three distinct same-polarity cards->one owning-controller choice/no pickups/no duplicate; explicit F3 dormant spawn/AI; boss damage/target/nav chase/telegraph/impact/death. Network replication not verified by this solo demonstration."));
    }
}
