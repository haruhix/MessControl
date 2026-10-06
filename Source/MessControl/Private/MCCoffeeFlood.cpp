#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCFoodActor.h"
#include "MCGripComponent.h"
#include "MCTongue.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
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
    RiverSurface=CreateDefaultSubobject<UMCRiverSurfaceComponent>(TEXT("RiverSurface"));
    RiverSurface->SetupAttachment(Surface); RiverSurface->SetAbsolute(true,true,true);
    RiverSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision); RiverSurface->SetCastShadow(false);
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
    if (bRiverFlood)
    {
        const FString MaterialPath=Base?Base->GetPathName():FString();
        const TCHAR* RiverPath=MaterialPath.Contains(TEXT("ColdCola"))?TEXT("/Game/Gameplay/Liquid/River/MI_RiverCola.MI_RiverCola"):
            MaterialPath.Contains(TEXT("Water"))?TEXT("/Game/Gameplay/Liquid/River/MI_RiverWater.MI_RiverWater"):
            TEXT("/Game/Gameplay/Liquid/River/MI_RiverCoffee.MI_RiverCoffee");
        if (auto* RiverBase=LoadObject<UMaterialInterface>(nullptr,RiverPath)) Base=RiverBase;
    }
    if (!Base) Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_CoffeeLiquid.M_CoffeeLiquid"));
    if (Base) { Material=UMaterialInstanceDynamic::Create(Base,this); Surface->SetMaterial(0,Material); RiverSurface->SetMaterial(0,Material); }
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
void AMCCoffeeFlood::Start(const UMCDayPlan* Plan,float SwimTestSeconds,bool bUseLegacyFlood)
{
    if (!HasAuthority() || !Plan) return;
    bRiverFlood=!bUseLegacyFlood && SwimTestSeconds<=0;
    Height=Plan->FloodHeight; Flow=Plan->FlowAcceleration; Paddle=Plan->PaddleAcceleration; Reach=Plan->AnchorReach; HalfSize=Plan->ArenaHalfSize;
    ArenaCenter=Plan->ArenaCenter.ContainsNaN()?FVector::ZeroVector:Plan->ArenaCenter;
    Profile=Plan->CoffeeProfile.LoadSynchronous();
    WaterSettings=Profile?Profile->Settings:FMCCoffeeWaterSettings(); WaterSettings.Sanitize();
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty()) {Tongue=*It;break;}
    if(Tongue)
    {
        const FBox Bounds=Tongue->Surface->Bounds.GetBox();
        const FVector AuthoredHalf=HalfSize.ComponentMax(FVector(1));
        const FVector AuthoredCenter=ArenaCenter;
        ArenaCenter=Bounds.GetCenter(); HalfSize=Bounds.GetExtent().ComponentMax(FVector(1));
        auto Remap=[&](FVector Point)
        {
            Point.X=ArenaCenter.X+(Point.X-AuthoredCenter.X)*HalfSize.X/AuthoredHalf.X;
            Point.Y=ArenaCenter.Y+(Point.Y-AuthoredCenter.Y)*HalfSize.Y/AuthoredHalf.Y;
            return Point;
        };
        WaterSettings.Inlet=Remap(WaterSettings.Inlet); WaterSettings.DrainPoint=Remap(WaterSettings.DrainPoint);
        const float Scale=FMath::Sqrt(float(HalfSize.X/AuthoredHalf.X*HalfSize.Y/AuthoredHalf.Y));
        WaterSettings.RippleLength*=Scale; WaterSettings.JetRadius*=Scale;
        WaterSettings.FrontWidth*=Scale; WaterSettings.DrainRadius*=Scale;
        WaterSettings.FrontSpeed*=FMath::Max(float(HalfSize.X/AuthoredHalf.X),float(HalfSize.Y/AuthoredHalf.Y));
        // Keep the flood depth in gameplay centimetres while moving the dry
        // sheet beneath the complete replacement floor, including its slope.
        Height=Bounds.Max.Z+Plan->FloodHeight; WaterSettings.DryHeight=Bounds.Min.Z+WaterSettings.DryHeight;
        WaterSettings.Inlet.Z=FMath::Max(WaterSettings.Inlet.Z,double(Height+600));
        FHitResult Interior;
        if(!bRiverFlood && !Tongue->InteriorSurfacePoint(WaterSettings.Inlet,WaterSettings.JetRadius+40,Interior))
        {
            FRandomStream Random(GetTypeHash(GetWorld()->GetTimeSeconds()));
            if(Tongue->RandomInteriorPoint(Random,WaterSettings.JetRadius+40,0,TConstArrayView<FVector>(),Interior))
            {WaterSettings.Inlet.X=Interior.ImpactPoint.X;WaterSettings.Inlet.Y=Interior.ImpactPoint.Y;}
        }
    }
    if (SwimTestSeconds>0) {
        WaterSettings.HoldSeconds=FMath::Clamp(SwimTestSeconds,1.f,3600.f); WaterSettings.Cycles=1;
        // Cover even the raised back of the tongue with enough depth to swim.
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
            for (const FVector& Vertex:It->CurrentVertices())
                Height=FMath::Max(Height,float(It->GetActorTransform().TransformPosition(Vertex).Z+150));
    }
    FHitResult Floor; FVector FloorProbe=WaterSettings.Inlet; FloorProbe.Z=Height+200;
    const bool FoundFloor=Tongue?Tongue->SurfacePoint(FloorProbe,Floor):GetWorld()->LineTraceSingleByObjectType(Floor,FloorProbe,
        FloorProbe-FVector(0,0,1400),FCollisionObjectQueryParams(ECC_WorldStatic));
    InletFloorZ=FoundFloor?Floor.ImpactPoint.Z:WaterSettings.DryHeight;
    if (WaterSettings.bUseThroatActor)
        for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (!It->bBrushBin)
        { WaterSettings.DrainPoint.X=It->GetActorLocation().X; WaterSettings.DrainPoint.Y=It->GetActorLocation().Y; break; }
    RiverTongue=Tongue;
    if (bRiverFlood)
    {
        // The external/green side is opposite the placed mouth outlet. Resolve
        // world coordinates from the current arena rather than a fixed map axis.
        RiverDirection=(WaterSettings.DrainPoint-ArenaCenter).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
        const float Span=FMath::Abs(RiverDirection.X)*HalfSize.X+FMath::Abs(RiverDirection.Y)*HalfSize.Y;
        RiverOrigin=ArenaCenter-RiverDirection*(Span+160.f);
        RiverLength=Span*2+320.f;
        RiverWidth=FMath::Clamp(RiverLength*.45f,900.f,2200.f);
        RiverSpeed=FMath::Clamp(RiverLength/4.4f,850.f,1250.f);
        RiverDepth=FMath::Clamp(Plan->FloodHeight*.5f,65.f,95.f);
        WaterSettings.Inlet=RiverOrigin;
        WaterSettings.FrontWidth=100; WaterSettings.FrontHeight=FMath::Clamp(Plan->FloodHeight*1.7f,240.f,320.f);
        WaterSettings.FillSeconds=.35f+RiverLength/RiverSpeed;
        WaterSettings.HoldSeconds=0;
        WaterSettings.DrainSeconds=(RiverWidth+200.f)/RiverSpeed;
        RiverMeshVertexCount=0; RiverSurface->ClearAllMeshSections();
    }
    OnRep_Profile(); Waves=WaterSettings.Cycles;
    Seconds=WaterSettings.CycleSeconds()*Waves; StartedAt=GetWorld()->GetTimeSeconds(); Wave=0;
    HitThisWave.Empty(); FoodHitThisWave.Empty(); bActive=true; UpdateSurface(); ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_LIQUID_ARENA center=%s half_size=%s floor=%.1f dry=%.1f water=%.1f inlet=%s"),
        *ArenaCenter.ToString(),*HalfSize.ToString(),InletFloorZ,WaterSettings.DryHeight,Height,*WaterSettings.Inlet.ToString());
}
EMCCoffeePhase AMCCoffeeFlood::GetPhase() const
{
    return bActive?WaterSettings.Phase(WaterTime()):EMCCoffeePhase::Inactive;
}
AMCTongue* AMCCoffeeFlood::FindRiverTongue() const
{
    if (!RiverTongue.IsValid())
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
            if (!It->CurrentVertices().IsEmpty()) { RiverTongue=*It; break; }
    return RiverTongue.Get();
}
float AMCCoffeeFlood::RiverFloorAt(FVector P) const
{
    if (auto* Tongue=FindRiverTongue())
    {
        P.Z=Tongue->Surface->Bounds.GetBox().Max.Z+200;
        FHitResult Floor; if (Tongue->SurfacePoint(P,Floor)) return float(Floor.ImpactPoint.Z);
    }
    return InletFloorZ;
}
float AMCCoffeeFlood::RiverFrontDistance() const
{
    return FMath::Max(0.f,WaterSettings.CycleTime(WaterTime())-.35f)*RiverSpeed;
}
float AMCCoffeeFlood::RiverFrontAt(FVector P,float Time) const
{
    const float Local=WaterSettings.CycleTime(Time);
    const FVector Side=FVector::CrossProduct(FVector::UpVector,RiverDirection);
    const float Across=FVector::DotProduct(P-RiverOrigin,Side);
    // The same moving, uneven edge drives wetness, the surface, foam and spray.
    return FMath::Max(0.f,Local-.35f)*RiverSpeed
        +FMath::Sin(Across*.0035f+Local*1.6f)*75.f+FMath::Sin(Across*.0081f-Local*2.2f)*32.f;
}
float AMCCoffeeFlood::RiverWeightAt(FVector P) const
{
    if (!bActive || !bRiverFlood || GetPhase()==EMCCoffeePhase::Inactive) return 0;
    const float Along=FVector::DotProduct(P-RiverOrigin,RiverDirection),Front=RiverFrontAt(P,WaterTime());
    return FMath::SmoothStep(Front-RiverWidth-100,Front-RiverWidth+100,Along)
        *(1-FMath::SmoothStep(Front-100,Front+100,Along));
}
float AMCCoffeeFlood::RiverHeightAt(FVector P,float Time,float FloorZ) const
{
    const float Local=WaterSettings.CycleTime(Time),Front=RiverFrontAt(P,Time);
    const float Along=FVector::DotProduct(P-RiverOrigin,RiverDirection);
    const FVector Side=FVector::CrossProduct(FVector::UpVector,RiverDirection);
    const float Across=FVector::DotProduct(P-RiverOrigin,Side);
    const float Weight=FMath::SmoothStep(Front-RiverWidth-100,Front-RiverWidth+100,Along)*(1-FMath::SmoothStep(Front-100,Front+100,Along));
    const float Distance=Along-Front+80.f;
    const float Crest=WaterSettings.FrontHeight*(.78f+.22f*FMath::Sin(Across*.005f-Local*3.1f))
        *FMath::Exp(-FMath::Square(Distance/(Distance>0?155.f:270.f)));
    const float Chop=22.f*FMath::Sin(Along*.014f-Local*8.f+FMath::Sin(Across*.009f))*FMath::Sin(Across*.018f+Local*4.5f)
        +12.f*FMath::Sin(Along*.027f+Across*.021f-Local*10.f);
    return FloorZ+RiverDepth+Weight*(Crest+Chop+WaterSettings.Ripple(P-RiverDirection*Time*360.f,Time));
}
float AMCCoffeeFlood::WaterTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return FMath::Clamp(float((GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-StartedAt),0.f,Seconds);
}
float AMCCoffeeFlood::BaseHeight(float T) const
{
    return FMath::Lerp(WaterSettings.DryHeight,Height,WaterSettings.FillAmount(T));
}
float AMCCoffeeFlood::SurfaceHeightAt(FVector P) const
{
    const float T=WaterTime(); return bRiverFlood?RiverHeightAt(P,T,RiverFloorAt(P)):BaseHeight(T)+WaterSettings.SurfaceOffset(P,T);
}
float AMCCoffeeFlood::SurfaceVerticalSpeedAt(FVector P) const
{
    const float T=WaterTime(),Before=FMath::Max(0.f,T-.04f),After=FMath::Min(Seconds,T+.04f);
    if (bRiverFlood) return FMath::Clamp((RiverHeightAt(P,After,RiverFloorAt(P))-RiverHeightAt(P,Before,RiverFloorAt(P)))/FMath::Max(.001f,After-Before),-220.f,220.f);
    return FMath::Clamp((BaseHeight(After)+WaterSettings.SurfaceOffset(P,After)-BaseHeight(Before)-WaterSettings.SurfaceOffset(P,Before))/FMath::Max(.001f,After-Before),-220.f,220.f);
}
bool AMCCoffeeFlood::Contains(FVector P) const
{
    if (!bActive || P.ContainsNaN() || FMath::Abs(P.X-ArenaCenter.X)>=HalfSize.X || FMath::Abs(P.Y-ArenaCenter.Y)>=HalfSize.Y || P.Z<=WaterSettings.DryHeight-100 || P.Z>=SurfaceHeightAt(P)+35) return false;
    if (bRiverFlood) return RiverWeightAt(P)>.05f;
    return GetPhase()!=EMCCoffeePhase::Filling || FVector::Dist2D(P,WaterSettings.Inlet)<WaterSettings.FrontRadius(WaterTime())+WaterSettings.FrontWidth;
}
bool AMCCoffeeFlood::IsFlowBlocked(FVector P,const AActor* Ignore) const
{
    FVector Source=GetPhase()==EMCCoffeePhase::Draining?WaterSettings.DrainPoint:WaterSettings.Inlet;
    if (bRiverFlood)
    {
        // A nearby upstream obstruction shelters a runner. Do not treat the
        // sloping tongue itself as a wall blocking the entire river.
        Source=P-RiverDirection*240.f; Source.Z=P.Z=FMath::Max(P.Z,double(RiverFloorAt(P)+35.f));
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCRiverShelter),false,Ignore);
        if (auto* Tongue=FindRiverTongue()) Params.AddIgnoredActor(Tongue);
        return GetWorld()->LineTraceSingleByChannel(Hit,Source,P,ECC_Visibility,Params);
    }
    // A low swimmer on the sloping tongue must not trace from underneath the inlet floor.
    // Teeth and walls still shelter players at this height.
    Source.Z=P.Z=FMath::Max(P.Z,InletFloorZ+25.f);
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCoffeeFlow),false,Ignore);
    return GetWorld()->LineTraceSingleByChannel(Hit,Source,P,ECC_Visibility,Params);
}
FVector AMCCoffeeFlood::FlowAtPosition(FVector P,const AActor* Ignore) const
{
    if (bRiverFlood)
    {
        if (!Contains(P) || IsFlowBlocked(P,Ignore)) return FVector::ZeroVector;
        const float Along=FVector::DotProduct(P-RiverOrigin,RiverDirection);
        const float Impact=FMath::Exp(-FMath::Square((Along-RiverFrontAt(P,WaterTime())+80.f)/180.f));
        // A breaking front overpowers upstream sprinting. The body keeps pushing,
        // while cover, sideward escape and a tooth grip remain useful responses.
        return RiverDirection*Flow*6.f*(1.f+.6f*Impact)*RiverWeightAt(P);
    }
    return bActive && !IsFlowBlocked(P,Ignore)?WaterSettings.FlowAt(P,WaterTime(),Flow):FVector::ZeroVector;
}
bool AMCCoffeeFlood::IsRiverDebris(const UPrimitiveComponent* Body)
{
    if (!IsValid(Body) || !Body->IsSimulatingPhysics()) return false;
    const AActor* Actor=Body->GetOwner();
    // Actor movement replication describes the root body. Detached food pieces
    // are actors of their own; moving a child cosmetic mesh would not replicate.
    if (!IsValid(Actor) || Actor->GetRootComponent()!=Body || Actor->IsA<APawn>()) return false;
    const FBoxSphereBounds Size=Body->CalcBounds(FTransform(FQuat::Identity,FVector::ZeroVector,Body->GetComponentScale()));
    const float Mass=Body->GetMass();
    if (Size.BoxExtent.ContainsNaN() || Size.BoxExtent.GetMax()>55 || !FMath::IsFinite(Mass) || Mass<=0) return false;
    if (const auto* Food=Cast<AMCFoodActor>(Actor))
    {
        if (Food->StackCarrier || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling && Food->Phase!=EMCFoodPhase::Carried)) return false;
        return Food->Visual && Food->Visual->Bounds.SphereRadius<=85;
    }
    return Size.SphereRadius<=85;
}
FVector AMCCoffeeFlood::RiverDebrisAcceleration(const UPrimitiveComponent* Body) const
{
    if (!bActive || !bRiverFlood || !IsRiverDebris(Body)) return FVector::ZeroVector;
    const FVector P=Body->Bounds.Origin,V=Body->GetPhysicsLinearVelocity();
    // Contact begins at the submerged lower part, rather than waiting until the
    // water reaches the centre of a resting food piece.
    const FVector Probe=P-FVector(0,0,FMath::Min(55.,Body->Bounds.BoxExtent.Z*.8));
    const FVector Current=FlowAtPosition(Probe,Body->GetOwner());
    if (Current.IsNearlyZero() || V.ContainsNaN()) return FVector::ZeroVector;
    FVector A=WaterSettings.FloatAcceleration(SurfaceHeightAt(P),P,V,Current,
        FMath::Abs(GetWorld()->GetGravityZ()),float(Body->Bounds.BoxExtent.Z*.35));
    // Compact dense pieces can still wash, but accelerate more slowly than the
    // ordinary food mass. A hand chain weighs down its anchor as well. Grip
    // supplies the chain's downward weight separately; reduce only XY here.
    const float Load=UMCGripComponent::AttachedMassFor(Body->GetOwner());
    const float Ratio=FMath::Min(10.f,Body->GetMass())/(Body->GetMass()+FMath::Max(0.f,Load));
    A.X*=Ratio; A.Y*=Ratio;
    return A;
}
void AMCCoffeeFlood::UpdateRiverDebris()
{
    if (!HasAuthority()) return;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
    {
        auto* Body=Cast<UPrimitiveComponent>(It->GetRootComponent());
        const FVector Acceleration=RiverDebrisAcceleration(Body);
        if (Acceleration.IsNearlyZero()) continue;
        const float Mass=Body->GetMass();
        Body->WakeAllRigidBodies(); Body->AddForce(Acceleration*Mass);
        if (auto* Food=Cast<AMCFoodActor>(*It)) Food->MarkRiverSwept(FMath::Min(-250.f,WaterSettings.DryHeight-250.f));
        if (!FoodHitThisWave.Contains(*It))
        {
            FoodHitThisWave.Add(*It);
            const float Load=UMCGripComponent::AttachedMassFor(*It),Ratio=FMath::Min(10.f,Mass)/(Mass+FMath::Max(0.f,Load));
            const FVector Probe=Body->Bounds.Origin-FVector(0,0,FMath::Min(55.,Body->Bounds.BoxExtent.Z*.8));
            Body->AddImpulse(RiverDirection*FMath::Min(350.f,RiverSpeed*.28f)*RiverWeightAt(Probe)*Ratio,NAME_None,true);
            // Map-placed loose props use the engine's root physics replication.
            // Food already has its own server pose and kinematic client smoothing.
            if (!It->GetIsReplicated()) It->SetReplicates(true);
            It->SetReplicateMovement(true); It->FlushNetDormancy(); It->ForceNetUpdate();
        }
    }
}
void AMCCoffeeFlood::Stop()
{
    if (!HasAuthority()) return;
    bActive=false; Level=-40; ForceNetUpdate();
    Surface->SetVisibility(false); RiverSurface->SetVisibility(false); Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false);
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
            const FVector WaterProbe=bRiverFlood && Hero->ToothPhysics->GetBodyState()!=EMCBodyState::Ragdoll?
                P-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-10.f):P;
            Hero->bInCoffee=!Hero->SwallowedBy && Contains(WaterProbe) && Hero->Status->IsAlive();
            if (Hero->SwallowedBy || !Hero->Status->IsAlive() || FMath::Abs(P.X-ArenaCenter.X)>HalfSize.X || FMath::Abs(P.Y-ArenaCenter.Y)>HalfSize.Y) { Hero->ClingTooth=nullptr; continue; }
            const auto* Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
            // The old LMB anchor can still brace in water. E belongs to the
            // predicted climbing movement, including its swim-to-wall transfer.
            if (!bRiverFlood && Hero->IsPrimaryHeld() && Hero->bWantsCling && !Move->WantsClimb() && Hero->bInCoffee && Move->IsSwimming())
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
            const FVector CurrentForce=FlowAtPosition(WaterProbe,Hero);
            if (bRiverFlood && Hero->bInCoffee && !HitThisWave.Contains(Hero))
            {
                HitThisWave.Add(Hero); Hero->Status->ApplyCoffee();
            }
            const float Distance=FVector::Dist2D(P,WaterSettings.Inlet), Front=WaterSettings.FrontRadius(Age);
            const bool CrossedFront=Distance<=Front+WaterSettings.FrontWidth && Distance>=Front-WaterSettings.FrontSpeed*Dt-WaterSettings.FrontWidth;
            if (!bRiverFlood && !Hero->ClingTooth && !HitThisWave.Contains(Hero) && GetPhase()==EMCCoffeePhase::Filling
                && WaterSettings.CycleTime(Age)>.25f && P.Z<Height+100 && P.Z>WaterSettings.DryHeight
                && (CrossedFront || Distance<WaterSettings.JetRadius*1.4f) && !IsFlowBlocked(P,Hero))
            {
                HitThisWave.Add(Hero);
                const FVector Away=(P-WaterSettings.Inlet).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
                // Entering the drink keeps player control. The current belongs to
                // swimming movement, not the combat ragdoll / stun system.
                Hero->GetCharacterMovement()->AddImpulse(Away*FMath::Min(90.f,WaterSettings.ImpactImpulse*.12f),true);
                Hero->Status->ApplyCoffee();
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
            else if (Hero->ToothPhysics->CanAct() && !Hero->GetCharacterMovement()->IsSwimming())
            {
                if (bRiverFlood)
                {
                    // Grounded current is predicted additive movement. Applying
                    // another server-only force here would double the host drift.
                    if (Hero->ClingTooth) Hero->GetCharacterMovement()->StopMovementImmediately();
                }
                else if (Hero->ClingTooth) { Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->AddForce((Hero->ClingPoint-P)*1500.f); }
                else Hero->GetCharacterMovement()->AddForce(CurrentForce*Hero->GetCharacterMovement()->Mass);
            }
        }
        if (bRiverFlood) UpdateRiverDebris();
        else for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            if (It->Body->IsSimulatingPhysics() && Contains(It->GetActorLocation()))
            {
                const FVector P=It->GetActorLocation(), V=It->Body->GetPhysicsLinearVelocity();
                const float Draft=It->Body->GetScaledBoxExtent().Z*.35f;
                It->Body->AddForce(WaterSettings.FloatAcceleration(SurfaceHeightAt(P),P,V,FlowAtPosition(P,*It),FMath::Abs(GetWorld()->GetGravityZ()),Draft)*It->Settings.Mass);
                if (GetPhase()==EMCCoffeePhase::Filling && !FoodHitThisWave.Contains(*It) && !IsFlowBlocked(P,*It))
                {
                    FoodHitThisWave.Add(*It);
                    It->Body->AddImpulse((P-WaterSettings.Inlet).GetSafeNormal2D()*WaterSettings.ImpactImpulse*3.f);
                }
            }
    }
    UpdateSurface();
}
void AMCCoffeeFlood::UpdateSurface()
{
    if (GetNetMode()==NM_DedicatedServer) return;
    Surface->SetVisibility(bActive && !bRiverFlood); RiverSurface->SetVisibility(bActive && bRiverFlood);
    if (!bActive) { Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false); return; }
    const float T=WaterTime();
    UpdatePour(T);
    if (bRiverFlood) { UpdateRiverSurface(T); return; }
    const FBoxSphereBounds MeshBounds=Surface->GetStaticMesh()?Surface->GetStaticMesh()->GetBounds():FBoxSphereBounds(FVector::ZeroVector,FVector(50),86.6);
    const FVector PlaneScale(HalfSize.X/FMath::Max(1.,MeshBounds.BoxExtent.X),HalfSize.Y/FMath::Max(1.,MeshBounds.BoxExtent.Y),1);
    Surface->SetWorldScale3D(PlaneScale);
    Surface->SetWorldLocation(FVector(ArenaCenter.X,ArenaCenter.Y,BaseHeight(T))-MeshBounds.Origin*PlaneScale);
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
    Material->SetVectorParameterValue(TEXT("ArenaCenter"),FLinearColor(ArenaCenter.X,ArenaCenter.Y,0,0));
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
void AMCCoffeeFlood::UpdateRiverSurface(float Time)
{
    // Reuse the tongue's exact triangles: a flat flood plane would disappear
    // under its raised end or drown the lower half of the arena.
    AMCTongue* Tongue=FindRiverTongue();
    const TArray<FVector> Flat={FVector(ArenaCenter.X-HalfSize.X,ArenaCenter.Y-HalfSize.Y,InletFloorZ),
        FVector(ArenaCenter.X+HalfSize.X,ArenaCenter.Y-HalfSize.Y,InletFloorZ),
        FVector(ArenaCenter.X+HalfSize.X,ArenaCenter.Y+HalfSize.Y,InletFloorZ),
        FVector(ArenaCenter.X-HalfSize.X,ArenaCenter.Y+HalfSize.Y,InletFloorZ)};
    const TArray<int32> FlatIndices={0,1,2,0,2,3};
    const TArray<FVector>& Floor=Tongue?Tongue->CurrentWorldVertices():Flat;
    const TArray<int32>& Indices=Tongue?Tongue->TriangleIndices():FlatIndices;
    RiverVertices.SetNumUninitialized(Floor.Num()); RiverNormals.Init(FVector::ZeroVector,Floor.Num());
    const bool NewMesh=RiverMeshVertexCount!=Floor.Num();
    if (NewMesh)
    {
        RiverUVs.SetNumUninitialized(Floor.Num()); RiverColors.Init(FColor::White,Floor.Num());
        RiverTangents.Init(FProcMeshTangent(RiverDirection,false),Floor.Num());
        for (int32 I=0;I<Floor.Num();++I) RiverUVs[I]=FVector2D((Floor[I].X-ArenaCenter.X)/600.f,(Floor[I].Y-ArenaCenter.Y)/600.f);
    }
    for (int32 I=0;I<Floor.Num();++I)
    {
        RiverVertices[I]=Floor[I]; RiverVertices[I].Z=RiverHeightAt(Floor[I],Time,float(Floor[I].Z));
    }
    for (int32 I=0;I+2<Indices.Num();I+=3)
    {
        const int32 A=Indices[I],B=Indices[I+1],C=Indices[I+2];
        FVector Normal=FVector::CrossProduct(RiverVertices[B]-RiverVertices[A],RiverVertices[C]-RiverVertices[A]);
        if (Normal.Z<0) Normal=-Normal;
        RiverNormals[A]+=Normal; RiverNormals[B]+=Normal; RiverNormals[C]+=Normal;
    }
    for (FVector& Normal:RiverNormals) Normal=Normal.GetSafeNormal(KINDA_SMALL_NUMBER,FVector::UpVector);
    RiverSurface->SetWorldTransform(FTransform::Identity);
    if (NewMesh)
    {
        RiverSurface->CreateMeshSection(0,RiverVertices,Indices,RiverNormals,RiverUVs,RiverColors,RiverTangents,false);
        RiverMeshVertexCount=Floor.Num();
    }
    else RiverSurface->UpdateMeshSection(0,RiverVertices,RiverNormals,RiverUVs,RiverColors,RiverTangents);
    if (!Material) return;
    const float Front=RiverFrontDistance();
    Material->SetScalarParameterValue(TEXT("WaterTime"),Time);
    Material->SetScalarParameterValue(TEXT("RiverCycleTime"),WaterSettings.CycleTime(Time));
    Material->SetScalarParameterValue(TEXT("FillAmount"),1);
    Material->SetScalarParameterValue(TEXT("DrainAmount"),0);
    Material->SetScalarParameterValue(TEXT("JetAmount"),0);
    Material->SetScalarParameterValue(TEXT("Filling"),1);
    Material->SetScalarParameterValue(TEXT("RippleHeight"),0);
    Material->SetScalarParameterValue(TEXT("FrontRadius"),Front);
    Material->SetScalarParameterValue(TEXT("FrontWidth"),100);
    Material->SetScalarParameterValue(TEXT("FrontHeight"),WaterSettings.FrontHeight);
    Material->SetScalarParameterValue(TEXT("RiverFront"),Front);
    Material->SetScalarParameterValue(TEXT("RiverTail"),Front-RiverWidth);
    Material->SetScalarParameterValue(TEXT("RiverEdge"),100);
    Material->SetVectorParameterValue(TEXT("RiverDirection"),FLinearColor(RiverDirection.X,RiverDirection.Y,0,0));
    Material->SetVectorParameterValue(TEXT("RiverOrigin"),FLinearColor(RiverOrigin.X,RiverOrigin.Y,0,0));
    Material->SetVectorParameterValue(TEXT("ArenaSize"),FLinearColor(HalfSize.X,HalfSize.Y,0,0));
    Material->SetVectorParameterValue(TEXT("ArenaCenter"),FLinearColor(ArenaCenter.X,ArenaCenter.Y,0,0));
}
void AMCCoffeeFlood::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCoffeeFlood,bActive); DOREPLIFETIME(AMCCoffeeFlood,Level); DOREPLIFETIME(AMCCoffeeFlood,Wave); DOREPLIFETIME(AMCCoffeeFlood,Waves);
    DOREPLIFETIME(AMCCoffeeFlood,Height); DOREPLIFETIME(AMCCoffeeFlood,Flow); DOREPLIFETIME(AMCCoffeeFlood,Paddle); DOREPLIFETIME(AMCCoffeeFlood,Reach);
    DOREPLIFETIME(AMCCoffeeFlood,HalfSize); DOREPLIFETIME(AMCCoffeeFlood,StartedAt); DOREPLIFETIME(AMCCoffeeFlood,Seconds);
    DOREPLIFETIME(AMCCoffeeFlood,Profile); DOREPLIFETIME(AMCCoffeeFlood,WaterSettings);
    DOREPLIFETIME(AMCCoffeeFlood,ArenaCenter); DOREPLIFETIME(AMCCoffeeFlood,InletFloorZ);
    DOREPLIFETIME(AMCCoffeeFlood,bRiverFlood); DOREPLIFETIME(AMCCoffeeFlood,RiverOrigin); DOREPLIFETIME(AMCCoffeeFlood,RiverDirection);
    DOREPLIFETIME(AMCCoffeeFlood,RiverLength); DOREPLIFETIME(AMCCoffeeFlood,RiverWidth); DOREPLIFETIME(AMCCoffeeFlood,RiverSpeed); DOREPLIFETIME(AMCCoffeeFlood,RiverDepth);
}
