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
    const FVector Center=GetActorLocation();
    const auto* Perks=UMCPerkComponent::ForActor(LastHandledBy);
    const float ScatterFactor=Perks?Perks->RogueValue(EMCRogueStat::FoodScatter):1.f;
    const float Scatter=FMath::Clamp(float(Visual->Bounds.SphereRadius)*.3f,25.f,70.f)*ScatterFactor;
    const int32 Count=FoodData.FragmentCount();
    struct FFragment
    {
        AMCFoodActor* Food=nullptr;
        FTransform Pose;
        FVector Extent=FVector::ZeroVector;
    };
    TArray<FFragment> Pending;
    FVector LargestExtent=FVector::ZeroVector;
    double LargestRadius=0;
    for(int32 I=0;I<Count;++I) {
        const float Angle=I*PI*2/Count;
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FTransform Pose(FRotator(0,Random.FRandRange(-180.f,180.f),0),Center+Ray*Scatter+FVector(0,0,20));
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
        const FVector BodyExtent=Piece->Body->CalcBounds(Piece->Body->GetComponentTransform()).BoxExtent;
        const FVector VisualExtent=Piece->Visual->CalcBounds(Piece->Visual->GetComponentTransform()).BoxExtent;
        const FVector Extent=BodyExtent.ComponentMax(VisualExtent);
        LargestExtent=LargestExtent.ComponentMax(Extent);
        LargestRadius=FMath::Max(LargestRadius,Extent.Size2D());
        Pending.Add({Piece,Pose,Extent});
    }
    bool DenseLayout=Count>5;
    // Preserve the original small burst when it really fits. Even three large
    // authored fragments can otherwise begin inside one another.
    for(int32 I=0;!DenseLayout && I<Pending.Num();++I)
        for(int32 J=0;J<I;++J) {
            const auto& A=Pending[I];const auto& B=Pending[J];
            if(FBox(A.Pose.GetLocation()-A.Extent-FVector(3),A.Pose.GetLocation()+A.Extent+FVector(3))
                .Intersect(FBox(B.Pose.GetLocation()-B.Extent-FVector(3),B.Pose.GetLocation()+B.Extent+FVector(3)))) {
                DenseLayout=true;break;
            }
        }
    if(DenseLayout && !Pending.IsEmpty()) {
        // A fixed 70 cm ring has a 22 cm neighbour gap with twenty pieces.
        // Pack disjoint footprints first, rather than ask Chaos to resolve a
        // deeply overlapping pile with explosive depenetration velocities.
        const double Pitch=LargestRadius*2+6;
        TArray<FVector> Offsets;
        for(int32 Q=-6;Q<=6;++Q) for(int32 R=-6;R<=6;++R)
            if(FMath::Max3(FMath::Abs(Q),FMath::Abs(R),FMath::Abs(Q+R))<=6)
                Offsets.Add(FVector(Pitch*(Q+R*.5),Pitch*R*FMath::Sqrt(3.)*.5,0));
        Offsets.Sort([](const FVector& A,const FVector& B) {
            const double DA=A.SizeSquared2D(),DB=B.SizeSquared2D();
            if(!FMath::IsNearlyEqual(DA,DB,.01)) return DA<DB;
            return A.X==B.X?A.Y<B.Y:A.X<B.X;
        });
        AMCTongue* Tongue=nullptr;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;break;}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFragmentSpawn),false,this);
        for(const auto& Entry:Pending) Query.AddIgnoredActor(Entry.Food);
        FCollisionObjectQueryParams Objects(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        Objects.AddObjectTypesToQuery(FragmentCollisionChannel);
        const FCollisionShape Shape=FCollisionShape::MakeBox(LargestExtent+FVector(2));
        TArray<FVector> Supported,Positions;
        int32 Attempts=0;
        for(const FVector& Offset:Offsets) {
            FVector Position=Center+Offset+FVector(0,0,20);
            if(Tongue) {
                FHitResult Floor;
                if(!Tongue->GameplaySpawnFootprint(Center+Offset,float(LargestRadius+4),Floor)) continue;
                Position=Floor.ImpactPoint+FVector(0,0,LargestExtent.Z+6);
            }
            Supported.Add(Position);
            ++Attempts;
            if(!GetWorld()->OverlapAnyTestByObjectType(Position,FQuat::Identity,Objects,Shape,Query))
                Positions.Add(Position);
            if(Positions.Num()>=Pending.Num()) break;
        }
        // Tight edges may offer fewer supported columns. Reuse their footprint
        // at separated heights; never shrink the requested count or the meshes.
        int32 Layer=1;
        const double LayerPitch=LargestExtent.Z*2+8;
        for(;Positions.Num()<Pending.Num() && !Supported.IsEmpty() && Attempts<256;++Layer)
            for(const FVector& Base:Supported) {
                if(Positions.Num()>=Pending.Num() || Attempts++>=256) break;
                const FVector Position=Base+FVector(0,0,LayerPitch*Layer);
                if(!GetWorld()->OverlapAnyTestByObjectType(Position,FQuat::Identity,Objects,Shape,Query))
                    Positions.Add(Position);
            }
        if(Positions.Num()<Pending.Num()) {
            // Exceptional cut outside supported tissue or under fully blocked
            // scenery: retain every item in disjoint air slots and let ordinary
            // unreachable-food recovery handle it, instead of silently losing
            // the configured pieces or reinstating overlapping spawn bodies.
            UE_LOG(LogTemp,Warning,TEXT("MC_FOOD_FRAGMENT_SUPPORT item=%s grounded=%d requested=%d; using separated airborne fallback"),*ItemName.ToString(),Positions.Num(),Pending.Num());
            const FVector Anchor=Supported.IsEmpty()?Center+FVector(0,0,20):Supported[0];
            while(Positions.Num()<Pending.Num()) {
                Positions.Add(Anchor+FVector(0,0,LayerPitch*(Layer+Pending.Num())));
                ++Layer;
            }
        }
        for(int32 I=0;I<Pending.Num();++I) Pending[I].Pose.SetLocation(Positions[I]);
    }
    for(auto& Entry:Pending) {
        auto* Piece=Entry.Food;
        Piece->FinishSpawning(Entry.Pose);
        const FVector Ray=(Entry.Pose.GetLocation()-Center).GetSafeNormal2D();
        const FVector Velocity=DenseLayout
            ?(Ray*22+Direction.GetSafeNormal()*12)*ScatterFactor+FVector(0,0,45)
            :(Ray*90+Direction.GetSafeNormal()*35)*ScatterFactor+FVector(0,0,110);
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
    if(!Found) {
        FRandomStream Random(HashCombine(GetTypeHash(GetActorLocation()),GetTypeHash(GetWorld()->GetTimeSeconds())));
        Found=Tongue->RandomGameplaySpawnPoint(Random,Margin,Margin*2,TConstArrayView<FVector>(),Floor);
    }
    if(!Found) return;
    EndMouthEntry();
    SetActorLocation(Floor.ImpactPoint+FVector(0,0,Body->Bounds.BoxExtent.Z+8),false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    LastAccessiblePosition=Floor.ImpactPoint;UnreachableSeconds=0;ForceNetUpdate();
}
