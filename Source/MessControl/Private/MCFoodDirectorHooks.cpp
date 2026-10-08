#include "MCFoodDirectorHooks.h"

#include "MCDayPlan.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCArenaTooth.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"

namespace MCFoodDirectorPrivate
{
float Positive(float Value,float Default)
{
    return FMath::IsFinite(Value) && Value>0 ? Value : Default;
}

bool DeliveryMatches(const AMCFoodDisposal* Exit,EMCFoodKind Kind,bool WrongIngredient)
{
    return Kind==EMCFoodKind::Spicy || Exit->bBrushBin==WrongIngredient;
}

bool HardRow(FName Name,const FMCFoodRow& Row)
{
    if(Row.Resistance!=EMCFoodResistance::Automatic) return Row.Resistance==EMCFoodResistance::Hard;
    return Name==TEXT("Carrot") || Name==TEXT("Nut") || Name==TEXT("Crust") || Name==TEXT("Tartar");
}

float CuttingWork(float Health,bool Hard,const FMCFoodPipelineTuning& Tuning)
{
    const float Efficiency=FMath::Clamp(Positive(Tuning.EffectiveWorkFraction,.84f),.1f,1.f);
    const float Damage=Positive(Hard?Tuning.PickaxeDamage:Tuning.KnifeDamage,Hard?40.f:25.f);
    const float Swing=Positive(Hard?Tuning.PickaxeSwingSeconds:Tuning.KnifeSwingSeconds,Hard?.65f:.70f);
    return FMath::CeilToInt(FMath::Max(0.f,Health)/Damage)*Swing/Efficiency;
}

float LooseTransportWork(int32 Pieces,float Distance,const FMCFoodPipelineTuning& Tuning)
{
    if(Pieces<=0) return 0;
    const int32 Trips=FMath::DivideAndRoundUp(Pieces,FMath::Clamp(Tuning.StackCapacity,1,64));
    return Distance*Positive(Tuning.PathFactor,1.25f)*(2.f*Trips-1.f)/Positive(Tuning.CarrySpeed,340.f)
        +Trips*Positive(Tuning.DeliveryTripSeconds,.4f);
}

float DeliveryDistance(UWorld* World,FVector Point,const AMCFoodActor* Food)
{
    float Best=MAX_flt;
    for(TActorIterator<AMCFoodDisposal> It(World);It;++It)
    {
        // Fresh ordinary food goes to the throat; wrong ingredients go overboard.
        if(!DeliveryMatches(*It,Food->FoodData.Kind,Food->IsWrongIngredient())) continue;
        FTransform Zone;FVector Extent;bool Circular=false;
        It->GetDeliveryZoneGeometry(Zone,Extent,Circular);
        Best=FMath::Min(Best,float(FVector::Dist2D(Point,Zone.GetLocation())));
    }
    // No exit means no valid transport estimate; callers still see outstanding actors.
    return Best==MAX_flt ? 0.f : Best;
}

FVector ToothBoxContact(const UBoxComponent* Body,FVector WorldPoint)
{
    const FTransform Pose=Body->GetComponentTransform();
    FVector Local=Pose.InverseTransformPosition(WorldPoint);
    const FVector Extent=Body->GetUnscaledBoxExtent();
    Local.X=FMath::Clamp(Local.X,-Extent.X,Extent.X);
    Local.Y=FMath::Clamp(Local.Y,-Extent.Y,Extent.Y);
    Local.Z=FMath::Clamp(Local.Z,-Extent.Z,Extent.Z);
    return Pose.TransformPosition(Local);
}
}

AMCFoodActor* MCSpawnDirectedFoodEntry(UWorld* World,const UMCDayPlan* Plan,FName RowName,
    int32 Batch,FRandomStream& Random)
{
    if(!World || World->GetNetMode()==NM_Client || !Plan || RowName.IsNone()) return nullptr;
    UDataTable* Menu=Plan->Menu.LoadSynchronous();
    const FMCFoodRow* Row=Menu ? Menu->FindRow<FMCFoodRow>(RowName,TEXT("Directed food"),false) : nullptr;
    if(!Row) return nullptr;
    TArray<AMCTongue*> Tongues;
    for(TActorIterator<AMCTongue> It(World);It;++It)
        if(It->Surface && It->Surface->IsRegistered() && !It->CurrentVertices().IsEmpty()) Tongues.Add(*It);
    if(Tongues.IsEmpty()) return nullptr;

    FTransform Pose(FRotator(0,Random.FRandRange(-180.f,180.f),0),FVector::ZeroVector);
    AMCFoodActor* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,
        nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Food) return nullptr;
    Food->ConfigureItem(RowName,*Row,Random);
    bool AuthoredMesh=false;
    for(const auto& Mesh:Row->WholeMeshes) AuthoredMesh|=Mesh.Get()==Food->ItemMesh.Get() && Food->ItemMesh!=nullptr;
    // A missing authored variant must not silently become a prototype food ticket.
    if(!AuthoredMesh) {Food->Destroy();return nullptr;}
    Food->Batch=Batch;
    const float Margin=Food->Body->GetScaledBoxExtent().Size2D()+20.f;
    FHitResult Floor;AMCTongue* LandingTongue=nullptr;
    for(AMCTongue* Tongue:Tongues)
        if(Tongue->RandomGameplaySpawnPoint(Random,Margin,Margin*2.f+40.f,TConstArrayView<FVector>(),Floor))
        {LandingTongue=Tongue;break;}
    FVector Start,Velocity;
    const FVector Landing=Floor.ImpactPoint+FVector(0,0,Food->Body->GetScaledBoxExtent().Z+5.f);
    if(!LandingTongue || !Plan->FoodEntry.BuildTrajectory(LandingTongue->Surface->Bounds.GetBox(),
        Landing,World->GetGravityZ(),Start,Velocity))
    {
        Food->Destroy();
        return nullptr;
    }
    Pose.SetLocation(Start);
    UGameplayStatics::FinishSpawningActor(Food,Pose);
    if(!IsValid(Food) || Food->IsActorBeingDestroyed()) return nullptr;
    Food->BeginMouthEntry(Velocity,Plan->FoodEntry.PushSpeed,Landing);
    return Food;
}

AMCFoodActor* MCSpawnDirectedStuckFood(UWorld* World,const UMCDayPlan* Plan,FName RowName,
    int32 Batch,FRandomStream& Random)
{
    if(!World || World->GetNetMode()==NM_Client || !Plan || RowName.IsNone()) return nullptr;
    UDataTable* Menu=Plan->Menu.LoadSynchronous();
    const FMCFoodRow* Row=Menu?Menu->FindRow<FMCFoodRow>(RowName,TEXT("Directed stuck food"),false):nullptr;
    if(!Row || Row->Kind!=EMCFoodKind::Food) return nullptr;
    TArray<AMCTongue*> Tongues;
    for(TActorIterator<AMCTongue> It(World);It;++It)
        if(It->Surface && It->Surface->IsRegistered() && !It->CurrentVertices().IsEmpty()) Tongues.Add(*It);
    if(Tongues.IsEmpty()) return nullptr;
    AMCTongue* Tongue=Tongues[Random.RandRange(0,Tongues.Num()-1)];
    const FBox TongueBounds=Tongue->Surface->Bounds.GetBox();
    const float LeftChance=FMath::IsFinite(Tongue->GameplaySpawnLeftChance)
        ?FMath::Clamp(Tongue->GameplaySpawnLeftChance,0.f,1.f):.30f;
    const int32 Zone=Random.FRand()<LeftChance?0:1;
    TArray<AMCArenaTooth*> Teeth;
    for(TActorIterator<AMCArenaTooth> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && It->IsAvailable() && It->Body->IsRegistered()) Teeth.Add(*It);
    if(Teeth.IsEmpty()) return nullptr;
    for(int32 I=Teeth.Num()-1;I>0;--I) Teeth.Swap(I,Random.RandRange(0,I));

    FTransform Pose(FRotator(0,Random.FRandRange(-180.f,180.f),0),FVector::ZeroVector);
    AMCFoodActor* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,
        nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Food) return nullptr;
    Food->ConfigureItem(RowName,*Row,Random);
    bool AuthoredMesh=false;
    for(const auto& Mesh:Row->WholeMeshes) AuthoredMesh|=Mesh.Get()==Food->ItemMesh.Get() && Food->ItemMesh!=nullptr;
    if(!AuthoredMesh) {Food->Destroy();return nullptr;}
    const FVector Extent=Food->Body->GetScaledBoxExtent();
    const float Margin=Extent.Size2D()+20.f;
    const float InitialYaw=Pose.Rotator().Yaw;
    AMCArenaTooth* Anchor=nullptr;FVector Inward=FVector::ZeroVector;
    for(AMCArenaTooth* Tooth:Teeth)
    {
        const FVector Center=Tooth->Body->Bounds.Origin;
        // Centre-only contacts can sit over the tongue's concave rim. Search the
        // same physical inner face tangentially; never change the selected band.
        const int32 FirstSample=Random.RandRange(0,12);
        for(int32 Orientation=0;Orientation<12;++Orientation)
        {
            // Keep the configured variant and band; only its legal contact pose changes.
            Pose.SetRotation(FRotator(0,InitialYaw+Orientation*30.f,0).Quaternion());
            Food->SetActorRotation(Pose.GetRotation());
            for(int32 Sample=0;Sample<13;++Sample)
            {
                const float Offset=-.9f+.15f*((FirstSample+Sample)%13);
                const FVector Target(Center.X+Offset*Tooth->Body->Bounds.BoxExtent.X,TongueBounds.GetCenter().Y,Center.Z);
                // The authored teeth form two rows; their inner face points to the tongue centre.
                const FVector Contact=MCFoodDirectorPrivate::ToothBoxContact(Tooth->Body,Target);
                const FVector Direction=(Target-Contact).GetSafeNormal2D();
                if(Direction.IsNearlyZero()) continue;
                const float Support=FMath::Abs(FVector::DotProduct(Food->GetActorForwardVector(),Direction))*Extent.X
                    +FMath::Abs(FVector::DotProduct(Food->GetActorRightVector(),Direction))*Extent.Y;
                const FVector Candidate=Contact+Direction*(Support-FMath::Min(22.f,Support*.35f));
                FHitResult Floor;
                if(Tongue->GameplaySpawnZone(Candidate)!=Zone || !Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor)) continue;
                const FVector Position=Floor.ImpactPoint+FVector(0,0,Extent.Z+5.f);
                const FVector Nearest=MCFoodDirectorPrivate::ToothBoxContact(Tooth->Body,Position);
                const FVector LocalContact=Pose.GetRotation().UnrotateVector(Nearest-Position).GetAbs();
                // Floor-supported height must still intersect the tooth, rather than float above it.
                if(LocalContact.X>Extent.X+1 || LocalContact.Y>Extent.Y+1 || LocalContact.Z>Extent.Z+1) continue;
                bool Occupied=false;
                for(TActorIterator<AMCFoodActor> It(World);It;++It)
                {
                    if(*It==Food || It->IsDisposed() || It->bBrushTool || It->IsActorBeingDestroyed()) continue;
                    const FVector OtherExtent=It->Body->GetScaledBoxExtent();
                    if(FMath::Abs(It->GetActorLocation().Z-Position.Z)>Extent.Z+OtherExtent.Z+20) continue;
                    if(FVector::DistSquared2D(It->GetActorLocation(),Position)<FMath::Square(Margin+OtherExtent.Size2D()+20.f))
                        {Occupied=true;break;}
                }
                if(Occupied) continue;
                Pose.SetLocation(Position);Anchor=Tooth;Inward=Direction;break;
            }
            if(Anchor) break;
        }
        if(Anchor) break;
    }
    if(!Anchor) {Food->Destroy();return nullptr;}
    Food->Batch=Batch;Food->Initialize(true,Inward);
    Food->Phase=EMCFoodPhase::Stuck;Food->StuckTooth=Anchor;
    UGameplayStatics::FinishSpawningActor(Food,Pose);
    if(!IsValid(Food) || Food->IsActorBeingDestroyed()) return nullptr;
    Food->Body->SetSimulatePhysics(false);Food->ForceNetUpdate();
    return Food;
}

float MCForecastDirectedFoodWork(UWorld* World,const UMCDayPlan* Plan,FName RowName,bool bStuck,
    const FMCFoodPipelineTuning& Tuning)
{
    if(!World || World->GetNetMode()==NM_Client || !Plan || RowName.IsNone()) return MAX_flt;
    const UDataTable* Menu=Plan->Menu.LoadSynchronous();
    const FMCFoodRow* SavedRow=Menu?Menu->FindRow<FMCFoodRow>(RowName,TEXT("Directed food forecast"),false):nullptr;
    if(!SavedRow) return MAX_flt;
    FMCFoodRow Row=*SavedRow;Row.Sanitize();
    using namespace MCFoodDirectorPrivate;
    float MaximumDistance=0;bool FoundTongue=false;
    for(TActorIterator<AMCTongue> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed() || !It->Surface || !It->Surface->IsRegistered() || It->CurrentVertices().IsEmpty()) continue;
        FBox Bounds=It->Surface->Bounds.GetBox();
        if(!Bounds.IsValid || Bounds.GetSize().X<=KINDA_SMALL_NUMBER || Bounds.GetSize().Y<=KINDA_SMALL_NUMBER) continue;
        const double Near=FMath::Clamp(double(FMath::IsFinite(It->GameplaySpawnNearDepth)?It->GameplaySpawnNearDepth:.18f),0.,.95);
        const double Far=FMath::Clamp(double(FMath::IsFinite(It->GameplaySpawnFarDepth)?It->GameplaySpawnFarDepth:.82f),Near+.05,1.);
        const double MaxX=Bounds.Max.X-Bounds.GetSize().X*Near;
        const double MinX=Bounds.Max.X-Bounds.GetSize().X*Far;
        // The legal footprint is a subset of this rectangle. For each valid exit,
        // its farthest rectangle corner bounds every delivery distance. Taking
        // the best exit retains that upper bound, including spicy dual routing.
        float BestExitMaximum=MAX_flt;
        for(TActorIterator<AMCFoodDisposal> Exit(World);Exit;++Exit)
        {
            if(Exit->IsActorBeingDestroyed() || !DeliveryMatches(*Exit,Row.Kind,Row.Kind==EMCFoodKind::ForeignObject)) continue;
            FTransform Zone;FVector Extent;bool Circular=false;Exit->GetDeliveryZoneGeometry(Zone,Extent,Circular);
            const FVector Destination=Zone.GetLocation();
            if(Destination.ContainsNaN()) continue;
            float Farthest=0;
            for(const FVector Corner:{FVector(MinX,Bounds.Min.Y,0),FVector(MinX,Bounds.Max.Y,0),
                FVector(MaxX,Bounds.Min.Y,0),FVector(MaxX,Bounds.Max.Y,0)})
                Farthest=FMath::Max(Farthest,float(FVector::Dist2D(Corner,Destination)));
            BestExitMaximum=FMath::Min(BestExitMaximum,Farthest);
        }
        if(BestExitMaximum==MAX_flt) return MAX_flt;
        MaximumDistance=FMath::Max(MaximumDistance,BestExitMaximum);FoundTongue=true;
    }
    if(!FoundTongue) return MAX_flt;
    const float Efficiency=FMath::Clamp(Positive(Tuning.EffectiveWorkFraction,.84f),.1f,1.f);
    const float Pickup=Positive(Tuning.PickupPieceSeconds,.275f)/Efficiency;
    if(Row.Kind!=EMCFoodKind::Food)
        return Pickup+MaximumDistance*Positive(Tuning.PathFactor,1.25f)/Positive(Tuning.PepperCarrySpeed,288.57f)
            +Positive(Tuning.DeliveryTripSeconds,.4f);
    const int32 Pieces=FMath::Clamp(Row.Fragments,1,128);
    // Stuck items require cutting; ordinary entry variants conservatively do too.
    (void)bStuck;
    return CuttingWork(Row.Health,HardRow(RowName,Row),Tuning)+Pieces*Pickup+LooseTransportWork(Pieces,MaximumDistance,Tuning);
}

FMCFoodPipelineLoad MCMeasureFoodPipeline(UWorld* World,int32 Batch,const FMCFoodPipelineTuning& Tuning)
{
    FMCFoodPipelineLoad Out;
    if(!World || World->GetNetMode()==NM_Client) return Out;
    using namespace MCFoodDirectorPrivate;
    const float Efficiency=FMath::Clamp(Positive(Tuning.EffectiveWorkFraction,.84f),.1f,1.f);
    const float CarrySpeed=Positive(Tuning.CarrySpeed,340.f);
    const float Path=Positive(Tuning.PathFactor,1.25f);
    const float Pickup=Positive(Tuning.PickupPieceSeconds,.275f);
    const float Delivery=Positive(Tuning.DeliveryTripSeconds,.4f);
    const int32 Capacity=FMath::Clamp(Tuning.StackCapacity,1,64);
    int32 LoosePieces=0;
    double LooseDistance=0;
    TMap<TWeakObjectPtr<AMCToothCharacter>,float> CarrierDistances;
    for(TActorIterator<AMCFoodActor> It(World);It;++It)
    {
        const AMCFoodActor* Food=*It;
        if(Food->IsActorBeingDestroyed() || Food->bBrushTool || (Batch!=INDEX_NONE && Food->Batch!=Batch)) continue;
        if(Food->IsDisposed())
        {
            if(Food->FoodData.Kind==EMCFoodKind::Spicy && !Food->IsHazardResolved()) ++Out.UnresolvedHazards;
            continue;
        }
        ++Out.OutstandingActors;
        const bool Ordinary=Food->FoodData.Kind==EMCFoodKind::Food;
        if(Ordinary) {if(Food->bFragment) ++Out.Fragments;else ++Out.Whole;}
        if(Food->FoodData.Kind==EMCFoodKind::Spicy) ++Out.Spicy;
        if(Food->Phase==EMCFoodPhase::Stuck) ++Out.Stuck;
        if(Food->Phase==EMCFoodPhase::Swallowing) {++Out.Swallowing;continue;}

        const bool Carried=Food->StackCarrier || !Food->Holders.IsEmpty() || Food->Phase==EMCFoodPhase::Carried;
        if(Carried) ++Out.Carried;
        if(Ordinary && Food->StackCarrier)
        {
            AMCToothCharacter* Carrier=Food->StackCarrier.Get();
            const float Distance=DeliveryDistance(World,Carrier->GetActorLocation(),Food);
            float& Existing=CarrierDistances.FindOrAdd(TWeakObjectPtr<AMCToothCharacter>(Carrier));
            Existing=FMath::Max(Existing,Distance);
            continue;
        }
        const float Distance=DeliveryDistance(World,Food->WorkPosition(),Food);
        if(!Ordinary)
        {
            ++Out.EstimatedTrips;
            Out.EstimatedWorkerSeconds+=(Carried?0.f:Pickup/Efficiency)
                +Distance*Path/Positive(Tuning.PepperCarrySpeed,288.57f)+Delivery;
            continue;
        }
        // The same size limits guard CanCollect; small whole items need not be chopped.
        const bool NeedsCut=!Food->bFragment && (Food->Phase==EMCFoodPhase::Stuck
            || Food->Body->GetScaledBoxExtent().GetMax()>55.f || Food->Visual->Bounds.SphereRadius>85.f);
        const int32 Pieces=NeedsCut ? FMath::Clamp(Food->FoodData.Fragments,1,128) : 1;
        if(NeedsCut)
        {
            const float Health=FMath::IsFinite(Food->Health)?FMath::Max(0.f,Food->Health):Food->FoodData.Health;
            Out.EstimatedWorkerSeconds+=CuttingWork(Health,HardRow(Food->ItemName,Food->FoodData),Tuning);
        }
        LoosePieces+=Pieces;
        LooseDistance+=double(Distance)*Pieces;
        Out.EstimatedWorkerSeconds+=Pieces*Pickup/Efficiency;
    }
    if(LoosePieces>0)
    {
        const int32 Trips=FMath::DivideAndRoundUp(LoosePieces,Capacity);
        const float MeanDistance=float(LooseDistance/LoosePieces);
        Out.EstimatedTrips+=Trips;
        Out.EstimatedWorkerSeconds+=LooseTransportWork(LoosePieces,MeanDistance,Tuning);
    }
    for(const auto& Carrier:CarrierDistances)
    {
        ++Out.EstimatedTrips;
        Out.EstimatedWorkerSeconds+=Carrier.Value*Path/CarrySpeed+Delivery;
    }
    for(TActorIterator<AMCThroat> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed()) continue;
        Out.ThroatQueued+=FMath::Max(0,It->QueuedFoodCount);
        Out.bThroatBusy|=It->ThroatPhase!=EMCThroatPhase::Collecting || It->FoodInZone>0;
        // Reserve the upcoming airflow during gathering too, without refusing deliveries.
        Out.bThroatMovement|=It->IsAmbientSuctionActive() || It->ThroatPhase==EMCThroatPhase::Swallowing
            || (It->ThroatPhase==EMCThroatPhase::Anticipation && It->QueuedFoodCount>0);
    }
    return Out;
}
