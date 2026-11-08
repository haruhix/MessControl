#if !UE_BUILD_SHIPPING
#include "MCActiveSkillAction.h"
#include "MCActiveSkillMotion.h"
#include "MCFoodActor.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHookSkillAction.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCRocketSkillAction.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace MCActiveSkillNetworkSmoke
{
constexpr int32 FixtureID=5492;
// Preparation shares the real loading barrier and may take ~78 seconds cold.
// The three action stages plus recovery take ~37 seconds AFTER that barrier;
// keep their independent bounded workload inside a 180-second total wall cap.
constexpr double WallTimeoutSeconds=180;
const FName HookID(TEXT("Active_Hook")),RocketID(TEXT("Consumable_Rocket"));

struct FObservedAction
{
    TWeakObjectPtr<AMCActiveSkillAction> Actor;
    FDelegateHandle CueHandle;
    FGuid ID,SlotInstance;
    FName Perk;
    uint32 PressSequence=0;
    int32 PlayerID=INDEX_NONE,PulledPlayerID=INDEX_NONE;
    int32 Start=0,Impact=0,End=0,Destroyed=0,Detonations=0,HandSamples=0;
    uint32 Phases=0;
    bool Committed=false,Contact=false,PullSample=false,ImpactedBeforeExpiry=false;
    FVector PullStart=FVector::ZeroVector;
    float PullDistance=0,HandError=0,HandAngle=0;
};

struct FRun
{
    TWeakObjectPtr<UWorld> World;
    FDelegateHandle SpawnHandle,DestroyHandle;
    TMap<TWeakObjectPtr<AMCActiveSkillAction>,FObservedAction> Actions;
    double StartedWall=FPlatformTime::Seconds();
    double NextLog=0;
    int32 LocalStage=INDEX_NONE;
    bool Setup=false,Failed=false,Selected=false,Aimed=false,Fired=false,Released=false,Cleaned=false;
    bool RecoveryInput=false,CleanupPeersObserved=false;
    int32 RocketCharges=0;
    FGuid RocketInstance;
    FVector Lane=FVector::ZeroVector,MovementStart=FVector::ZeroVector;
    float MovementDistance=0;
};

void Observe(FObservedAction& O,AMCActiveSkillAction* A,bool SampleHands)
{
    O.ID=A->GetActionID();O.SlotInstance=A->GetSlotInstanceID();O.PressSequence=A->GetPressSequence();
    O.Perk=A->GetPerkID();O.Committed|=A->IsCommitted();
    if(const auto* State=A->SourceState()) O.PlayerID=State->GetPlayerId();
    if(const auto* Hook=Cast<AMCHookSkillAction>(A)) {
        O.Phases|=1u<<uint8(Hook->GetCastPhase());O.Contact|=Hook->DidContactTarget();
        if(IsValid(Hook->PulledPawn)) {
            if(const auto* State=Hook->PulledPawn->GetPlayerState()) O.PulledPlayerID=State->GetPlayerId();
            if(!O.PullSample) {O.PullStart=Hook->PulledPawn->GetActorLocation();O.PullSample=true;}
            O.PullDistance=FMath::Max(O.PullDistance,float(FVector::Dist(O.PullStart,Hook->PulledPawn->GetActorLocation())));
        }
    }
    if(const auto* Rocket=Cast<AMCRocketSkillAction>(A)) {
        O.Phases|=1u<<uint8(Rocket->Phase);O.Detonations=FMath::Max(O.Detonations,Rocket->DetonationCount);
        // Natural timeout also detonates once. Only an actual flight contact
        // can detonate before its unchanged production deadline in this case.
        O.ImpactedBeforeExpiry|=Rocket->Phase==EMCRocketSkillPhase::Detonated
            && Rocket->FlightEndsAt>0 && Rocket->Now()+.05<Rocket->FlightEndsAt;
    }
    auto* Hero=A->Hero();
    if(!SampleHands || !Hero || Hero->GetActiveSkillPresentation()!=A) return;
    for(bool Left:{false,true}) {
        FTransform Target;
        const FName Bone=Hero->RigBone(Left?TEXT("hand_l"):TEXT("hand_r"));
        if(!Hero->ActiveSkillHandTarget(Left,Target) || !Hero->GetMesh()->DoesSocketExist(Bone)) continue;
        const FTransform Visible=Hero->GetMesh()->GetSocketTransform(Bone);
        if(Target.ContainsNaN() || Visible.ContainsNaN()) continue;
        ++O.HandSamples;
        O.HandError=FMath::Max(O.HandError,float(FVector::Dist(Target.GetLocation(),Visible.GetLocation())));
        O.HandAngle=FMath::Max(O.HandAngle,float(FMath::RadiansToDegrees(Target.GetRotation().AngularDistance(Visible.GetRotation()))));
    }
}

void StopObserving(FRun& R)
{
    if(auto* World=R.World.Get()) {
        World->RemoveOnActorSpawnedHandler(R.SpawnHandle);
        World->RemoveOnActorDestroyedHandler(R.DestroyHandle);
    }
    for(auto& Pair:R.Actions) if(auto* A=Pair.Value.Actor.Get()) A->OnVFXStage.Remove(Pair.Value.CueHandle);
}

void StartObserving(FRun& R,UWorld* World)
{
    R.World=World;
    R.SpawnHandle=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&R](AActor* Actor) {
        auto* A=Cast<AMCActiveSkillAction>(Actor);if(!A) return;
        const TWeakObjectPtr<AMCActiveSkillAction> Key(A);
        auto& O=R.Actions.FindOrAdd(Key);O.Actor=A;
        // Deferred preparation has not committed yet. Bind before StartCommitted,
        // then resolve the real action/press identity when its native cue arrives.
        O.CueHandle=A->OnVFXStage.AddLambda([&R,Key](FName Stage) {
            auto* Action=Key.Get();if(!Action) return;
            auto& Seen=R.Actions.FindChecked(Key);Observe(Seen,Action,false);
            if(Stage==TEXT("Start")) ++Seen.Start;
            else if(Stage==TEXT("Impact")) ++Seen.Impact;
            else if(Stage==TEXT("End")) ++Seen.End;
            UE_LOG(LogTemp,Display,TEXT("MC_SKILL_NET_CUE net=%d action=%s perk=%s player=%d stage=%s"),
                int32(Action->GetNetMode()),*Seen.ID.ToString(),*Seen.Perk.ToString(),Seen.PlayerID,*Stage.ToString());
        });
    }));
    R.DestroyHandle=World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda([&R](AActor* Actor) {
        auto* A=Cast<AMCActiveSkillAction>(Actor);if(!A) return;
        if(auto* O=R.Actions.Find(TWeakObjectPtr<AMCActiveSkillAction>(A))) {Observe(*O,A,false);++O->Destroyed;}
    }));
}

bool LocalReady(const AMCPlayerController* PC,const AMCToothCharacter* Hero)
{
    return PC && Hero && PC->GetPawn()==Hero && PC->IsWorldPresentationReady() && PC->HasAcknowledgedGameplay()
        && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() && !PC->bShowMouseCursor && !Hero->IsArenaArrivalHeld();
}

bool Place(AMCTongue* Tongue,AMCToothCharacter* Hero,FVector Point)
{
    FHitResult Floor;if(!Tongue->SurfacePoint(Point,Floor) || Floor.ImpactNormal.Z<.65f) return false;
    Hero->CancelGameplayInput();Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),
        FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    Hero->NotifyNetworkPositionReset();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);Hero->ForceNetUpdate();return true;
}

bool FindLane(UWorld* World,AMCTongue* Tongue,FVector& Lane)
{
    const FVector Center=Tongue->GetActorLocation();
    for(float Y:{-250.f,0.f,250.f}) for(float X:{-450.f,-300.f,-150.f}) {
        const FVector A=Center+FVector(X,Y,0),B=A+FVector(600,0,0);FHitResult Start,End;
        if(!Tongue->SurfacePoint(A,Start) || !Tongue->SurfacePoint(B,End) || FMath::Abs(Start.ImpactPoint.Z-End.ImpactPoint.Z)>30) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSkillNetworkLane),false);
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) Query.AddIgnoredActor(*It);
        FHitResult Block;
        if(World->LineTraceSingleByChannel(Block,Start.ImpactPoint+FVector(0,0,90),End.ImpactPoint+FVector(0,0,90),ECC_Visibility,Query)) continue;
        Lane=A;return true;
    }
    return false;
}
}

// Two separate game processes: genuine owned input/RPCs; no direct action spawn,
// launch, contact, charge debit, phase, damage or motion writes during an action.
void MCTickActiveSkillNetworkValidation(UWorld* World)
{
    using namespace MCActiveSkillNetworkSmoke;
    static TUniquePtr<FRun> Run;
    if(!Run || Run->World.Get()!=World) {
        if(Run) StopObserving(*Run);
        Run=MakeUnique<FRun>();StartObserving(*Run,World);
    }
    FRun& R=*Run;
    const bool Host=World->GetNetMode()!=NM_Client;
    auto Check=[&](bool Pass,const TCHAR* Text) {
        R.Failed|=!Pass;UE_LOG(LogTemp,Display,TEXT("MC_SKILL_NET_CHECK net=%d %s %s"),int32(World->GetNetMode()),Pass?TEXT("PASS"):TEXT("FAIL"),Text);
    };
    auto Finish=[&](bool Pass,const TCHAR* Reason) {
        int32 Commits=0,Hooks=0,Rockets=0,Contacts=0,Detonations=0,Destroyed=0;
        for(const auto& Pair:R.Actions) {
            const auto& O=Pair.Value;if(!O.Committed) continue;
            ++Commits;Hooks+=O.Perk==HookID;Rockets+=O.Perk==RocketID;Contacts+=O.Contact;Detonations+=O.Detonations;Destroyed+=O.Destroyed;
            UE_LOG(LogTemp,Display,TEXT("MC_SKILL_NET_ACTION net=%d action=%s instance=%s press=%u perk=%s player=%d pulled_player=%d phases=%u start=%d impact=%d end=%d destroyed=%d contact=%d pull_cm=%.2f detonations=%d impact_before_expiry=%d hand_samples=%d hand_cm=%.2f hand_deg=%.2f"),
                int32(World->GetNetMode()),*O.ID.ToString(),*O.SlotInstance.ToString(),O.PressSequence,*O.Perk.ToString(),O.PlayerID,O.PulledPlayerID,O.Phases,O.Start,O.Impact,O.End,O.Destroyed,O.Contact,O.PullDistance,O.Detonations,O.ImpactedBeforeExpiry,O.HandSamples,O.HandError,O.HandAngle);
        }
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s ACTIVE_SKILL_NETWORK net=%d commits=%d hooks=%d rockets=%d contacts=%d detonations=%d destroyed=%d recovery_move_cm=%.2f reason=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),Commits,Hooks,Rockets,Contacts,Detonations,Destroyed,R.MovementDistance,Reason);
        StopObserving(R);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    double TotalWallBudget=WallTimeoutSeconds;
    FParse::Value(FCommandLine::Get(),TEXT("MCActiveSkillNetworkTimeout="),TotalWallBudget);
    TotalWallBudget=FMath::Clamp(TotalWallBudget,60.,600.);
    // A rendered review may spend longer in real prewarm. The per-action
    // deadlines and all outcome checks below remain identical.
    if(FPlatformTime::Seconds()-R.StartedWall>TotalWallBudget) {Finish(false,TEXT("ReadinessOrScenarioTimeout"));return;}
    auto* GS=World->GetGameState<AMCGameState>();auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* Own=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    if(!GS || !PC || !Own || !Tongue) return;
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B) {return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    // The remote exits one second before the host. Keep the host's final report
    // reachable after that expected disconnect, only after both clean pawns
    // were actually observed together during the cleanup stage.
    const bool Finishing=GS->TasksTotal==FixtureID && GS->TasksLeft==3 && R.CleanupPeersObserved;
    if((Heroes.Num()!=2 || GS->PlayerArray.Num()!=2) && !Finishing) return;
    const double Now=GS->GetServerWorldTimeSeconds();
    if(Host && !R.Setup) {
        bool Ready=LocalReady(PC,Own);
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It) {
            const auto* P=Cast<AMCPlayerController>(It->Get());const auto* H=P?Cast<AMCToothCharacter>(P->GetPawn()):nullptr;
            Ready&=P && H && P->HasAcknowledgedGameplay() && !H->IsArenaArrivalHeld()
                && (P->IsLocalController() || P->AcknowledgedPawn==H);
        }
        if(!Ready) return;
        Check(Own==Heroes[0],TEXT("sorted first participant is the real listen host"));
        Check(FindLane(World,Tongue,R.Lane),TEXT("actual tongue has a clear supported 600cm hook lane"));
        if(R.Failed) {Finish(false,TEXT("Setup"));return;}
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();Tongue->Settings.bAutomaticJolts=false;Tongue->ForceNetUpdate();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        for(auto* Hero:Heroes) {
            Hero->CancelGameplayInput();Hero->Status->Initialize(100);
            auto* Perks=Hero->GetPlayerState<AMCPlayerState>()->Perks.Get();
            Check(Perks && Perks->TryAcquirePerk(HookID,0),TEXT("authority grants production Hook to this participant"));
        }
        auto* RemotePerks=Heroes[1]->GetPlayerState<AMCPlayerState>()->Perks.Get();
        Check(RemotePerks && RemotePerks->TryAcquirePerk(RocketID,1),TEXT("authority grants production Rocket to remote participant"));
        if(R.Failed) {Finish(false,TEXT("Grant"));return;}
        R.RocketCharges=RemotePerks->ActiveSkillSlots[1].Charges;R.RocketInstance=RemotePerks->ActiveSkillSlots[1].InstanceID;
        Check(Place(Tongue,Heroes[1],R.Lane) && Place(Tongue,Heroes[0],R.Lane+FVector(600,0,0)),TEXT("remote Hook setup uses actual supported pawns"));
        GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        GS->TasksTotal=FixtureID;GS->TasksLeft=0;GS->DayStartedAt=Now+2;GS->ForceNetUpdate();R.Setup=true;
    }
    if(GS->TasksTotal!=FixtureID || !GS->bDevManualEvents) return;
    for(auto* Hero:Heroes) if(Hero->GetActorTransform().ContainsNaN() || Hero->GetVelocity().ContainsNaN()) {
        Finish(false,TEXT("NonFinitePawnMotion"));return;
    }
    const int32 Stage=GS->TasksLeft;const double T=Now-GS->DayStartedAt;
    if(R.LocalStage!=Stage) {
        R.LocalStage=Stage;R.Selected=R.Aimed=R.Fired=R.Released=false;
        PC->CancelSkillInputs();Own->CameraOrbitHeight=30;
        Own->SetCameraOrbitView(0,Stage==2?-12.f:0.f,300);PC->SetControlRotation(FRotator(Stage==2?-12.f:0.f,0,0));
        PC->SetViewTarget(Own);
        UE_LOG(LogTemp,Display,TEXT("MC_SKILL_NET_STAGE net=%d stage=%d local_player=%d role=%d"),int32(World->GetNetMode()),Stage,Own->GetPlayerState()->GetPlayerId(),int32(Own->GetLocalRole()));
        if(Stage==3) {R.RecoveryInput=false;R.MovementDistance=0;}
    }
    for(auto& Pair:R.Actions) if(auto* A=Pair.Value.Actor.Get()) Observe(Pair.Value,A,true);
    if(T<0) return;
    if(Stage<3) {
        const bool Caster=(Stage==1?Own==Heroes[0]:Own==Heroes[1]);const int32 Slot=Stage==2?1:0;
        if(Caster && LocalReady(PC,Own)) {
            const auto* Perks=Own->GetPlayerState<AMCPlayerState>()->Perks.Get();
            if(!R.Selected && T>.2 && Perks && Perks->ActiveSkillSlots.IsValidIndex(Slot)
                && Perks->ActiveSkillSlots[Slot].PerkID==(Stage==2?RocketID:HookID)) {
                PC->BeginSkillInput(Slot);R.Selected=true;
            }
            if(R.Selected && !R.Aimed && T>.5) {Check(PC->RouteSkillAimInput(true),TEXT("real local RMB skill aim consumed"));R.Aimed=true;}
            // The production shoulder camera shifts 140cm sideways and 75cm
            // upward. Aim from its ACTUAL settled view, never assume pawn +X is
            // the camera ray or enlarge the target/collision to force contact.
            if(R.Aimed && !R.Fired) {
                FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
                FVector Aim=Heroes[Stage==1?1:0]->GetActorLocation();
                if(Stage==2) {
                    FHitResult Floor;
                    if(!Tongue->SurfacePoint(Own->GetActorLocation()+FVector(700,0,0),Floor)) {Finish(false,TEXT("RocketFloorTarget"));return;}
                    Aim=Floor.ImpactPoint;
                }
                const FRotator Desired=(Aim-Eye).Rotation();
                Own->SetCameraOrbitView(Desired.Yaw,Desired.Pitch,300);PC->SetControlRotation(Desired);
            }
            if(R.Aimed && !R.Fired && T>1) {
                FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
                FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSkillNetworkAim),false,Own);
                const bool AimedAtTarget=Stage==2?(World->LineTraceSingleByChannel(Hit,Eye,Eye+View.Vector()*3000,ECC_Visibility,Query)
                        && Hit.GetActor()==Tongue && Hit.ImpactNormal.Z>.65f)
                    :Heroes[Stage==1?1:0]->GetCapsuleComponent()->LineTraceComponent(Hit,Eye,Eye+View.Vector()*3000,Query);
                if(!AimedAtTarget && T<2.5) return;
                Check(AimedAtTarget,TEXT("actual settled gameplay camera ray contacts the real target shape"));
                Check(PC->IsSkillAimHeld(),TEXT("current local aimed skill is held before LMB"));
                Check(PC->RouteSkillFireInput(true),TEXT("real local LMB skill fire consumed"));R.Fired=true;
            }
        }
        if(Caster && R.Fired && !R.Released && T>2.7) {
            // Release remains legal after an accepted action takes movement or
            // the Rocket knocks its caster down; never gate it on CanWork.
            PC->RouteSkillFireInput(false);PC->RouteSkillAimInput(false);PC->EndSkillInput(Slot);R.Released=true;
        }
        if(Host && T>8) {
            int32 ExpectedCommits=Stage+1,Commits=0,Alive=0;
            for(const auto& Pair:R.Actions) if(Pair.Value.Committed) {++Commits;Alive+=Pair.Value.Actor.IsValid();}
            Check(Commits==ExpectedCommits && Alive==0,TEXT("one accepted action per stage has naturally ended before reposition/reset"));
            if(R.Failed) {Finish(false,TEXT("StageDidNotNaturallyComplete"));return;}
            if(Stage<2) {
                const int32 Next=Stage+1;
                const bool Placed=Next==1?(Place(Tongue,Heroes[0],R.Lane) && Place(Tongue,Heroes[1],R.Lane+FVector(600,0,0)))
                    :(Place(Tongue,Heroes[1],R.Lane) && (Place(Tongue,Heroes[0],R.Lane+FVector(0,450,0))
                        || Place(Tongue,Heroes[0],R.Lane-FVector(0,450,0))));
                Check(Placed,TEXT("next stage setup occurs only after the previous action ended; Rocket lane excludes the other pawn"));
                if(R.Failed) {Finish(false,TEXT("StagePlacement"));return;}
                GS->TasksLeft=Next;GS->DayStartedAt=Now+2;GS->ForceNetUpdate();
            } else {
                auto* Perks=Heroes[1]->GetPlayerState<AMCPlayerState>()->Perks.Get();const auto& SlotAfter=Perks->ActiveSkillSlots[1];
                Check((SlotAfter.InstanceID==R.RocketInstance && SlotAfter.Charges==R.RocketCharges-1)
                    || (R.RocketCharges==1 && SlotAfter.PerkID.IsNone()),TEXT("the actual remote Rocket consumed exactly one production charge"));
                GS->TasksLeft=3;GS->DayStartedAt=Now+2;GS->ForceNetUpdate();
            }
        }
    } else {
        if(!R.Cleaned) {
            PC->CancelSkillInputs();
            if(Host) for(auto* Hero:Heroes) Hero->GetPlayerState<AMCPlayerState>()->Perks->CancelActiveSkillActions(EMCActiveSkillEndReason::RunReset);
            R.Cleaned=true;
        }
        if(Heroes.Num()==2) {
            bool Clean=true;
            for(auto* Hero:Heroes) Clean&=!Hero->GetActiveSkillPresentation() && MCActiveSkillMotion::CanAcquire(Hero);
            R.CleanupPeersObserved|=Clean;
        }
        if(T>1 && T<3 && LocalReady(PC,Own) && Own->ToothPhysics->CanAct() && Own->GetCharacterMovement()->IsMovingOnGround()) {
            if(!R.RecoveryInput) {R.MovementStart=Own->GetActorLocation();R.RecoveryInput=true;}
            Own->AddMovementInput(FVector(0,1,0));
        }
        if(R.RecoveryInput) R.MovementDistance=FMath::Max(R.MovementDistance,float(FVector::Dist2D(R.MovementStart,Own->GetActorLocation())));
        if(T>(Host?5:4)) {
            int32 Commits=0,Hooks=0,Rockets=0,Contacts=0,Detonations=0;
            TSet<FGuid> Identities;TSet<int32> HookSources;
            for(const auto& Pair:R.Actions) {
                const auto& O=Pair.Value;if(!O.Committed) continue;
                ++Commits;Hooks+=O.Perk==HookID;Rockets+=O.Perk==RocketID;Contacts+=O.Contact;Detonations+=O.Detonations;
                Check(O.ID.IsValid() && O.SlotInstance.IsValid() && O.PressSequence!=0 && !Identities.Contains(O.ID),
                    TEXT("observed accepted action/slot/press identity is valid and unique"));Identities.Add(O.ID);
                if(O.Perk==HookID) HookSources.Add(O.PlayerID);
                Check(O.Start<=1 && O.Impact<=1 && O.End<=1 && O.Destroyed<=1,TEXT("observed native cues/despawn have no duplicates"));
                if(Host) {
                    Check(O.Start==1 && O.Impact==1 && O.End==1 && O.Destroyed==1,TEXT("authority observed one real start/contact/end/despawn per action"));
                    if(O.Perk==HookID) Check(O.Contact && O.PulledPlayerID!=INDEX_NONE && O.PulledPlayerID!=O.PlayerID && O.PullDistance>100,
                        TEXT("real Hook contact moved the other actual participant by more than 100cm"));
                    if(O.Perk==RocketID) Check(O.ImpactedBeforeExpiry,TEXT("real Rocket flight impacted before its production timeout"));
                }
            }
            // Replication may legitimately coalesce Windup/Flying/Recovery. Log
            // phase masks and received cues; never require every short phase.
            Check(Host?Commits==3 && Hooks==2 && HookSources.Num()==2 && Rockets==1 && Contacts==2 && Detonations==1:Hooks>=1 && Rockets==1 && Commits<=3,
                TEXT("authority exact outcomes / client observed both real skill families"));
            Check(R.CleanupPeersObserved,TEXT("both connected pawns released actual skill presentation and motion ownership"));
            if(Host) for(auto* Hero:Heroes) Check(Hero->GetPlayerState<AMCPlayerState>()->Perks->GetActiveSkillActions().IsEmpty(),TEXT("authority retained no running skill actions after reset"));
            Check(R.RecoveryInput && Own->ToothPhysics->CanAct() && R.MovementDistance>40,TEXT("local owner returned to ordinary ground movement after skill cleanup"));
            Finish(!R.Failed,TEXT("Completed"));return;
        }
    }
    if(Now>=R.NextLog) {
        R.NextLog=Now+1;
        UE_LOG(LogTemp,Display,TEXT("MC_SKILL_NET_TICK net=%d stage=%d t=%.3f records=%d local_ready=%d role=%d"),int32(World->GetNetMode()),Stage,T,R.Actions.Num(),LocalReady(PC,Own),int32(Own->GetLocalRole()));
    }
}
#endif
