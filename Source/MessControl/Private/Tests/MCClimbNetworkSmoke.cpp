#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "InputActionValue.h"

// Four real processes. Each owning controller supplies normal E/W input;
// the other clients must preserve the replicated custom mode without E input.
void MCTickClimbNetworkValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCArenaTooth> Tooth;
        TWeakObjectPtr<AMCCoffeeFlood> Water;
        float Age=0,NextLog=0;
        bool Setup=false,WaterSetup=false,Invalid=false,WaterStopped=false;
        bool Pressed[2]={false,false},Released[2]={false,false},JumpPressed=false;
        bool InputFinished[2]={false,false};
        float GroundSeen[4]={},GroundStable[4]={},WaterSeen[4]={},WaterStable[4]={};
        float GroundBad[4]={},WaterBad[4]={};
        float StartZ[2][4]={};
        FVector HangPositions[2][4]={};
        bool HangBaseline[2][4]={};
        uint8 Grounded=0,GroundClimb=0,GroundRelease=0,Swam=0,WaterClimb=0,Jumped=0,ProxyWithoutInput=0,ExpectedProxies=0;
    };
    static FRun R;
    if(R.World!=World) {R=FRun();R.World=World;}
    const float Dt=World->GetDeltaSeconds();R.Age+=Dt;
    auto* GS=World->GetGameState<AMCGameState>();
    auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    for(const auto& Tooth:GS->ArenaTeeth) if(Tooth && Tooth->State.ToothId==8) R.Tooth=Tooth;
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    for(TActorIterator<AMCCoffeeFlood> It(World);It;++It) if(It->IsActive()) {R.Water=*It;break;}
    const double Now=GS->GetServerWorldTimeSeconds();
    const double T=GS->bDevManualEvents?Now-GS->DayStartedAt:-1;
    auto Finish=[&]()
    {
        bool Stable=true;
        for(int32 I=0;I<4;++I)
        {
            Stable&=R.GroundStable[I]>1.5f && R.WaterStable[I]>1.f && R.GroundBad[I]<.05f && R.WaterBad[I]<.05f;
            UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_NETWORK_SLOT net=%d slot=%d ground_seen=%.2f ground_stable=%.2f ground_bad=%.3f water_seen=%.2f water_stable=%.2f water_bad=%.3f"),
                int32(World->GetNetMode()),I,R.GroundSeen[I],R.GroundStable[I],R.GroundBad[I],R.WaterSeen[I],R.WaterStable[I],R.WaterBad[I]);
        }
        const bool Pass=!R.Invalid && Stable && R.Grounded==15 && R.GroundClimb==15 && R.GroundRelease==15 && R.Swam==15 && R.WaterClimb==15 && R.Jumped==15
            && (R.ProxyWithoutInput&R.ExpectedProxies)==R.ExpectedProxies;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s CLIMB_NETWORK net=%d grounded=%d ground=%d release=%d swimming=%d water_climb=%d jump=%d proxies=%d expected_proxies=%d invalid=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),int32(R.Grounded),int32(R.GroundClimb),int32(R.GroundRelease),int32(R.Swam),int32(R.WaterClimb),int32(R.Jumped),int32(R.ProxyWithoutInput),int32(R.ExpectedProxies),R.Invalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(Heroes.Num()!=4 || !R.Tooth.IsValid() || !Tongue)
    {
        if(T>(Host?17:16) || R.Age>110) Finish();
        return;
    }
    // Preserve original slot identities when other clients finish and leave.
    if(!Host) for(int32 I=0;I<4;++I) if(!Heroes[I]->IsLocallyControlled()) R.ExpectedProxies|=uint8(1u<<I);
    const auto Box=R.Tooth->Body->Bounds.GetBox();
    const float Radius=Heroes[0]->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const double Spacing=(Box.GetSize().X-36)/3;
    auto PlaceOnTongue=[&]()
    {
        if(Spacing<Radius*2+2) {R.Invalid=true;return;}
        for(int32 I=0;I<4;++I)
        {
            auto* H=Heroes[I];const FVector Approach(Box.GetCenter().X+(I-1.5)*Spacing,Box.Min.Y-Radius-60,0);
            FHitResult Floor;
            if(!Tongue->SurfacePoint(Approach,Floor)) {R.Invalid=true;continue;}
            H->CancelGameplayInput();H->Status->Initialize(100);
            H->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
            H->GetCharacterMovement()->SetMovementMode(MOVE_Falling);H->ForceNetUpdate();
        }
    };
    if(Host && !R.Setup && R.Age>8)
    {
        bool Ready=GS->PlayerArray.Num()==4;
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if(!Ready) return;
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        Tongue->ResetPain();Tongue->ResetPressure();Tongue->Settings.bAutomaticJolts=false;Tongue->ForceNetUpdate();
        GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        if(!GS->DayPlan) GS->DayPlan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
        if(!GS->DayPlan) {R.Invalid=true;Finish();return;}
        // Give new transforms a full second to reach the owning clients.
        GS->DayStartedAt=Now+1;GS->ForceNetUpdate();PlaceOnTongue();R.Setup=true;return;
    }
    if(T<0) {if(R.Age>110) Finish();return;}
    const int32 Local=Heroes.IndexOfByKey(Cast<AMCToothCharacter>(PC->GetPawn()));
    if(!Heroes.IsValidIndex(Local)) return;
    auto* Own=Heroes[Local];auto* OwnMove=CastChecked<UMCToothMovementComponent>(Own->GetCharacterMovement());
    auto ApproachAndHold=[&](int32 Phase,double StopAt)
    {
        if(!R.Pressed[Phase]) {Own->StartHandle();R.Pressed[Phase]=true;}
        // Latch the first completed ascent: network corrections must not make
        // the fixture inject fresh movement during the hanging measurement.
        // Every owner stops at least .75s before the common hang window.
        if(!R.InputFinished[Phase] && (T>=StopAt || (OwnMove->IsClimbing()
            && (Own->GetActorLocation().Z>=R.StartZ[Phase][Local]+35 || Own->GetActorLocation().Z>=Box.Max.Z-25))))
        {
            R.InputFinished[Phase]=true;
            UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_NETWORK_INPUT_STOP net=%d slot=%d phase=%d t=%.3f z=%.3f"),int32(World->GetNetMode()),Local,Phase,T,Own->GetActorLocation().Z);
        }
        if(R.InputFinished[Phase]) {Own->MoveForward(FInputActionValue(0.f));Own->MoveRight(FInputActionValue(0.f));}
        else if(!OwnMove->IsClimbing()) Own->MoveRight(FInputActionValue(1.f));
        else Own->MoveForward(FInputActionValue(1.f));
    };
    // Capture settled ground positions, then send E once from each actual owner.
    if(T<.8) for(int32 I=0;I<4;++I) if(Heroes[I]->GetCharacterMovement()->IsMovingOnGround())
    {
        R.Grounded|=uint8(1u<<I);R.StartZ[0][I]=float(Heroes[I]->GetActorLocation().Z);
    }
    if(T>=1 && T<5)
    {
        ApproachAndHold(0,2.25);
    }
    if(T>=5 && !R.Released[0]) {Own->StopHandle();Own->MoveForward(FInputActionValue(0.f));Own->MoveRight(FInputActionValue(0.f));R.Released[0]=true;}
    if(Host && T>=7 && !R.WaterSetup)
    {
        PlaceOnTongue();FHitResult Floor;
        if(!Tongue->SurfacePoint(FVector(Box.GetCenter().X,Box.Min.Y-Radius-60,0),Floor)) {R.Invalid=true;return;}
        auto* Plan=DuplicateObject<UMCDayPlan>(GS->DayPlan,World);
        if(!Plan) {R.Invalid=true;return;}
        Plan->FloodHeight=float(Floor.ImpactPoint.Z+120);
        R.Water=World->SpawnActor<AMCCoffeeFlood>();R.Water->Start(Plan,0.f,true);
        R.Water->HalfSize=FVector(1900,1400,500);R.Water->WaterSettings.DryHeight=float(Floor.ImpactPoint.Z-40);
        R.Water->WaterSettings.FillSeconds=15;R.Water->WaterSettings.DrainSeconds=10;R.Water->Seconds=25;
        R.Water->WaterSettings.RippleHeight=3;R.Water->WaterSettings.FrontHeight=0;R.Water->Flow=0;R.Water->WaterSettings.DrainAcceleration=0;
        R.Water->StartedAt=Now-10;R.Water->ForceNetUpdate();R.WaterSetup=true;
    }
    if(T>=8 && T<8.8) for(int32 I=0;I<4;++I) R.StartZ[1][I]=float(Heroes[I]->GetActorLocation().Z);
    if(T>=9 && T<13)
    {
        ApproachAndHold(1,10.75);
    }
    if(T>=13 && !R.JumpPressed)
    {
        Own->MoveForward(FInputActionValue(0.f));Own->MoveRight(FInputActionValue(0.f));Own->Jump();Own->StopHandle();R.JumpPressed=true;
    }
    if(T>=13.8 && !R.Released[1]) {Own->StopJumping();R.Released[1]=true;}
    if(Host && T>=15 && R.Water.IsValid() && !R.WaterStopped) {R.Water->Stop();R.WaterStopped=true;}
    for(int32 I=0;I<4;++I)
    {
        auto* H=Heroes[I];const auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());const uint8 Bit=uint8(1u<<I);
        R.Invalid|=H->GetActorLocation().ContainsNaN() || Move->Velocity.ContainsNaN();
        const bool Climbing=Move->IsClimbing() && H->AnimationClimb>.8f && !H->ClingTooth && FVector::DotProduct(FVector(Move->ClimbNormal),FVector(0,-1,0))>.95;
        if(T>1.5 && T<5 && Climbing) {R.GroundClimb|=Bit;R.GroundSeen[I]+=Dt;}
        if(T>5.7 && T<7 && !Move->IsClimbing()) R.GroundRelease|=Bit;
        if(T>7.5 && T<9 && Move->IsSwimming()) R.Swam|=Bit;
        if(T>9.5 && T<13 && Climbing) {R.WaterClimb|=Bit;R.WaterSeen[I]+=Dt;}
        if(T>13 && T<14.1 && !Move->IsClimbing() && Move->IsFalling() && Move->Velocity.Y<-100 && Move->Velocity.Z>100) R.Jumped|=Bit;
        if(Climbing && H->GetLocalRole()==ROLE_SimulatedProxy && !Move->WantsClimb()) R.ProxyWithoutInput|=Bit;
        for(int32 Phase=0;Phase<2;++Phase)
        {
            const bool Window=Phase==0?(T>3 && T<5):(T>11.5 && T<13);
            if(!Window) continue;
            if(!R.HangBaseline[Phase][I]) {R.HangPositions[Phase][I]=H->GetActorLocation();R.HangBaseline[Phase][I]=true;}
            const bool Stable=Climbing && Move->Velocity.Size()<5 && FMath::Abs(H->GetActorLocation().Z-R.HangPositions[Phase][I].Z)<4;
            if(Phase==0) {if(Stable) R.GroundStable[I]+=Dt;else R.GroundBad[I]+=Dt;}
            else {if(Stable) R.WaterStable[I]+=Dt;else R.WaterBad[I]+=Dt;}
        }
    }
    if(R.Age>R.NextLog)
    {
        R.NextLog=R.Age+1;
        UE_LOG(LogTemp,Display,TEXT("MC_CLIMB_NETWORK net=%d local=%d t=%.2f ground=%d release=%d swimming=%d water=%d jump=%d proxies=%d own_mode=%d own_custom=%d own_wants=%d"),
            int32(World->GetNetMode()),Local,T,int32(R.GroundClimb),int32(R.GroundRelease),int32(R.Swam),int32(R.WaterClimb),int32(R.Jumped),int32(R.ProxyWithoutInput),int32(OwnMove->MovementMode),int32(OwnMove->CustomMovementMode),OwnMove->WantsClimb());
    }
    if(T>(Host?17:16) || R.Age>110) Finish();
}
#endif
