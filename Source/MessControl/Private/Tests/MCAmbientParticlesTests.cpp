#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCAmbientParticles.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCInteractiveAmbient,
    "MessControl.VFX.Ambient.PlayerWakeAndMouthAirflow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCInteractiveAmbient::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto Cleanup=[&]() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Ambient=World->SpawnActor<AMCAmbientParticles>(FVector(600,-150,420),FRotator(0,90,0));
    auto* First=World->SpawnActor<AMCToothCharacter>(FVector(300,200,2000),FRotator::ZeroRotator);
    auto* Second=World->SpawnActor<AMCToothCharacter>(FVector(-400,-200,2000),FRotator::ZeroRotator);
    auto* Throat=World->SpawnActor<AMCThroat>(FVector(1000,0,2000),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Ambient actor"),Ambient) || !First || !Second || !Throat) {Cleanup();return false;}
    auto* FX=Ambient->GetNiagaraComponent();
    TestNotNull(TEXT("Interactive Niagara system is assigned"),FX->GetAsset());
    auto Step=[&](float Seconds) {for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) Ambient->Tick(1.f/60);};
    FX->SetVariableVec3(TEXT("User.DriftVelocity"),FVector(7,2,4));
    Ambient->ReactionRadius=500;
    Step(1);
    TestEqual(TEXT("Idle players do not displace particles"),Ambient->ActiveInteractionCount,0);
    TestTrue(TEXT("The Blueprint's calm drift reaches the simulation"),Ambient->CurrentAirVelocity.Equals(FVector(7,2,4),.01));
    bool Valid=false;
    TestEqual(TEXT("The editable reaction radius reaches Niagara"),FX->GetVariableFloat(TEXT("User.ReactionRadius"),Valid),500.f);
    TestTrue(TEXT("The radius user binding exists"),Valid);

    First->GetCharacterMovement()->Velocity=FVector(450,0,0);
    Second->GetCharacterMovement()->Velocity=FVector(0,-450,0);
    Step(1);
    TestEqual(TEXT("Both players drive separate particle wakes"),Ambient->ActiveInteractionCount,2);
    bool FoundFirst=false,FoundSecond=false;
    for(int32 I=0;I<8;++I) {
        const FName PositionName(*FString::Printf(TEXT("User.Player%dPosition"),I));
        const FName StrengthName(*FString::Printf(TEXT("User.Player%dStrength"),I));
        const FVector Local=FX->GetVariableVec3(PositionName,Valid);
        const FVector Position=FX->GetComponentTransform().TransformPosition(Local)-FVector(0,0,60);
        const float Strength=FX->GetVariableFloat(StrengthName,Valid);
        if(Strength>.9f && Position.Equals(First->GetActorLocation(),.1)) FoundFirst=true;
        if(Strength>.9f && Position.Equals(Second->GetActorLocation(),.1)) FoundSecond=true;
    }
    TestTrue(TEXT("Player positions use the rotated emitter's local space"),FoundFirst && FoundSecond);
    First->GetCharacterMovement()->Velocity=FVector::ZeroVector;
    Second->Status->State.Health=0;
    Step(1.5f);
    TestEqual(TEXT("Stopping and death clear the wake without stale slots"),Ambient->ActiveInteractionCount,0);

    Throat->ThroatPhase=EMCThroatPhase::Vomiting;
    Step(1);
    TestTrue(TEXT("The visible mouth opening drives air strength"),Ambient->MouthOpenAmount>.9f);
    const FVector ToMouth=FX->GetComponentTransform().InverseTransformVectorNoScale(
        Throat->VacuumInlet()-FX->GetComponentLocation()).GetSafeNormal();
    TestTrue(TEXT("An open mouth noticeably accelerates air toward its inlet"),
        Ambient->CurrentAirVelocity.Size()>60 && FVector::DotProduct(Ambient->CurrentAirVelocity,ToMouth)>60);
    TestTrue(TEXT("Air velocity reaches the Niagara particle update"),
        FX->GetVariableVec3(TEXT("User.AirVelocity"),Valid).Equals(Ambient->CurrentAirVelocity,.01) && Valid);
    Throat->ThroatPhase=EMCThroatPhase::Collecting;
    Step(2);
    TestTrue(TEXT("Closing the mouth restores the artist's calm drift"),
        Ambient->MouthOpenAmount<.001f && Ambient->CurrentAirVelocity.Equals(FVector(7,2,4),.01));
    Cleanup();return true;
}
#endif
