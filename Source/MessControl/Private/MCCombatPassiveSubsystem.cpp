#include "MCCombatPassiveSubsystem.h"
#include "MCPassiveCombatCue.h"
#include "MCPassiveFeedbackCue.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCCataclysmJelly.h"
#include "MCBossCharacter.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/UnrealMathUtility.h"

namespace
{
const FName Mighty(TEXT("B2_MightyTooth")),Dash(TEXT("B2_StartFromDash")),Thunder(TEXT("B2_ThunderGod")),Echo(TEXT("B2_EchoSwing")),Billiards(TEXT("B2_Billiards"));
double Now(const UWorld* World) {return World?World->GetTimeSeconds():0;}
bool Ended(const UWorld* World)
{
    const auto* State=World?World->GetGameState<AMCGameState>():nullptr;
    return State && (State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost);
}
}

UMCCombatPassiveSubsystem* UMCCombatPassiveSubsystem::For(const UObject* Object)
{
    UWorld* World=Object?Object->GetWorld():nullptr;
    return World && World->GetNetMode()!=NM_Client?World->GetSubsystem<UMCCombatPassiveSubsystem>():nullptr;
}
AMCToothCharacter* UMCCombatPassiveSubsystem::HeroFor(const UMCPerkComponent* Perks)
{
    const auto* State=Perks?Cast<AMCPlayerState>(Perks->GetOwner()):nullptr;
    return State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
}
bool UMCCombatPassiveSubsystem::Live(const AMCToothCharacter* Hero)
{
    return IsValid(Hero) && Hero->HasAuthority() && !Hero->IsActorBeingDestroyed()
        && Hero->Status && Hero->Status->IsAlive() && !Hero->SwallowedBy && !Hero->IsMimicCaptured() && !Ended(Hero->GetWorld());
}
bool UMCCombatPassiveSubsystem::Owns(const UMCPerkComponent* Perks,FName ID)
{
    const auto* Definition=Perks?Perks->FindDefinition(ID):nullptr;
    return Definition && UMCPerkComponent::IsDefinitionUsable(*Definition) && Perks->GetStacks(ID)>0;
}
float UMCCombatPassiveSubsystem::Param(const UMCPerkComponent* Perks,FName ID,FName Key,float Default,float Min,float Max)
{
    const float Value=Perks?Perks->PerkParameter(ID,Key,Default):Default;
    return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Min,Max);
}
UMCCombatPassiveSubsystem::ETargetGroup UMCCombatPassiveSubsystem::GroupFor(AActor* Target)
{
    if(!IsValid(Target) || Target->IsActorBeingDestroyed()) return ETargetGroup::None;
    if(auto* Nut=Cast<AMCNutEnemy>(Target);Nut && Nut->CanReceiveToolHit()) return ETargetGroup::Enemy;
    if(auto* Boss=Cast<AMCBossCharacter>(Target);Boss && Boss->CanReceiveWeaponHit()) return ETargetGroup::Enemy;
    if(auto* Food=Cast<AMCFoodActor>(Target);Food && !Food->bFragment && !Food->IsDisposed()
        && Food->FoodData.Kind==EMCFoodKind::Food && Food->Phase!=EMCFoodPhase::Swallowing) return ETargetGroup::Food;
    if(auto* Hero=Cast<AMCToothCharacter>(Target);Hero && Hero->Status && Hero->Status->IsAlive()) return ETargetGroup::Ally;
    return ETargetGroup::None;
}
FVector UMCCombatPassiveSubsystem::TargetPoint(AActor* Target,FVector Origin)
{
    if(const auto* Nut=Cast<AMCNutEnemy>(Target)) return Nut->GetToolTargetPoint(Origin);
    if(const auto* Boss=Cast<AMCBossCharacter>(Target)) return Boss->GetMeleeTargetPoint(Origin);
    if(const auto* Food=Cast<AMCFoodActor>(Target);Food && Food->Visual) return Food->Visual->Bounds.GetBox().GetClosestPointTo(Origin);
    if(const auto* Hero=Cast<AMCToothCharacter>(Target);Hero && Hero->ToothPhysics && !Hero->ToothPhysics->CanAct()) return Hero->ToothPhysics->PhysicalLocation();
    return Target->GetActorLocation();
}
bool UMCCombatPassiveSubsystem::Visible(FVector Origin,AActor* Target,const AMCToothCharacter* Hero,AActor* Ignore) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPassiveCombat),false,Hero);
    Query.AddIgnoredActor(Target);if(Ignore) Query.AddIgnoredActor(Ignore);
    FHitResult Hit;return !GetWorld()->LineTraceSingleByChannel(Hit,Origin,TargetPoint(Target,Origin),ECC_Visibility,Query);
}
bool UMCCombatPassiveSubsystem::InAttack(const FAttack& Attack,AActor* Target) const
{
    if(Target==Attack.Hero.Get() || GroupFor(Target)!=Attack.Group || Attack.HitTargets.Contains(Target)) return false;
    if(const auto* Food=Cast<AMCFoodActor>(Target)) {
        if(!Food->GripSurface || !Food->GripSurface->GetStaticMesh()) return false;
        const float Radius=FMath::Min(45.f,Attack.Reach*.25f);
        for(float Height:{-45.f,15.f,75.f}) {
            FHitResult Contact;
            const FVector Start=Attack.Origin+Attack.Forward*Radius+FVector(0,0,Height);
            const FVector End=Attack.Origin+Attack.Forward*(Attack.Reach-Radius)+FVector(0,0,Height);
            if(!Food->GripSurface->SweepComponent(Contact,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(Radius),true)) continue;
            const FVector Offset=Contact.ImpactPoint-Attack.Origin;
            if(FVector::DotProduct(Offset,Attack.Forward)<-2.f || Offset.SizeSquared()>FMath::Square(Attack.Reach)) continue;
            FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPassiveFoodContact),false,Attack.Hero.Get());
            Query.AddIgnoredActor(Food);if(Attack.FirstTarget.IsValid()) Query.AddIgnoredActor(Attack.FirstTarget.Get());
            if(!GetWorld()->LineTraceSingleByChannel(Block,Attack.Origin,Contact.ImpactPoint,ECC_Visibility,Query)) return true;
        }
        return false;
    }
    const FVector Point=TargetPoint(Target,Attack.Origin),Offset=Point-Attack.Origin;
    if(Offset.SizeSquared()>FMath::Square(Attack.Reach) || FMath::Abs(Offset.Z)>120) return false;
    const FVector Aim=(Target->GetActorLocation()-Attack.Origin).GetSafeNormal2D(UE_SMALL_NUMBER,Attack.Forward);
    return FVector::DotProduct(Aim,Attack.Forward)>=.25f && Visible(Attack.Origin,Target,Attack.Hero.Get(),Attack.FirstTarget.Get());
}
UMCCombatPassiveSubsystem::FOwner* UMCCombatPassiveSubsystem::StateFor(const UMCPerkComponent* Perks,bool bCreate)
{
    auto* Hero=HeroFor(Perks);
    if(!Live(Hero)) {ResetOwner(Perks);return nullptr;}
    const TWeakObjectPtr<UMCPerkComponent> Key(const_cast<UMCPerkComponent*>(Perks));
    FOwner* State=Owners.Find(Key);
    if(State && State->Hero.Get()!=Hero) {ResetOwner(Perks);State=nullptr;}
    if(!State && bCreate) {State=&Owners.Add(Key);State->Hero=Hero;}
    if(State && State->Attack.bImplicit && State->Attack.Frame!=GFrameCounter) State->Attack=FAttack();
    return State;
}
uint64 UMCCombatPassiveSubsystem::BeginAttack(AMCToothCharacter* Hero,bool bMelee)
{
    auto* Perks=UMCPerkComponent::ForActor(Hero);auto* State=StateFor(Perks,true);
    if(!State || !Hero->Inventory || State->Attack.ID) return 0;
    auto& Attack=State->Attack;Attack=FAttack();Attack.ID=NextAttackID++;Attack.Hero=Hero;
    Attack.Frame=GFrameCounter;Attack.Tool=uint8(Hero->Inventory->Selected);
    Attack.Origin=Hero->GetActorLocation();Attack.Forward=Hero->GetActorForwardVector().GetSafeNormal2D();
    Attack.Reach=Hero->Inventory->MeleeReach();Attack.Damage=Hero->Inventory->Damage();Attack.bMelee=bMelee;
    if(bMelee && Owns(Perks,Echo)) {
        const FTransform Frame(Attack.Forward.Rotation(),Attack.Origin);
        auto Capture=[&](UStaticMeshComponent* Part) {
            if(!Part || !Part->GetStaticMesh() || !Part->IsVisible()) return;
            FMCEchoToolPart Snapshot;Snapshot.Mesh=Part->GetStaticMesh();Snapshot.ContactPose=Part->GetComponentTransform().GetRelativeTransform(Frame);
            Attack.ToolParts.Add(Snapshot);RetainedEchoMeshes.AddUnique(Snapshot.Mesh);
        };
        if(Hero->Inventory->Selected==EMCToolSlot::Brush) Capture(Hero->Brush);
        else {
            Capture(FindObject<UStaticMeshComponent>(Hero,TEXT("InventoryTool")));
            Capture(FindObject<UStaticMeshComponent>(Hero,TEXT("InventoryDetail")));
        }
    }
    return Attack.ID;
}
FMCCombatAttackScope::FMCCombatAttackScope(AMCToothCharacter* Hero,bool bOrdinaryMelee):Source(Hero)
{
    if(auto* Runtime=UMCCombatPassiveSubsystem::For(Hero)) ID=Runtime->BeginAttack(Hero,bOrdinaryMelee);
}
FMCCombatAttackScope::~FMCCombatAttackScope()
{
    if(ID) if(auto* Runtime=UMCCombatPassiveSubsystem::For(Source.Get())) Runtime->EndAttack(Source.Get(),ID);
}
void UMCCombatPassiveSubsystem::NotifyDashStarted(AMCToothCharacter* Hero)
{
    auto* Perks=UMCPerkComponent::ForActor(Hero);auto* Runtime=For(Hero);
    if(!Runtime || !Owns(Perks,Dash)) return;
    if(auto* State=Runtime->StateFor(Perks,true)) State->DashUntil=Now(Hero->GetWorld())+Param(Perks,Dash,TEXT("Window"),2,.01f,30);
}
float UMCCombatPassiveSubsystem::ModifyWeaponDamage(const UMCPerkComponent* Perks,AActor* Target,float Damage)
{
    auto* Runtime=For(Perks);
    if(!Runtime || !Perks || Perks->bRogueExtraHit || !FMath::IsFinite(Damage) || Damage<=0) return Damage;
    const auto Group=GroupFor(Target);
    if(Group!=ETargetGroup::Enemy && Group!=ETargetGroup::Food) return Damage;
    auto* State=Runtime->StateFor(Perks,true);if(!State) return Damage;
    if(!State->Attack.ID) {
        Runtime->BeginAttack(HeroFor(Perks),false);State=Runtime->StateFor(Perks,false);
        if(!State) return Damage;State->Attack.bImplicit=true;
    }
    auto& Attack=State->Attack;
    if(!Attack.bAccepted) Attack.DashScale=Owns(Perks,Dash) && Now(Runtime->GetWorld())<=State->DashUntil
        ?1+Param(Perks,Dash,TEXT("DamageBonus"),.25f,0,3):1;
    Damage*=Attack.DashScale;
    Attack.PendingDamage.Add(Target,Damage);return Damage;
}
void UMCCombatPassiveSubsystem::NotifyWeaponHit(const UMCPerkComponent* Perks,AActor* Target)
{
    auto* Runtime=For(Perks);if(!Runtime || !Perks || Perks->bRogueExtraHit) return;
    auto* State=Runtime->StateFor(Perks,false);if(!State || !State->Attack.ID) return;
    auto& Attack=State->Attack;const float* Damage=Attack.PendingDamage.Find(Target);
    if(!Damage || Attack.HitTargets.Contains(Target)) return;
    TWeakObjectPtr<AMCToothCharacter> PowerHitSource;
    FVector PowerHitDirection=FVector::ZeroVector;
    if(!Attack.bAccepted) {
        if(Attack.DashScale>1.f+UE_KINDA_SMALL_NUMBER) {PowerHitSource=Attack.Hero;PowerHitDirection=Attack.Forward;}
        Attack.bAccepted=true;Attack.Damage=*Damage;Attack.FirstTarget=Target;
        Attack.FirstPoint=TargetPoint(Target,Attack.Origin);
        // A fatal hit can have reduced HP to zero before this callback.
        Attack.Group=Cast<AMCFoodActor>(Target)?ETargetGroup::Food:ETargetGroup::Enemy;
        State->DashUntil=-100;
    }
    Attack.HitTargets.Add(Target);
    if(Owns(Perks,Billiards)) Runtime->Launch(Perks,Attack,Target,*Damage);
    if(Attack.bImplicit) Runtime->EndAttack(Attack.Hero.Get(),Attack.ID);
    // Presentation follows the confirmed one-shot consumption, never a query,
    // empty swing, secondary effect or another target in the same cleave.
    if(PowerHitSource.IsValid()) AMCPassiveFeedbackCue::Spawn(PowerHitSource.Get(),EMCPassiveFeedbackKind::PowerHit,1.f,PowerHitDirection);
}
void UMCCombatPassiveSubsystem::NotifyPhysicalHit(AMCToothCharacter* Hero,AMCToothCharacter* Target,float Damage)
{
    auto* Perks=UMCPerkComponent::ForActor(Hero);auto* Runtime=For(Hero);
    if(!Runtime || !Live(Hero) || !Target || !Target->Status->IsAlive()) return;
    auto* State=Runtime->StateFor(Perks,false);if(!State || !State->Attack.ID || State->Attack.bSecondary) return;
    auto& Attack=State->Attack;if(Attack.HitTargets.Contains(Target)) return;
    Attack.HitTargets.Add(Target);
    if(!Attack.bAccepted) {
        Attack.bAccepted=true;Attack.Damage=Damage;Attack.FirstTarget=Target;Attack.FirstPoint=Target->GetActorLocation();Attack.Group=ETargetGroup::Ally;
    }
}
void UMCCombatPassiveSubsystem::EndAttack(AMCToothCharacter* Hero,uint64 ID)
{
    auto* Perks=UMCPerkComponent::ForActor(Hero);auto* State=StateFor(Perks,false);
    if(!State || State->Attack.ID!=ID) return;
    if(!State->Attack.bAccepted || State->Attack.bSecondary) {State->Attack=FAttack();return;}
    FAttack Snapshot=State->Attack;
    if(Snapshot.bMelee && Owns(Perks,Mighty)) Repeat(Snapshot,false);
    State=StateFor(Perks,false);
    if(!State || State->Hero.Get()!=Hero || State->Attack.ID!=ID) return;
    State->Attack=FAttack();
    if(Snapshot.bMelee && Owns(Perks,Echo)) Echoes.Add({Snapshot,Now(GetWorld())+Param(Perks,Echo,TEXT("Delay"),.5f,.01f,10),Perks});
    if(Owns(Perks,Thunder)) Chain(Perks,Snapshot);
}
bool UMCCombatPassiveSubsystem::Apply(FAttack& Attack,AActor* Target,float Damage,bool bSecondary)
{
    auto* Hero=Attack.Hero.Get();auto* Perks=UMCPerkComponent::ForActor(Hero);
    if(!Live(Hero) || !Target || !Perks || !FMath::IsFinite(Damage) || Damage<=0) return false;
    TGuardValue<bool> Secondary(Perks->bRogueExtraHit,bSecondary);
    bool Accepted=false;
    if(auto* Food=Cast<AMCFoodActor>(Target)) Accepted=(bSecondary || (Hero->Inventory && Hero->Inventory->CanBreak(Food))) && Food->HitFood(Damage,Attack.Forward,Hero);
    else if(auto* Enemy=Cast<AMCNutEnemy>(Target)) {
        const auto* Knight=Cast<AMCNutBoss>(Enemy);const float Shield=Knight?Knight->ShieldHealth:0;
        Accepted=Enemy->ReceiveToolDamage(Damage,Hero)>0 || (Knight && Knight->ShieldHealth<Shield);
    } else if(auto* Boss=Cast<AMCBossCharacter>(Target)) Accepted=Boss->ReceiveBossDamage(Damage,Hero)>0;
    else if(auto* Ally=Cast<AMCToothCharacter>(Target);Ally && Ally!=Hero && Ally->Status->IsAlive() && Ally->ToothPhysics) {
        const FVector Direction=(Ally->GetActorLocation()-Attack.Origin).GetSafeNormal2D(UE_SMALL_NUMBER,Attack.Forward);
        const float Strength=bSecondary?FMath::Clamp(Damage/60.f,.15f,1.f):1.f;
        Ally->ToothPhysics->ApplyHit((Direction*Hero->ToothPhysics->Settings.Knockback+FVector(0,0,Hero->ToothPhysics->Settings.Lift))*Strength,Ally->GetActorLocation());
        if(!bSecondary) NotifyPhysicalHit(Hero,Ally,Damage);Accepted=true;
    }
    if(Accepted) {
        Attack.HitTargets.Add(Target);if(IsValid(Hero)) ++Hero->ConfirmedHitCount;
    }
    return Accepted;
}
void UMCCombatPassiveSubsystem::Repeat(FAttack& Attack,bool bSecondary)
{
    TArray<AActor*> Candidates;
    if(Attack.Group==ETargetGroup::Enemy) {
        for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) if(InAttack(Attack,*It)) Candidates.Add(*It);
        for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(InAttack(Attack,*It)) Candidates.Add(*It);
    } else if(Attack.Group==ETargetGroup::Food) {
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(InAttack(Attack,*It)) Candidates.Add(*It);
    } else if(Attack.Group==ETargetGroup::Ally) {
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(InAttack(Attack,*It)) Candidates.Add(*It);
    }
    Candidates.Sort([&](const AActor& A,const AActor& B){return FVector::DistSquared(A.GetActorLocation(),Attack.Origin)<FVector::DistSquared(B.GetActorLocation(),Attack.Origin);});
    for(auto* Target:Candidates) {
        auto* Hero=Attack.Hero.Get();auto* Perks=UMCPerkComponent::ForActor(Hero);
        if(!Live(Hero) || HeroFor(Perks)!=Hero || !Owns(Perks,bSecondary?Echo:Mighty)) break;
        if(!InAttack(Attack,Target)) continue;
        // Primary cleave recomputes the ordinary base exactly once at each target.
        const float Damage=bSecondary?Attack.Damage:(Attack.Hero.IsValid() && Attack.Hero->Inventory?Attack.Hero->Inventory->Damage():0);
        Apply(Attack,Target,Damage,bSecondary);
        if(bSecondary && !Owns(UMCPerkComponent::ForActor(Attack.Hero.Get()),Mighty)) break;
    }
}
void UMCCombatPassiveSubsystem::Chain(const UMCPerkComponent* Perks,const FAttack& Attack)
{
    float Roll=FMath::FRand();if(NextThunderRoll>=0) {Roll=NextThunderRoll;NextThunderRoll=-1;}
    if(Roll>=Param(Perks,Thunder,TEXT("Chance"),.2f,0,1)) return;
    const float Radius=Param(Perks,Thunder,TEXT("Radius"),300,1,2000);
    const int32 Limit=FMath::FloorToInt(Param(Perks,Thunder,TEXT("MaxTargets"),3,1,8));
    FAttack Secondary=Attack;Secondary.bSecondary=true;Secondary.Damage=Attack.Damage*Param(Perks,Thunder,TEXT("DamageFraction"),.3f,0,1);
    FVector From=Attack.FirstPoint;AActor* Previous=Attack.FirstTarget.Get();
    for(int32 Jump=0;Jump<Limit;++Jump) {
        AActor* Best=nullptr;double Distance=FMath::Square(Radius);
        auto Consider=[&](AActor* Candidate) {
            const auto Group=GroupFor(Candidate);
            if((Group!=ETargetGroup::Food && Group!=ETargetGroup::Ally) || Candidate==Attack.Hero.Get() || Secondary.HitTargets.Contains(Candidate)) return;
            const double D=FVector::DistSquared(TargetPoint(Candidate,From),From);
            if(D>=Distance || !Visible(From,Candidate,Attack.Hero.Get(),Previous)) return;
            Best=Candidate;Distance=D;
        };
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) Consider(*It);
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Consider(*It);
        if(!Best) break;
        const FVector To=TargetPoint(Best,From);Secondary.Origin=From;Secondary.Forward=(To-From).GetSafeNormal2D(UE_SMALL_NUMBER,Attack.Forward);
        // Chain food damage is independent of the currently selected tool's ability to cut it.
        bool Accepted=false;
        if(auto* Food=Cast<AMCFoodActor>(Best)) {TGuardValue<bool> Guard(const_cast<UMCPerkComponent*>(Perks)->bRogueExtraHit,true);Accepted=Food->HitFood(Secondary.Damage,Secondary.Forward,Attack.Hero.Get());}
        else Accepted=Apply(Secondary,Best,Secondary.Damage,true);
        Secondary.HitTargets.Add(Best);
        if(Accepted) AMCPassiveCombatCue::Spawn(GetWorld(),From,To,uint32(Attack.ID*17+Jump));
        From=To;Previous=Best;
    }
}
void UMCCombatPassiveSubsystem::Launch(const UMCPerkComponent* Perks,const FAttack& Attack,AActor* Target,float Damage)
{
    auto* Enemy=Cast<AMCNutEnemy>(Target);
    if(!Enemy || Cast<AMCNutBoss>(Enemy) || !Enemy->IsEncounterAlive()) return;
    if(const auto* Jelly=Cast<AMCCataclysmJelly>(Enemy);Jelly && Jelly->JellySize!=EMCCataclysmJellySize::Small) return;
    // A new real hit replaces the previous launch episode, never accumulating damage.
    Launches.RemoveAll([&](const FLaunch& Existing){return Existing.Enemy.Get()==Enemy;});
    FLaunch Launch;Launch.ID=NextAttackID++;Launch.Enemy=Enemy;Launch.Hero=Attack.Hero;Launch.Perks=const_cast<UMCPerkComponent*>(Perks);
    Launch.Velocity=(Enemy->GetActorLocation()-Attack.Origin).GetSafeNormal2D(UE_SMALL_NUMBER,Attack.Forward)*Param(Perks,Billiards,TEXT("LaunchSpeed"),700,0,2000);
    Launch.Damage=Damage*Param(Perks,Billiards,TEXT("DamageFraction"),.3f,0,1);
    Launch.MoveUntil=Now(GetWorld())+Param(Perks,Billiards,TEXT("LaunchDuration"),.4f,.01f,2);
    Launch.ContactUntil=Now(GetWorld())+Param(Perks,Billiards,TEXT("ContactWindow"),1,.01f,5);
    Launch.HitTargets.Add(Enemy);Launches.Add(MoveTemp(Launch));
}
bool UMCCombatPassiveSubsystem::AdvanceLaunch(AMCNutEnemy* Enemy,float Dt)
{
    const int32 Index=Launches.IndexOfByPredicate([&](const FLaunch& Item){return Item.Enemy.Get()==Enemy;});
    if(Index==INDEX_NONE) return false;
    // Damage/physics callbacks may remove/regrant a passive or replace the pawn.
    // Keep an episode copy, and reacquire storage by identity after those callbacks.
    FLaunch Episode=Launches[Index];auto* Hero=Episode.Hero.Get();auto* Perks=Episode.Perks.Get();
    auto CurrentIndex=[&](){return Launches.IndexOfByPredicate([&](const FLaunch& Item){return Item.ID==Episode.ID;});};
    const double Time=Now(GetWorld());
    if(!Live(Hero) || HeroFor(Perks)!=Hero || !Owns(Perks,Billiards) || !Enemy->IsEncounterAlive() || Time>Episode.ContactUntil) {Launches.RemoveAtSwap(Index);return false;}
    const FVector Start=Enemy->GetActorLocation();FVector End=Start;
    const bool Moving=Time<=Episode.MoveUntil && !Episode.Velocity.IsNearlyZero();
    TWeakObjectPtr<AActor> BlockingContact;
    if(Moving) {
        const float StepTime=FMath::Clamp(Dt,0.f,.1f);
        FVector Goal=Start+Episode.Velocity*StepTime;
        if(IsValid(Enemy->Tongue)) {
            FHitResult Floor;
            const float MaxHeightChange=FMath::Max(Enemy->Settings.BodyRadius*2.f,Episode.Velocity.Size()*StepTime);
            if(Enemy->Tongue->InteriorSurfacePoint(Goal,Enemy->Settings.BodyRadius+5,Floor)
                && FMath::Abs(Floor.ImpactPoint.Z+Enemy->Settings.BodyRadius+4-Start.Z)<=MaxHeightChange)
                Goal=Floor.ImpactPoint+FVector(0,0,Enemy->Settings.BodyRadius+4);
        }
        FHitResult Block;Enemy->SetActorLocation(Goal,true,&Block);
        if(!IsValid(Enemy) || !Live(Hero) || HeroFor(Perks)!=Hero || !Owns(Perks,Billiards)) return false;
        End=Enemy->GetActorLocation();if(Block.bBlockingHit) {Episode.Velocity=FVector::ZeroVector;BlockingContact=Block.GetActor();}
        Enemy->ForceNetUpdate();
    }
    int32 StoredIndex=CurrentIndex();if(StoredIndex==INDEX_NONE) return false;
    Launches[StoredIndex].Velocity=Episode.Velocity;
    TArray<AActor*> Contacts;
    // UE pulls a swept actor back slightly from the blocking surface. Its real
    // FHitResult is still a contact, even when the resulting spheres do not overlap.
    if(AActor* Other=BlockingContact.Get();Other && Other!=Enemy && !Episode.HitTargets.Contains(Other)
        && ((Cast<AMCNutEnemy>(Other) && CastChecked<AMCNutEnemy>(Other)->CanReceiveToolHit())
            || (Cast<AMCBossCharacter>(Other) && CastChecked<AMCBossCharacter>(Other)->CanReceiveWeaponHit()))) Contacts.Add(Other);
    for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) {
        if(!It->CanReceiveToolHit() || Episode.HitTargets.Contains(*It)) continue;
        const float Radius=Enemy->Settings.BodyRadius+It->Settings.BodyRadius;
        if(FMath::PointDistToSegmentSquared(It->GetActorLocation(),Start,End)<=FMath::Square(Radius)) Contacts.AddUnique(*It);
    }
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        if(!It->CanReceiveWeaponHit() || Episode.HitTargets.Contains(*It)) continue;
        const FVector Point=It->GetMeleeTargetPoint(Start);
        if(FMath::PointDistToSegmentSquared(Point,Start,End)<=FMath::Square(Enemy->Settings.BodyRadius+30.f)) Contacts.AddUnique(*It);
    }
    for(auto* Other:Contacts) {
        StoredIndex=CurrentIndex();if(StoredIndex==INDEX_NONE || !Live(Hero) || HeroFor(Perks)!=Hero || !Owns(Perks,Billiards)) break;
        if(!IsValid(Other) || Launches[StoredIndex].HitTargets.Contains(Other)) continue;
        if(!Visible(End,Other,Hero,Enemy)) continue;
        Launches[StoredIndex].HitTargets.Add(Other);FAttack Secondary;Secondary.Hero=Hero;Secondary.Origin=End;
        Secondary.Forward=(Other->GetActorLocation()-End).GetSafeNormal2D(UE_SMALL_NUMBER,Hero->GetActorForwardVector());Secondary.bSecondary=true;
        // Contact bypasses melee reach, but still goes through the target's authoritative damage/protection.
        auto* State=StateFor(Perks,true);if(!State) break;
        const auto Saved=State->Attack;Secondary.ID=NextAttackID++;Secondary.Reach=2000;State->Attack=Secondary;
        const bool Accepted=Apply(Secondary,Other,Episode.Damage,true);
        if(auto* Current=StateFor(Perks,false);Current && Current->Hero.Get()==Hero && Current->Attack.ID==Secondary.ID) Current->Attack=Saved;
        if(Accepted && IsValid(Other)) AMCPassiveFeedbackCue::Spawn(Other,EMCPassiveFeedbackKind::Billiards,1.f,Secondary.Forward);
    }
    return Moving;
}
bool UMCCombatPassiveSubsystem::TickBilliards(AMCNutEnemy* Enemy,float Dt)
{
    if(!Enemy || !Enemy->HasAuthority() || Cast<AMCNutBoss>(Enemy)) return false;
    auto* Runtime=For(Enemy);return Runtime && Runtime->AdvanceLaunch(Enemy,Dt);
}
bool UMCCombatPassiveSubsystem::GetSnapshotContact(const AMCToothCharacter* Hero,FVector& Origin,FVector& Forward,float& Reach)
{
    auto* Runtime=For(Hero);auto* Perks=UMCPerkComponent::ForActor(Hero);
    if(!Runtime) return false;auto* State=Runtime->StateFor(Perks,false);
    if(!State || !State->Attack.bSecondary) return false;
    Origin=State->Attack.Origin;Forward=State->Attack.Forward;Reach=State->Attack.Reach;return true;
}
void UMCCombatPassiveSubsystem::ResetOwner(const UMCPerkComponent* Perks)
{
    auto* Runtime=For(Perks);if(!Runtime || !Perks) return;
    Runtime->Owners.Remove(const_cast<UMCPerkComponent*>(Perks));
    Runtime->Echoes.RemoveAll([&](const FEcho& Item){
        const bool Remove=Item.Owner.Get()==Perks;
        if(Remove && Item.Cue.IsValid()) Item.Cue->Destroy();return Remove;
    });
    Runtime->Launches.RemoveAll([&](const FLaunch& Item){return Item.Perks.Get()==Perks;});
}
void UMCCombatPassiveSubsystem::Prune(const UMCPerkComponent* Perks)
{
    auto* State=StateFor(Perks,false);if(State && !Owns(Perks,Dash)) State->DashUntil=-100;
    Echoes.RemoveAll([&](const FEcho& Item){
        const bool Remove=Item.Owner.Get()==Perks && !Owns(Perks,Echo);
        if(Remove && Item.Cue.IsValid()) Item.Cue->Destroy();return Remove;
    });
    Launches.RemoveAll([&](const FLaunch& Item){return Item.Perks.Get()==Perks && !Owns(Perks,Billiards);});
}
void UMCCombatPassiveSubsystem::PruneOwner(const UMCPerkComponent* Perks) {if(auto* Runtime=For(Perks)) Runtime->Prune(Perks);}
bool UMCCombatPassiveSubsystem::IsTickable() const {return !IsTemplate() && GetWorld() && GetWorld()->GetNetMode()!=NM_Client;}
TStatId UMCCombatPassiveSubsystem::GetStatId() const {RETURN_QUICK_DECLARE_CYCLE_STAT(UMCCombatPassiveSubsystem,STATGROUP_Tickables);}
void UMCCombatPassiveSubsystem::Tick(float)
{
    if(Ended(GetWorld())) {
        for(const auto& Item:Echoes) if(Item.Cue.IsValid()) Item.Cue->Destroy();
        Owners.Reset();Echoes.Reset();Launches.Reset();return;
    }
    TArray<TWeakObjectPtr<UMCPerkComponent>> Keys;Owners.GetKeys(Keys);
    for(const auto& Key:Keys) {if(!Key.IsValid()) Owners.Remove(Key);else Prune(Key.Get());}
    Launches.RemoveAll([&](const FLaunch& Item){return !Item.Enemy.IsValid() || !Live(Item.Hero.Get()) || HeroFor(Item.Perks.Get())!=Item.Hero.Get() || Now(GetWorld())>Item.ContactUntil;});
    Echoes.RemoveAll([&](const FEcho& Item){
        const bool Remove=!Live(Item.Attack.Hero.Get()) || HeroFor(Item.Owner.Get())!=Item.Attack.Hero.Get() || !Owns(Item.Owner.Get(),Echo);
        if(Remove && Item.Cue.IsValid()) Item.Cue->Destroy();return Remove;
    });
    for(auto& Item:Echoes) if(!Item.Cue.IsValid() && Now(GetWorld())>=Item.At-.1) {
        auto* Hero=Item.Attack.Hero.Get();auto* Perks=Item.Owner.Get();
        if(Live(Hero) && HeroFor(Perks)==Hero && Owns(Perks,Echo))
            Item.Cue=AMCPassiveCombatCue::SpawnEcho(GetWorld(),Item.Attack.Origin,Item.Attack.Forward,Item.Attack.ToolParts,Item.Attack.Group==ETargetGroup::Food,Item.At);
    }
    TArray<FEcho> Due;
    for(int32 I=Echoes.Num()-1;I>=0;--I) if(Now(GetWorld())>=Echoes[I].At) {Due.Add(Echoes[I]);Echoes.RemoveAtSwap(I);}
    for(auto& Item:Due) {
        auto* Hero=Item.Attack.Hero.Get();auto* Perks=Item.Owner.Get();
        if(!Live(Hero) || HeroFor(Perks)!=Hero || !Owns(Perks,Echo)) continue;
        auto* State=StateFor(Perks,true);if(!State || State->Attack.ID) continue;
        FAttack Snapshot=Item.Attack;Snapshot.bSecondary=true;Snapshot.HitTargets.Reset();Snapshot.FirstTarget.Reset();
        Snapshot.Damage*=Param(Perks,Echo,TEXT("DamageFraction"),.4f,0,1);State->Attack=Snapshot;
        Repeat(Snapshot,true);
        if(auto* Current=StateFor(Perks,false);Current && Current->Hero.Get()==Hero && Current->Attack.ID==Snapshot.ID) Current->Attack=FAttack();
    }
}
void UMCCombatPassiveSubsystem::Deinitialize() {Owners.Reset();Echoes.Reset();Launches.Reset();RetainedEchoMeshes.Reset();Super::Deinitialize();}
