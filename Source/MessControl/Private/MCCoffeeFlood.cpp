#include "MCCoffeeFlood.h"
#include "MCCataclysmEvent.h"
#include "MCCataclysmRaft.h"
#include "MCCataclysmStormMath.h"
#include "MCPerkComponent.h"
#include "MCGameDirector.h"
#include "MCDayPlan.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothMovementComponent.h"
#include "MCFoodActor.h"
#include "MCGripComponent.h"
#include "MCTongue.h"
#include "MCDirtPlacement.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
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
    StormSurface=CreateDefaultSubobject<UMCCataclysmStormSurfaceComponent>(TEXT("CataclysmStormSurface"));
    StormSurface->SetupAttachment(Surface); StormSurface->SetAbsolute(true,true,true);
    StormSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision); StormSurface->SetCastShadow(false);
    StormSurface->SetBoundsScale(1.25f);
    StormSpray=CreateDefaultSubobject<UMCCoffeeDropComponent>(TEXT("CataclysmStormSpray"));
    StormSpray->SetupAttachment(Surface); StormSpray->SetAbsolute(true,true,true);
    StormSpray->SetCollisionEnabled(ECollisionEnabled::NoCollision); StormSpray->SetCastShadow(false);
    StormSpray->SetTranslucentSortPriority(3);
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
    if(bCataclysmStorm && !bRiverFlood)
    {
        const FString Path=Base?Base->GetPathName():FString();
        const TCHAR* StormPath=Path.Contains(TEXT("Cola"))?TEXT("/Game/Cataclysm/Storm/MI_StormCola.MI_StormCola"):
            TEXT("/Game/Cataclysm/Storm/MI_StormCoffee.MI_StormCoffee");
        if(auto* StormBase=LoadObject<UMaterialInterface>(nullptr,StormPath)) Base=StormBase;
    }
    if (Base) { Material=UMaterialInstanceDynamic::Create(Base,this); Surface->SetMaterial(0,Material); RiverSurface->SetMaterial(0,Material); StormSurface->SetMaterial(0,Material); }
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
    if(bCataclysmStorm && !bRiverFlood)
    {
        static const TCHAR* SpherePath=TEXT("/Engine/BasicShapes/Sphere.Sphere");
        StormSpray->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,SpherePath));
        if(auto* SprayBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Cataclysm/Storm/M_StormSpray.M_StormSpray")))
        {
            StormSprayMaterial=UMaterialInstanceDynamic::Create(SprayBase,this);
            StormSpray->SetMaterial(0,StormSprayMaterial);
            const FLinearColor Foam=Material?Material->K2_GetVectorParameterValue(TEXT("FoamColor")):FLinearColor(.72f,.49f,.24f);
            StormSprayMaterial->SetVectorParameterValue(TEXT("FoamColor"),Foam);
        }
        if(!StormSpray->GetInstanceCount()) for(int32 I=0;I<48;++I)
            StormSpray->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector));
        StormDrops.SetNum(48);StormSprayBucket=INDEX_NONE;StormImpactVisualCursor=0;
    }
}
void AMCCoffeeFlood::Start(const UMCDayPlan* Plan,float SwimTestSeconds,bool bUseLegacyFlood)
{
    if (!HasAuthority() || !Plan) return;
    if(const auto* Director=AMCGameDirector::Find(GetWorld()); Director && Director->IsManagingEvents()
        && !Director->IsLaunchingEvent(EMCGameDirectorEvent::CoffeeFlood)
        && !Cast<AMCCataclysmEvent>(GetOwner())) return;
    bCataclysmStorm=false;StormSeed=0;LastStormImpactAt=-10.f;StormImpactSerial=0;
    bRiverFlood=!bUseLegacyFlood && SwimTestSeconds<=0;
    Height=Plan->FloodHeight; Flow=Plan->FlowAcceleration; Paddle=Plan->PaddleAcceleration; Reach=Plan->AnchorReach; HalfSize=Plan->ArenaHalfSize;
    ArenaCenter=Plan->ArenaCenter.ContainsNaN()?FVector::ZeroVector:Plan->ArenaCenter;
    Profile=Plan->CoffeeProfile.LoadSynchronous();
    WaterSettings=Profile?Profile->Settings:FMCCoffeeWaterSettings(); WaterSettings.Sanitize();
    AMCTongue* Tongue=Cast<AMCTongue>(GetOwner());
    if (!Tongue) for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty()) {Tongue=*It;break;}
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
    InitializeResidue();
    UE_LOG(LogTemp,Display,TEXT("MC_LIQUID_ARENA center=%s half_size=%s floor=%.1f dry=%.1f water=%.1f inlet=%s"),
        *ArenaCenter.ToString(),*HalfSize.ToString(),InletFloorZ,WaterSettings.DryHeight,Height,*WaterSettings.Inlet.ToString());
}
void AMCCoffeeFlood::SetCataclysmDuration(float FillSeconds,float HoldSeconds,float DrainSeconds)
{
    if(!HasAuthority() || bRiverFlood || !Cast<AMCCataclysmEvent>(GetOwner())) return;
    WaterSettings.FillSeconds=FMath::Clamp(FMath::IsFinite(FillSeconds)?FillSeconds:4.f,1.f,12.f);
    WaterSettings.HoldSeconds=FMath::Clamp(FMath::IsFinite(HoldSeconds)?HoldSeconds:35.f,5.f,120.f);
    WaterSettings.DrainSeconds=FMath::Clamp(FMath::IsFinite(DrainSeconds)?DrainSeconds:4.f,1.f,12.f);
    WaterSettings.Cycles=1;Waves=1;Seconds=WaterSettings.CycleSeconds();ForceNetUpdate();
}
void AMCCoffeeFlood::EnableCataclysmStorm(int32 Seed)
{
    if(!HasAuthority() || !bActive || bRiverFlood) return;
    // Live walkable gum samples extend ~200 cm beyond the tongue Y bounds.
    // The same expanded extent drives containment, current, grid and shader mask.
    if(!bCataclysmStorm) {
        // Resolve the same exterior edge as the first incoming wave before the
        // gum margin enlarges the wet bounds. No overhead pour origin remains.
        RiverDirection=(WaterSettings.DrainPoint-ArenaCenter).GetSafeNormal2D(UE_SMALL_NUMBER,FVector::ForwardVector);
        const float Span=FMath::Abs(RiverDirection.X)*HalfSize.X+FMath::Abs(RiverDirection.Y)*HalfSize.Y;
        RiverOrigin=ArenaCenter-RiverDirection*(Span+160.f);
        RiverOrigin.Z=InletFloorZ;
        HalfSize.X+=350.f;HalfSize.Y+=350.f;
    }
    const float FullSpan=FMath::Abs(RiverDirection.X)*HalfSize.X+FMath::Abs(RiverDirection.Y)*HalfSize.Y;
    RiverLength=FMath::Max(500.f,float(FVector::DotProduct(ArenaCenter-RiverOrigin,RiverDirection))+FullSpan);
    RiverSpeed=FMath::Clamp((RiverLength+MCCataclysmStormMath::FrontWidth*2.f)/FMath::Max(.75f,WaterSettings.FillSeconds-.3f),1550.f,2200.f);
    WaterSettings.Inlet=RiverOrigin;
    WaterSettings.FrontWidth=MCCataclysmStormMath::FrontWidth;
    WaterSettings.FrontHeight=MCCataclysmStormMath::FrontAmplitude;
    WaterSettings.DrainRadius=MCCataclysmStormMath::DrainRadius;
    WaterSettings.DrainDepth=MCCataclysmStormMath::DrainDepth;
    bCataclysmStorm=true;StormSeed=Seed;
    InitializeResidue();
    OnRep_Profile();UpdateSurface();ForceNetUpdate();
}
bool AMCCoffeeFlood::IsCataclysmStorm() const { return bCataclysmStorm && bActive && !bRiverFlood; }
FVector AMCCoffeeFlood::StormStreamDirection() const
{
    return bCataclysmStorm?RiverDirection:(WaterSettings.DrainPoint-WaterSettings.Inlet).GetSafeNormal2D(UE_SMALL_NUMBER,FVector::ForwardVector);
}
float AMCCoffeeFlood::StormStrengthAt(float Time) const
{
    return bCataclysmStorm && !bRiverFlood?FMath::SmoothStep(0.f,.2f,Time):0.f;
}
float AMCCoffeeFlood::StormStrength() const { return IsCataclysmStorm()?StormStrengthAt(WaterTime()):0.f; }
float AMCCoffeeFlood::StormWaveAt(FVector Position,float Time) const
{
    if(!bCataclysmStorm || bRiverFlood) return 0.f;
    return StormWetAt(Position,Time)*MCCataclysmStormMath::InflowOffset(Position,ArenaCenter,RiverOrigin,
        WaterSettings.DrainPoint,StormStreamDirection(),Time,StormSeed,FMath::Max(0.f,Time-.2f)*RiverSpeed);
}
float AMCCoffeeFlood::StormWaveHeightAt(FVector Position) const { return IsCataclysmStorm()?StormWaveAt(Position,WaterTime()):0.f; }
float AMCCoffeeFlood::StormFrontAt(FVector P,float Time) const
{
    const FVector Side(-RiverDirection.Y,RiverDirection.X,0);
    return FMath::Max(0.f,Time-.2f)*RiverSpeed
        +MCCataclysmStormMath::FrontJitter(FVector::DotProduct(P-RiverOrigin,Side),Time);
}
float AMCCoffeeFlood::StormWetAt(FVector P,float Time) const
{
    const float DrainStart=WaterSettings.FillSeconds+WaterSettings.HoldSeconds;
    const float Tail=Time<DrainStart?-100000.f:(Time-DrainStart)*RiverSpeed;
    return MCCataclysmStormMath::WetWeight(P,ArenaCenter,RiverOrigin,RiverDirection,HalfSize,
        Time,FMath::Max(0.f,Time-.2f)*RiverSpeed,Tail);
}
float AMCCoffeeFlood::StormWetWeightAt(FVector P) const
{
    return IsCataclysmStorm() && !P.ContainsNaN()
        && FMath::Abs(P.X-ArenaCenter.X)<HalfSize.X && FMath::Abs(P.Y-ArenaCenter.Y)<HalfSize.Y?StormWetAt(P,WaterTime()):0.f;
}
float AMCCoffeeFlood::StormHeightAt(FVector P,float Time) const
{
    const float Wet=StormWetAt(P,Time);
    const float DrainStart=WaterSettings.FillSeconds+WaterSettings.HoldSeconds;
    const float Tail=Time<DrainStart?-100000.f:(Time-DrainStart)*RiverSpeed;
    const float Arrival=MCCataclysmStormMath::ArrivalWeight(P,RiverOrigin,RiverDirection,Time,
        FMath::Max(0.f,Time-.2f)*RiverSpeed,Tail);
    // Only arrival/recession changes the base level. Lowering edge vertices by
    // the full water depth would create serrated curtains at the channel mask.
    return FMath::Lerp(WaterSettings.DryHeight,Height,Arrival)+Wet*WaterSettings.Ripple(P,Time)+StormWaveAt(P,Time);
}
void AMCCoffeeFlood::NotifyStormImpact(FVector Position,FVector Direction,float Strength)
{
    if(!HasAuthority() || !IsCataclysmStorm() || Position.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(Strength) || FMath::Abs(Position.X-ArenaCenter.X)>HalfSize.X
        || FMath::Abs(Position.Y-ArenaCenter.Y)>HalfSize.Y) return;
    const float Time=WaterTime();
    // An authority contact callback may repeat while actors remain pressed together.
    // One short burst per flood is sufficient; never stream an RPC on each tick.
    if(Time-LastStormImpactAt<.12f) return;
    LastStormImpactAt=Time;
    Position.Z=SurfaceHeightAt(Position)+4.f;
    MulticastStormImpact(Position,Direction.GetSafeNormal2D(UE_SMALL_NUMBER,StormStreamDirection()),
        FMath::Clamp(Strength,.3f,1.25f),int32(HashCombine(GetTypeHash(StormSeed),GetTypeHash(++StormImpactSerial))));
}
void AMCCoffeeFlood::MulticastStormImpact_Implementation(FVector Position,FVector Direction,float Strength,int32 ImpactSeed)
{
    if(GetNetMode()==NM_DedicatedServer || !IsCataclysmStorm() || StormDrops.Num()!=48) return;
    const FVector Side(-Direction.Y,Direction.X,0);const float Time=WaterTime();
    FRandomStream R(ImpactSeed);
    // Slots 36..47 are reserved for real impacts so routine crest spray cannot
    // overwrite a contact's short silhouette before it has been seen.
    for(int32 Drop=0;Drop<6;++Drop)
    {
        auto& Particle=StormDrops[36+(StormImpactVisualCursor++%12)];
        Particle.Origin=Position+Side*R.FRandRange(-14.f,14.f);
        Particle.Velocity=Direction*R.FRandRange(65.f,150.f)*Strength+Side*R.FRandRange(-105.f,105.f)
            +FVector(0,0,R.FRandRange(160.f,235.f)*Strength);
        Particle.Born=Time;Particle.Size=R.FRandRange(6.f,11.f)*Strength;Particle.LifeSeconds=.6f;
    }
}
FVector AMCCoffeeFlood::StormCurrentAt(FVector Position) const
{
    if(!IsCataclysmStorm() || Position.ContainsNaN() || FMath::Abs(Position.X-ArenaCenter.X)>HalfSize.X
        || FMath::Abs(Position.Y-ArenaCenter.Y)>HalfSize.Y) return FVector::ZeroVector;
    const float Time=WaterTime(),Phase=MCCataclysmStormMath::SeedPhase(StormSeed),Strength=StormWetAt(Position,Time);
    const FVector Direction=StormStreamDirection(),Side(-Direction.Y,Direction.X,0);
    const float Gust=.5f+.5f*FMath::Sin(Time*.65f+Phase);
    const float Distance=FVector::Dist2D(Position,WaterSettings.DrainPoint);
    const float Along=FVector::DotProduct(Position-RiverOrigin,Direction);
    const float OutAlong=FVector::DotProduct(WaterSettings.DrainPoint-RiverOrigin,Direction);
    const float Approach=FMath::SmoothStep(OutAlong-1400.f,OutAlong-100.f,Along);
    const float Pull=FMath::Max(Approach,1-FMath::SmoothStep(250.f,1100.f,Distance));
    const FVector Into=(WaterSettings.DrainPoint-Position).GetSafeNormal2D(UE_SMALL_NUMBER,Direction);
    const FVector Tangent(-Into.Y,Into.X,0);
    const FVector Heading=(Direction*(1-Pull)+Into*Pull).GetSafeNormal2D(UE_SMALL_NUMBER,Direction);
    return (Heading*(Flow*(1.6f+.35f*Gust)+Pull*180.f)
        +Side*(Flow*.13f*FMath::Sin(Time*1.6f+Phase))
        +Tangent*(Pull*Flow*.22f*FMath::Sin(Time*2.4f+Phase)))*Strength;
}
EMCCoffeePhase AMCCoffeeFlood::GetPhase() const
{
    return bActive?WaterSettings.Phase(WaterTime()):EMCCoffeePhase::Inactive;
}
AMCTongue* AMCCoffeeFlood::FindRiverTongue() const
{
    if (auto* ExplicitFloor=Cast<AMCTongue>(GetOwner())) return ExplicitFloor;
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
    return bCataclysmStorm?Height:FMath::Lerp(WaterSettings.DryHeight,Height,WaterSettings.FillAmount(T));
}
float AMCCoffeeFlood::SurfaceHeightAt(FVector P) const
{
    const float T=WaterTime();
    if(bCataclysmStorm && !bRiverFlood)
        return bActive && StormWetAt(P,T)>.001f?StormHeightAt(P,T):WaterSettings.DryHeight;
    return bRiverFlood?RiverHeightAt(P,T,RiverFloorAt(P)):BaseHeight(T)+WaterSettings.SurfaceOffset(P,T)+StormWaveAt(P,T);
}
float AMCCoffeeFlood::SurfaceVerticalSpeedAt(FVector P) const
{
    const float T=WaterTime(),Before=FMath::Max(0.f,T-.04f),After=FMath::Min(Seconds,T+.04f);
    if(bCataclysmStorm && !bRiverFlood) return bActive?FMath::Clamp((StormHeightAt(P,After)-StormHeightAt(P,Before))/FMath::Max(.001f,After-Before),-220.f,220.f):0.f;
    if (bRiverFlood) return FMath::Clamp((RiverHeightAt(P,After,RiverFloorAt(P))-RiverHeightAt(P,Before,RiverFloorAt(P)))/FMath::Max(.001f,After-Before),-220.f,220.f);
    return FMath::Clamp((BaseHeight(After)+WaterSettings.SurfaceOffset(P,After)+StormWaveAt(P,After)
        -BaseHeight(Before)-WaterSettings.SurfaceOffset(P,Before)-StormWaveAt(P,Before))/FMath::Max(.001f,After-Before),-220.f,220.f);
}
bool AMCCoffeeFlood::Contains(FVector P) const
{
    if (!bActive || P.ContainsNaN() || FMath::Abs(P.X-ArenaCenter.X)>=HalfSize.X || FMath::Abs(P.Y-ArenaCenter.Y)>=HalfSize.Y || P.Z<=WaterSettings.DryHeight-100 || P.Z>=SurfaceHeightAt(P)+35) return false;
    if (bRiverFlood) return RiverWeightAt(P)>.05f;
    if(IsCataclysmStorm()) return StormWetAt(P,WaterTime())>.05f;
    return GetPhase()!=EMCCoffeePhase::Filling || FVector::Dist2D(P,WaterSettings.Inlet)<WaterSettings.FrontRadius(WaterTime())+WaterSettings.FrontWidth;
}
bool AMCCoffeeFlood::IsFlowBlocked(FVector P,const AActor* Ignore) const
{
    FVector Source=GetPhase()==EMCCoffeePhase::Draining?WaterSettings.DrainPoint:WaterSettings.Inlet;
    if(IsCataclysmStorm())
    {
        const FVector Heading=StormCurrentAt(P).GetSafeNormal2D(UE_SMALL_NUMBER,StormStreamDirection());
        Source=P-Heading*280.f;Source.Z=P.Z=SurfaceHeightAt(P)+5.f;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCataclysmShelter),false,Ignore);
        if(auto* Tongue=FindRiverTongue()) Params.AddIgnoredActor(Tongue);
        FHitResult Hit;return GetWorld()->LineTraceSingleByChannel(Hit,Source,P,ECC_Visibility,Params);
    }
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
    const auto* Perks=Cast<AMCToothCharacter>(Ignore)?UMCPerkComponent::ForActor(Ignore):nullptr;
    const float WaveScale=Perks?Perks->RogueValue(EMCRogueStat::WaveResistance):1.f;
    if(IsCataclysmStorm())
    {
        const FVector Current=StormCurrentAt(P);
        return Current.IsNearlyZero() || IsFlowBlocked(P,Ignore)?FVector::ZeroVector:Current*WaveScale;
    }
    if (bRiverFlood)
    {
        if (!Contains(P) || IsFlowBlocked(P,Ignore)) return FVector::ZeroVector;
        const float Along=FVector::DotProduct(P-RiverOrigin,RiverDirection);
        const float Impact=FMath::Exp(-FMath::Square((Along-RiverFrontAt(P,WaterTime())+80.f)/180.f));
        // A breaking front overpowers upstream sprinting. The body keeps pushing,
        // while cover, sideward escape and a tooth grip remain useful responses.
        return RiverDirection*Flow*6.f*(1.f+.6f*Impact)*RiverWeightAt(P)*WaveScale;
    }
    return bActive && !IsFlowBlocked(P,Ignore)?WaterSettings.FlowAt(P,WaterTime(),Flow)*WaveScale:FVector::ZeroVector;
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
void AMCCoffeeFlood::InitializeResidue()
{
    ResidueCells.Reset(); ResiduePatches.Reset(); LastResidueTime=-100;
    auto* Tongue=FindRiverTongue(); if (!Tongue) return;
    ResidueCells.SetNum(ResidueSize*ResidueSize);
    for (int32 Y=0;Y<ResidueSize;++Y) for (int32 X=0;X<ResidueSize;++X) {
        auto& Cell=ResidueCells[Y*ResidueSize+X];
        const FVector Probe=ArenaCenter+FVector(((X+.5f)/ResidueSize*2-1)*HalfSize.X,((Y+.5f)/ResidueSize*2-1)*HalfSize.Y,0);
        // Keep the complete liquid rectangle for teeth beside the tongue too.
        // Only supported cells are eligible to host a floor puddle.
        Cell.Point=FVector(Probe.X,Probe.Y,InletFloorZ);
        FHitResult Floor;
        if (Tongue->SurfacePoint(Probe,Floor) && Floor.ImpactNormal.Z>.65f) {
            Cell.Point=Floor.ImpactPoint; Cell.bSurface=true;
        }
    }
    TArray<FVector> Placed;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
        if(!It->bUlcer && !It->IsClean() && (!Cast<AMCTongue>(GetOwner()) || It->GetTongue()==Tongue)) Placed.Add(It->GetActorLocation());
    const int32 Budget=FMath::Clamp(18-Placed.Num(),0,8);
    uint32 PlacementSeed=HashCombine(GetTypeHash(StartedAt),GetTypeHash(ArenaCenter));
    if(const auto* Game=GetWorld()->GetGameState<AMCGameState>())
        PlacementSeed=HashCombine(PlacementSeed,GetTypeHash(Game->RunSeed));
    FRandomStream Random(PlacementSeed);
    // Choose supported footprints before arrival. Their visibility is solely the
    // swept wet mask, never a random spawn performed when the event finishes.
    // Centres are continuous seeded samples, independent of the wet-cell grid;
    // separation rejects crowding without constructing rings or ordered rows.
    for(int32 Attempt=0;Attempt<ResidueSize*ResidueSize && ResiduePatches.Num()<Budget;++Attempt)
    {
        const float RequestedHalf=Random.FRandRange(160.f,320.f);
        float Half=RequestedHalf;
        FHitResult Floor;bool Found=false;
        for(int32 SizePass=0;SizePass<3 && !Found;++SizePass) {
            Half=FMath::Max(160.f,RequestedHalf*FMath::Pow(.8f,SizePass));
            const FBox Bounds=MCDirtPlacement::WorkBounds(Tongue,Half*1.415f+65.f);
            if(!Bounds.IsValid) continue;
            // Keep the candidate within this wave's liquid rectangle as well
            // as the safe tongue area, so it can actually receive swept water.
            const double MinX=FMath::Max(Bounds.Min.X,ArenaCenter.X-HalfSize.X);
            const double MaxX=FMath::Min(Bounds.Max.X,ArenaCenter.X+HalfSize.X);
            const double MinY=FMath::Max(Bounds.Min.Y,ArenaCenter.Y-HalfSize.Y);
            const double MaxY=FMath::Min(Bounds.Max.Y,ArenaCenter.Y+HalfSize.Y);
            if(MinX>=MaxX || MinY>=MaxY) continue;
            const FVector Point(FMath::Lerp(MinX,MaxX,Random.FRand()),
                FMath::Lerp(MinY,MaxY,Random.FRand()),Bounds.GetCenter().Z);
            bool Near=false; for(const FVector& Other:Placed) if(FVector::Dist2D(Point,Other)<Half*1.6f+200) {Near=true;break;}
            if(!Near) Found=MCDirtPlacement::FindSurface(Tongue,Point,Half*1.415f,Floor);
        }
        if(!Found) continue;
        FResiduePatch Patch;Patch.Point=Floor.ImpactPoint;Patch.HalfSize=Half;
        ResiduePatches.Add(Patch);Placed.Add(Patch.Point);
    }
}
void AMCCoffeeFlood::AccumulateResidue(float Time)
{
    // One cached tissue grid records the same uneven wave edge as gameplay and rendering.
    // Substeps also preserve the swept footprint on a slow server frame.
    if (ResidueCells.IsEmpty() || Time-LastResidueTime<.1f) return;
    const float From=LastResidueTime<0?0:LastResidueTime;
    const int32 Steps=FMath::Clamp(FMath::CeilToInt((Time-From)/.1f),1,64);
    for (int32 Step=1;Step<=Steps;++Step) {
        const float T=FMath::Lerp(From,Time,float(Step)/Steps);
        if (WaterSettings.Phase(T)==EMCCoffeePhase::Inactive) continue;
        for (auto& Cell:ResidueCells) {
            float Top;
            if (bRiverFlood) {
                const float Along=FVector::DotProduct(Cell.Point-RiverOrigin,RiverDirection),Front=RiverFrontAt(Cell.Point,T);
                const float Weight=FMath::SmoothStep(Front-RiverWidth-100,Front-RiverWidth+100,Along)*(1-FMath::SmoothStep(Front-100,Front+100,Along));
                if (Weight<=.05f) continue;
                Top=RiverHeightAt(Cell.Point,T,Cell.Point.Z);
            } else if(bCataclysmStorm) {
                if(StormWetAt(Cell.Point,T)<=.05f) continue;
                Top=StormHeightAt(Cell.Point,T);
            } else {
                if (WaterSettings.Phase(T)==EMCCoffeePhase::Filling && FVector::Dist2D(Cell.Point,WaterSettings.Inlet)>=WaterSettings.FrontRadius(T)+WaterSettings.FrontWidth) continue;
                Top=BaseHeight(T)+WaterSettings.SurfaceOffset(Cell.Point,T)+StormWaveAt(Cell.Point,T);
            }
            // A tooth can be reached above an otherwise unsupported side cell.
            if (!Cell.bSurface || Top>=Cell.Point.Z) Cell.WetTop=FMath::Max(Cell.WetTop,Top);
        }
    }
    LastResidueTime=Time;
    DepositResidue();
}
bool AMCCoffeeFlood::HasResidueAt(FVector P,bool bNewOnly) const
{
    if (ResidueCells.IsEmpty() || P.ContainsNaN()) return false;
    const FVector2D UV((P.X-ArenaCenter.X)/(2*HalfSize.X)+.5,(P.Y-ArenaCenter.Y)/(2*HalfSize.Y)+.5);
    if (UV.X<0 || UV.Y<0 || UV.X>=1 || UV.Y>=1) return false;
    const int32 X=FMath::Clamp(int32(UV.X*ResidueSize),0,ResidueSize-1),Y=FMath::Clamp(int32(UV.Y*ResidueSize),0,ResidueSize-1);
    const auto& Cell=ResidueCells[Y*ResidueSize+X];
    return Cell.WetTop> -MAX_flt && P.Z<=Cell.WetTop+5
        && (!bNewOnly || Cell.EmittedTop<=-MAX_flt || P.Z>Cell.EmittedTop+5);
}
void AMCCoffeeFlood::DepositResidue()
{
    auto* Tongue=FindRiverTongue(); if (!Tongue || ResidueCells.IsEmpty()) return;
    bool NewWet=false; for(const auto& Cell:ResidueCells) if(Cell.WetTop>Cell.EmittedTop) {NewWet=true;break;}
    if(!NewWet) return;
    for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) {
        const FBox Bounds=It->Visual->Bounds.GetBox();
        if(!It->IsAvailable() || Bounds.Min.X>ArenaCenter.X+HalfSize.X || Bounds.Max.X<ArenaCenter.X-HalfSize.X
            || Bounds.Min.Y>ArenaCenter.Y+HalfSize.Y || Bounds.Max.Y<ArenaCenter.Y-HalfSize.Y) continue;
        const auto CellIndex=[&](double Position,double Center,double Half) {
            return FMath::Clamp(FMath::FloorToInt(((Position-Center)/(Half*2)+.5)*ResidueSize),0,ResidueSize-1);
        };
        const int32 MinX=CellIndex(Bounds.Min.X,ArenaCenter.X,HalfSize.X),MaxX=CellIndex(Bounds.Max.X,ArenaCenter.X,HalfSize.X);
        const int32 MinY=CellIndex(Bounds.Min.Y,ArenaCenter.Y,HalfSize.Y),MaxY=CellIndex(Bounds.Max.Y,ArenaCenter.Y,HalfSize.Y);
        bool Reached=false;
        for(int32 Y=MinY;Y<=MaxY && !Reached;++Y) for(int32 X=MinX;X<=MaxX;++X) {
            const auto& Cell=ResidueCells[Y*ResidueSize+X];
            if(Cell.WetTop>Cell.EmittedTop && Cell.WetTop+5>=Bounds.Min.Z && Cell.EmittedTop+5<Bounds.Max.Z) {Reached=true;break;}
        }
        // Avoid projecting every initially clean tooth on the first wave tick.
        // Detailed coating work starts only when its actual bounds are reached.
        if(Reached) It->DepositLiquidGrime([this](FVector Point) { return HasResidueAt(Point,true); });
    }
    for(auto& Planned:ResiduePatches) {
        const float Half=Planned.HalfSize;
        const FTransform Pose(FQuat::Identity,Planned.Point+FVector(0,0,5));
        TArray<uint8> Mask; Mask.Init(0,FMCCoffeeWipe::Count);
        int32 Wet=0;
        // Match the supported source footprint on the server. The shader uses
        // precisely this cleanable mask; it does not invent extra visible dirt.
        const int32 Seed=1+int32(HashCombine(GetTypeHash(FMath::RoundToInt(Planned.Point.X)),GetTypeHash(FMath::RoundToInt(Planned.Point.Y)))%4093);
        const TArray<uint8> Full;
        for (int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for (int32 X=0;X<FMCCoffeeWipe::Size;++X) {
            const FVector2D UV((X+.5f)/FMCCoffeeWipe::Size,(Y+.5f)/FMCCoffeeWipe::Size);
            if(!FMCCoffeeWipe::WetAt(Full,UV,Seed)) continue;
            const FVector Local((UV.X-.5)*2*Half,(UV.Y-.5)*2*Half,0);
            if (HasResidueAt(Pose.TransformPosition(Local))) { Mask[Y*FMCCoffeeWipe::Size+X]=255; ++Wet; }
        }
        if(Wet==0) continue;
        auto* Patch=Planned.Surface.Get();
        if(!Patch) {
            Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
            if(!Patch) continue;
            Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=Half; Patch->Batch=ResidueBatch;
            Patch->LiquidMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Cataclysm/Jelly/M_CataclysmPaintTrail.M_CataclysmPaintTrail")));
            Patch->Tags.AddUnique(TEXT("MCCoffeeWaveResidue"));
            if(ResidueBatch>0) Patch->Tags.AddUnique(TEXT("MCFoodServedDirt"));
            Patch->InitializePaintTile(Tongue);Patch->FinishSpawning(Pose);Planned.Surface=Patch;
        }
        Patch->AppendLiquidDepositMask(Mask);
    }
    for(auto& Cell:ResidueCells) Cell.EmittedTop=Cell.WetTop;
}
void AMCCoffeeFlood::Stop()
{
    if (!HasAuthority()) return;
    bActive=false; Level=-40; ForceNetUpdate();
    Surface->SetVisibility(false); RiverSurface->SetVisibility(false); StormSurface->SetVisibility(false);StormSpray->SetVisibility(false); Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false);
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        if (Cast<AMCTongue>(GetOwner()) && (FMath::Abs(It->GetActorLocation().X-ArenaCenter.X)>HalfSize.X
            || FMath::Abs(It->GetActorLocation().Y-ArenaCenter.Y)>HalfSize.Y)) continue;
        It->bInCoffee=false; It->ClingTooth=nullptr; It->ForceNetUpdate();
    }
}
void AMCCoffeeFlood::Tick(float Dt)
{
    Super::Tick(Dt);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (HasAuthority() && bActive)
    {
        const auto* Director=AMCGameDirector::Find(GetWorld());
        const bool CarryingIntoRest=GS && GS->Phase==EMCShiftPhase::Intermission
            && Director && Director->IsManagingEvents();
        if (!GS || (GS->Phase!=EMCShiftPhase::Working && !CarryingIntoRest)) { Stop(); return; }
        const float Age=WaterTime();
        if (Age>=Seconds) { AccumulateResidue(Seconds-UE_KINDA_SMALL_NUMBER); Stop(); return; }
        AccumulateResidue(Age);
        const int32 Current=FMath::FloorToInt(Age/WaterSettings.CycleSeconds())+1;
        if (Current!=Wave) { Wave=Current; HitThisWave.Empty(); FoodHitThisWave.Empty(); ForceNetUpdate(); }
        Level=BaseHeight(Age);
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It; const FVector P=Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
            if (Cast<AMCTongue>(GetOwner()) && (FMath::Abs(P.X-ArenaCenter.X)>HalfSize.X || FMath::Abs(P.Y-ArenaCenter.Y)>HalfSize.Y)) continue;
            const FVector WaterProbe=bRiverFlood && Hero->ToothPhysics->GetBodyState()!=EMCBodyState::Ragdoll?
                P-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-10.f):P;
            Hero->bInCoffee=!Hero->SwallowedBy && !Hero->MimicCaptor && Contains(WaterProbe) && Hero->Status->IsAlive();
            if (Hero->SwallowedBy || Hero->MimicCaptor || !Hero->Status->IsAlive() || FMath::Abs(P.X-ArenaCenter.X)>HalfSize.X || FMath::Abs(P.Y-ArenaCenter.Y)>HalfSize.Y) { Hero->ClingTooth=nullptr; continue; }
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
            const float Distance=bCataclysmStorm?float(FVector::DotProduct(P-RiverOrigin,RiverDirection)):FVector::Dist2D(P,WaterSettings.Inlet);
            const float Front=bCataclysmStorm?StormFrontAt(P,Age):WaterSettings.FrontRadius(Age);
            const float FrontSpeed=bCataclysmStorm?RiverSpeed:WaterSettings.FrontSpeed;
            const bool CrossedFront=Distance<=Front+WaterSettings.FrontWidth && Distance>=Front-FrontSpeed*Dt-WaterSettings.FrontWidth;
            if (!bRiverFlood && !Hero->ClingTooth && !HitThisWave.Contains(Hero) && GetPhase()==EMCCoffeePhase::Filling
                && WaterSettings.CycleTime(Age)>.25f && P.Z<Height+100 && P.Z>WaterSettings.DryHeight
                && (CrossedFront || (!bCataclysmStorm && Distance<WaterSettings.JetRadius*1.4f))
                && (!bCataclysmStorm || StormWetAt(P,Age)>.05f) && !IsFlowBlocked(P,Hero))
            {
                HitThisWave.Add(Hero);
                const FVector Away=bCataclysmStorm?StormStreamDirection():(P-WaterSettings.Inlet).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
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
                    const FVector Heading=bCataclysmStorm?StormCurrentAt(P).GetSafeNormal2D(UE_SMALL_NUMBER,StormStreamDirection()):(P-WaterSettings.Inlet).GetSafeNormal2D();
                    It->Body->AddImpulse(Heading*WaterSettings.ImpactImpulse*3.f);
                }
            }
    }
    UpdateSurface();
}
void AMCCoffeeFlood::UpdateSurface()
{
    if (GetNetMode()==NM_DedicatedServer) return;
    Surface->SetVisibility(bActive && !bRiverFlood && !bCataclysmStorm); RiverSurface->SetVisibility(bActive && bRiverFlood);
    StormSurface->SetVisibility(IsCataclysmStorm());StormSpray->SetVisibility(IsCataclysmStorm());
    if (!bActive) { Jet->SetVisibility(false); Crown->SetVisibility(false); DrainRibbon->SetVisibility(false); Drops->SetVisibility(false); return; }
    const float T=WaterTime();
    UpdatePour(T);
    if (bRiverFlood) { UpdateRiverSurface(T); return; }
    const FBoxSphereBounds MeshBounds=Surface->GetStaticMesh()?Surface->GetStaticMesh()->GetBounds():FBoxSphereBounds(FVector::ZeroVector,FVector(50),86.6);
    const FVector PlaneScale(HalfSize.X/FMath::Max(1.,MeshBounds.BoxExtent.X),HalfSize.Y/FMath::Max(1.,MeshBounds.BoxExtent.Y),1);
    Surface->SetWorldScale3D(PlaneScale);
    Surface->SetWorldLocation(FVector(ArenaCenter.X,ArenaCenter.Y,BaseHeight(T))-MeshBounds.Origin*PlaneScale);
    if(IsCataclysmStorm()) {UpdateStormSurface(T);UpdateStormSpray(T);}
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
    if(IsCataclysmStorm())
    {
        Material->SetScalarParameterValue(TEXT("StormTime"),T);
        Material->SetScalarParameterValue(TEXT("StormStrength"),StormStrength());
        Material->SetScalarParameterValue(TEXT("StormPhase"),MCCataclysmStormMath::SeedPhase(StormSeed));
        const FVector StormDirection=StormStreamDirection();
        Material->SetVectorParameterValue(TEXT("StormDirection"),FLinearColor(StormDirection.X,StormDirection.Y,0,0));
        Material->SetScalarParameterValue(TEXT("OpticalFlowSpeed"),MCCataclysmStormMath::OpticalSpeed);
        Material->SetScalarParameterValue(TEXT("StormOpticalSpeed"),MCCataclysmStormMath::OpticalSpeed);
        Material->SetScalarParameterValue(TEXT("StormFrontDistance"),FMath::Max(0.f,T-.2f)*RiverSpeed);
        Material->SetScalarParameterValue(TEXT("StormTailDistance"),T<WaterSettings.FillSeconds+WaterSettings.HoldSeconds?-100000.f:(T-WaterSettings.FillSeconds-WaterSettings.HoldSeconds)*RiverSpeed);
        Material->SetScalarParameterValue(TEXT("StormFrontWidth"),MCCataclysmStormMath::FrontWidth);
        Material->SetScalarParameterValue(TEXT("StormWetDepth"),Height-WaterSettings.DryHeight);
        Material->SetScalarParameterValue(TEXT("StormDrainRadius"),MCCataclysmStormMath::DrainRadius);
        Material->SetScalarParameterValue(TEXT("StormDrainDepth"),MCCataclysmStormMath::DrainDepth);
        Material->SetScalarParameterValue(TEXT("StormActivation"),StormStrengthAt(T));
    }
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
void AMCCoffeeFlood::UpdateStormSurface(float Time)
{
    const FVector2D Size(HalfSize.X,HalfSize.Y);
    if(StormSurface->GetNumSections()==0 || !StormMeshHalfSize.Equals(Size,.1))
    {
        const int32 XSteps=FMath::Clamp(FMath::CeilToInt(HalfSize.X*2./40.),32,96);
        const int32 YSteps=FMath::Clamp(FMath::CeilToInt(HalfSize.Y*2./40.),32,96);
        TArray<FVector> Vertices,Normals;TArray<FVector2D> UVs;TArray<FColor> Colors;
        TArray<FProcMeshTangent> Tangents;TArray<int32> Indices;
        const int32 Count=(XSteps+1)*(YSteps+1);
        Vertices.Reserve(Count);Normals.Init(FVector::UpVector,Count);Colors.Init(FColor::White,Count);
        Tangents.Init(FProcMeshTangent(FVector::ForwardVector,false),Count);UVs.Reserve(Count);
        for(int32 Y=0;Y<=YSteps;++Y) for(int32 X=0;X<=XSteps;++X)
        {
            const float U=float(X)/XSteps,V=float(Y)/YSteps;
            Vertices.Add(FVector((U*2.f-1.f)*HalfSize.X,(V*2.f-1.f)*HalfSize.Y,0));UVs.Add(FVector2D(U,V));
            if(X<XSteps && Y<YSteps)
            {
                const int32 A=Y*(XSteps+1)+X,B=A+XSteps+1;
                Indices.Append({A,A+1,B+1,A,B+1,B});
            }
        }
        StormSurface->CreateMeshSection(0,Vertices,Indices,Normals,UVs,Colors,Tangents,false);
        StormMeshHalfSize=Size;
    }
    StormSurface->SetWorldLocationAndRotation(FVector(ArenaCenter.X,ArenaCenter.Y,BaseHeight(Time)),FRotator::ZeroRotator);
    StormSurface->SetWorldScale3D(FVector::OneVector);
}

void AMCCoffeeFlood::UpdateStormSpray(float Time)
{
    if(!StormSpray->GetStaticMesh() || StormDrops.Num()!=48) return;
    const int32 Bucket=FMath::FloorToInt(Time*8.f);
    const float Strength=StormStrengthAt(Time);
    if(Bucket!=StormSprayBucket)
    {
        StormSprayBucket=Bucket;
        TArray<AMCCataclysmRaft*> Rafts;
        for(TActorIterator<AMCCataclysmRaft> It(GetWorld());It;++It) if(It->Flood==this) Rafts.Add(*It);
        Rafts.Sort([](const AMCCataclysmRaft& A,const AMCCataclysmRaft& B){return A.GetFName().LexicalLess(B.GetFName());});
        const FVector Direction=StormStreamDirection(),Side(-Direction.Y,Direction.X,0);
        for(int32 Source=0;Source<FMath::Min(2,Rafts.Num());++Source)
        {
            FVector Center=Rafts[Source]->GetActorLocation();
            const float Crest=StormWaveAt(Center,Time);
            if(StormWetAt(Center,Time)<.15f || Crest<4.f) continue;
            for(int32 Drop=0;Drop<3;++Drop)
            {
                const int32 Index=(Bucket*6+Source*3+Drop)%36;
                FRandomStream R(HashCombine(GetTypeHash(StormSeed),GetTypeHash(Bucket*17+Source*5+Drop)));
                auto& Particle=StormDrops[Index];
                Particle.Origin=Center-Direction*115.f+Side*R.FRandRange(-72.f,72.f);
                Particle.Origin.Z=SurfaceHeightAt(Particle.Origin)+3.f;
                Particle.Velocity=Direction*R.FRandRange(220.f,370.f)+Side*R.FRandRange(-80.f,80.f)+FVector(0,0,R.FRandRange(160.f,260.f));
                Particle.Born=Time;Particle.Size=R.FRandRange(4.f,8.f)*Strength;Particle.LifeSeconds=.85f;
            }
        }
    }
    for(int32 Index=0;Index<48;++Index)
    {
        const auto& Drop=StormDrops[Index];const float Age=Time-Drop.Born;
        const bool Alive=Age>=0.f && Age<Drop.LifeSeconds && Drop.Size>0.f && Strength>.03f;
        FVector Point=Drop.Origin+Drop.Velocity*Age+FVector(0,0,-240.f*Age*Age);
        if(!Alive) Point=FVector(ArenaCenter.X,ArenaCenter.Y,BaseHeight(Time));
        const float Size=Alive && Point.Z>SurfaceHeightAt(Point)-2.f?Drop.Size*(1.f-FMath::SmoothStep(Drop.LifeSeconds*.65f,Drop.LifeSeconds,Age)):0.f;
        const FVector Velocity=Drop.Velocity-FVector(0,0,480.f*Age);
        const FQuat Rotation=FRotationMatrix::MakeFromZ(Velocity.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector)).ToQuat();
        StormSpray->UpdateInstanceTransform(Index,FTransform(Rotation,Point,FVector(Size*.01f,Size*.01f,Size*.023f)),true,Index==47,true);
    }
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
    DOREPLIFETIME(AMCCoffeeFlood,bCataclysmStorm);DOREPLIFETIME(AMCCoffeeFlood,StormSeed);
}
