#include "MCDefenseSprayPassives.h"

#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
#include "MCInventoryComponent.h"
#include "MCResultPassives.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

namespace
{
const FName Enamel(TEXT("B2_ThickEnamel")),Shield(TEXT("B2_Shield")),SecondWind(TEXT("B2_SecondWind"));
const FName Houdini(TEXT("B2_HoudiniDash")),Softener(TEXT("B2_Softener")),Antiseptic(TEXT("B2_Antiseptic")),Lasting(TEXT("B2_LastingFormula"));
double Time(const UWorld* World) { return World?World->GetTimeSeconds():0.; }
bool Has(const AMCToothCharacter* Hero,FName ID)
{const auto* Perks=UMCPerkComponent::ForActor(Hero);return Perks && Perks->GetStacks(ID)>0;}
float Amount(const AMCToothCharacter* Hero,FName ID,FName Key,float Fallback,float Low,float High)
{
    const auto* Perks=UMCPerkComponent::ForActor(Hero);
    const float Value=Perks?Perks->PerkParameter(ID,Key,Fallback):Fallback;
    return FMath::Clamp(FMath::IsFinite(Value)?Value:Fallback,Low,High);
}
bool LiveWorker(const AMCToothCharacter* Hero)
{
    if(!IsValid(Hero) || Hero->IsActorBeingDestroyed() || !Hero->Status || !Hero->Status->IsAlive()) return false;
    const auto* State=Hero->GetPlayerState<AMCPlayerState>();
    if(!State || State->GetPawn()!=Hero) return false;
    const auto* Game=Hero->GetWorld()->GetGameState<AMCGameState>();
    return !Game || (Game->Phase!=EMCShiftPhase::Won && Game->Phase!=EMCShiftPhase::Lost);
}
bool CleanAt(AActor* Target,AMCToothCharacter* Worker,FVector Point,float Radius,float Seconds,float Rate,EMCStainCleanReason Reason)
{
    bool Changed=false;
    // The native contact APIs cap a single dose at .1s. Preserve elapsed time
    // when a 10Hz sample lands between rendered frames, with a bounded backlog.
    for(float Remaining=FMath::Clamp(Seconds,0.f,.3f);Remaining>UE_SMALL_NUMBER;Remaining-=.1f) {
        const float Dose=FMath::Min(Remaining,.1f);
        if(auto* Surface=Cast<AMCMouthSurface>(Target)) Changed|=Surface->CleanLiquidAt(Worker,Point,Radius,Dose,Rate,Reason);
        else if(auto* Tooth=Cast<AMCArenaTooth>(Target)) Changed|=Tooth->CleanGrimeAt(Worker,Point,Radius,Dose,Rate,Reason);
    }
    return Changed;
}
}

AMCSprayCoating::AMCSprayCoating()
{
    bReplicates=true;SetReplicateMovement(true);SetNetUpdateFrequency(5.f);
    Surface=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CompositionCoating"));SetRootComponent(Surface);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"));
    CoatingMaterial=Material.Object;Surface->SetMaterial(0,CoatingMaterial);
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->SetCanEverAffectNavigation(false);Surface->SetCastShadow(false);
}
void AMCSprayCoating::BeginPlay() {Super::BeginPlay();Surface->SetMaterial(0,CoatingMaterial);OnRep_Radius();}
void AMCSprayCoating::SetRadius(float InRadius)
{Radius=FMath::Clamp(FMath::IsFinite(InRadius)?InRadius:40.f,5.f,250.f);OnRep_Radius();ForceNetUpdate();}
void AMCSprayCoating::OnRep_Radius()
{
    if(!HasActorBegunPlay() || GetNetMode()==NM_DedicatedServer) return;
    constexpr int32 Sides=48,Rings=6;
    const float RingRadius[Rings]={0.f,.35f,.64f,.80f,.93f,1.f};
    const float RingAlpha[Rings]={.035f,.055f,.075f,.09f,.045f,0.f};
    TArray<FVector> Vertices,Normals;TArray<int32> Triangles;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    for(int32 Ring=0;Ring<Rings;++Ring) for(int32 Side=0;Side<=Sides;++Side) {
        const float Angle=2.f*PI*Side/Sides;
        const FVector Axis(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const float Envelope=RingRadius[Ring];
        const float Ripple=.035f*(FMath::Sin(Angle*3.f)+.55f*FMath::Cos(Angle*7.f+.9f))*FMath::Sin(Envelope*PI);
        const float LocalRadius=Envelope+Ripple;
        const float Density=.65f+.2f*FMath::Sin(Angle*3.f+.7f)+.15f*FMath::Cos(Angle*7.f);
        Vertices.Add(Axis*(Radius*LocalRadius));Normals.Add(FVector::UpVector);
        UV.Add(FVector2D(.5)+FVector2D(Axis)*(.5f*LocalRadius));
        // M_ReactionSoft multiplies its warm emissive tint by vertex RGB.
        // Low, desaturated values and a broad weak opacity taper retain the
        // underlying stain/tissue instead of making a glowing orange marker.
        const FLinearColor Tint=FMath::Lerp(FLinearColor(.13f,.20f,.55f),FLinearColor(.22f,.32f,.75f),Envelope);
        Colors.Add(FLinearColor(Tint.R,Tint.G,Tint.B,RingAlpha[Ring]*Density));Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false));
        if(Ring>0 && Side>0) {
            const int32 A=(Ring-1)*(Sides+1)+Side-1,B=A+1,C=Ring*(Sides+1)+Side-1,D=C+1;
            Triangles.Append({A,C,D,A,D,B});
        }
    }
    // One collision-free disk shares the exact world-space hazard radius.
    // Irregular translucent film fades to zero at the exact radius, using only
    // the prewarmed vertex-color material; there is no bright continuous ring.
    Surface->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,Tangents,false);
}
void AMCSprayCoating::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCSprayCoating,Radius);}

UMCDefenseSprayPassives* UMCDefenseSprayPassives::ForWorld(const UObject* Context)
{return Context && Context->GetWorld()?Context->GetWorld()->GetSubsystem<UMCDefenseSprayPassives>():nullptr;}
TStatId UMCDefenseSprayPassives::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCDefenseSprayPassives,STATGROUP_Tickables); }
bool UMCDefenseSprayPassives::IsTickable() const { return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld(); }
float UMCDefenseSprayPassives::ResolveIncomingDamage(AMCToothCharacter* Hero,float Damage)
{
    if(!Hero || !Hero->HasAuthority() || !FMath::IsFinite(Damage) || Damage<=0.f) return Damage;
    if(Has(Hero,Enamel) && FMath::FRand()<Amount(Hero,Enamel,TEXT("Chance"),.2f,0.f,1.f)) return 0.f;
    if(Has(Hero,Shield) && Hero->Status && Hero->Status->State.Health<Hero->Status->State.MaxHealth*Amount(Hero,Shield,TEXT("Threshold"),.2f,0.f,1.f))
        Damage*=1.f-Amount(Hero,Shield,TEXT("Reduction"),.5f,0.f,1.f);
    return Damage;
}
void UMCDefenseSprayPassives::RegisterThreat(AMCToothCharacter* Hero,AActor* Source,uint64 AttackID)
{
    auto* Self=ForWorld(Hero);
    if(!Self || !LiveWorker(Hero) || !Hero->HasAuthority() || !IsValid(Source) || Source->GetWorld()!=Hero->GetWorld() || !Has(Hero,SecondWind)) return;
    for(const auto& Threat:Self->Threats) if(Threat.Hero==Hero && Threat.Source==Source && Threat.AttackID==AttackID) return;
    // Bound pending/finished receipts even under a malformed attack source.
    if(Self->Threats.Num()>=256) Self->Threats.RemoveAt(0);
    auto& Threat=Self->Threats.AddDefaulted_GetRef();Threat.Hero=Hero;Threat.Owner=UMCPerkComponent::ForActor(Hero);Threat.Source=Source;Threat.AttackID=AttackID;
    Threat.StartedAt=Time(Hero->GetWorld());Threat.ExpiresAt=Threat.StartedAt+30.;
    Threat.bDashed=Hero->IsDashing();
}
void UMCDefenseSprayPassives::NotifyDashStarted(AMCToothCharacter* Hero)
{
    auto* Self=ForWorld(Hero);if(!Self || !Hero || !Hero->HasAuthority()) return;
    const double Now=Time(Hero->GetWorld());
    for(auto& Threat:Self->Threats) if(Threat.Hero==Hero && !Threat.bResolved && Threat.ExpiresAt>Now && Threat.StartedAt<=Now) Threat.bDashed=true;
}
void UMCDefenseSprayPassives::ResolveThreat(FThreat& Threat,bool bDodged)
{
    if(Threat.bResolved) return;
    Threat.bResolved=true;
    auto* Hero=Threat.Hero.Get();
    if(!bDodged || !Threat.bDashed || !LiveWorker(Hero) || !Has(Hero,SecondWind)) return;
    if(auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()))
        Move->RestoreStaminaFraction(Amount(Hero,SecondWind,TEXT("StaminaFraction"),.3f,0.f,1.f));
}
void UMCDefenseSprayPassives::NotifyIncomingHit(AMCToothCharacter* Hero,AActor* Source)
{
    auto* Self=ForWorld(Hero);if(!Self || !Hero || !Hero->HasAuthority()) return;
    for(auto& Threat:Self->Threats) if(Threat.Hero==Hero && Threat.Source==Source) Self->ResolveThreat(Threat,false);
}
void UMCDefenseSprayPassives::NotifyDodgedAttack(AMCToothCharacter* Hero,AActor* Source,uint64 AttackID)
{
    auto* Self=ForWorld(Hero);if(!Self || !Hero || !Hero->HasAuthority()) return;
    for(auto& Threat:Self->Threats) if(Threat.Hero==Hero && Threat.Source==Source && Threat.AttackID==AttackID) Self->ResolveThreat(Threat,true);
}
void UMCDefenseSprayPassives::NotifyAttackFinished(AActor* Source,uint64 AttackID,bool bCanceled)
{
    auto* Self=ForWorld(Source);if(!Self || !Source || !Source->HasAuthority()) return;
    for(auto& Threat:Self->Threats) if(Threat.Source==Source && Threat.AttackID==AttackID) Self->ResolveThreat(Threat,!bCanceled);
}
void UMCDefenseSprayPassives::UpdateDashVisibility(AMCToothCharacter* Hero)
{
    // ActorHidden is replicated. Only authority owns this flag; clients never
    // snapshot a server-hidden value and later mistake it for the baseline.
    auto* Self=ForWorld(Hero);if(!Self || !IsValid(Hero) || !Hero->HasAuthority()) return;
    const bool Hide=LiveWorker(Hero) && Has(Hero,Houdini) && Hero->IsDashing();
    if(Hide) {
        if(!Self->HiddenOverrides.Contains(Hero)) {
            auto& Override=Self->HiddenOverrides.Add(Hero);Override.bWasHidden=Hero->IsHidden();Override.Owner=UMCPerkComponent::ForActor(Hero);
        }
        if(!Hero->IsHidden()) {Hero->SetActorHiddenInGame(true);Hero->ForceNetUpdate();}
    } else if(const auto* Override=Self->HiddenOverrides.Find(Hero)) {
        Hero->SetActorHiddenInGame(Override->bWasHidden);Hero->ForceNetUpdate();Self->HiddenOverrides.Remove(Hero);
    }
}
float UMCDefenseSprayPassives::BrushMultiplier(const AActor* Target,FVector Point)
{
    const auto* Self=ForWorld(Target);if(!Self || !Target || Point.ContainsNaN()) return 1.f;
    const double Now=Time(Target->GetWorld());float Bonus=0.f;
    for(const auto& Stamp:Self->Softened)
        if(Stamp.Target==Target && Stamp.Until>Now && LiveWorker(Stamp.Worker.Get()) && Has(Stamp.Worker.Get(),Softener)
            && FVector::DistSquared(Target->GetActorTransform().TransformPosition(Stamp.LocalPoint),Point)<=FMath::Square(Stamp.Radius))
            Bonus=FMath::Max(Bonus,Amount(Stamp.Worker.Get(),Softener,TEXT("BrushBonus"),.4f,0.f,4.f));
    return 1.f+Bonus;
}
void UMCDefenseSprayPassives::StampTreatment(AMCToothCharacter* Hero,AActor* Target,FVector Point,FVector Normal,float Radius)
{
    const double Now=Time(GetWorld());
    if(Has(Hero,Softener) && UMCResultPassives::IsDirtyTarget(Target)) {
        FSoftened* Found=nullptr;
        for(auto& Stamp:Softened) if(Stamp.Worker==Hero && Stamp.Target==Target
            && FVector::DistSquared(Target->GetActorTransform().TransformPosition(Stamp.LocalPoint),Point)<FMath::Square(Radius*.5f)) {Found=&Stamp;break;}
        if(!Found) {
            if(Softened.Num()>=128) Softened.RemoveAt(0);
            Found=&Softened.AddDefaulted_GetRef();Found->Worker=Hero;Found->Owner=UMCPerkComponent::ForActor(Hero);Found->Target=Target;
            Found->LocalPoint=Target->GetActorTransform().InverseTransformPosition(Point);Found->Radius=Radius;
        }
        Found->Until=Now+Amount(Hero,Softener,TEXT("Duration"),5.f,.1f,30.f);
    }
    if(!Has(Hero,Lasting)) return;
    FCoating* Found=nullptr;int32 OwnerCount=0;
    for(auto& Coating:Coatings) if(Coating.Worker==Hero) {
        ++OwnerCount;
        if(Coating.Target==Target && FVector::DistSquared(Coating.Point,Point)<FMath::Square(Radius*.5f)) Found=&Coating;
    }
    if(!Found) {
        if(OwnerCount>=24) {
            const int32 Old=Coatings.IndexOfByPredicate([Hero](const auto& P){return P.Worker==Hero;});
            if(Old!=INDEX_NONE) {DestroyCoating(Coatings[Old]);Coatings.RemoveAt(Old);}
        }
        Found=&Coatings.AddDefaulted_GetRef();Found->Worker=Hero;Found->Owner=UMCPerkComponent::ForActor(Hero);Found->Target=Target;Found->Point=Point;Found->Normal=Normal;
        Found->Radius=Radius;Found->LastCleanAt=Now;
        if(Target) {Found->LocalPoint=Target->GetActorTransform().InverseTransformPosition(Point);Found->LocalNormal=Target->GetActorTransform().InverseTransformVectorNoScale(Normal);}
        FActorSpawnParameters Params;Params.Owner=Hero;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Found->Visual=GetWorld()->SpawnActor<AMCSprayCoating>(Point+Normal*.8f,FRotationMatrix::MakeFromZ(Normal).Rotator(),Params);
        if(Found->Visual.IsValid()) Found->Visual->SetRadius(Radius);
    }
    if(!FMath::IsNearlyEqual(Found->Radius,Radius)) {
        Found->Radius=Radius;if(Found->Visual.IsValid()) Found->Visual->SetRadius(Radius);
    }
    Found->CleanUntil=Now+Amount(Hero,Lasting,TEXT("CleanDuration"),3.f,.1f,30.f);
    Found->Until=Now+Amount(Hero,Lasting,TEXT("CoatingDuration"),10.f,.1f,60.f);
}
void UMCDefenseSprayPassives::ApplySpray(AMCToothCharacter* Hero,FVector Origin,FVector Direction,float Reach,float Seconds)
{
    auto* Self=ForWorld(Hero);
    if(!Self || !LiveWorker(Hero) || !Hero->HasAuthority() || !Hero->Inventory || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(Seconds) || Seconds<=0.f || (!Has(Hero,Softener) && !Has(Hero,Antiseptic) && !Has(Hero,Lasting))) return;
    Direction=Direction.GetSafeNormal();if(Direction.IsNearlyZero() || !FMath::IsFinite(Reach) || Reach<=0.f) return;
    const double Now=Time(Hero->GetWorld());
    float& Accumulated=Self->SpraySeconds.FindOrAdd(Hero);Accumulated=FMath::Min(.3f,Accumulated+FMath::Min(Seconds,.1f));
    double& Next=Self->NextSprayAt.FindOrAdd(Hero);
    if(Now<Next) return;
    Next=Now+.1;const float Dose=Accumulated;Accumulated=0.f;
    FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPassiveSpray),true,Hero);
    const bool Blocked=Hero->GetWorld()->LineTraceSingleByChannel(Block,Origin,Origin+Direction*Reach,ECC_Visibility,Query);
    const float Limit=Blocked?FMath::Clamp(float(Block.Distance),0.f,Reach):Reach;
    AActor* Target=nullptr;FVector Point=FVector::ZeroVector,Normal=FVector::UpVector;float Distance=Limit;
    if(auto* Tooth=Cast<AMCArenaTooth>(Block.GetActor());Tooth && UMCResultPassives::IsDirtyTarget(Tooth)) {
        Target=Tooth;Point=Block.ImpactPoint;Normal=Block.ImpactNormal;
    }
    TArray<AActor*> Candidates;
    if(auto* Results=UMCResultPassives::ForWorld(Hero)) Results->GetDirtyTargets(Candidates);
    for(auto* Candidate:Candidates) if(auto* Surface=Cast<AMCMouthSurface>(Candidate);Surface && !Surface->bUlcer && !Surface->IsClean()) {
        const FVector PlaneNormal=Surface->GetActorUpVector();
        const float Denominator=FVector::DotProduct(Direction,PlaneNormal);
        if(FMath::Abs(Denominator)<.015f) continue;
        const float Along=FVector::DotProduct(Surface->GetActorLocation()-Origin,PlaneNormal)/Denominator;
        if(Along<=0.f || Along>Distance+2.f) continue;
        const FVector At=Origin+Direction*Along;
        const FVector Local=Surface->GetActorTransform().InverseTransformPosition(At);
        if(FMath::Abs(Local.X)>Surface->LiquidHalfSize || FMath::Abs(Local.Y)>Surface->LiquidHalfSize) continue;
        FHitResult Occlusion;FCollisionQueryParams Sight(Query);Sight.AddIgnoredActor(Surface);
        if(Hero->GetWorld()->LineTraceSingleByChannel(Occlusion,Origin,At-PlaneNormal,ECC_Visibility,Sight)
            && Occlusion.Distance+3.f<Along) continue;
        Target=Surface;Point=At;Normal=PlaneNormal;Distance=Along;
    }
    if(!Target) {
        if(!Blocked || Block.ImpactNormal.Z<.45f) return;
        Target=Block.GetActor();Point=Block.ImpactPoint;Normal=Block.ImpactNormal;
    }
    const float Radius=FMath::Clamp(Hero->Inventory->SprayRadiusAtDistance(Distance),5.f,250.f);
    Self->StampTreatment(Hero,Target,Point,Normal,Radius);
    if(Has(Hero,Antiseptic)) CleanAt(Target,Hero,Point,Radius,Dose,Amount(Hero,Antiseptic,TEXT("CleanEfficiency"),1.f,0.f,16.f),EMCStainCleanReason::Spray);
}
bool UMCDefenseSprayPassives::IsStandingInCoating(const AActor* Target,FVector Point,FVector Normal,float Radius)
{
    if(!IsValid(Target) || Point.ContainsNaN() || Normal.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0.f) return false;
    FVector Feet=Target->GetActorLocation();
    if(const auto* Hero=Cast<AMCToothCharacter>(Target)) Feet-=FVector::UpVector*Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    else if(const auto* Enemy=Cast<AMCNutEnemy>(Target)) Feet-=FVector::UpVector*Enemy->Settings.BodyRadius;
    else return false;
    const FVector Delta=Feet-Point;const float Height=FVector::DotProduct(Delta,Normal);
    return FMath::Abs(Height)<=25.f && (Delta-Normal*Height).SizeSquared()<=FMath::Square(Radius);
}
void UMCDefenseSprayPassives::DestroyCoating(FCoating& Coating)
{if(Coating.Visual.IsValid()) Coating.Visual->Destroy();Coating.Visual.Reset();}
void UMCDefenseSprayPassives::PruneOwner(const UMCPerkComponent* Owner)
{
    auto* Self=ForWorld(Owner);if(!Self || !Owner || !Owner->GetOwner() || !Owner->GetOwner()->HasAuthority()) return;
    Self->Softened.RemoveAll([Owner](const auto& Stamp){return Stamp.Owner==Owner && (!LiveWorker(Stamp.Worker.Get()) || !Has(Stamp.Worker.Get(),Softener));});
    Self->Threats.RemoveAll([Owner](const auto& Threat){return Threat.Owner==Owner && (!LiveWorker(Threat.Hero.Get()) || !Has(Threat.Hero.Get(),SecondWind));});
    for(int32 I=Self->Coatings.Num()-1;I>=0;--I)
        if(Self->Coatings[I].Owner==Owner && (!LiveWorker(Self->Coatings[I].Worker.Get()) || !Has(Self->Coatings[I].Worker.Get(),Lasting))) {
            Self->DestroyCoating(Self->Coatings[I]);Self->Coatings.RemoveAt(I);
        }
    TArray<TWeakObjectPtr<AMCToothCharacter>> Heroes;Self->HiddenOverrides.GetKeys(Heroes);
    for(const auto& Hero:Heroes) if(Hero.IsValid() && Self->HiddenOverrides[Hero].Owner==Owner) UpdateDashVisibility(Hero.Get());
    if(const auto* Player=Cast<AMCPlayerState>(Owner->GetOwner())) if(auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn()))
        if(Hero->Inventory && !Has(Hero,Antiseptic)) Hero->Inventory->ResetPassiveSprayCycle();
}
void UMCDefenseSprayPassives::ResetOwner(const UMCPerkComponent* Owner)
{
    auto* Self=ForWorld(Owner);if(!Self || !Owner || !Owner->GetOwner() || !Owner->GetOwner()->HasAuthority()) return;
    Self->Softened.RemoveAll([Owner](const auto& Stamp){return Stamp.Owner==Owner;});
    Self->Threats.RemoveAll([Owner](const auto& Threat){return Threat.Owner==Owner;});
    for(int32 I=Self->Coatings.Num()-1;I>=0;--I) if(Self->Coatings[I].Owner==Owner) {
        Self->DestroyCoating(Self->Coatings[I]);Self->Coatings.RemoveAt(I);
    }
    for(auto It=Self->HiddenOverrides.CreateIterator();It;++It) if(It.Value().Owner==Owner) {
        if(auto* Hero=It.Key().Get();Hero && Hero->HasAuthority()) {Hero->SetActorHiddenInGame(It.Value().bWasHidden);Hero->ForceNetUpdate();}
        It.RemoveCurrent();
    }
    if(const auto* Player=Cast<AMCPlayerState>(Owner->GetOwner())) if(auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn())) {
        Self->NextSprayAt.Remove(Hero);Self->SpraySeconds.Remove(Hero);
        if(Hero->Inventory) Hero->Inventory->ResetPassiveSprayCycle();
    }
}
void UMCDefenseSprayPassives::Tick(float DeltaTime)
{
    const double Now=Time(GetWorld());
    TArray<TWeakObjectPtr<AMCToothCharacter>> Hidden;HiddenOverrides.GetKeys(Hidden);
    for(const auto& Hero:Hidden) if(Hero.IsValid()) UpdateDashVisibility(Hero.Get());else HiddenOverrides.Remove(Hero);
    if(GetWorld()->GetNetMode()==NM_Client) return;
    Threats.RemoveAll([Now](const auto& T){return !LiveWorker(T.Hero.Get()) || !T.Source.IsValid() || T.ExpiresAt<Now;});
    Softened.RemoveAll([Now](const auto& S){return S.Until<=Now || !S.Target.IsValid() || !LiveWorker(S.Worker.Get()) || !Has(S.Worker.Get(),Softener);});
    for(auto It=NextSprayAt.CreateIterator();It;++It) if(!LiveWorker(It.Key().Get())) {SpraySeconds.Remove(It.Key());It.RemoveCurrent();}
    for(int32 I=Coatings.Num()-1;I>=0;--I)
        if(Coatings[I].Until<=Now || !LiveWorker(Coatings[I].Worker.Get()) || !Has(Coatings[I].Worker.Get(),Lasting)) {DestroyCoating(Coatings[I]);Coatings.RemoveAt(I);}
    if(Now>=NextTreatmentAt) {
        LastTreatmentAt=Now;NextTreatmentAt=Now+.1;
        for(auto& Coating:Coatings) {
            if(Coating.Target.IsValid()) {
                Coating.Point=Coating.Target->GetActorTransform().TransformPosition(Coating.LocalPoint);
                Coating.Normal=Coating.Target->GetActorTransform().TransformVectorNoScale(Coating.LocalNormal).GetSafeNormal();
                if(Coating.Visual.IsValid()) Coating.Visual->SetActorLocationAndRotation(Coating.Point+Coating.Normal*.8f,FRotationMatrix::MakeFromZ(Coating.Normal).Rotator());
            }
            const float CleanSeconds=FMath::Max(0.f,float(FMath::Min(Now,Coating.CleanUntil)-Coating.LastCleanAt));
            Coating.LastCleanAt=Now;
            if(CleanSeconds>0.f) CleanAt(Coating.Target.Get(),Coating.Worker.Get(),Coating.Point,Coating.Radius,CleanSeconds,
                Amount(Coating.Worker.Get(),Lasting,TEXT("CleanEfficiency"),.3f,0.f,16.f),EMCStainCleanReason::Residue);
        }
    }
    if(Now<NextDamageAt) return;
    const float Seconds=LastDamageAt>0?FMath::Clamp(float(Now-LastDamageAt),0.f,.6f):.5f;
    LastDamageAt=Now;NextDamageAt=Now+.5;
    TMap<TWeakObjectPtr<AMCToothCharacter>,TSet<TWeakObjectPtr<AActor>>> Damaged;
    for(const auto& Coating:Coatings) {
        auto* Worker=Coating.Worker.Get();if(!LiveWorker(Worker)) continue;
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        TArray<FOverlapResult> Overlaps;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCompositionHazard),false);
        GetWorld()->OverlapMultiByObjectType(Overlaps,Coating.Point+Coating.Normal*50.f,FQuat::Identity,Objects,FCollisionShape::MakeSphere(Coating.Radius+100.f),Query);
        for(const auto& Overlap:Overlaps) {
            AActor* Target=Overlap.GetActor();
            if(!IsStandingInCoating(Target,Coating.Point,Coating.Normal,Coating.Radius) || Damaged.FindOrAdd(Worker).Contains(Target)) continue;
            const float Damage=Amount(Worker,Lasting,TEXT("DamagePerSecond"),5.f,0.f,100.f)*Seconds;
            bool Applied=false;
            if(auto* Hero=Cast<AMCToothCharacter>(Target)) {
                // The coating actor is the hazard source: direct hero damage remains friendly-fire protected.
                if(Coating.Visual.IsValid()) Applied=Hero->Status->Damage(Damage,Coating.Normal,Coating.Visual.Get());
            } else if(auto* Enemy=Cast<AMCNutEnemy>(Target);Enemy && !Cast<AMCNutBoss>(Enemy) && Enemy->CanReceiveToolHit()) {
                auto* Perks=UMCPerkComponent::ForActor(Worker);
                if(Perks) {TGuardValue<bool> Secondary(Perks->bRogueExtraHit,true);Applied=Enemy->ReceiveToolDamage(Damage,Worker)>0.f;}
            }
            // An enamel dodge still consumes this owner's one pulse opportunity.
            if(Applied || Cast<AMCToothCharacter>(Target)) Damaged.FindOrAdd(Worker).Add(Target);
        }
    }
}
void UMCDefenseSprayPassives::Deinitialize()
{
    for(const auto& Pair:HiddenOverrides) if(Pair.Key.IsValid() && Pair.Key->HasAuthority()) Pair.Key->SetActorHiddenInGame(Pair.Value.bWasHidden);
    for(auto& Coating:Coatings) DestroyCoating(Coating);
    HiddenOverrides.Reset();Coatings.Reset();Softened.Reset();Threats.Reset();NextSprayAt.Reset();SpraySeconds.Reset();
    Super::Deinitialize();
}
