#include "MCFoodActor.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCFoodCollectionComponent.h"
#include "MCReactionVFX.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCTutorialDirector.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AMCFoodActor* AMCFoodActor::FindNearbySpoiledFood(UWorld* World,FVector Point,float Radius)
{
    if(!World || Point.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    AMCFoodActor* Closest=nullptr;double Distance=FMath::Square(Radius);
    for(TActorIterator<AMCFoodActor> It(World);It;++It) {
        if(!It->bSpoiled || It->IsDisposed() || It->IsActorBeingDestroyed() || It->bBrushTool) continue;
        const double Candidate=FVector::DistSquared(Point,It->Visual->Bounds.Origin);
        if(Candidate<Distance) {Distance=Candidate;Closest=*It;}
    }
    return Closest;
}

void AMCFoodActor::UpdateSmell()
{
    if(!HasAuthority()) return;
    const bool Smelly=bSpoiled && !bBrushTool && FoodData.Kind==EMCFoodKind::Food && !IsDisposed();
    if(!Smelly) {
        if(IsValid(SmellVFX)) {SmellVFX->Destroy();SmellVFX=nullptr;}
        return;
    }
    if(!IsValid(SmellVFX)) {
        SmellVFX=AMCReactionVFX::Spawn(GetWorld(),Visual->Bounds.Origin,EMCReactionEffect::SpoiledSmell,0,
            FMath::Clamp(float(Visual->Bounds.SphereRadius)*.75f,40.f,150.f));
        if(IsValid(SmellVFX)) {
            SmellVFX->SetOwner(this);
            SmellVFX->AttachToComponent(Visual,FAttachmentTransformRules::KeepWorldTransform);
        }
    }
}

bool AMCFoodActor::SpawnFragments(FVector Direction)
{
    if(!HasAuthority() || bFragment || bBrushTool || FoodData.Kind!=EMCFoodKind::Food) return false;
    FRandomStream Random(HashCombine(GetTypeHash(GetActorLocation()),GetTypeHash(GetWorld()->GetTimeSeconds())));
    int32 Created=0;
    const FBox ParentBounds=Body->CalcBounds(Body->GetComponentTransform()).GetBox()
        +Visual->CalcBounds(Visual->GetComponentTransform()).GetBox();
    const FVector Center=ParentBounds.GetCenter();
    const auto* Perks=UMCPerkComponent::ForActor(LastHandledBy);
    const float ScatterFactor=Perks?Perks->RogueValue(EMCRogueStat::FoodScatter):1.f;
    const int32 Count=FoodData.FragmentCount();
    struct FFragment
    {
        AMCFoodActor* Food=nullptr;
        FTransform Pose;
        FVector Extent=FVector::ZeroVector;
        FVector BoundsOffset=FVector::ZeroVector;
    };
    TArray<FFragment> Pending;
    FVector LargestExtent=FVector::ZeroVector;
    for(int32 I=0;I<Count;++I) {
        const FQuat Rotation=GetActorQuat()*FRotator(0,Random.FRandRange(-12.f,12.f),0).Quaternion();
        const FTransform Pose(Rotation,Center);
        auto* Piece=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Piece) continue;
        Piece->ConfigureItem(ItemName,FoodData,Random,true);
        Piece->Batch=Batch;Piece->LastHandledBy=LastHandledBy;
        if(ActorHasTag(TEXT("MCNutRain"))) {
            Piece->Tags.AddUnique(TEXT("MCNutRain"));
            Piece->SetOwner(GetOwner());
        }
        Piece->bSpoiled=bSpoiled;Piece->SpoilAt=SpoilAt;
        Piece->EntryRecoveryLanding=Center;
        // Configure the actual variant before choosing positions: different
        // imported meshes/scales do not have one universal fragment diameter.
        const FBox Envelope=Piece->Body->CalcBounds(Piece->Body->GetComponentTransform()).GetBox()
            +Piece->Visual->CalcBounds(Piece->Visual->GetComponentTransform()).GetBox();
        const FVector Extent=Envelope.GetExtent();
        LargestExtent=LargestExtent.ComponentMax(Extent);
        Pending.Add({Piece,Pose,Extent,Envelope.GetCenter()-Pose.GetLocation()});
    }
    if(!Pending.IsEmpty()) {
        // Fit a compact volume to the parent's proportions. A floor-wide ring
        // and a supported-spawn search moved remote pieces before physics even
        // began. All slots are now local to this cut, including airborne cuts.
        const FVector Pitch=LargestExtent*2+FVector(4);
        const FVector ParentSize=ParentBounds.GetSize().ComponentMax(LargestExtent*2).ComponentMax(FVector(1));
        FIntVector Grid(1,1,Pending.Num());
        double Best=DBL_MAX;
        for(int32 NX=1;NX<=Pending.Num();++NX) for(int32 NY=1;NY<=Pending.Num();++NY) {
            const int32 NZ=FMath::DivideAndRoundUp(Pending.Num(),NX*NY);
            const FVector Size=FVector(NX-1,NY-1,NZ-1)*Pitch+LargestExtent*2;
            const FVector Ratio=Size/ParentSize;
            const double Cost=Ratio.GetMax()+Ratio.SizeSquared()*.05+(NX*NY*NZ-Pending.Num())*.002+NZ*.0001;
            if(Cost<Best) {Best=Cost;Grid=FIntVector(NX,NY,NZ);}
        }
        TArray<FVector> Offsets;
        for(int32 Z=0;Z<Grid.Z;++Z) for(int32 Y=0;Y<Grid.Y;++Y) for(int32 X=0;X<Grid.X;++X)
            Offsets.Add(FVector(X-(Grid.X-1)*.5,Y-(Grid.Y-1)*.5,Z-(Grid.Z-1)*.5)*Pitch);
        Offsets.Sort([](const FVector& A,const FVector& B) {
            const double DA=A.SizeSquared(),DB=B.SizeSquared();
            if(!FMath::IsNearlyEqual(DA,DB,.01)) return DA<DB;
            if(A.Z!=B.Z) return A.Z<B.Z;
            return A.X==B.X?A.Y<B.Y:A.X<B.X;
        });
        FVector Mean=FVector::ZeroVector;double Bottom=DBL_MAX;
        for(int32 I=0;I<Pending.Num();++I) {Mean+=Offsets[I];Bottom=FMath::Min(Bottom,Offsets[I].Z-LargestExtent.Z);}
        Mean/=Pending.Num();Mean.Z=0;
        const double Lift=FMath::Max(0.,ParentBounds.Min.Z+3-(Center.Z+Bottom));
        for(int32 I=0;I<Pending.Num();++I) {
            auto& Entry=Pending[I];
            Entry.Pose.SetLocation(Center+Offsets[I]-Mean+FVector(0,0,Lift)-Entry.BoundsOffset);
        }
    }
    for(auto& Entry:Pending) {
        auto* Piece=Entry.Food;
        Piece->FinishSpawning(Entry.Pose);
        const FVector Ray=(Entry.Pose.GetLocation()-Center).GetSafeNormal2D();
        const FVector Velocity=(Ray*22+Direction.GetSafeNormal()*12)*ScatterFactor+FVector(0,0,45);
        Piece->Body->SetPhysicsLinearVelocity(Velocity);
        Piece->ForceNetUpdate();++Created;
    }
    if(Created!=Count)
        UE_LOG(LogTemp,Warning,TEXT("MC_FOOD_FRAGMENT_COUNT item=%s requested=%d created=%d"),*ItemName.ToString(),Count,Created);
    return Created>0;
}

void AMCFoodActor::RecoverUnreachableFood(float Dt)
{
    if(!HasAuthority() || IsDisposed() || bBrushTool || StackCarrier || EquippedBy || !Holders.IsEmpty()
        || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Stuck || IsMouthEntryActive()) return;
    AccessCheckElapsed+=Dt;
    if(AccessCheckElapsed<.75f) return;
    const float Interval=AccessCheckElapsed;AccessCheckElapsed=0;
    // Wait for the throw to finish. A piece already below the arena can recover
    // immediately; otherwise fast airborne food keeps its real trajectory.
    const bool BelowArena=GetActorLocation().Z<OutOfArenaZ();
    if(!BelowArena && Body->GetPhysicsLinearVelocity().SizeSquared()>FMath::Square(80.f)) {UnreachableSeconds=0;return;}
    for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
        if(It->ContainsDeliveryPosition(GetActorLocation())) {UnreachableSeconds=0;return;}
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;break;}
    if(!Tongue) return;
    FHitResult Floor;
    const float Margin=FMath::Max(35.f,float(Body->GetScaledBoxExtent().Size2D())+15);
    const bool Accessible=Tongue->GameplaySpawnFootprint(GetActorLocation(),Margin,Floor)
        && FMath::Abs(GetActorLocation().Z-Floor.ImpactPoint.Z)<Body->Bounds.BoxExtent.Z+100;
    if(Accessible) {LastAccessiblePosition=Floor.ImpactPoint;UnreachableSeconds=0;return;}
    UnreachableSeconds=BelowArena?2.25f:UnreachableSeconds+Interval;
    if(UnreachableSeconds<2.25f) return;
    // Deliberately discarded spoiled/foreign objects stay discarded. Ordinary
    // missed food still recovers so the meal cannot become impossible to finish.
    if(bManuallyThrown && IsWrongIngredient()) {
        Dispose();
        if(AMCTutorialDirector::IsTutorialTarget(this))
            if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld()))
                Tutorial->NotifyAction(LastHandledBy?Cast<AMCToothCharacter>(LastHandledBy->GetPawn()):nullptr,
                    bSpoiled?EMCTutorialAction::SpoiledDiscarded:EMCTutorialAction::TrashDiscarded,this);
        return;
    }
    bool Found=false;
    for(const auto& Point:{LastAccessiblePosition,EntryRecoveryLanding})
        if(Point.IsSet() && Tongue->GameplaySpawnFootprint(Point.GetValue(),Margin,Floor)) {Found=true;break;}
    if(!Found && bFragment && EntryRecoveryLanding.IsSet()) {
        // A cut piece may settle over an edge. Recover close to its parent,
        // never at an unrelated random point elsewhere on the tongue.
        for(float Radius:{25.f,50.f,100.f,150.f}) {
            for(int32 I=0;I<8 && !Found;++I) {
                const float Angle=I*PI*.25f;
                const FVector Probe=EntryRecoveryLanding.GetValue()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius;
                Found=Tongue->GameplaySpawnFootprint(Probe,Margin,Floor);
            }
            if(Found) break;
        }
    }
    if(!Found && !bFragment) {
        FRandomStream Random(HashCombine(GetTypeHash(GetActorLocation()),GetTypeHash(GetWorld()->GetTimeSeconds())));
        Found=Tongue->RandomGameplaySpawnPoint(Random,Margin,Margin*2,TConstArrayView<FVector>(),Floor);
    }
    if(!Found) return;
    EndMouthEntry();
    SetActorLocation(Floor.ImpactPoint+FVector(0,0,Body->Bounds.BoxExtent.Z+8),false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    LastAccessiblePosition=Floor.ImpactPoint;UnreachableSeconds=0;ForceNetUpdate();
}
