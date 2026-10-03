#include "MCAmbientParticles.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AMCAmbientParticles::AMCAmbientParticles(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    bReplicates=false;
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> System(
        TEXT("/Game/Gameplay/VFX/Ambient/NS_AmbientInteractive.NS_AmbientInteractive"));
    if(System.Succeeded()) GetNiagaraComponent()->SetAsset(System.Object);
}

void AMCAmbientParticles::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer) {SetActorTickEnabled(false);return;}
    // Gather the final movement/smoothing positions before Niagara simulates.
    GetNiagaraComponent()->SetTickGroup(TG_PostPhysics);
    GetNiagaraComponent()->AddTickPrerequisiteActor(this);
    Tick(0);
}

void AMCAmbientParticles::Tick(float Dt)
{
    Super::Tick(Dt);
    if(GetNetMode()==NM_DedicatedServer) return;
    auto* FX=GetNiagaraComponent();
    if(!FX || !FX->GetAsset() || !GetWorld()) return;
    const FTransform Space=FX->GetComponentTransform();
    const float SafeDt=FMath::Max(0.f,FMath::IsFinite(Dt)?Dt:0.f);
    auto Safe=[](float Value,float Default,float Min,float Max) {
        return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default;
    };
    const float Radius=Safe(ReactionRadius,260,1,5000);
    const float MoveMin=Safe(MinimumMoveSpeed,60,0,999);
    const float MoveFull=FMath::Max(MoveMin+1,Safe(FullReactionSpeed,450,1,1000));
    const float Smooth=Safe(ReactionSmoothing,8,.1f,100);

    bool Valid=false;
    const FVector BaseAir=FX->GetVariableVec3(TEXT("User.DriftVelocity"),Valid);
    const FVector Drift=Valid && !BaseAir.ContainsNaN()?BaseAir:FVector(4,-2,5);
    float Open=0,NearestDistance=MAX_flt;
    FVector MouthDirection=FVector::ForwardVector;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
        Open=FMath::Max(Open,FMath::Clamp(It->OpenAmount(),0.f,1.f));
        const FVector Inlet=It->VacuumInlet();
        const float Distance=FVector::DistSquared(Inlet,Space.GetLocation());
        if(Distance<NearestDistance) {
            NearestDistance=Distance;
            MouthDirection=Space.InverseTransformVectorNoScale(Inlet-Space.GetLocation()).GetSafeNormal();
        }
    }
    const float MouthBlend=1-FMath::Exp(-Safe(MouthSmoothing,6,.1f,100)*SafeDt);
    MouthOpenAmount=FMath::Lerp(MouthOpenAmount,Open,MouthBlend);
    CurrentAirVelocity=Drift*FMath::Lerp(1.f,Safe(OpenMouthSpeedMultiplier,4,1,100),MouthOpenAmount)
        +MouthDirection*Safe(OpenMouthAirSpeed,95,0,1000)*MouthOpenAmount;
    FX->SetVariableVec3(TEXT("User.AirVelocity"),CurrentAirVelocity);
    FX->SetVariableFloat(TEXT("User.ReactionRadius"),Radius);
    FX->SetVariableFloat(TEXT("User.RunPushSpeed"),Safe(RunPushSpeed,95,0,1000));

    TArray<AMCToothCharacter*,TInlineAllocator<MaxPlayers>> Players;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed() || (It->Status && !It->Status->IsAlive())) continue;
        if(const auto* State=It->GetPlayerState();State && State->GetPawn()!=*It) continue;
        Players.Add(*It);
    }
    // Keep slots stable across join, death and respawn so another player's wake
    // never interpolates from the old player's position.
    for(int32 I=0;I<MaxPlayers;++I) if(!Players.Contains(PlayerSlots[I].Get())) {
        PlayerSlots[I].Reset();SmoothedVelocities[I]=FVector::ZeroVector;
    }
    for(auto* Player:Players) {
        bool Present=false;
        for(const auto& Slot:PlayerSlots) Present|=Slot.Get()==Player;
        if(Present) continue;
        for(int32 I=0;I<MaxPlayers;++I) if(!PlayerSlots[I].IsValid()) {PlayerSlots[I]=Player;break;}
    }
    ActiveInteractionCount=0;
    for(int32 I=0;I<MaxPlayers;++I) {
        FVector Position=FVector::ZeroVector;
        float Strength=0;
        if(const auto* Player=PlayerSlots[I].Get()) {
            const FVector Velocity=Player->GetVelocity().GetClampedToMaxSize(1000);
            const float Blend=1-FMath::Exp(-Smooth*SafeDt);
            SmoothedVelocities[I]=FMath::Lerp(SmoothedVelocities[I],Velocity,Blend);
            Position=Space.InverseTransformPosition(Player->GetActorLocation()+FVector(0,0,60));
            Strength=FMath::SmoothStep(MoveMin,MoveFull,static_cast<float>(SmoothedVelocities[I].Size()));
            if(Strength>.01f) ++ActiveInteractionCount;
        }
        FX->SetVariableVec3(FName(*FString::Printf(TEXT("User.Player%dPosition"),I)),Position);
        FX->SetVariableVec3(FName(*FString::Printf(TEXT("User.Player%dVelocity"),I)),
            Space.InverseTransformVector(SmoothedVelocities[I]));
        FX->SetVariableFloat(FName(*FString::Printf(TEXT("User.Player%dStrength"),I)),Strength);
    }
}
