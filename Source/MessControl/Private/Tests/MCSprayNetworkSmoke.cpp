#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCHazardWave.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Four real owners use normal press/release methods without a healing target.
// NullRHI verifies replicated intent; a rendered peer also verifies activation.
void MCTickSprayNetworkValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ACameraActor> Camera;
        float Age=0,NextLog=0;
        float Stable[4][4]={},Bad[4][4]={};
        bool Setup=false,Selected=false,Invalid=false,CameraSet=false;
        bool Pressed[2]={false,false},Released[2]={false,false};
        uint8 Seen[4]={},Proxies=0,ExpectedProxies=0;
    };
    static FRun R;if(R.World!=World) {R=FRun();R.World=World;}
    const float Dt=World->GetDeltaSeconds();R.Age+=Dt;
    auto* GS=World->GetGameState<AMCGameState>();
    auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    const bool Rendered=FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi"));
    const double Now=GS->GetServerWorldTimeSeconds();
    const double T=GS->bDevManualEvents?Now-GS->DayStartedAt:-1;
    auto Finish=[&]()
    {
        bool Stable=true;
        for(int32 Phase=0;Phase<4;++Phase) for(int32 I=0;I<4;++I) {
            Stable&=R.Stable[Phase][I]>1.f && R.Bad[Phase][I]<.05f;
            UE_LOG(LogTemp,Display,TEXT("MC_SPRAY_NETWORK_SLOT net=%d phase=%d slot=%d stable=%.3f bad=%.3f"),int32(World->GetNetMode()),Phase,I,R.Stable[Phase][I],R.Bad[Phase][I]);
        }
        const bool Pass=!R.Invalid && Stable && R.Seen[0]==15 && R.Seen[1]==15 && R.Seen[2]==15 && R.Seen[3]==15
            && (R.Proxies&R.ExpectedProxies)==R.ExpectedProxies;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s SPRAY_NETWORK net=%d hold1=%d release1=%d hold2=%d release2=%d proxies=%d expected_proxies=%d rendered_activation=%d invalid=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),int32(R.Seen[0]),int32(R.Seen[1]),int32(R.Seen[2]),int32(R.Seen[3]),int32(R.Proxies),int32(R.ExpectedProxies),Rendered,R.Invalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    // Clients may leave after their final measurement. The host waits longer
    // but does not require disconnected actors to exist during final reporting.
    if((GS->TasksTotal==5432 && T>(Host?18:16)) || R.Age>110) {Finish();return;}
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if(Heroes.Num()!=4) return;
    if(Host && !R.Setup && R.Age>8) {
        bool Ready=GS->PlayerArray.Num()==4;
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if(!Ready) return;
        auto* Mode=World->GetAuthGameMode();if(Mode) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(It->bUlcer) It->Destroy();
        for(TActorIterator<AMCHazardWave> It(World);It;++It) It->Destroy();
        AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
        if(!Tongue) {R.Invalid=true;Finish();return;}
        Tongue->ResetPain();Tongue->ResetPressure();Tongue->Settings.bAutomaticJolts=false;Tongue->ForceNetUpdate();
        for(int32 I=0;I<4;++I) {
            auto* H=Heroes[I];FHitResult Floor;
            if(!Tongue->SurfacePoint(FVector(-300,-600+I*400,0),Floor)) {R.Invalid=true;Finish();return;}
            H->CancelGameplayInput();H->Status->Initialize(100);
            H->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            H->GetCharacterMovement()->SetMovementMode(MOVE_Falling);H->ForceNetUpdate();
        }
        GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        GS->TasksTotal=5432;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();R.Setup=true;
        return;
    }
    if(GS->TasksTotal!=5432 || T<0) return;
    auto* Own=Cast<AMCToothCharacter>(PC->GetPawn());
    const int32 Local=Heroes.IndexOfByKey(Own);if(!Heroes.IsValidIndex(Local)) return;
    for(int32 I=0;I<4;++I) if(!Heroes[I]->IsLocallyControlled()) R.ExpectedProxies|=uint8(1u<<I);
    if(!R.Selected) {Own->Inventory->ServerSelect(EMCToolSlot::Spray);R.Selected=true;}
    if(Rendered && !R.CameraSet) {
        R.Camera=World->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(48);
        const FVector Aim=Own->GetActorLocation()+FVector(50,0,5),Offset(-360,-420,220);
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());PC->SetViewTarget(R.Camera.Get());R.CameraSet=true;
    }
    for(int32 I=0;I<2;++I) {
        const double Start=I==0?2:9,Stop=I==0?6:13;
        if(T>=Start && T<Stop && !R.Pressed[I] && Own->Inventory->Selected==EMCToolSlot::Spray) {Own->StartPrimary();R.Pressed[I]=true;}
        if(T>=Stop && !R.Released[I]) {Own->StopPrimary();R.Released[I]=true;}
    }
    int32 Phase=INDEX_NONE;
    if(T>=3 && T<5.5) Phase=0;
    else if(T>=7 && T<8.5) Phase=1;
    else if(T>=10 && T<12.5) Phase=2;
    else if(T>=14 && T<15.5) Phase=3;
    if(Phase!=INDEX_NONE) for(int32 I=0;I<4;++I) {
        auto* H=Heroes[I];const bool Holding=Phase==0 || Phase==2;
        bool OK=H->Inventory && H->Inventory->Selected==EMCToolSlot::Spray && !H->Inventory->HealingTarget
            && H->IsPrimaryHeld()==Holding && H->Inventory->bSprayEmitting==Holding;
        // Check actual component activation only in a process with a render
        // device. Active components in NullRHI would not prove visible mist.
        if(Rendered) OK&=H->Inventory->SprayMist && (Holding?H->Inventory->SprayMist->IsActive():!H->Inventory->SprayMist->IsActive());
        if(OK) {R.Stable[Phase][I]+=Dt;R.Seen[Phase]|=uint8(1u<<I);if(Holding && !H->IsLocallyControlled()) R.Proxies|=uint8(1u<<I);}
        else R.Bad[Phase][I]+=Dt;
    }
    if(T>R.NextLog) {
        R.NextLog+=2;
        UE_LOG(LogTemp,Display,TEXT("MC_SPRAY_NETWORK_PROGRESS net=%d t=%.2f local=%d held=%d selected=%d emit=%d phases=%d,%d,%d,%d rendered=%d"),
            int32(World->GetNetMode()),T,Local,Own->IsPrimaryHeld(),int32(Own->Inventory->Selected),Own->Inventory->bSprayEmitting,
            int32(R.Seen[0]),int32(R.Seen[1]),int32(R.Seen[2]),int32(R.Seen[3]),Rendered);
    }
}
#endif
