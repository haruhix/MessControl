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
#include "GameFramework/PlayerState.h"
#include "UObject/ConstructorHelpers.h"
AMCCoffeeFlood::AMCCoffeeFlood()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    Surface=CreateDefaultSubobject<UMCCoffeeSurfaceComponent>(TEXT("CoffeeSurface")); SetRootComponent(Surface);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision); Surface->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    if (Plane.Succeeded()) Surface->SetStaticMesh(Plane.Object);
}
void AMCCoffeeFlood::BeginPlay()
{
    Super::BeginPlay();
    if (!Profile) Profile=LoadObject<UMCCoffeeProfile>(nullptr,TEXT("/Game/Data/DA_CoffeeWater.DA_CoffeeWater"));
    OnRep_Profile(); UpdateSurface();
}
void AMCCoffeeFlood::OnRep_Profile()
{
    if (Profile) if (auto* Mesh=Profile->SurfaceMesh.LoadSynchronous()) Surface->SetStaticMesh(Mesh);
    UMaterialInterface* Base=Profile?Profile->SurfaceMaterial.LoadSynchronous():nullptr;
    if (!Base) Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_CoffeeLiquid.M_CoffeeLiquid"));
    if (Base) { Material=UMaterialInstanceDynamic::Create(Base,this); Surface->SetMaterial(0,Material); }
    Surface->SetBoundsScale(1.1f);
}
void AMCCoffeeFlood::Start(const UMCDayPlan* Plan,float Duration)
{
    if (!HasAuthority() || !Plan) return;
    Waves=Plan->WaveCount; Height=Plan->FloodHeight; Flow=Plan->FlowAcceleration; Paddle=Plan->PaddleAcceleration; Reach=Plan->AnchorReach; HalfSize=Plan->ArenaHalfSize;
    Profile=Plan->CoffeeProfile.LoadSynchronous();
    WaterSettings=Profile?Profile->Settings:FMCCoffeeWaterSettings(); WaterSettings.Sanitize(); OnRep_Profile();
    Seconds=FMath::Max(1.f,Duration); StartedAt=GetWorld()->GetTimeSeconds(); Wave=0; bActive=true; ForceNetUpdate();
}
float AMCCoffeeFlood::WaterTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return FMath::Clamp(float((GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-StartedAt),0.f,Seconds);
}
float AMCCoffeeFlood::BaseHeight(float T) const
{
    return FMath::Lerp(10.f,Height,FMath::Sin(FMath::Frac(T/FMath::Max(1.f,Seconds)*Waves)*PI));
}
float AMCCoffeeFlood::SurfaceHeightAt(FVector P) const
{
    const float T=WaterTime(); return BaseHeight(T)+WaterSettings.Ripple(P,T);
}
bool AMCCoffeeFlood::Contains(FVector P) const { return bActive && !P.ContainsNaN() && FMath::Abs(P.X)<HalfSize.X && FMath::Abs(P.Y)<HalfSize.Y && P.Z<SurfaceHeightAt(P)+35 && P.Z>-200; }
void AMCCoffeeFlood::Stop()
{
    if (!HasAuthority()) return;
    bActive=false; Level=-40; ForceNetUpdate();
    Surface->SetVisibility(false);
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) { It->bInCoffee=false; It->ClingTooth=nullptr; It->ForceNetUpdate(); }
}
void AMCCoffeeFlood::Tick(float Dt)
{
    Super::Tick(Dt);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (HasAuthority() && bActive)
    {
        if (!GS || GS->Phase!=EMCShiftPhase::Working) { Stop(); return; }
        const float Age=WaterTime();
        if (Age>=Seconds) { Stop(); return; }
        const float Cycle=Age/Seconds*Waves; const int32 Current=FMath::FloorToInt(Cycle)+1;
        if (Current!=Wave) { Wave=Current; HitThisWave.Empty(); ForceNetUpdate(); }
        // Four smooth rises and troughs; gameplay volume matches this cheap visible surface.
        Level=BaseHeight(Age);
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
                const FVector V=Hero->GetMesh()->GetPhysicsLinearVelocity(Hero->RigBone(TEXT("body")));
                const float Gravity=FMath::Abs(GetWorld()->GetGravityZ());
                FVector A=WaterSettings.FloatAcceleration(SurfaceHeightAt(P),P,V,CurrentForce+FVector(Hero->PaddleInput.X,Hero->PaddleInput.Y,0)*Paddle,Gravity,WaterSettings.FloatDepth);
                if (Hero->ClingTooth) A=((Hero->ClingPoint-P)*35.f-V*8.f+FVector(0,0,Gravity)).GetClampedToMaxSize(2400);
                Hero->GetMesh()->AddForceToAllBodiesBelow(A,Hero->RigBone(TEXT("body")),true,true);
            }
            else if (Hero->ToothPhysics->CanAct())
            {
                if (Hero->ClingTooth) { Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->AddForce((Hero->ClingPoint-P)*1500.f); }
                else Hero->GetCharacterMovement()->AddForce(CurrentForce*Hero->GetCharacterMovement()->Mass);
            }
        }
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            if (It->Body->IsSimulatingPhysics() && Contains(It->GetActorLocation()))
            {
                const FVector P=It->GetActorLocation(), V=It->Body->GetPhysicsLinearVelocity();
                const float Draft=It->Body->GetScaledBoxExtent().Z*.35f;
                It->Body->AddForce(WaterSettings.FloatAcceleration(SurfaceHeightAt(P),P,V,FVector(Flow,0,0),FMath::Abs(GetWorld()->GetGravityZ()),Draft)*It->Settings.Mass);
            }
    }
    UpdateSurface();
}
void AMCCoffeeFlood::UpdateSurface()
{
    Surface->SetVisibility(bActive); if (!bActive) return;
    const float T=WaterTime();
    Surface->SetWorldLocation(FVector(0,0,BaseHeight(T))); Surface->SetWorldScale3D(FVector(HalfSize.X/50,HalfSize.Y/50,1));
    if (!Material) return;
    Material->SetScalarParameterValue(TEXT("WaterTime"),T);
    Material->SetScalarParameterValue(TEXT("RippleHeight"),WaterSettings.RippleHeight);
    Material->SetScalarParameterValue(TEXT("RippleLength"),WaterSettings.RippleLength);
    Material->SetScalarParameterValue(TEXT("RippleSpeed"),WaterSettings.RippleSpeed);
    Material->SetVectorParameterValue(TEXT("ArenaSize"),FLinearColor(HalfSize.X,HalfSize.Y,0,0));
    // Four local visual wakes, derived from the already replicated ragdoll poses (no extra RPCs).
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState() && It->bInCoffee) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    for (int32 I=0;I<4;++I)
    {
        FLinearColor Wake(0,0,0,0);
        if (Heroes.IsValidIndex(I))
        {
            const FVector P=Heroes[I]->ToothPhysics->PhysicalLocation();
            Wake=FLinearColor(P.X,P.Y,1,Heroes[I]->ClingTooth?.45f:1.f);
        }
        Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("Wake%d"),I)),Wake);
    }
}
void AMCCoffeeFlood::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCoffeeFlood,bActive); DOREPLIFETIME(AMCCoffeeFlood,Level); DOREPLIFETIME(AMCCoffeeFlood,Wave); DOREPLIFETIME(AMCCoffeeFlood,Waves);
    DOREPLIFETIME(AMCCoffeeFlood,Height); DOREPLIFETIME(AMCCoffeeFlood,Flow); DOREPLIFETIME(AMCCoffeeFlood,Paddle); DOREPLIFETIME(AMCCoffeeFlood,Reach);
    DOREPLIFETIME(AMCCoffeeFlood,HalfSize); DOREPLIFETIME(AMCCoffeeFlood,StartedAt); DOREPLIFETIME(AMCCoffeeFlood,Seconds);
    DOREPLIFETIME(AMCCoffeeFlood,Profile); DOREPLIFETIME(AMCCoffeeFlood,WaterSettings);
}
