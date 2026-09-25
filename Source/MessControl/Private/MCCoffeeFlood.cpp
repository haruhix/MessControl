#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
AMCCoffeeFlood::AMCCoffeeFlood()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    Surface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CoffeeSurface")); SetRootComponent(Surface);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision); Surface->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    if (Plane.Succeeded()) Surface->SetStaticMesh(Plane.Object);
}
void AMCCoffeeFlood::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_CoffeeLiquid.M_CoffeeLiquid")))
    { Material=UMaterialInstanceDynamic::Create(Base,this); Surface->SetMaterial(0,Material); }
}
void AMCCoffeeFlood::Start(const UMCDayPlan* Plan,float Duration)
{
    if (!HasAuthority() || !Plan) return;
    Waves=Plan->WaveCount; Height=Plan->FloodHeight; Flow=Plan->FlowAcceleration; Paddle=Plan->PaddleAcceleration; Reach=Plan->AnchorReach; HalfSize=Plan->ArenaHalfSize;
    Seconds=FMath::Max(1.f,Duration); StartedAt=GetWorld()->GetTimeSeconds(); Wave=0; bActive=true; ForceNetUpdate();
}
bool AMCCoffeeFlood::Contains(FVector P) const { return bActive && FMath::Abs(P.X)<HalfSize.X && FMath::Abs(P.Y)<HalfSize.Y && P.Z<Level+35 && P.Z>-200; }
void AMCCoffeeFlood::Stop()
{
    if (!HasAuthority()) return;
    bActive=false; Level=-40; ForceNetUpdate();
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) { It->bInCoffee=false; It->ClingTooth=nullptr; It->ForceNetUpdate(); }
}
void AMCCoffeeFlood::Tick(float Dt)
{
    Super::Tick(Dt);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (HasAuthority() && bActive)
    {
        if (!GS || GS->Phase!=EMCShiftPhase::Working) { Stop(); return; }
        const float Age=GetWorld()->GetTimeSeconds()-StartedAt;
        if (Age>=Seconds) { Stop(); return; }
        const float Cycle=Age/Seconds*Waves; const int32 Current=FMath::FloorToInt(Cycle)+1;
        if (Current!=Wave) { Wave=Current; HitThisWave.Empty(); ForceNetUpdate(); }
        // Four smooth rises and troughs; gameplay volume matches this cheap visible surface.
        Level=FMath::Lerp(10.f,Height,FMath::Sin(FMath::Frac(Cycle)*PI));
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It; const FVector P=Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
            Hero->bInCoffee=Contains(P) && Hero->Status->IsAlive();
            if (!Hero->bInCoffee) { Hero->ClingTooth=nullptr; continue; }
            if (Hero->bWantsCling)
            {
                if (!IsValid(Hero->ClingTooth))
                {
                    for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable())
                    {
                        const FVector Nearest=Tooth->Body->Bounds.GetBox().GetClosestPointTo(P);
                        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCoffeeGrip),false,Hero); Params.AddIgnoredActor(Tooth);
                        if (FVector::Dist(P,Nearest)<Reach && !GetWorld()->LineTraceSingleByChannel(Hit,P,Nearest,ECC_Visibility,Params))
                        { Hero->ClingTooth=Tooth; Hero->ClingPoint=P; Hero->DropFood(); break; }
                    }
                }
            }
            else Hero->ClingTooth=nullptr;
            if (Hero->ClingTooth && (!Hero->ClingTooth->IsAvailable() || FVector::Dist(P,Hero->ClingPoint)>Reach*2)) Hero->ClingTooth=nullptr;
            const FVector CurrentForce(Flow, FMath::Sin(Age*2)*Flow*.25f,0);
            if (!Hero->ClingTooth && !HitThisWave.Contains(Hero) && Level>Height*.55f)
            { HitThisWave.Add(Hero); Hero->ToothPhysics->ApplyHit(FVector(350,0,220),P); Hero->Status->ApplyCoffee(); }
            if (Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
            {
                const FVector V=Hero->GetMesh()->GetPhysicsLinearVelocity(TEXT("body"));
                const float Buoyancy=FMath::Clamp((Level-P.Z)*18.f-V.Z*4.f+980.f,0.f,1800.f);
                FVector A=CurrentForce+FVector(Hero->PaddleInput.X,Hero->PaddleInput.Y,0)*Paddle-V*1.2f+FVector(0,0,Buoyancy);
                if (Hero->ClingTooth) A=(Hero->ClingPoint-P)*35.f-V*8.f+FVector(0,0,980);
                A=A.GetClampedToMaxSize(2200);
                Hero->GetMesh()->AddForceToAllBodiesBelow(A,TEXT("body"),true,true);
            }
            else if (Hero->ToothPhysics->CanAct())
            {
                if (Hero->ClingTooth) { Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->AddForce((Hero->ClingPoint-P)*1500.f); }
                else Hero->GetCharacterMovement()->AddForce(CurrentForce*Hero->GetCharacterMovement()->Mass);
            }
        }
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            if (It->Body->IsSimulatingPhysics() && Contains(It->GetActorLocation()))
            { const FVector V=It->Body->GetPhysicsLinearVelocity(); It->Body->AddForce((FVector(Flow,0,FMath::Clamp((Level-It->GetActorLocation().Z)*12+980.f,0.f,1500.f))-V*2)*It->Settings.Mass); }
    }
    Surface->SetVisibility(bActive); Surface->SetWorldLocation(FVector(0,0,Level)); Surface->SetWorldScale3D(FVector(HalfSize.X/50,HalfSize.Y/50,1));
}
void AMCCoffeeFlood::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCoffeeFlood,bActive); DOREPLIFETIME(AMCCoffeeFlood,Level); DOREPLIFETIME(AMCCoffeeFlood,Wave); DOREPLIFETIME(AMCCoffeeFlood,Waves);
    DOREPLIFETIME(AMCCoffeeFlood,Height); DOREPLIFETIME(AMCCoffeeFlood,Flow); DOREPLIFETIME(AMCCoffeeFlood,Paddle); DOREPLIFETIME(AMCCoffeeFlood,Reach);
    DOREPLIFETIME(AMCCoffeeFlood,HalfSize); DOREPLIFETIME(AMCCoffeeFlood,StartedAt); DOREPLIFETIME(AMCCoffeeFlood,Seconds);
}
