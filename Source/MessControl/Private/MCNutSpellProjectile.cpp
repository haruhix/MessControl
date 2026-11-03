#include "MCNutSpellProjectile.h"
#include "MCNutCombatEffect.h"
#include "MCNutBoss.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutCombatVisuals.h"
#include "MCVFXNativeArtControls.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCMageFireballPresentation,Log,All);

namespace
{
// Two sparse shells leave the walnut centre open, then taper into the flight
// wake. All geometry remains bounded by the existing projectile radius.
void RenderFlyingFire(UProceduralMeshComponent* Component,float Radius,float Age,FVector Direction,float Travel,const FMCVFXNativePartSettings* Art=nullptr,float NativeSeconds=1)
{
    using namespace MCNutCombatVisuals;
    const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Up=FVector::CrossProduct(Side,Direction).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    FMesh Data;
    if(!MCNativeArtControls::Enabled(Art)){Component->SetVisibility(false);return;}
    const FMCVFXNativeFlameShape Default;const auto& S=Art?Art->Flame:Default;
    const int32 Layers=S.Layers,Ribbons=S.Ribbons,Rows=S.Rows;
    const int32 Vertices=Layers*Ribbons*(Rows+1)*2;
    Data.Points.Reserve(Vertices);Data.Normals.Reserve(Vertices);Data.UV.Reserve(Vertices);
    Data.Colors.Reserve(Vertices);Data.Tangents.Reserve(Vertices);Data.Indices.Reserve(Layers*Ribbons*Rows*6);
    const float Launch=FMath::SmoothStep(0.f,.14f,Age);
    const float Length=FMath::Clamp(Travel,Radius*S.MinLengthRadii,Radius*S.MaxLengthRadii)*FMath::Lerp(.5f,1.f,Launch);
    for(int32 Layer=0;Layer<Layers;++Layer) for(int32 Ribbon=0;Ribbon<Ribbons;++Ribbon) {
        const float Phase=(Ribbon+.5f*Layer)*2*PI/Ribbons+Age*(Layer==0?.62f:-.38f);
        const FVector Radial=Side*FMath::Cos(Phase)+Up*FMath::Sin(Phase);
        const FVector Across=FVector::CrossProduct(Direction,Radial).GetSafeNormal();
        const int32 First=Data.Points.Num();
        for(int32 Row=0;Row<=Rows;++Row) {
            const float T=Row/float(Rows);
            const float Taper=1-FMath::SmoothStep(.14f,1.f,T);
            const float ShellRadius=MCNativeArtControls::Expansion(Art,Age,NativeSeconds)*Radius*(Layer==0?S.InnerRadiusFraction:S.OuterRadiusFraction)*(.04f+.96f*Taper)
                *(1+.08f*FMath::Sin(T*16-Age*15+Phase));
            const float Width=Radius*(Layer==0?S.InnerWidthFraction:S.OuterWidthFraction)*Taper
                *(1+.16f*FMath::Sin(T*19-Age*20+Phase));
            const FVector Center=Direction*(Radius*.56f-Length*T)+Radial*ShellRadius
                +Up*(Radius*.18f*T*T);
            const float Alpha=Launch*(Layer==0?S.InnerOpacity:S.OuterOpacity)*(1-FMath::SmoothStep(.70f,1.f,T));
            for(int32 Edge=0;Edge<2;++Edge) {
                Data.Points.Add(Center+Across*((Edge*2-1)*Width));
                Data.Normals.Add(Radial);Data.UV.Add(FVector2D(float(Edge),T));
                Data.Colors.Add(FLinearColor(FMath::Frac(Ribbon/float(Ribbons)+Layer*.37f),Layer==0?.82f:.52f,0,
                    MCNativeArtControls::Opacity(Art,Age,NativeSeconds,Alpha)));
                Data.Tangents.Add(FProcMeshTangent(Across,false));
            }
            if(Row>0) {const int32 V=First+(Row-1)*2;Data.Indices.Append({V,V+1,V+3,V,V+3,V+2});}
        }
    }
    Data.Upload(Component);
}

void RenderFireHalo(UProceduralMeshComponent* Component,float Radius,float Age,FVector Direction,FVector Side,FVector Up,const FMCVFXNativePartSettings* Art=nullptr,float NativeSeconds=1)
{
    using namespace MCNutCombatVisuals;
    FMesh Data;
    if(!MCNativeArtControls::Enabled(Art)){Component->SetVisibility(false);return;}
    const FMCVFXNativeHaloShape Default;const auto& S=Art?Art->Halo:Default;
    const int32 Rings=S.Rings,Segments=S.Segments,Vertices=Rings*(Segments+1)*2;
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
                Data.Points.Add(Radial*Radius*(Edge==0?S.InnerRadiusFraction:S.OuterRadiusFraction)*Swell);
                Data.Normals.Add(Normal);
                Data.UV.Add(FVector2D(.5f,.5f)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(Edge==0?.12f:.48f));
                Data.Colors.Add(FLinearColor(1,1,1,MCNativeArtControls::Opacity(Art,Age,NativeSeconds,Launch*(Ring==0?S.FirstOpacity:S.SecondOpacity))));
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
    FireballPresentation=CreateDefaultSubobject<UMCVFXPresentationComponent>(TEXT("MageFireballPresentation"));
    FireballPresentation->SourceID=TEXT("Boss.NutMage.Fireball");
    FireballPresentation->Events={TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")};
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

AMCNutSpellProjectile* AMCNutSpellProjectile::Spawn(AActor* Source,AMCTongue* Surface,FVector Start,FVector Target,float Speed,float Amount,float Radius,
    UMCVFXPresentationComponent* PreparedSource,FGuid CastEpisode)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Speed) || Speed<=0
        || !FMath::IsFinite(Amount) || Amount<=0 || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    const FTransform Pose(Start);
    auto* Shot=Source->GetWorld()->SpawnActorDeferred<AMCNutSpellProjectile>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Shot) return nullptr;
    Shot->SourceActor=Source;Shot->Tongue=Surface;Shot->Damage=FMath::Clamp(Amount,1.f,35.f);
    Shot->Flight.Start=Start;Shot->Flight.Target=Target;Shot->Flight.Radius=FMath::Clamp(Radius,8.f,90.f);
    Shot->Flight.Seconds=FMath::Clamp(FVector::Dist(Start,Target)/FMath::Clamp(Speed,100.f,4000.f),.1f,8.f);
    Shot->EpisodeID=CastEpisode.IsValid()?CastEpisode:FGuid::NewGuid();
    if(const auto* Boss=Cast<AMCNutBoss>(Source))Shot->Flight.Seed=Boss->AttackSeed;
    FString Error;
    if(PreparedSource && !PreparedSource->CopyPreparedSourceTo(Shot->FireballPresentation->SourceID,Shot->FireballPresentation,Error)) {
        UE_LOG(LogMCMageFireballPresentation,Warning,TEXT("Fireball prepared handoff refused; preserving native accents: %s"),*Error);
        // Never substitute a newly changed canonical profile at first launch.
        Shot->FireballPresentation->PrepareSourceProfile(Shot->FireballPresentation->SourceID,Shot->FireballPresentation->Events,nullptr,Error,false);
    }
    if(const auto* Native=Shot->FireballPresentation->GetPreparedNativeSource(TEXT("Boss.NutMage.Fireball"))) {
        Shot->NativeArt=*Native;Shot->Flight.NativeStyle=Native->Reference;
    }
    Shot->Flight.StartedAt=Shot->Now();
    if(auto* Boss=Cast<AMCNutBoss>(Source))Boss->OnFireballSpawned.Broadcast(Shot);
    if(!IsValid(Shot) || Shot->IsActorBeingDestroyed())return nullptr;
    Shot->FinishSpawning(Pose);return Shot;
}

void AMCNutSpellProjectile::BeginPlay()
{
    Super::BeginPlay();if(HasAuthority()) SetLifeSpan(Flight.Seconds+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    FString Error;FireballPresentation->BindSourceEpisodeTickSource(FireballPresentation->SourceID,EpisodeID,this,Error);
    if(HasAuthority()) {
        FMCPerkVFXGameplayValues Values;Values.EpisodeID=EpisodeID;
        Values.Direction=(Flight.Target-Flight.Start).GetSafeNormal();Values.Radius=Flight.Radius;Values.Duration=Flight.Seconds;Values.Fill=0;
        Values.bHasEndpoints=true;Values.SourcePosition=Flight.Start;Values.TargetPosition=Flight.Target;
        Values.bHasSeed=true;Values.Seed=uint32(Flight.Seed);
        const auto Result=FireballPresentation->PlayEvent(TEXT("Flight"),FTransform(Values.Direction.Rotation(),Flight.Start),Values);
        EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        FireballPresentation->GetPreparedEventMode(TEXT("Boss.NutMage.Fireball"),TEXT("Flight"),Mode);
        Flight.bUseNativeAccents=Result==EMCVFXPresentationResult::UseDefault || Result==EMCVFXPresentationResult::Rejected
            || (Result==EMCVFXPresentationResult::Played && NativeArt.ShouldRenderNativeAccents(TEXT("Flight"),Mode));
    }
    if(GetNetMode()==NM_DedicatedServer) return;
    if(!NativeArt.Snapshot && !Flight.NativeStyle.Source.IsNull()) {
        if(!FMCVFXPreparedNativePresentation::ResolveResidentReference(Flight.NativeStyle,TEXT("Boss.NutMage.Fireball"),
            FireballPresentation->Events,this,NativeArt,Error))
            UE_LOG(LogMCMageFireballPresentation,Warning,TEXT("Projectile uses its legacy appearance: %s"),*Error);
    }
    Nut->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
    if(NativePart(TEXT("FlightFlames"))) {FireMID=NativeArt.CreatePrivateMaterial(TEXT("Flight"),TEXT("FlightFlames"),this);Flames->SetMaterial(0,FireMID);}
    else if(auto* Parent=FlameMaterial.LoadSynchronous()) FireMID=Flames->CreateDynamicMaterialInstance(0,Parent);
    if(NativePart(TEXT("FlightCore"))) {CoreMID=NativeArt.CreatePrivateMaterial(TEXT("Flight"),TEXT("FlightCore"),this);Core->SetMaterial(0,CoreMID);}
    else if(auto* Parent=CoreMaterial.LoadSynchronous()) CoreMID=Core->CreateDynamicMaterialInstance(0,Parent);
    if(NativePart(TEXT("FlightWake"))) {WakeMID=NativeArt.CreatePrivateMaterial(TEXT("Flight"),TEXT("FlightWake"),this);Wake->SetMaterial(0,WakeMID);}
    else if(auto* Parent=WakeMaterial.LoadSynchronous()) WakeMID=Wake->CreateDynamicMaterialInstance(0,Parent);
    if(WakeMID) {WakeMID->SetScalarParameterValue(TEXT("Beam"),1);WakeMID->SetVectorParameterValue(TEXT("Tint"),NativePart(TEXT("FlightWake"))?NativePart(TEXT("FlightWake"))->Tint:FLinearColor(1,.40f,.055f));}
    Trail->SetAsset(MCNativeArtControls::System(NativePart(TEXT("EmberTrail")),TrailSystem));
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
        // This spell removes a fixed share of health at contact, independently
        // of max health and ordinary incoming-damage perk multipliers.
        const float ImpactDamage=Hero->Status->State.Health*.8f;
        if(Hero->Status->Damage(ImpactDamage,Direction,SourceActor,EMCRogueDamageKind::Ordinary,false)) {
            ++DamageApplications;
            const FVector Away=Direction.GetSafeNormal2D(SMALL_NUMBER,FVector::ForwardVector);
            if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Away*820+FVector(0,0,260),Hit.ImpactPoint);
        }
    }
    // A real hit on the walkable tongue burns a normal, treatable pain ulcer.
    // Hitting a player/prop or reaching an airborne target never projects one
    // onto the floor. The terminal-hit guard above also prevents duplicates.
    if(Hit.GetActor()==Tongue && Hit.ImpactNormal.Z>=.35f) {
        TArray<AMCMouthSurface*> Existing;
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->bUlcer) Existing.Add(*It);
        if(auto* Ulcer=AMCMouthSurface::SpawnDamageUlcer(GetWorld(),Hit.ImpactPoint);Ulcer && !Existing.Contains(Ulcer)) {
            Ulcer->Tags.AddUnique(TEXT("MC_NutFireUlcer"));
            if(auto* Event=Cast<AMCNutRainEvent>(GetOwner())) {
                Ulcer->SetOwner(Event);Event->TrackEncounterActor(Ulcer);
            } else Ulcer->SetOwner(SourceActor);
        }
    }
    FireballPresentation->StopSourceEpisode(FireballPresentation->SourceID,EpisodeID,false);
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=EpisodeID;
    Values.Direction=Impact.Normal;Values.Radius=Flight.Radius*2.6f;Values.Duration=.65f;
    Values.bHasEndpoints=true;Values.SourcePosition=Impact.Point+Impact.Normal*Flight.Radius;Values.TargetPosition=Impact.Point;
    Values.bHasSeed=true;Values.Seed=uint32(Flight.Seed);
    const auto Result=FireballPresentation->PlayEvent(TEXT("Impact"),FTransform(Impact.Normal.Rotation(),Impact.Point),Values);
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    FireballPresentation->GetPreparedEventMode(TEXT("Boss.NutMage.Fireball"),TEXT("Impact"),Mode);
    const bool Suppress=Result==EMCVFXPresentationResult::Suppressed || (Result==EMCVFXPresentationResult::Played
        && !NativeArt.ShouldRenderNativeAccents(TEXT("Impact"),Mode));
    if(auto* Effect=AMCNutCombatEffect::Spawn(SourceActor,Tongue,EMCNutCombatCue::FireImpact,
        Impact.Point+Impact.Normal*Flight.Radius,Impact.Point,Flight.Radius*2.6f,0,.65f,0,90,8,.5f,Suppress,EpisodeID,
        NativeArt.Snapshot?&NativeArt:nullptr,TEXT("Impact"))) {
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
            || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) {Cancel();return;}
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
    const auto* FlameArt=NativePart(TEXT("FlightFlames"));
    const auto* CoreArt=NativePart(TEXT("FlightCore"));
    const auto* WakeArt=NativePart(TEXT("FlightWake"));
    const float Extent=Nut->GetStaticMesh()?Nut->GetStaticMesh()->GetBounds().BoxExtent.GetMax():1.f;
    Nut->SetRelativeScale3D(FVector(Flight.Radius*.85f/FMath::Max(.1f,Extent)));
    Nut->SetRelativeRotation(FRotator(Age*160,Age*100,Age*65));
    const FVector Direction=(Flight.Target-Flight.Start).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    // Native trajectory is reconstructed on every rendering peer. Only local
    // presentation players move here; there is no per-tick cosmetic RPC.
    FString FrameError;FireballPresentation->BindSourceEpisodeTickSource(FireballPresentation->SourceID,EpisodeID,this,FrameError);
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=EpisodeID;Values.Direction=Direction;Values.Radius=Flight.Radius;
    Values.Duration=Flight.Seconds;Values.Fill=FMath::Clamp(Age/FMath::Max(.1f,Flight.Seconds),0.f,1.f);
    Values.bHasEndpoints=true;Values.SourcePosition=GetActorLocation();Values.TargetPosition=Flight.Target;
    Values.bHasSeed=true;Values.Seed=uint32(Flight.Seed);
    FireballPresentation->UpdateSourceEpisode(FireballPresentation->SourceID,EpisodeID,FTransform(Direction.Rotation(),GetActorLocation()),Values);
    if(!Flight.bUseNativeAccents) {
        Flames->SetVisibility(false);Core->SetVisibility(false);Wake->SetVisibility(false);Trail->Deactivate();return;
    }
    const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Up=FVector::CrossProduct(Side,Direction).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const float History=FMath::Min(Age,.42f);
    const float Travel=FVector::Dist(PositionAt(Age),PositionAt(Age-History));
    Flames->SetVisibility(FireMID!=nullptr && MCNativeArtControls::Enabled(FlameArt));
    if(FireMID && MCNativeArtControls::Enabled(FlameArt)) {
        FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        RenderFlyingFire(Flames,Flight.Radius,Age,Direction,Travel,FlameArt,Flight.Seconds);
    }
    Core->SetVisibility(CoreMID && MCNativeArtControls::Enabled(CoreArt));
    if(CoreMID && MCNativeArtControls::Enabled(CoreArt)) {
        RenderFireHalo(Core,Flight.Radius,Age,Direction,Side,Up,CoreArt,Flight.Seconds);
        CoreMID->SetScalarParameterValue(TEXT("Age"),Age);
    }
    Wake->SetVisibility(WakeMID && MCNativeArtControls::Enabled(WakeArt));
    if(WakeMID && MCNativeArtControls::Enabled(WakeArt)) {
        MCNutCombatVisuals::FMesh Ribbon;
        const float WakeHistory=FMath::Min(Age,WakeArt?WakeArt->Wake.HistorySeconds:.42f);
        const int32 Segments=WakeArt?WakeArt->Wake.Segments:12;
        for(int32 Index=0;Index<Segments;++Index) {
            const float Fraction=Index/float(Segments);
            const FVector From=PositionAt(Age-WakeHistory*Fraction)-GetActorLocation();
            const FVector To=PositionAt(Age-WakeHistory*(Index+1)/float(Segments))-GetActorLocation();
            const float Length=FVector::Dist(From,To);
            if(Length<.05f) continue;
            const float Width=Flight.Radius*(WakeArt?WakeArt->Wake.WidthFraction:.26f)*(1-Fraction);
            const FLinearColor Alpha(1,1,1,MCNativeArtControls::Opacity(WakeArt,Age,Flight.Seconds,
                (WakeArt?WakeArt->Wake.Opacity:.34f)*(1-Fraction)*FMath::SmoothStep(0.f,.14f,Age)));
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

void AMCNutSpellProjectile::Cancel() {if(HasAuthority()) {FireballPresentation->StopPresentation(true);Destroy();}}
void AMCNutSpellProjectile::EndPlay(const EEndPlayReason::Type Reason)
{Trail->DeactivateImmediate();Super::EndPlay(Reason);}
void AMCNutSpellProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCNutSpellProjectile,Flight);DOREPLIFETIME(AMCNutSpellProjectile,Impact);
    DOREPLIFETIME(AMCNutSpellProjectile,Tongue);DOREPLIFETIME(AMCNutSpellProjectile,SourceActor);
    DOREPLIFETIME(AMCNutSpellProjectile,EpisodeID);
}
