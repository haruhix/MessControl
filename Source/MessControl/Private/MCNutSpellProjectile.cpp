#include "MCNutSpellProjectile.h"
#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
// Two sparse shells leave the walnut centre open, then taper into the flight
// wake. All geometry remains bounded by the existing projectile radius.
void RenderFlyingFire(UProceduralMeshComponent* Component,float Radius,float Age,FVector Direction,float Travel)
{
    using namespace MCNutCombatVisuals;
    const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Up=FVector::CrossProduct(Side,Direction).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    FMesh Data;
    constexpr int32 Layers=2,Ribbons=5,Rows=18;
    constexpr int32 Vertices=Layers*Ribbons*(Rows+1)*2;
    Data.Points.Reserve(Vertices);Data.Normals.Reserve(Vertices);Data.UV.Reserve(Vertices);
    Data.Colors.Reserve(Vertices);Data.Tangents.Reserve(Vertices);Data.Indices.Reserve(Layers*Ribbons*Rows*6);
    const float Launch=FMath::SmoothStep(0.f,.14f,Age);
    const float Length=FMath::Clamp(Travel,Radius*2.8f,Radius*8.f)*FMath::Lerp(.5f,1.f,Launch);
    for(int32 Layer=0;Layer<Layers;++Layer) for(int32 Ribbon=0;Ribbon<Ribbons;++Ribbon) {
        const float Phase=(Ribbon+.5f*Layer)*2*PI/Ribbons+Age*(Layer==0?.62f:-.38f);
        const FVector Radial=Side*FMath::Cos(Phase)+Up*FMath::Sin(Phase);
        const FVector Across=FVector::CrossProduct(Direction,Radial).GetSafeNormal();
        const int32 First=Data.Points.Num();
        for(int32 Row=0;Row<=Rows;++Row) {
            const float T=Row/float(Rows);
            const float Taper=1-FMath::SmoothStep(.14f,1.f,T);
            const float ShellRadius=Radius*(Layer==0?.96f:1.20f)*(.04f+.96f*Taper)
                *(1+.08f*FMath::Sin(T*16-Age*15+Phase));
            const float Width=Radius*(Layer==0?.34f:.29f)*Taper
                *(1+.16f*FMath::Sin(T*19-Age*20+Phase));
            const FVector Center=Direction*(Radius*.56f-Length*T)+Radial*ShellRadius
                +Up*(Radius*.18f*T*T);
            const float Alpha=Launch*(Layer==0?.52f:.29f)*(1-FMath::SmoothStep(.70f,1.f,T));
            for(int32 Edge=0;Edge<2;++Edge) {
                Data.Points.Add(Center+Across*((Edge*2-1)*Width));
                Data.Normals.Add(Radial);Data.UV.Add(FVector2D(float(Edge),T));
                Data.Colors.Add(FLinearColor(FMath::Frac(Ribbon/float(Ribbons)+Layer*.37f),Layer==0?.82f:.52f,0,Alpha));
                Data.Tangents.Add(FProcMeshTangent(Across,false));
            }
            if(Row>0) {const int32 V=First+(Row-1)*2;Data.Indices.Append({V,V+1,V+3,V,V+3,V+2});}
        }
    }
    Data.Upload(Component);
}

void RenderFireHalo(UProceduralMeshComponent* Component,float Radius,float Age,FVector Direction,FVector Side,FVector Up)
{
    using namespace MCNutCombatVisuals;
    FMesh Data;
    constexpr int32 Rings=2,Segments=24,Vertices=Rings*(Segments+1)*2;
    Data.Points.Reserve(Vertices);Data.Normals.Reserve(Vertices);Data.UV.Reserve(Vertices);
    Data.Colors.Reserve(Vertices);Data.Tangents.Reserve(Vertices);Data.Indices.Reserve(Rings*Segments*6);
    const float Launch=FMath::SmoothStep(0.f,.10f,Age);
    const float Swell=1+.025f*FMath::Sin(Age*22);
    for(int32 Ring=0;Ring<Rings;++Ring) {
        const FVector Right=Ring==0?Side:Direction;
        const FVector Normal=FVector::CrossProduct(Right,Up).GetSafeNormal();
        const int32 First=Data.Points.Num();
        for(int32 Segment=0;Segment<=Segments;++Segment) {
            const float Angle=Segment*2*PI/Segments;
            const FVector Radial=Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
            for(int32 Edge=0;Edge<2;++Edge) {
                // The halo has no centre faces; the hot material cannot cover
                // the recognisable walnut with three white crossed quads.
                Data.Points.Add(Radial*Radius*(Edge==0?.90f:1.18f)*Swell);
                Data.Normals.Add(Normal);
                Data.UV.Add(FVector2D(.5f,.5f)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(Edge==0?.12f:.48f));
                Data.Colors.Add(FLinearColor(1,1,1,Launch*(Ring==0?.22f:.13f)));
                Data.Tangents.Add(FProcMeshTangent(Right,false));
            }
            if(Segment>0) {const int32 V=First+(Segment-1)*2;Data.Indices.Append({V,V+1,V+3,V,V+3,V+2});}
        }
    }
    Data.Upload(Component);
}
}

AMCNutSpellProjectile::AMCNutSpellProjectile()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(false);SetNetUpdateFrequency(20);
    PrimaryActorTick.bCanEverTick=true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));SetRootComponent(Root);
    Nut=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BurningWalnut"));Nut->SetupAttachment(Root);
    Flames=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FireRibbons"));Flames->SetupAttachment(Root);
    Core=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HotGoldenCore"));Core->SetupAttachment(Root);
    Wake=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DirectionalFireWake"));Wake->SetupAttachment(Root);
    Trail=CreateDefaultSubobject<UNiagaraComponent>(TEXT("EmberTrail"));Trail->SetupAttachment(Root);Trail->SetAutoActivate(false);
    MCNutCombatVisuals::Configure(Nut);MCNutCombatVisuals::Configure(Flames);MCNutCombatVisuals::Configure(Trail);
    MCNutCombatVisuals::Configure(Core);MCNutCombatVisuals::Configure(Wake);
    Core->SetTranslucentSortPriority(5);Wake->SetTranslucentSortPriority(4);
    CoreMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutFireCore.M_NutFireCore"));
    WakeMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutArcane.M_NutArcane"));
    FlameMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"));
    TrailSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutEmberTrail.NS_NutEmberTrail"));
}

double AMCNutSpellProjectile::Now() const
{const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}

FVector AMCNutSpellProjectile::PositionAt(float Age) const
{return FMath::Lerp(Flight.Start,Flight.Target,FMath::Clamp(Age/FMath::Max(.1f,Flight.Seconds),0.f,1.f));}

AMCNutSpellProjectile* AMCNutSpellProjectile::Spawn(AActor* Source,AMCTongue* Surface,FVector Start,FVector Target,float Speed,float Amount,float Radius)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Speed) || Speed<=0
        || !FMath::IsFinite(Amount) || Amount<=0 || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    const FTransform Pose(Start);
    auto* Shot=Source->GetWorld()->SpawnActorDeferred<AMCNutSpellProjectile>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Shot) return nullptr;
    Shot->SourceActor=Source;Shot->Tongue=Surface;Shot->Damage=FMath::Clamp(Amount,1.f,35.f);
    Shot->Flight.Start=Start;Shot->Flight.Target=Target;Shot->Flight.Radius=FMath::Clamp(Radius,8.f,90.f);
    Shot->Flight.Seconds=FMath::Clamp(FVector::Dist(Start,Target)/FMath::Clamp(Speed,100.f,1800.f),.1f,8.f);
    Shot->Flight.StartedAt=Shot->Now();Shot->FinishSpawning(Pose);return Shot;
}

void AMCNutSpellProjectile::BeginPlay()
{
    Super::BeginPlay();if(HasAuthority()) SetLifeSpan(Flight.Seconds+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    if(GetNetMode()==NM_DedicatedServer) return;
    Nut->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
    if(auto* Parent=FlameMaterial.LoadSynchronous()) FireMID=Flames->CreateDynamicMaterialInstance(0,Parent);
    if(auto* Parent=CoreMaterial.LoadSynchronous()) CoreMID=Core->CreateDynamicMaterialInstance(0,Parent);
    if(auto* Parent=WakeMaterial.LoadSynchronous()) WakeMID=Wake->CreateDynamicMaterialInstance(0,Parent);
    if(WakeMID) {WakeMID->SetScalarParameterValue(TEXT("Beam"),1);WakeMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.40f,.055f));}
    Trail->SetAsset(TrailSystem.LoadSynchronous());
    if(Impact.bImpacted) PresentImpact();
}

void AMCNutSpellProjectile::ResolveImpact(const FHitResult& Hit)
{
    if(!HasAuthority() || Impact.bImpacted) return;
    Impact.bImpacted=true;Impact.Point=Hit.ImpactPoint;Impact.Normal=Hit.ImpactNormal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);Impact.At=Now();
    if(auto* Food=Cast<AMCFoodActor>(Hit.GetActor()); Food && Food->IsNutKineticsActive()) {
        const FVector Direction=(Flight.Target-Flight.Start).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        const float Speed=FMath::Min(900.f,float(FVector::Dist(Flight.Start,Flight.Target))/FMath::Max(.1f,Flight.Seconds)*.8f);
        Food->ApplyNutImpulse(Direction*Speed+FVector(0,0,90),Hit.ImpactPoint);
    }
    if(auto* Hero=Cast<AMCToothCharacter>(Hit.GetActor());Hero && Hero->Status && Hero->Status->IsAlive()) {
        const FVector Direction=(Flight.Target-Flight.Start).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        if(Hero->Status->Damage(Damage,Direction,SourceActor)) {
            ++DamageApplications;
            if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Direction*180+FVector(0,0,65),Hit.ImpactPoint);
        }
    }
    if(auto* Effect=AMCNutCombatEffect::Spawn(SourceActor,Tongue,EMCNutCombatCue::FireImpact,
        Impact.Point+Impact.Normal*Flight.Radius,Impact.Point,Flight.Radius*2.6f,0,.65f)) {
        // Keep the transient terminal burst in the encounter cleanup set as well as the projectile.
        if(auto* Event=Cast<AMCNutRainEvent>(GetOwner())) {Effect->SetOwner(Event);Event->TrackEncounterActor(Effect);}
    }
    PresentImpact();ForceNetUpdate();SetLifeSpan(.7f);
}

void AMCNutSpellProjectile::PresentImpact()
{
    if(!Impact.bImpacted) return;
    Nut->SetVisibility(false);Flames->SetVisibility(false);Core->SetVisibility(false);Wake->SetVisibility(false);Trail->Deactivate();SetActorLocation(Impact.Point);
    bImpactPresented=true;
}

void AMCNutSpellProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority()) {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(SourceActor) || SourceActor->IsActorBeingDestroyed() || !IsValid(Tongue)
            || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) {Destroy();return;}
    }
    if(Impact.bImpacted) {if(!bImpactPresented) PresentImpact();return;}
    if(Flight.StartedAt<0) return;
    const float Age=FMath::Clamp(float(Now()-Flight.StartedAt),0.f,Flight.Seconds);
    if(HasAuthority()) {
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_Pawn);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCNutFireball),false,this);Params.AddIgnoredActor(SourceActor);Params.AddIgnoredActor(GetOwner());
        FHitResult Hit;
        // The entire distance since the previous update is swept; low frame rates cannot tunnel through a tooth.
        bool bHit=GetWorld()->SweepSingleByObjectType(Hit,PositionAt(PreviousAge),PositionAt(Age),FQuat::Identity,Objects,FCollisionShape::MakeSphere(Flight.Radius),Params);
        // Whole encounter nuts are PhysicsBody objects. Query them separately so
        // unrelated food, fragments and ragdoll bodies keep their existing rules.
        TArray<FHitResult> NutHits;
        GetWorld()->SweepMultiByObjectType(NutHits,PositionAt(PreviousAge),PositionAt(Age),FQuat::Identity,
            FCollisionObjectQueryParams(ECC_PhysicsBody),FCollisionShape::MakeSphere(Flight.Radius),Params);
        for(const FHitResult& Candidate:NutHits)
            if(const auto* Food=Cast<AMCFoodActor>(Candidate.GetActor());Food && Food->IsNutKineticsActive()
                && (!bHit || Candidate.Time<Hit.Time)) {Hit=Candidate;bHit=true;}
        if(bHit) {ResolveImpact(Hit);return;}
        if(Age>=Flight.Seconds) {Hit.ImpactPoint=Flight.Target;Hit.ImpactNormal=FVector::UpVector;ResolveImpact(Hit);return;}
        PreviousAge=Age;
    }
    SetActorLocation(PositionAt(Age));
    if(GetNetMode()==NM_DedicatedServer) return;
    const float Extent=Nut->GetStaticMesh()?Nut->GetStaticMesh()->GetBounds().BoxExtent.GetMax():1.f;
    Nut->SetRelativeScale3D(FVector(Flight.Radius*.85f/FMath::Max(.1f,Extent)));
    Nut->SetRelativeRotation(FRotator(Age*160,Age*100,Age*65));
    const FVector Direction=(Flight.Target-Flight.Start).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Up=FVector::CrossProduct(Side,Direction).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const float History=FMath::Min(Age,.42f);
    const float Travel=FVector::Dist(PositionAt(Age),PositionAt(Age-History));
    Flames->SetVisibility(FireMID!=nullptr);
    if(FireMID) {
        FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        RenderFlyingFire(Flames,Flight.Radius,Age,Direction,Travel);
    }
    if(CoreMID) {
        RenderFireHalo(Core,Flight.Radius,Age,Direction,Side,Up);
        CoreMID->SetScalarParameterValue(TEXT("Age"),Age);
    }
    if(WakeMID) {
        MCNutCombatVisuals::FMesh Ribbon;
        constexpr int32 Segments=12;
        for(int32 Index=0;Index<Segments;++Index) {
            const float Fraction=Index/float(Segments);
            const FVector From=PositionAt(Age-History*Fraction)-GetActorLocation();
            const FVector To=PositionAt(Age-History*(Index+1)/float(Segments))-GetActorLocation();
            const float Length=FVector::Dist(From,To);
            if(Length<.05f) continue;
            const float Width=Flight.Radius*.26f*(1-Fraction);
            const FLinearColor Alpha(1,1,1,.34f*(1-Fraction)*FMath::SmoothStep(0.f,.14f,Age));
            Ribbon.Quad((From+To)*.5f,Direction,Side,Length*.52f,Width,Alpha);
            Ribbon.Quad((From+To)*.5f,Direction,Up,Length*.52f,Width*.8f,Alpha);
        }
        if(!Ribbon.Points.IsEmpty()) Ribbon.Upload(Wake);
        Wake->SetVisibility(!Ribbon.Points.IsEmpty());WakeMID->SetScalarParameterValue(TEXT("Age"),Age);
    }
    Trail->SetWorldRotation((-Direction).Rotation());
    Trail->SetWorldScale3D(FVector(FMath::Clamp(Flight.Radius/28.f,1.f,2.5f)));
    if(!bTrailStarted && Trail->GetAsset()) {bTrailStarted=true;Trail->Activate(true);}
}

void AMCNutSpellProjectile::Cancel() {if(HasAuthority()) Destroy();}
void AMCNutSpellProjectile::EndPlay(const EEndPlayReason::Type Reason)
{Trail->DeactivateImmediate();Super::EndPlay(Reason);}
void AMCNutSpellProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCNutSpellProjectile,Flight);DOREPLIFETIME(AMCNutSpellProjectile,Impact);
    DOREPLIFETIME(AMCNutSpellProjectile,Tongue);DOREPLIFETIME(AMCNutSpellProjectile,SourceActor);
}
