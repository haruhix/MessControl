#include "MCFogBrawlSmoke.h"
#include "MCArenaTooth.h"
#include "MCDevCommands.h"
#include "MCFogBrawlEvent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGripComponent.h"
#include "MCPlayerController.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace MCFogBrawlSmokePrivate
{
template<class T> T* First(UWorld* World)
{
    for(TActorIterator<T> It(World);It;++It) if(!It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}
template<class T> int32 Count(UWorld* World)
{
    int32 Number=0; for(TActorIterator<T> It(World);It;++It) if(!It->IsActorBeingDestroyed()) ++Number;
    return Number;
}
constexpr uint32 ClientRequired=2047;
}

bool UMCFogBrawlSmoke::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
    return FParse::Param(FCommandLine::Get(),TEXT("MCFogBrawlSmoke")) && Super::ShouldCreateSubsystem(Outer);
#else
    return false;
#endif
}

void UMCFogBrawlSmoke::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); StartedAt=FPlatformTime::Seconds();
    FParse::Value(FCommandLine::Get(),TEXT("MCFogExpectedPlayers="),ExpectedPlayers); ExpectedPlayers=FMath::Clamp(ExpectedPlayers,1,2);
    bCapture=FParse::Param(FCommandLine::Get(),TEXT("MCFogCapture"));
    CaptureFolder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FogBrawlFrames"));
    FParse::Value(FCommandLine::Get(),TEXT("MCFogCaptureDir="),CaptureFolder);
}

void UMCFogBrawlSmoke::Fail(const TCHAR* Reason)
{
    if(bFinished) return; bFinished=true;
    UE_LOG(LogTemp,Error,TEXT("MC_FOG_SMOKE_FAIL role=%s stage=%d strikes=%d guards=%d remote_seen=%u reason=%s health=%.1f remote_health=%.1f"),
        GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"),Stage,Fog?Fog->StrikesResolved:-1,
        Fog?Fog->GuardCount:-1,ClientSeen,Reason,Hero && Hero->Status?Hero->Status->State.Health:-1,
        Remote && Remote->Status?Remote->Status->State.Health:-1);
    FPlatformMisc::RequestExitWithStatus(false,1);
}

void UMCFogBrawlSmoke::Enter(int32 Next,const TCHAR* Label)
{
    Stage=Next; StageAt=GetWorld()->GetTimeSeconds(); StageLabel=Label;
    UE_LOG(LogTemp,Display,TEXT("MC_FOG_STAGE stage=%d label=%s server_time=%.3f"),Stage,Label,StageAt);
}

void UMCFogBrawlSmoke::SetBraceKey(bool Held)
{
    if(bLocalBraceKeyHeld==Held || !Host || !Host->IsLocalController()) return;
    Host->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::RightMouseButton,
        Held?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
    bLocalBraceKeyHeld=Held; if(Held) ++InputPresses;
    UE_LOG(LogTemp,Display,TEXT("MC_FOG_RAW_RMB role=%s held=%d presses=%d"),
        GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"),Held,InputPresses);
}

bool UMCFogBrawlSmoke::RefreshCrew()
{
    Host=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    Hero=Host?Cast<AMCToothCharacter>(Host->GetPawn()):nullptr;
    if(!Tongue) Tongue=MCFogBrawlSmokePrivate::First<AMCTongue>(GetWorld());
    Remote=nullptr;
    if(GetWorld()->GetNetMode()!=NM_Client)
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if(auto* PC=It->Get();PC && PC!=Host && !PC->IsLocalController())
                if(auto* Worker=Cast<AMCToothCharacter>(PC->GetPawn());Worker && Worker->Status && Worker->Status->IsAlive()) { Remote=Worker; break; }
    return Host && Hero && Hero->Status && Hero->Grip && Tongue && Tongue->Surface && !Tongue->CurrentVertices().IsEmpty()
        && (GetWorld()->GetNetMode()==NM_Client || ExpectedPlayers==1 || Remote);
}

bool UMCFogBrawlSmoke::Place(AMCToothCharacter* Worker,FVector FloorPoint,FVector Facing)
{
    if(!Worker || !Worker->CanWork() || !Tongue) return false;
    FHitResult Floor;
    if(!Tongue->InteriorSurfacePoint(FloorPoint,45,Floor)) return false;
    const FVector Position=Floor.ImpactPoint+FVector::UpVector*(Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
    Worker->SetActorLocationAndRotation(Position,FRotator(0,Facing.Rotation().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
    Worker->GetCharacterMovement()->StopMovementImmediately(); Worker->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if(auto* PC=Cast<APlayerController>(Worker->GetController())) PC->SetControlRotation(Worker->GetActorRotation());
    Worker->ForceNetUpdate(); return true;
}

bool UMCFogBrawlSmoke::PlaceNear(AMCToothCharacter* Worker,int32 Side)
{
    if(!Worker || !Worker->CanWork() || !Fog || !IsValid(Fog->ActiveTarget) || !Fog->ActiveTarget->Body) return false;
    auto* Target=Fog->ActiveTarget.Get(); const FBoxSphereBounds Bounds=Target->Body->Bounds;
    const FVector Inward=(Tongue->Surface->Bounds.Origin-Bounds.Origin).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
    const float InitialAngle=Inward.Rotation().Yaw+(Side?40.f:-40.f);
    const float Radius=Worker->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight=Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFogSmokePlacement),false,Worker);
    for(int32 I=0;I<32;++I)
    {
        const float Angle=FMath::DegreesToRadians(InitialAngle+(I/2)*(I%2?-1.f:1.f)*11.25f);
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const float Extent=FMath::Abs(Direction.X)*Bounds.BoxExtent.X+FMath::Abs(Direction.Y)*Bounds.BoxExtent.Y;
        for(int32 Distance=0;Distance<3;++Distance)
        {
            const FVector XY=Bounds.Origin+Direction*(Extent+Radius+35+Distance*55);
            FHitResult Floor; if(!Tongue->InteriorSurfacePoint(XY,45,Floor)) continue;
            const FVector Position=Floor.ImpactPoint+FVector::UpVector*(HalfHeight+3);
            FVector Closest; const float BodyDistance=Target->Body->GetClosestPointOnCollision(Position,Closest);
            if(BodyDistance<0 || BodyDistance>Fog->GuardRadius-15) continue;
            if(GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) continue;
            FHitResult Wall; FCollisionQueryParams LOS(SCENE_QUERY_STAT(MCFogSmokeLOS),false,Worker); LOS.AddIgnoredActor(Target);
            for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) LOS.AddIgnoredActor(*It);
            if(GetWorld()->LineTraceSingleByChannel(Wall,Position,Closest,ECC_Visibility,LOS)) continue;
            if(Place(Worker,Floor.ImpactPoint,Bounds.Origin-Position)) return true;
        }
    }
    return false;
}

bool UMCFogBrawlSmoke::PlaceFar(AMCToothCharacter* Worker)
{
    if(!Worker || !Fog || !IsValid(Fog->ActiveTarget) || !Fog->ActiveTarget->Body) return false;
    const FVector Center=Fog->ActiveTarget->Body->Bounds.Origin;
    for(int32 I=0;I<32;++I)
    {
        const float Angle=I*UE_TWO_PI/32;
        const FVector Point=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(Fog->MarkerRevealDistance+600);
        if(Place(Worker,Point,Center-Point) && !Fog->ShouldRevealMarker(Worker)) return true;
    }
    return false;
}

bool UMCFogBrawlSmoke::SmokeCleared(const AMCFogBrawlEvent* Event) const
{
    if(!IsValid(Event)) return true;
    TInlineComponentArray<UExponentialHeightFogComponent*> Fogs(Event);
    for(auto* Component:Fogs) if(Component->IsVisible() || Component->FogDensity>.001f) return false;
    TInlineComponentArray<UPostProcessComponent*> Moods(Event);
    for(auto* Component:Moods) if(Component->bEnabled || Component->BlendWeight>.001f) return false;
    TInlineComponentArray<UProceduralMeshComponent*> Meshes(Event);
    for(auto* Component:Meshes) if(Component->IsVisible() || Component->GetNumSections()>0) return false;
    return !Event->TargetGlow || !Event->TargetGlow->IsVisible();
}

bool UMCFogBrawlSmoke::IsSmokeVisible(const AMCFogBrawlEvent* Event) const
{
    if(!IsValid(Event)) return false;
    TInlineComponentArray<UExponentialHeightFogComponent*> Components(Event);
    for(auto* Component:Components) if(Component->IsVisible() && Component->FogDensity>.01f) return true;
    return false;
}

void UMCFogBrawlSmoke::TickClient()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS || !RefreshCrew()) return;
    Fog=MCFogBrawlSmokePrivate::First<AMCFogBrawlEvent>(GetWorld());
    if(Fog)
    {
        if(Fog->Stage==EMCFogBrawlStage::SmokeIn)
        {
            ClientSeen|=1;
            if(ClientInitialHealth==0) ClientInitialHealth=Hero->Status->State.Health;
        }
        if(Fog->Stage==EMCFogBrawlStage::Warning && IsValid(Fog->ActiveTarget) && Fog->WarningRemaining()>0)
        {
            ClientSeen|=2;
            const bool Want=Fog->StrikesResolved==0 || Fog->StrikesResolved>=3;
            SetBraceKey(Want);
            if(ClientInputSerial!=Fog->StrikesResolved)
            {
                ClientInputSerial=Fog->StrikesResolved;
                UE_LOG(LogTemp,Display,TEXT("MC_FOG_CLIENT_INPUT strike=%d held=%d local_controller=%d"),ClientInputSerial,Want,Host->IsLocalController());
            }
            if(Fog->GuardCount==2 && Fog->ShouldRevealMarker(Hero)) ClientSeen|=4;
            if(Fog->StrikesResolved==3)
                for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if(It->State.bConsumed) ClientSeen|=32;
        }
        else SetBraceKey(Fog->Stage==EMCFogBrawlStage::Recovery && (Fog->StrikesResolved==3 || Fog->StrikesResolved==4));
        if(Fog->StrikesResolved>0 && Hero->Status->State.Health<ClientInitialHealth-14.5f) ClientSeen|=64;
        if(Fog->Stage==EMCFogBrawlStage::Recovery && !Hero->CanWork()) ClientSeen|=128;
        if(Fog->StrikesResolved>ClientCheckedStrikes && Fog->Stage==EMCFogBrawlStage::Recovery && GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-Fog->LastImpactAt>.2)
        {
            if(Fog->StrikesResolved==1)
            {
                if(Fog->LastGuardCount!=2 || !FMath::IsNearlyEqual(Fog->LastDamagePerGuard,15.f,.1f)) {Fail(TEXT("Client missed the replicated two-player shared strike"));return;}
                if(ClientInitialHealth==0 || !FMath::IsNearlyEqual(Hero->Status->State.Health,ClientInitialHealth-15,.5f)) {Fail(TEXT("Remote player did not receive exactly its fifteen-health share"));return;}
            }
            if(Fog->StrikesResolved==2 && Fog->LastGuardCount==1 && FMath::IsNearlyEqual(Fog->LastDamagePerGuard,30.f,.1f)) ClientSeen|=8;
            if(Fog->StrikesResolved==3 && Fog->LastGuardCount==0 && IsValid(Fog->ActiveTarget) && Fog->ActiveTarget->State.bLost) ClientSeen|=16;
            ClientCheckedStrikes=Fog->StrikesResolved;
            UE_LOG(LogTemp,Display,TEXT("MC_FOG_CLIENT_SNAPSHOT strikes=%d target=%s guards=%d damage=%.1f health=%.1f remote_seen=%u"),
                Fog->StrikesResolved,*GetNameSafe(Fog->ActiveTarget),Fog->LastGuardCount,Fog->LastDamagePerGuard,Hero->Status->State.Health,ClientSeen);
        }
        if(Fog->IsComplete())
        {
            if(Fog->StrikesResolved!=5) {Fail(TEXT("Client completion did not retain five actual strikes"));return;}
            ClientSeen|=256;
            if(SmokeCleared(Fog)) ClientSeen|=512;
        }
    }
    if(GS->DirectorState.CurrentTitle==TEXT("MC_FOG_SMOKE_DONE"))
    {
        if(MCFogBrawlSmokePrivate::Count<AMCFogBrawlEvent>(GetWorld())==0) ClientSeen|=1024;
        if(ClientSeen!=MCFogBrawlSmokePrivate::ClientRequired) {Fail(TEXT("Remote peer missed the replicated fog protocol, own input or smoke cleanup"));return;}
        SetBraceKey(false);
        UE_LOG(LogTemp,Display,TEXT("MC_FOG_SMOKE_PASS role=client remote_seen=%u own_enhanced_rmb=1 presses=%d shared_hp=15 lone_share=30 complete=1 reset=1"),ClientSeen,InputPresses);
        bFinished=true; FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void UMCFogBrawlSmoke::FrameCamera()
{
    if(!bCapture || !Hero || !Host) return;
    if(!CaptureCamera) CaptureCamera=GetWorld()->SpawnActor<ACameraActor>();
    const FVector Player=Hero->CanWork()?Hero->GetActorLocation():Hero->ToothPhysics->PhysicalLocation();
    const FVector Target=Fog && IsValid(Fog->ActiveTarget)?Fog->ActiveTarget->Visual->Bounds.Origin:Player;
    const FVector Aim=FMath::Lerp(Player,Target,.4f)+FVector(0,0,60);
    const FVector Inward=(Tongue->Surface->Bounds.Origin-Target).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector(-1,0,0));
    const FVector Eye=Aim+Inward*680+FVector(0,0,440);
    CaptureCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
    CaptureCamera->GetCameraComponent()->SetFieldOfView(60);
    CaptureCamera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
    CaptureCamera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
    Host->SetViewTarget(CaptureCamera);
}

void UMCFogBrawlSmoke::Capture()
{
    if(!bCaptureActive || !Host || !Host->IsLocalController() || FScreenshotRequest::IsScreenshotRequested()) return;
    const double Time=GetWorld()->GetTimeSeconds(); if(Time<NextCaptureAt) return;
    NextCaptureAt=Time+.045;
    const FString Name=FString::Printf(TEXT("Frame%05d.png"),Frame++);
    FScreenshotRequest::RequestScreenshot(CaptureFolder/Name,true,false);
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f,%s\n"),*Name,Time,*StageLabel),
        *(CaptureFolder/TEXT("FrameTimes.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}

void UMCFogBrawlSmoke::CheckStrike()
{
    const int32 Index=Fog->StrikesResolved-1;
    const int32 Guards=Index==2?0:(ExpectedPlayers==2 && Index!=1?2:1);
    const float Share=Guards?15.f*ExpectedPlayers/Guards:0;
    if(Fog->LastGuardCount!=Guards || Fog->LastLivingTeamCount!=ExpectedPlayers || !FMath::IsNearlyEqual(Fog->LastDamagePerGuard,Share,.1f))
    {Fail(TEXT("Actual server strike did not use the expected living crew and RMB guard share"));return;}
    if(Guards)
    {
        if(!PendingTarget || !PendingTarget->IsAvailable() || !FMath::IsNearlyEqual(PendingTarget->State.Health,TargetBeforeHealth,.1f))
        {Fail(TEXT("A guarded strike changed the real reserve tooth health"));return;}
        if(!FMath::IsNearlyEqual(Hero->Status->State.Health,BeforeHealth-Share,.5f)) {Fail(TEXT("Host guard did not receive the actual expected health damage"));return;}
        const float RemoteShare=ExpectedPlayers==2 && Index!=1?Share:0;
        if(Remote && !FMath::IsNearlyEqual(Remote->Status->State.Health,RemoteBeforeHealth-RemoteShare,.5f))
        {Fail(TEXT("Remote health did not match that player's real guard participation"));return;}
    }
    else
    {
        if(!PendingTarget || !PendingTarget->State.bLost || PendingTarget->State.Health!=0 || PendingTarget->IsAvailable())
        {Fail(TEXT("An unguarded strike did not lose the real reserve tooth through normal status"));return;}
        auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(GS->AvailableArenaTeeth()!=InitialReserve-1) {Fail(TEXT("Missed defence did not reduce the real reserve count"));return;}
    }
    ++CheckedStrikes;
    UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS strike=%d guards=%d living=%d damage_each=%.1f impulse=%.1f host_health=%.1f remote_health=%.1f saved=%d"),
        CheckedStrikes,Fog->LastGuardCount,Fog->LastLivingTeamCount,Fog->LastDamagePerGuard,Fog->LastImpulse,
        Hero->Status->State.Health,Remote?Remote->Status->State.Health:-1,Fog->bLastToothSaved);
    if(Index==0 && !bSawPush) {Fail(TEXT("First actual strike did not cause native push or ragdoll"));return;}
    if(Index==1 && (!bSawRecovery || !bSawBraceRelease || !bSawHeldReassert))
    {Fail(TEXT("Held Enhanced Input RMB did not reassert after ordinary ragdoll/getup without a second press"));return;}
}

void UMCFogBrawlSmoke::Tick(float Dt)
{
    if(bFinished) return;
    if(FPlatformTime::Seconds()-StartedAt>(bCapture?880:280)) {Fail(TEXT("Bounded fog integration timed out"));return;}
    if(GetWorld()->GetNetMode()==NM_Client) { TickClient(); return; }
#if WITH_EDITOR
    if(bCapture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>(); auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Time=GetWorld()->GetTimeSeconds();
    if(!bPrepared)
    {
        if(!Mode || !GS || Time<3 || GS->PlayerArray.Num()<ExpectedPlayers || !RefreshCrew()) return;
        if(!Mode->CanUseDevPanel(Host)) {Fail(TEXT("Authority cannot use the real F3 commands"));return;}
        Mode->ExecuteDevAction(Host,EMCDevAction::BotsStop);
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            TInlineComponentArray<UExponentialHeightFogComponent*> Components(*It);
            for(auto* Component:Components) OriginalFogVisibility.Add(Component,Component->IsVisible());
        }
        if(bCapture)
        {
            IFileManager::Get().MakeDirectory(*CaptureFolder,true);
            FFileHelper::SaveStringToFile(TEXT("frame,server_time,stage\n"),*(CaptureFolder/TEXT("FrameTimes.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            GetWorld()->GetWorldSettings()->SetTimeDilation(.20f);
        }
        const FText Feedback=Mode->ExecuteDevAction(Host,EMCDevAction::FogEvent);
        Fog=MCFogBrawlSmokePrivate::First<AMCFogBrawlEvent>(GetWorld()); RefreshCrew();
        if(!Fog || !Fog->IsActive() || Fog->bFailed || !GS->bDevManualEvents || !Hero) {Fail(TEXT("Clean production F3 fog launch failed"));return;}
        if(MCFogBrawlSmokePrivate::Count<AMCFogBrawlEvent>(GetWorld())!=1) {Fail(TEXT("Clean F3 start duplicated the fog owner"));return;}
        if(ExpectedPlayers==2 && Mode->CanUseDevPanel(Cast<APlayerController>(Remote->GetController()))) {Fail(TEXT("Remote client improperly has host F3 authority"));return;}
        InitialKnockdowns=Hero->ToothPhysics->KnockdownCount; InitialRecoveries=Hero->ToothPhysics->RecoveryCount;
        InitialReserve=GS->AvailableArenaTeeth();
        if(InitialReserve<3) {Fail(TEXT("Saved map lacks enough real reserve teeth for loss and consumed retarget"));return;}
        bPrepared=true; bCaptureActive=bCapture; CaptureStartedAt=Time;
        if(bCapture) { Host->ToggleDevPanel(); bPanelOpen=true; }
        UE_LOG(LogTemp,Display,TEXT("MC_FOG_F3_START PASS feedback=%s expected_players=%d reserve=%d"),*Feedback.ToString(),ExpectedPlayers,InitialReserve);
        Enter(1,TEXT("FOG_SMOKE_IN"));
    }
    // Once the remote has observed DONE it exits independently. Host teardown
    // must no longer require that disconnected pawn to still be present.
    if(Stage<10 && !RefreshCrew()) return;
    if(bPanelOpen && Time-StageAt>1) { Host->ToggleDevPanel(); bPanelOpen=false; }
    if(Stage==9)
    {
        if(Time-StageAt>3 && !FScreenshotRequest::IsScreenshotRequested())
        {
            bCaptureActive=false; GS->DirectorState.CurrentTitle=TEXT("MC_FOG_SMOKE_DONE"); GS->ForceNetUpdate();
            Enter(10,TEXT("FOG_TEST_DONE"));
        }
    }
    else if(Stage==10)
    {
        if(Time-StageAt>3)
        {
            UE_LOG(LogTemp,Display,TEXT("MC_FOG_SMOKE_PASS role=authority five_strikes=%d shared=1 lone=1 miss=1 retarget=1 native_push=%d native_recovery=%d proximity=1 complete=1 reset=1 frames=%d capture_seconds=%.2f"),
                CheckedStrikes,bSawPush,bSawRecovery,Frame,Time-CaptureStartedAt);
            bFinished=true; FPlatformMisc::RequestExitWithStatus(false,0);
        }
        return;
    }
    else
    {
        if(!IsValid(Fog) || Fog->bFailed) {Fail(TEXT("Production fog failed or disappeared before full completion"));return;}
        bSawSmoke|=IsSmokeVisible(Fog);
        bSawPush|=Hero->ToothPhysics->KnockdownCount>InitialKnockdowns
            || (Fog->StrikesResolved>0 && FVector::DistSquared(Hero->GetActorLocation(),BeforeLocation)>FMath::Square(65.f));
        bSawRecovery|=Hero->ToothPhysics->RecoveryCount>InitialRecoveries;
        if(Fog->StrikesResolved==1 && bLocalBraceKeyHeld && !Hero->CanWork() && !Hero->Grip->IsBraceInputHeld()) bSawBraceRelease=true;
        if(Fog->Stage==EMCFogBrawlStage::Warning && Fog->StrikesResolved==1 && Fog->IsGuarding(Hero) && InputPresses==1) bSawHeldReassert=true;
        if(Fog->StrikesResolved>CheckedStrikes)
        {
            if(Time-Fog->LastImpactAt>.2) { CheckStrike(); if(bFinished) return; Enter(6,TEXT("FOG_NATIVE_RECOVERY")); }
        }
        else if(Fog->IsComplete())
        {
            if(!bCompletionChecked)
            {
                if(CheckedStrikes!=5 || !bSawSmoke || !bFarChecked || !bNearChecked || !bRetargetChecked || !SmokeCleared(Fog))
                {Fail(TEXT("Completion missed a required real gameplay, proximity or smoke-reset check"));return;}
                for(auto& Entry:OriginalFogVisibility) if(Entry.Key.IsValid() && Entry.Key->IsVisible()!=Entry.Value)
                {Fail(TEXT("Complete event did not restore existing map fog visibility"));return;}
                bCompletionChecked=true; Enter(8,TEXT("FOG_COMPLETE"));
                UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS complete_five=1 smoke_reset=1 original_fog_restored=1"));
            }
            if(Time-StageAt>5 && (!bCapture || Time-CaptureStartedAt>=65) && !FScreenshotRequest::IsScreenshotRequested())
            {
                SetBraceKey(false);
                Mode->ExecuteDevAction(Host,EMCDevAction::FogEventStop);
                if(MCFogBrawlSmokePrivate::Count<AMCFogBrawlEvent>(GetWorld())!=0) {Fail(TEXT("Real F3 stop retained the completed fog actor"));return;}
                Fog=nullptr; Enter(9,TEXT("FOG_STOP_CLEAN"));
                UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS f3_stop_full_cleanup=1"));
            }
        }
        else if(Fog->Stage==EMCFogBrawlStage::Warning && IsValid(Fog->ActiveTarget))
        {
            const int32 Strike=Fog->StrikesResolved;
            if(WarningSerial!=Strike)
            {
                WarningSerial=Strike; PendingTarget=Fog->ActiveTarget;
                BeforeHealth=Hero->Status->State.Health; RemoteBeforeHealth=Remote?Remote->Status->State.Health:0;
                TargetBeforeHealth=PendingTarget->State.Health;
                Enter(2,Strike==2?TEXT("FOG_UNBRACED_WARNING"):Strike==1?TEXT("FOG_LONE_DEFENDER"):TEXT("FOG_GUARDED_WARNING"));
            }
            if(Strike==0 && !bFarChecked)
            {
                if(!PlaceFar(Hero)) {Fail(TEXT("No supported far-search point hides the marker from the hero"));return;}
                if(Fog->ShouldRevealMarker(Hero)) {Fail(TEXT("Marker leaked beyond the hero's proximity radius"));return;}
                SetBraceKey(false); bFarChecked=true; Enter(3,TEXT("FOG_SEARCH_FAR"));
                UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS far_hero_marker_hidden=1"));
            }
            const bool Search=Strike==0 && Stage==3 && Time-StageAt<1.35;
            if(Search && Time-StageAt>.2 && ((Fog->TargetGlow && Fog->TargetGlow->IsVisible()) || Fog->Pulse->IsVisible()))
            {Fail(TEXT("The actual red tooth presentation leaked at the far search position"));return;}
            if(!Search)
            {
                if(Hero->CanWork() && !PlaceNear(Hero,0)) {Fail(TEXT("Selected target has no reachable supported host guard position"));return;}
                SetBraceKey(Strike!=2);
                if(Strike==0 && !bNearChecked && Fog->ShouldRevealMarker(Hero)
                    && Fog->TargetGlow && Fog->TargetGlow->IsVisible() && Fog->TargetGlow->GetMaterial(0)
                    && Fog->Pulse->IsVisible())
                {
                    bNearChecked=true; Enter(4,TEXT("FOG_NEAR_RED_TOOTH"));
                    UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS near_hero_marker_visible=1"));
                }
            }
            if(Remote && Remote->CanWork() && !PlaceNear(Remote,1)) {Fail(TEXT("Selected target has no reachable supported remote guard position"));return;}
            if(Strike==3 && !bRetargetRequested && Fog->WarningRemaining()>2.8f && Time-StageAt>1)
            {
                ConsumedTarget=Fog->ActiveTarget; RetargetDeadline=Fog->WarningEndsAt;
                if(!ConsumedTarget->ConsumeForRespawn()) {Fail(TEXT("Real reserve-consumption transition failed"));return;}
                bRetargetRequested=true; Enter(5,TEXT("FOG_CONSUMED_RETARGET"));
            }
            if(bRetargetRequested && !bRetargetChecked && Fog->ActiveTarget!=ConsumedTarget)
            {
                if(!ConsumedTarget->State.bConsumed || Fog->StrikesResolved!=3 || Fog->WarningEndsAt<RetargetDeadline+.5)
                {Fail(TEXT("Consumed target did not restart a fresh full warning without spending a strike"));return;}
                bRetargetChecked=true; PendingTarget=Fog->ActiveTarget; TargetBeforeHealth=PendingTarget->State.Health;
                UE_LOG(LogTemp,Display,TEXT("MC_FOG_CHECK PASS consumed_retarget=1 fresh_warning=1"));
            }
            if(Fog->WarningRemaining()<.15 && !Search)
            {
                if(Strike==0 && !bNearChecked) {Fail(TEXT("The real near-tooth red glow and pulse did not become visible"));return;}
                const int32 Required=Strike==2?0:ExpectedPlayers==2 && Strike!=1?2:1;
                if(Fog->GuardCount!=Required) {Fail(TEXT("Ordinary local and remote RMB input did not reach expected server guard count before impact"));return;}
                BeforeLocation=Hero->GetActorLocation();
            }
        }
        else if(Fog->Stage==EMCFogBrawlStage::Recovery) SetBraceKey(Fog->StrikesResolved==1 || Fog->StrikesResolved==3 || Fog->StrikesResolved==4);
    }
    if(bCapture) FrameCamera();
    Capture();
}
