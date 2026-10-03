#if !UE_BUILD_SHIPPING
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

// Both real owners jump; each peer must observe both replicated keys depress and return.
void MCTickToothPianoValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        float Age=0;
        uint8 Pressed=0,Returned=0;
        bool Setup=false,FirstJump=false,SecondJump=false;
    };
    static FRun R; if(R.World.Get()!=World) {R=FRun(); R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>();
    auto* PC=World->GetFirstPlayerController();
    const bool Host=World->GetNetMode()!=NM_Client;
    auto Finish=[&](bool Pass) {
        UE_LOG(LogTemp,Display,TEXT("MC_PIANO_%s net=%d pressed=%d returned=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.Pressed,R.Returned);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(R.Age>45) {Finish(false);return;}
    if(!GS || !PC || !PC->GetPawn()) return;
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It)
        if(It->GetPlayerState() && It->GetPlayerState()->GetPawn()==*It) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B) {
        return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();
    });
    const double Now=GS->GetServerWorldTimeSeconds();
    if(Host && !R.Setup && R.Age>3 && Heroes.Num()==2) {
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            if(!It->Get()->IsLocalController() && It->Get()->AcknowledgedPawn!=It->Get()->GetPawn()) return;
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(AMCArenaTooth* Key:GS->ArenaTeeth) if(IsValid(Key)) Key->Destroy();
        GS->ArenaTeeth.Reset();
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        for(int32 I=0;I<2;++I) {
            const FTransform T(FVector(10000+I*500,0,10000));
            auto* Key=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),T);
            Key->SetAppearance(Mesh,FVector(4,4,2)); Key->Initialize(I+1,FMCArenaToothSettings()); Key->FinishSpawning(T);
            GS->ArenaTeeth.Add(Key);
            Heroes[I]->CancelGameplayInput(); auto* Move=Heroes[I]->GetCharacterMovement(); Move->StopMovementImmediately();
            Heroes[I]->SetActorLocation(T.GetLocation()+FVector(0,0,Key->Body->GetScaledBoxExtent().Z+Heroes[I]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
            Move->SetMovementMode(MOVE_Walking); Heroes[I]->ForceNetUpdate();
        }
        GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
        GS->TasksTotal=90910; GS->DayStartedAt=Now+2; GS->ForceNetUpdate(); R.Setup=true;
    }
    if(GS->TasksTotal!=90910 || GS->ArenaTeeth.Num()!=2) return;
    const double T=Now-GS->DayStartedAt;
    auto* Local=Cast<AMCToothCharacter>(PC->GetPawn());
    if(T>1 && !R.FirstJump) {Local->Jump();R.FirstJump=true;}
    if(T>1.15 && T<4) Local->StopJumping();
    if(T>4 && !R.SecondJump) {Local->Jump();R.SecondJump=true;}
    if(T>4.15) Local->StopJumping();
    bool Notes=true;
    for(int32 I=0;I<2;++I) {
        const AMCArenaTooth* Key=GS->ArenaTeeth[I]; if(!IsValid(Key)) return;
        const uint8 Bit=1u<<I;
        if(Key->PianoState.Serial>0 && Key->PianoOffset()>1) R.Pressed|=Bit;
        if(Key->PianoState.Serial>0 && Key->PianoOffset()==0 && T>2.5) R.Returned|=Bit;
        Notes&=Key->PianoState.Serial==2 && Key->State.Health==Key->Settings.MaxHealth && Key->PianoSound!=nullptr;
    }
    if(T>(Host?8:7)) Finish(Notes && R.Pressed==3 && R.Returned==3
        && GS->ArenaTeeth[0]->PianoMidiNote()!=GS->ArenaTeeth[1]->PianoMidiNote());
}
#endif
