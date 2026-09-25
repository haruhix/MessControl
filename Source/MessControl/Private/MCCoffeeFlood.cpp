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
    Jet=CreateDefaultSubobject<UMCCoffeeSurfaceComponent>(TEXT("PourJet"));
    Crown=CreateDefaultSubobject<UMCCoffeeSurfaceComponent>(TEXT("ImpactCrown"));
    DrainRibbon=CreateDefaultSubobject<UMCCoffeeSurfaceComponent>(TEXT("ThroatOutflow"));
    Drops=CreateDefaultSubobject<UMCCoffeeDropComponent>(TEXT("SplashDrops"));
    for (UStaticMeshComponent* Part:{Jet.Get(),Crown.Get(),DrainRibbon.Get(),static_cast<UStaticMeshComponent*>(Drops.Get())})
    {
        Part->SetupAttachment(Surface); Part->SetAbsolute(true,true,true);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetCastShadow(false);
    }
    // The broad transparent plane must not composite over the splash sheets above it.
    Jet->SetTranslucentSortPriority(1); Crown->SetTranslucentSortPriority(2); DrainRibbon->SetTranslucentSortPriority(1);
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
    if (!Profile) return;
    Jet->SetStaticMesh(Profile->JetMesh.LoadSynchronous()); Crown->SetStaticMesh(Profile->CrownMesh.LoadSynchronous());
    DrainRibbon->SetStaticMesh(Profile->DrainMesh.LoadSynchronous()); Drops->SetStaticMesh(Profile->DropMesh.LoadSynchronous());
    if (auto* Pour=Profile->PourMaterial.LoadSynchronous())
    {
        JetMaterial=UMaterialInstanceDynamic::Create(Pour,this); Jet->SetMaterial(0,JetMaterial);
        CrownMaterial=UMaterialInstanceDynamic::Create(Pour,this); Crown->SetMaterial(0,CrownMaterial);
        DrainMaterial=UMaterialInstanceDynamic::Create(Pour,this); DrainRibbon->SetMaterial(0,DrainMaterial);
        CrownMaterial->SetScalarParameterValue(TEXT("IsCrown"),1);
        DrainMaterial->SetScalarParameterValue(TEXT("IsDrain"),1);
    }
    if (auto* DropMat=Profile->DropMaterial.LoadSynchronous()) Drops->SetMaterial(0,DropMat);
    if (!Drops->GetInstanceCount()) for (int32 I=0;I<72;++I) Drops->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector));
}
void AMCCoffeeFlood::Start(const UMCDayPlan* Plan)
{
    if (!HasAuthority() || !Plan) return;
    Height=Plan->FloodHeight; Flow=Plan->FlowAcceleration; Paddle=Plan->PaddleAcceleration; Reach=Plan->AnchorReach; HalfSize=Plan->ArenaHalfSize;
    Profile=Plan->CoffeeProfile.LoadSynchronous();
    WaterSettings=Profile?Profile->Settings:FMCCoffeeWaterSettings(); WaterSettings.Sanitize();
    if (WaterSettings.bUseThroatActor)
        for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (!It->bBrushBin)
        { WaterSettings.DrainPoint.X=It->GetActorLocation().X; WaterSettings.DrainPoint.Y=It->GetActorLocation().Y; break; }
    OnRep_Profile(); Waves=WaterSettings.Cycles;
    Seconds=WaterSettings.CycleSeconds()*Waves; StartedAt=GetWorld()->GetTimeSeconds(); Wave=0;
    HitThisWave.Empty(); FoodHitThisWave.Empty(); bActive=true; UpdateSurface(); ForceNetUpdate();
}
float AMCCoffeeFlood::WaterTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return FMath::Clamp(float((GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-StartedAt),0.f,Seconds);
}
float AMCCoffeeFlood::BaseHeight(float T) const
{
    return FMath::Lerp(-18.f,Height,WaterSettings.FillAmount(T));
}
float AMCCoffeeFlood::SurfaceHeightAt(FVector P) const
{
    const float T=WaterTime(); return BaseHeight(T)+WaterSettings.SurfaceOffset(P,T);
}
bool AMCCoffeeFlood::Contains(FVector P) const
{
    if (!bActive || P.ContainsNaN() || FMath::Abs(P.X)>=HalfSize.X || FMath::Abs(P.Y)>=HalfSize.Y || P.Z<=-200 || P.Z>=SurfaceHeightAt(P)+35) return false;
    return GetPhase()!=EMCCoffeePhase::Filling || FVector::Dist2D(P,WaterSettings.Inlet)<WaterSettings.FrontRadius(WaterTime())+WaterSettings.FrontWidth;
}
bool AMCCoffeeFlood::IsFlowBlocked(FVector P,const AActor* Ignore) const
{
    FVector Source=GetPhase()==EMCCoffeePhase::Draining?WaterSettings.DrainPoint:WaterSettings.Inlet; Source.Z=P.Z;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCoffeeFlow),false,Ignore);
    return GetWorld()->LineTraceSingleByChannel(Hit,Source,P,ECC_Visibility,Params);
}
FVector AMCCoffeeFlood::FlowAtPosition(FVector P,const AActor* Ignore) const
{
    return bActive && !IsFlowBlocked(P,Ignore)?WaterSettings.FlowAt(P,WaterTime(),Flow):FVector::ZeroVector;
}
void AMCCoffeeFlood::Stop()
{
    if (!HasAuthority()) return;
    bActive=false; Level=-40; ForceNetUpdate();
    Surface->SetVisibility(false); Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false);
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
        const int32 Current=FMath::FloorToInt(Age/WaterSettings.CycleSeconds())+1;
        if (Current!=Wave) { Wave=Current; HitThisWave.Empty(); FoodHitThisWave.Empty(); ForceNetUpdate(); }
        Level=BaseHeight(Age);
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It; const FVector P=Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
            Hero->bInCoffee=Contains(P) && Hero->Status->IsAlive();
            if (!Hero->Status->IsAlive() || FMath::Abs(P.X)>HalfSize.X || FMath::Abs(P.Y)>HalfSize.Y) { Hero->ClingTooth=nullptr; continue; }
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
            const FVector CurrentForce=FlowAtPosition(P,Hero);
            const float Distance=FVector::Dist2D(P,WaterSettings.Inlet), Front=WaterSettings.FrontRadius(Age);
            const bool CrossedFront=Distance<=Front+WaterSettings.FrontWidth && Distance>=Front-WaterSettings.FrontSpeed*Dt-WaterSettings.FrontWidth;
            if (!Hero->ClingTooth && !HitThisWave.Contains(Hero) && GetPhase()==EMCCoffeePhase::Filling
                && WaterSettings.CycleTime(Age)>.25f && P.Z<Height+100 && P.Z>-80
                && (CrossedFront || Distance<WaterSettings.JetRadius*1.4f) && !IsFlowBlocked(P,Hero))
            {
                HitThisWave.Add(Hero);
                const FVector Away=(P-WaterSettings.Inlet).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
                Hero->ToothPhysics->ApplyHit((Away+FVector(0,0,.35f))*WaterSettings.ImpactImpulse,P); Hero->Status->ApplyCoffee();
            }
            if (!Hero->bInCoffee) continue;
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
                It->Body->AddForce(WaterSettings.FloatAcceleration(SurfaceHeightAt(P),P,V,FlowAtPosition(P,*It),FMath::Abs(GetWorld()->GetGravityZ()),Draft)*It->Settings.Mass);
                if (GetPhase()==EMCCoffeePhase::Filling && !FoodHitThisWave.Contains(*It) && !IsFlowBlocked(P,*It))
                {
                    FoodHitThisWave.Add(*It);
                    It->Body->AddImpulse((P-WaterSettings.Inlet).GetSafeNormal2D()*WaterSettings.ImpactImpulse*3);
                }
            }
    }
    UpdateSurface();
}
void AMCCoffeeFlood::UpdateSurface()
{
    Surface->SetVisibility(bActive);
    if (!bActive) { Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false); return; }
    const float T=WaterTime();
    UpdatePour(T);
    Surface->SetWorldLocation(FVector(0,0,BaseHeight(T))); Surface->SetWorldScale3D(FVector(HalfSize.X/50,HalfSize.Y/50,1));
    if (!Material) return;
    Material->SetScalarParameterValue(TEXT("WaterTime"),T);
    Material->SetScalarParameterValue(TEXT("RippleHeight"),WaterSettings.RippleHeight);
    Material->SetScalarParameterValue(TEXT("RippleLength"),WaterSettings.RippleLength);
    Material->SetScalarParameterValue(TEXT("RippleSpeed"),WaterSettings.RippleSpeed);
    Material->SetScalarParameterValue(TEXT("FillAmount"),WaterSettings.FillAmount(T));
    Material->SetScalarParameterValue(TEXT("DrainAmount"),WaterSettings.DrainAmount(T));
    Material->SetScalarParameterValue(TEXT("JetAmount"),WaterSettings.JetAmount(T));
    Material->SetScalarParameterValue(TEXT("Filling"),GetPhase()==EMCCoffeePhase::Filling?1:0);
    Material->SetScalarParameterValue(TEXT("FrontRadius"),WaterSettings.FrontRadius(T));
    Material->SetScalarParameterValue(TEXT("FrontWidth"),WaterSettings.FrontWidth);
    Material->SetScalarParameterValue(TEXT("FrontHeight"),WaterSettings.FrontHeight);
    Material->SetScalarParameterValue(TEXT("DrainRadius"),WaterSettings.DrainRadius);
    Material->SetScalarParameterValue(TEXT("DrainDepth"),WaterSettings.DrainDepth);
    Material->SetVectorParameterValue(TEXT("Inlet"),FLinearColor(WaterSettings.Inlet.X,WaterSettings.Inlet.Y,0,0));
    Material->SetVectorParameterValue(TEXT("Outlet"),FLinearColor(WaterSettings.DrainPoint.X,WaterSettings.DrainPoint.Y,0,0));
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
