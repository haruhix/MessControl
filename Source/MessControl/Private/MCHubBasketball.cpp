#include "MCHubBasketball.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFoodCollectionComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AMCHubBasketball::AMCHubBasketball()
{
    bReplicates=true; SetReplicateMovement(true);
    SetNetUpdateFrequency(60.f); SetMinNetUpdateFrequency(15.f);
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    Body=CreateDefaultSubobject<USphereComponent>(TEXT("BallPhysics")); SetRootComponent(Body);
    Body->InitSphereRadius(Radius);
    Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    // Character movement applies very large push forces to light physics props.
    Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body->SetGenerateOverlapEvents(false);
    Body->SetLinearDamping(.025f); Body->SetAngularDamping(.18f);
    Body->BodyInstance.bUseCCD=true;
    Body->BodyInstance.PositionSolverIterationCount=12;
    Body->BodyInstance.VelocitySolverIterationCount=8;
    Body->SetSimulatePhysics(true);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallVisual")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(Sphere.Succeeded()) Visual->SetStaticMesh(Sphere.Object);
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Confetti(TEXT("/Game/Hub/VFX/NS_BasketConfetti.NS_BasketConfetti"));
    if(Confetti.Succeeded()) BasketConfetti=Confetti.Object;
}

void AMCHubBasketball::BeginPlay()
{
    Super::BeginPlay(); Home=GetActorLocation();
    Body->SetSphereRadius(FMath::Clamp(Radius,10.f,80.f));
    Visual->SetRelativeScale3D(FVector(Body->GetUnscaledSphereRadius()/50.f));
    Rubber=NewObject<UPhysicalMaterial>(this);
    Rubber->Restitution=.5f;
    Rubber->Friction=.65f; Rubber->StaticFriction=.7f;
    Rubber->bOverrideRestitutionCombineMode=true;
    Rubber->RestitutionCombineMode=EFrictionCombineMode::Max;
    Body->SetPhysMaterialOverride(Rubber);
    Body->SetMassOverrideInKg(NAME_None,.62f,true);
    OnRep_Holder();
}

AMCHubBasketball* AMCHubBasketball::HeldBy(const AMCToothCharacter* Hero)
{
    if(!IsValid(Hero)) return nullptr;
    for(TActorIterator<AMCHubBasketball> It(Hero->GetWorld());It;++It)
        if(It->Holder==Hero) return *It;
    return nullptr;
}

AMCHubBasketball* AMCHubBasketball::PickupCandidate(const AMCToothCharacter* Hero)
{
    if(!IsValid(Hero) || !Hero->CanWork()) return nullptr;
    AMCHubBasketball* Best=nullptr;
    double Nearest=TNumericLimits<double>::Max();
    for(TActorIterator<AMCHubBasketball> It(Hero->GetWorld());It;++It) {
        if(IsValid(It->Holder)) continue;
        const double Distance=FVector::DistSquared(It->GetActorLocation(),Hero->GetActorLocation());
        if(Distance>FMath::Square(It->PickupReach) || Distance>=Nearest) continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(HubBallPickup),false,Hero); Params.AddIgnoredActor(*It);
        FHitResult Hit;
        if(Hero->GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Params)) continue;
        Nearest=Distance;Best=*It;
    }
    return Best;
}

bool AMCHubBasketball::TryPickup(AMCToothCharacter* Hero)
{
    if(!HasAuthority() || !IsValid(Hero) || HeldBy(Hero) || PickupCandidate(Hero)!=this) return false;
    Hero->CancelGameplayInput(); Hero->DropFood();
    Holder=Hero; SetOwner(Hero); OnRep_Holder(); ForceNetUpdate(); return true;
}

void AMCHubBasketball::OnRep_Holder()
{
    bHasPreviousFreePosition=false; bWasAboveRim=false; bPassingThroughRim=false;
    if(IsValid(Holder)) {
        Body->SetSimulatePhysics(false);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        AttachToComponent(Holder->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
        AddTickPrerequisiteActor(Holder);
        UpdateHeldPosition();
    } else {
        bAimHeld=false;
        if(AActor* Parent=GetAttachParentActor()) RemoveTickPrerequisiteActor(Parent);
        DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Body->SetSimulatePhysics(true);
        Body->WakeAllRigidBodies();
    }
}

void AMCHubBasketball::UpdateHeldPosition()
{
    if(!IsValid(Holder)) return;
    const FVector Start=Holder->GetActorLocation()+FVector(0,0,25);
    // The same hand target as food carrying; the sphere rests between the palms.
    const FVector Desired=Holder->FoodCollection->HandPoint()+Holder->GetActorForwardVector()*(Radius*.75f)
        +FVector(0,0,Radius*.3f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HubBallCarry),false,Holder); Params.AddIgnoredActor(this);
    FHitResult Hit;
    const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Start,Desired,FQuat::Identity,ECC_PhysicsBody,
        FCollisionShape::MakeSphere(Body->GetScaledSphereRadius()),Params);
    SetActorLocation(Blocked?Hit.Location:Desired,false,nullptr,ETeleportType::TeleportPhysics);
}

void AMCHubBasketball::Release(FVector Velocity)
{
    if(!HasAuthority() || !IsValid(Holder)) return;
    if(Velocity.ContainsNaN()) Velocity=FVector::ZeroVector;
    RemoveTickPrerequisiteActor(Holder);
    bAimHeld=false;Holder=nullptr; SetOwner(nullptr); OnRep_Holder();
    Body->SetPhysicsLinearVelocity(Velocity.GetClampedToMaxSize(ThrowSpeed+600.f));
    // Backspin is transferred through friction when the ball meets the court/rim.
    Body->SetPhysicsAngularVelocityInRadians(FVector::CrossProduct(Velocity.GetSafeNormal(),FVector::UpVector)*6.f);
    ForceNetUpdate();
}

void AMCHubBasketball::SetAimHeld(bool Held)
{
    bAimHeld=Held && IsValid(Holder) && Holder->IsLocallyControlled() && Holder->CanWork();
}

bool AMCHubBasketball::IsAiming() const
{
    return bAimHeld && IsValid(Holder);
}

FVector AMCHubBasketball::LaunchVelocity() const
{
    const auto* Hero=Holder.Get();
    if(!IsValid(Hero)) return FVector::ZeroVector;
    FVector Origin=Hero->GetActorLocation(); FRotator View=Hero->GetBaseAimRotation();
    if(const auto* PC=Cast<APlayerController>(Hero->GetController());PC && PC->IsLocalController())
        PC->GetPlayerViewPoint(Origin,View);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HubBallAim),true,Hero);Params.AddIgnoredActor(this);
    FHitResult Hit;
    const FVector End=Origin+View.Vector()*6000.f;
    const bool HitSurface=GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Params);
    FVector Target=HitSurface?Hit.ImpactPoint:End;
    bool AimAtRim=false;
    if(IsValid(Hoop)) {
        const FVector Center=Hoop->GetActorTransform().TransformPosition(LocalRimCenter);
        const FVector Normal=Hoop->GetActorUpVector();
        const double Denominator=FVector::DotProduct(View.Vector(),Normal);
        if(FMath::Abs(Denominator)>.001) {
            const double Distance=FVector::DotProduct(Center-Origin,Normal)/Denominator;
            const FVector OnPlane=Origin+View.Vector()*Distance;
            if(Distance>0 && Distance<6000 && FVector::DistSquared(OnPlane,Center)<FMath::Square(RimAimRadius)
                && (!HitSurface || Hit.Distance>=Distance-15.f)) {
                // Aim through the visible opening instead of at scenery behind it.
                // Keep the player's exact aim; a shot near the edge can hit the rim.
                Target=OnPlane;AimAtRim=true;
            }
        }
    }
    if(AimAtRim) {
        // A full-power high arc on a short shot rises far above the hoop into
        // the scenery. Keep a descending shot with a useful, bounded apex.
        const FVector Delta=Target-GetActorLocation();
        const double Gravity=FMath::Max(1.,-double(GetWorld()->GetGravityZ()));
        // The authored sphere is large relative to the opening. A shallow
        // descending centerline still clips the front lip with its lower edge.
        const double FlightTime=FMath::Max(Delta.Size2D()/(ThrowSpeed*.72),
            FMath::Sqrt(2.*FMath::Max(1.,Delta.Z+Delta.Size2D()*1.4)/Gravity));
        const double Drag=FMath::Max(.0001,double(Body->GetLinearDamping()));
        const double Travel=(1.-FMath::Exp(-Drag*FlightTime))/Drag;
        const FVector Velocity=Delta/Travel+FVector(0,0,Gravity*(FlightTime-Travel)/(Drag*Travel));
        if(Velocity.SizeSquared()<=FMath::Square(ThrowSpeed)) return Velocity;
    }
    UGameplayStatics::FSuggestProjectileVelocityParameters Arc(this,GetActorLocation(),Target,ThrowSpeed);
    Arc.OverrideGravityZ=GetWorld()->GetGravityZ(); Arc.TraceOption=ESuggestProjVelocityTraceOption::DoNotTrace;
    FVector Velocity;
    if(FVector::DotProduct(Target-GetActorLocation(),View.Vector())>25.f && UGameplayStatics::SuggestProjectileVelocity(Arc,Velocity))
        return Velocity;
    return (View.Vector()*ThrowSpeed+FVector(0,0,400.f)).GetSafeNormal()*ThrowSpeed;
}

void AMCHubBasketball::ThrowFromView()
{
    if(IsValid(Holder) && Holder->CanWork() && (HasAuthority() || Holder->IsLocallyControlled()))
        ServerThrow(LaunchVelocity());
}

void AMCHubBasketball::ServerThrow_Implementation(FVector_NetQuantize Velocity)
{
    if(!IsValid(Holder) || !Holder->CanWork() || Velocity.ContainsNaN() || Velocity.IsNearlyZero()
        || Velocity.SizeSquared()>FMath::Square(ThrowSpeed+2.f)) return;
    Release(FVector(Velocity).GetClampedToMaxSize(ThrowSpeed));
}

TArray<FVector> AMCHubBasketball::PredictThrowTrajectory() const
{
    TArray<FVector> Points;
    if(!IsValid(Holder)) return Points;
    FVector Position=GetActorLocation(),Velocity=LaunchVelocity();
    Points.Reserve(136);Points.Add(Position);
    constexpr float Step=1.f/120.f;
    const FVector Acceleration(0,0,GetWorld()->GetGravityZ());
    const float Damping=FMath::Max(0.f,1.f-Body->GetLinearDamping()*Step);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HubBallTrajectory),false,Holder);Params.AddIgnoredActor(this);
    const FCollisionResponseParams Responses(Body->GetCollisionResponseToChannels());
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Body->GetScaledSphereRadius());
    for(int32 I=1;I<=540;++I) {
        Velocity=(Velocity+Acceleration*Step)*Damping;
        const FVector Next=Position+Velocity*Step;
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Position,Next,FQuat::Identity,ECC_PhysicsBody,Shape,Params,Responses)) {
            Points.Add(Hit.Location);break;
        }
        Position=Next;
        if(I%4==0) Points.Add(Position);
    }
    return Points;
}

void AMCHubBasketball::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(IsValid(Holder)) {
        if(HasAuthority() && (!Holder->Status->IsAlive() || !Holder->GetController())) Release(FVector::ZeroVector);
        else UpdateHeldPosition();
    } else if(HasAuthority() && !Body->IsSimulatingPhysics()) {
        Holder=nullptr;SetOwner(nullptr);OnRep_Holder();ForceNetUpdate();
    } else if(HasAuthority() && (GetActorLocation().Z<Home.Z-1500.f || FVector::DistSquared(GetActorLocation(),Home)>FMath::Square(9000.f))) {
        SetActorLocation(Home,false,nullptr,ETeleportType::TeleportPhysics);
        Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        bHasPreviousFreePosition=false; bWasAboveRim=false; bPassingThroughRim=false;
        ForceNetUpdate();
    }
    CheckBasket();
}

void AMCHubBasketball::CheckBasket()
{
    if(!HasAuthority()) return;
    if(IsValid(Holder) || !IsValid(Hoop) || !Body->IsSimulatingPhysics()) {
        bHasPreviousFreePosition=false; bWasAboveRim=false; bPassingThroughRim=false; return;
    }
    const FVector Position=GetActorLocation();
    const FVector Center=Hoop->GetActorTransform().TransformPosition(LocalRimCenter);
    const FVector Up=Hoop->GetActorUpVector();
    const double Height=FVector::DotProduct(Position-Center,Up);
    const float BallRadius=Body->GetScaledSphereRadius();
    if(bHasPreviousFreePosition) {
        const double PreviousHeight=FVector::DotProduct(PreviousFreePosition-Center,Up);
        const double DownSpeed=-FVector::DotProduct(Body->GetPhysicsLinearVelocity(),Up);
        // Sweep the center across the opening so a fast descending shot cannot skip the event.
        if(bWasAboveRim && PreviousHeight>0. && Height<=0. && DownSpeed>20.) {
            const FVector Crossing=FMath::Lerp(PreviousFreePosition,Position,PreviousHeight/(PreviousHeight-Height));
            const float Clearance=FMath::Max(1.f,RimAimRadius-BallRadius);
            bPassingThroughRim=FVector::DistSquared(Crossing,Center)<=FMath::Square(Clearance);
            bWasAboveRim=false;
        }
        if(bPassingThroughRim) {
            const FVector Radial=Position-Center-Up*Height;
            if(DownSpeed<=0. || Radial.SizeSquared()>FMath::Square(RimAimRadius+BallRadius)) bPassingThroughRim=false;
            else if(Height<-BallRadius && GetWorld()->GetTimeSeconds()-LastBasketTime>.75) {
                bPassingThroughRim=false; LastBasketTime=GetWorld()->GetTimeSeconds(); ++BasketsMade;
                CelebrateBasket(Center+Up*35.f,Up);
            }
        }
    }
    if(Height>BallRadius) bWasAboveRim=true;
    PreviousFreePosition=Position; bHasPreviousFreePosition=true;
}

void AMCHubBasketball::CelebrateBasket_Implementation(FVector_NetQuantize Location, FVector_NetQuantizeNormal Up)
{
    if(GetNetMode()==NM_DedicatedServer || !BasketConfetti) return;
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,BasketConfetti,Location,
        FRotationMatrix::MakeFromZ(Up).Rotator(),FVector::OneVector,true,true,ENCPoolMethod::AutoRelease);
}

void AMCHubBasketball::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCHubBasketball,Holder);
}
