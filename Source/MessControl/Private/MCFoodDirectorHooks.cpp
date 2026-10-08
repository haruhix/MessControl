#include "MCFoodDirectorHooks.h"

#include "MCDayPlan.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCFoodCollectionComponent.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCDayDirector.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace MCFoodDirectorPrivate
{
const FName ServedDirtTag(TEXT("MCFoodServedDirt"));
struct FWorker
{
    AMCToothCharacter* Hero=nullptr;
    int32 Capacity=1,Held=0;
    float Speed=340.f;
};
TArray<FWorker> Workers(UWorld* World)
{
    TArray<FWorker> Out;
    if(!World) return Out;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
        if(It->IsActorBeingDestroyed() || !It->Status || !It->Status->IsAlive() || !It->FoodCollection
            || !It->CanWork() || !It->ToothPhysics || !It->ToothPhysics->CanAct()) continue;
        auto& Worker=Out.AddDefaulted_GetRef();Worker.Hero=*It;
        Worker.Capacity=FMath::Clamp(It->FoodCollection->MaxPieces,1,8);
        Worker.Held=FMath::Clamp(It->FoodCollection->Pieces.Num(),0,Worker.Capacity);
        const auto* Move=It->GetCharacterMovement();
        const float Speed=Move?Move->GetMaxSpeed():340.f;
        Worker.Speed=FMath::IsFinite(Speed)?FMath::Clamp(Speed,20.f,1500.f):340.f;
    }
    Out.Sort([](const FWorker& A,const FWorker& B){return A.Hero->GetName()<B.Hero->GetName();});
    return Out;
}
bool CommitServedDirt(AMCFoodActor* Food,const UMCDayPlan* Plan,AMCDayDirector* Services)
{
    if(Food->FoodData.Kind!=EMCFoodKind::Food) return true;
    UWorld* World=Food->GetWorld();
    if(!IsValid(Services)) {
        Services=nullptr;
        for(TActorIterator<AMCDayDirector> It(World);It;++It) {
            if(It->IsActorBeingDestroyed() || It->Settings!=Plan) continue;
            // A caller must disambiguate simultaneous executors instead of dirtying a random one.
            if(Services) return false;
            Services=*It;
        }
    }
    if(!IsValid(Services) || !Services->HasAuthority() || Services->GetWorld()!=World || Services->Settings!=Plan) return false;
    TSet<AMCMouthSurface*> Before;
    for(TActorIterator<AMCMouthSurface> It(World);It;++It) Before.Add(*It);
    Services->AddDirt(false,1,Food->Batch,0,0.f);
    int32 Created=0;
    for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(!Before.Contains(*It) && !It->bUlcer && It->Batch==Food->Batch) {
        It->SetOwner(Food);It->Tags.AddUnique(ServedDirtTag);++Created;
    }
    return Created>0;
}
float Positive(float Value,float Default)
{
    return FMath::IsFinite(Value) && Value>0 ? Value : Default;
}

bool DeliveryMatches(const AMCFoodDisposal* Exit,EMCFoodKind Kind,bool WrongIngredient)
{
    // Pepper is discarded in the current loop; IsWrongIngredient deliberately
    // excludes spicy items, so its hazard kind must be routed explicitly.
    return Exit->bBrushBin==(Kind==EMCFoodKind::Spicy || WrongIngredient);
}

bool HardRow(FName Name,const FMCFoodRow& Row)
{
    if(Row.Resistance!=EMCFoodResistance::Automatic) return Row.Resistance==EMCFoodResistance::Hard;
    return Name==TEXT("Carrot") || Name==TEXT("Nut") || Name==TEXT("Crust") || Name==TEXT("Tartar");
}

float DestructionWork(float Health,bool Hard,const FMCFoodPipelineTuning& Tuning,
    float WholeHealth=0.f,UWorld* World=nullptr)
{
    const float Efficiency=FMath::Clamp(Positive(Tuning.EffectiveWorkFraction,.84f),.1f,1.f);
    const auto Team=Tuning.bUseLivePlayerStats?Workers(World):TArray<FWorker>();
    if(!Team.IsEmpty()) {
        float Work=0;
        for(const auto& Worker:Team) {
            const auto* Inventory=Worker.Hero->Inventory.Get();const auto* Profile=Inventory?Inventory->Profile.Get():nullptr;
            const float Damage=Hard?(Inventory && Inventory->HasUpgrade(EMCToolUpgrade::Buffer)?75.f:Profile?Profile->PickaxeDamage:Tuning.PickaxeDamage)
                :(Profile?Profile->KnifeDamage:Tuning.KnifeDamage);
            const int32 Hits=FMath::CeilToInt(FMath::Max(0.f,Health)/Positive(Damage,Hard?40.f:25.f));
            const float Swing=Positive(Hard?Tuning.PickaxeSwingSeconds:Tuning.KnifeSwingSeconds,Hard?.65f:.70f);
            // The last hit finishes work at contact; its remaining animation is not another food task.
            float Seconds=Hits>0?Swing*(Hard?.38f/1.05f:.28f/.70f)+(Hits-1)*Swing:0.f;
            if(Inventory && Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)) {
                // SawContact removes half the authored food health per contact, with a .45s per-food cooldown.
                // It accepts hard food too; estimate the worker's faster available valid tool.
                const int32 Contacts=FMath::CeilToInt(FMath::Max(0.f,Health)/FMath::Max(1.f,WholeHealth*.5f));
                Seconds=FMath::Min(Seconds,Contacts>0?.12f+(Contacts-1)*.45f:0.f);
            }
            Work+=Seconds/Efficiency;
        }
        return Work/Team.Num();
    }
    const float Damage=Positive(Hard?Tuning.PickaxeDamage:Tuning.KnifeDamage,Hard?40.f:25.f);
    const float Swing=Positive(Hard?Tuning.PickaxeSwingSeconds:Tuning.KnifeSwingSeconds,Hard?.65f:.70f);
    const int32 Hits=FMath::CeilToInt(FMath::Max(0.f,Health)/Damage);
    return (Hits>0?Swing*(Hard?.38f/1.05f:.28f/.70f)+(Hits-1)*Swing:0.f)/Efficiency;
}

float WorkerApproachWork(const FBox& FoodBounds,const FMCFoodPipelineTuning& Tuning,const FWorker& Worker)
{
    const auto* Inventory=Worker.Hero->Inventory.Get();
    const float Reach=Inventory && Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)?190.f:180.f;
    const FVector Position=Worker.Hero->GetActorLocation();
    const float Distance=FMath::Max(0.f,float(FVector::Dist2D(Position,FoodBounds.GetClosestPointTo(Position)))-Reach);
    return Distance*Positive(Tuning.PathFactor,1.25f)/Positive(Worker.Speed,340.f);
}
float ApproachWork(const FBox& FoodBounds,const FMCFoodPipelineTuning& Tuning,const TArray<FWorker>& Team)
{
    if(!FoodBounds.IsValid || Team.IsEmpty()) return 0.f;
    float Best=MAX_flt;
    for(const auto& Worker:Team) Best=FMath::Min(Best,WorkerApproachWork(FoodBounds,Tuning,Worker));
    return Best==MAX_flt?0.f:Best;
}

float DeliveryDistance(UWorld* World,FVector Point,const AMCFoodActor* Food)
{
    float Best=MAX_flt;
    for(TActorIterator<AMCFoodDisposal> It(World);It;++It)
    {
        // Pepper and foreign/spoiled ingredients use the discard route.
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
    int32 Batch,FRandomStream& Random,AMCDayDirector* DirtServices)
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
    if(!MCFoodDirectorPrivate::CommitServedDirt(Food,Plan,DirtServices)) {Food->Destroy();return nullptr;}
    return Food;
}

AMCFoodActor* MCSpawnDirectedStuckFood(UWorld* World,const UMCDayPlan* Plan,FName RowName,
    int32 Batch,FRandomStream& Random,AMCDayDirector* DirtServices)
{
    const bool bDiagnostic=FParse::Param(FCommandLine::Get(),TEXT("MCStuckFoodDiagnostic"));
    const auto Failed=[&](const TCHAR* Reason)->AMCFoodActor*
    {
        if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC FAIL reason=%s row=%s batch=%d plan=%s"),
            Reason,*RowName.ToString(),Batch,*GetPathNameSafe(Plan));
        return nullptr;
    };
    if(!World || World->GetNetMode()==NM_Client || !Plan || RowName.IsNone()) return Failed(TEXT("world_authority_plan_or_row_guard"));
    UDataTable* Menu=Plan->Menu.LoadSynchronous();
    const FMCFoodRow* Row=Menu?Menu->FindRow<FMCFoodRow>(RowName,TEXT("Directed stuck food"),false):nullptr;
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC ROW menu=%s row=%s found=%d kind=%d whole_meshes=%d scale=%s services=%s services_plan=%s"),
        *GetPathNameSafe(Menu),*RowName.ToString(),Row!=nullptr,Row?int32(Row->Kind):-1,Row?Row->WholeMeshes.Num():0,
        Row?*Row->Scale.ToString():TEXT("none"),*GetNameSafe(DirtServices),*GetPathNameSafe(IsValid(DirtServices)?DirtServices->Settings.Get():nullptr));
    if(!Row || Row->Kind!=EMCFoodKind::Food) return Failed(TEXT("menu_row_or_kind_guard"));
    TArray<AMCTongue*> Tongues;
    for(TActorIterator<AMCTongue> It(World);It;++It)
        if(It->Surface && It->Surface->IsRegistered() && !It->CurrentVertices().IsEmpty()) Tongues.Add(*It);
    if(Tongues.IsEmpty()) return Failed(TEXT("no_registered_tongue_surface"));
    AMCTongue* Tongue=Tongues[Random.RandRange(0,Tongues.Num()-1)];
    const FBox TongueBounds=Tongue->Surface->Bounds.GetBox();
    const float LeftChance=FMath::IsFinite(Tongue->GameplaySpawnLeftChance)
        ?FMath::Clamp(Tongue->GameplaySpawnLeftChance,0.f,1.f):.30f;
    const int32 Zone=Random.FRand()<LeftChance?0:1;
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC TONGUE name=%s bounds_min=%s bounds_max=%s selected_zone=%d left_chance=%.3f near_depth=%.3f split_depth=%.3f far_depth=%.3f"),
        *Tongue->GetName(),*TongueBounds.Min.ToString(),*TongueBounds.Max.ToString(),Zone,LeftChance,
        Tongue->GameplaySpawnNearDepth,Tongue->GameplaySpawnSplitDepth,Tongue->GameplaySpawnFarDepth);
    TArray<AMCArenaTooth*> Teeth;
    for(TActorIterator<AMCArenaTooth> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && It->IsAvailable() && It->Body->IsRegistered()) Teeth.Add(*It);
    if(Teeth.IsEmpty()) return Failed(TEXT("no_available_registered_tooth"));
    for(int32 I=Teeth.Num()-1;I>0;--I) Teeth.Swap(I,Random.RandRange(0,I));

    FTransform Pose(FRotator(0,Random.FRandRange(-180.f,180.f),0),FVector::ZeroVector);
    AMCFoodActor* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,
        nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Food) return Failed(TEXT("deferred_spawn_failed"));
    Food->ConfigureItem(RowName,*Row,Random);
    bool AuthoredMesh=false;
    for(const auto& Mesh:Row->WholeMeshes) AuthoredMesh|=Mesh.Get()==Food->ItemMesh.Get() && Food->ItemMesh!=nullptr;
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC MESH path=%s authored=%d body_registered=%d"),
        *GetPathNameSafe(Food->ItemMesh.Get()),AuthoredMesh,Food->Body->IsRegistered());
    if(!AuthoredMesh) {Food->Destroy();return Failed(TEXT("authored_mesh_guard"));}
    const FVector Extent=Food->Body->GetScaledBoxExtent();
    const float Margin=Extent.Size2D()+20.f;
    // Diagnostic-only copy of GameplaySpawnFootprint's outer-strip calculation.
    // It classifies a rejected pose; it never replaces the authoritative query.
    double DiagnosticNear=0,DiagnosticFar=0;
    if(bDiagnostic)
    {
        const auto Finite=[](float Value,float Default) {return FMath::IsFinite(Value)?Value:Default;};
        const double NearDepth=FMath::Clamp(double(Finite(Tongue->GameplaySpawnNearDepth,.18f)),0.,.95);
        const double FarDepth=FMath::Clamp(double(Finite(Tongue->GameplaySpawnFarDepth,.82f)),NearDepth+.05,1.);
        DiagnosticNear=TongueBounds.Max.X-TongueBounds.GetSize().X*NearDepth;
        DiagnosticFar=TongueBounds.Max.X-TongueBounds.GetSize().X*FarDepth;
    }
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC FOOD extents=%s margin=%.3f candidates_teeth=%d"),
        *Extent.ToString(),Margin,Teeth.Num());
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC FOOTPRINT_LIMITS far=%.3f near=%.3f center_min_x=%.3f center_max_x=%.3f"),
        DiagnosticFar,DiagnosticNear,DiagnosticFar+Margin,DiagnosticNear-Margin);
    const float InitialYaw=Pose.Rotator().Yaw;
    const float CornerYaw=FMath::RadiansToDegrees(FMath::Atan2(Extent.Y,Extent.X));
    AMCArenaTooth* Anchor=nullptr;FVector Inward=FVector::ZeroVector;
    bool bBrushCapUsed=false;int32 SelectedContactPass=INDEX_NONE;
    int32 Candidates=0,ZeroDirection=0,WrongZone=0,NoFootprint=0,NoContact=0,OccupiedCount=0,SupportedCount=0;
    int32 OutsideStrip=0,OutsideTissue=0,DeliveryRejected=0;
    // Preserve the original two-row search first. Curved, wider authored arenas
    // also need corner contacts directed diagonally toward the tongue centre;
    // those poses still satisfy the same band, full footprint and contact tests.
    // Preserve the original overlap first, then search shallower but still
    // intersecting jams when a large authored mesh has little rim clearance.
    // Exhaust all ordinary poses before allowing a tooth-anchored jam in the
    // brush exit cap. Its native delivery guards ignore Stuck food; the throat
    // cap, tissue footprint and forbidden outer strips remain excluded.
    for(int32 ContactPass=0;ContactPass<7 && !Anchor;++ContactPass)
    for(AMCArenaTooth* Tooth:Teeth)
    {
        const bool bAllowBrushCap=ContactPass>=4;
        const TCHAR* QueryPolicy=bAllowBrushCap?TEXT("stuck_brush_cap"):TEXT("ordinary");
        const int32 DepthPass=bAllowBrushCap?ContactPass-3:ContactPass;
        const float JamDepth=DepthPass<2?22.f:DepthPass==2?12.f:4.f;
        const FVector Center=Tooth->Body->Bounds.Origin;
        const FVector TowardTongue=(TongueBounds.GetCenter()-Center).GetSafeNormal2D();
        const float TargetDistance=FMath::Max(float(FVector::Dist2D(Center,TongueBounds.GetCenter())),
            float(Tooth->Body->Bounds.BoxExtent.Size2D()*3.f));
        const int32 BeforeCandidates=Candidates,BeforeWrongZone=WrongZone,BeforeNoFootprint=NoFootprint,
            BeforeNoContact=NoContact,BeforeOccupied=OccupiedCount,BeforeSupported=SupportedCount,
            BeforeOutsideStrip=OutsideStrip,BeforeOutsideTissue=OutsideTissue,BeforeDelivery=DeliveryRejected;
        bool bLoggedFootprint=false,bLoggedContact=false,bLoggedDelivery=false;
        if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC TOOTH pass=%d query_policy=%s jam_depth=%.1f name=%s bounds_origin=%s bounds_extent=%s local_extent=%s transform=%s"),
            ContactPass,QueryPolicy,JamDepth,*Tooth->GetName(),*Center.ToString(),*Tooth->Body->Bounds.BoxExtent.ToString(),
            *Tooth->Body->GetUnscaledBoxExtent().ToString(),*Tooth->Body->GetComponentTransform().ToString());
        // The fallback aligns the actual box corner with each contact ray. This
        // reaches its physical diagonal exactly, without treating an arbitrary
        // yaw's support projection as its radial reach. Refine the inward arc to
        // one degree for narrow legal intervals along the authored tongue rim.
        const int32 SampleCount=ContactPass==0?13:141;
        const int32 FirstSample=Random.RandRange(0,SampleCount-1);
        const int32 OrientationCount=ContactPass==0?12:1;
        for(int32 Orientation=0;Orientation<OrientationCount;++Orientation)
        {
            // Keep the configured variant and band; only its legal contact pose changes.
            if(ContactPass==0)
            {
                Pose.SetRotation(FRotator(0,InitialYaw+Orientation*30.f,0).Quaternion());
                Food->SetActorRotation(Pose.GetRotation());
            }
            for(int32 Sample=0;Sample<SampleCount;++Sample)
            {
                if(bDiagnostic) ++Candidates;
                const int32 SampleIndex=(FirstSample+Sample)%SampleCount;
                const float Offset=-.9f+.15f*SampleIndex;
                const FVector Target=ContactPass==0
                    ?FVector(Center.X+Offset*Tooth->Body->Bounds.BoxExtent.X,TongueBounds.GetCenter().Y,Center.Z)
                    :Center+TowardTongue.RotateAngleAxis(-70.f+SampleIndex,FVector::UpVector)*TargetDistance;
                // Probe the actual tooth box toward the tissue, including the inner arch corners.
                const FVector Contact=MCFoodDirectorPrivate::ToothBoxContact(Tooth->Body,Target);
                const FVector Direction=(Target-Contact).GetSafeNormal2D();
                if(Direction.IsNearlyZero()) {if(bDiagnostic) ++ZeroDirection;continue;}
                if(ContactPass>0)
                {
                    Pose.SetRotation(FRotator(0,Direction.Rotation().Yaw-CornerYaw,0).Quaternion());
                    Food->SetActorRotation(Pose.GetRotation());
                }
                const float Support=FMath::Abs(FVector::DotProduct(Food->GetActorForwardVector(),Direction))*Extent.X
                    +FMath::Abs(FVector::DotProduct(Food->GetActorRightVector(),Direction))*Extent.Y;
                const FVector Candidate=Contact+Direction*(Support-FMath::Min(JamDepth,Support*.35f));
                FHitResult Floor;
                if(Tongue->GameplaySpawnZone(Candidate)!=Zone) {if(bDiagnostic) ++WrongZone;continue;}
                const bool bFootprint=bAllowBrushCap?Tongue->GameplayStuckFoodFootprint(Candidate,Margin,Floor)
                    :Tongue->GameplaySpawnFootprint(Candidate,Margin,Floor);
                if(!bFootprint)
                {
                    if(bDiagnostic)
                    {
                        ++NoFootprint;
                        const bool bBandFootprint=FMath::IsFinite(Margin) && Margin>=0
                            && Candidate.X+Margin<=DiagnosticNear && Candidate.X-Margin>=DiagnosticFar;
                        FHitResult Interior;
                        const bool bInterior=bBandFootprint && Tongue->InteriorSurfacePoint(Candidate,Margin,Interior);
                        if(!bBandFootprint) ++OutsideStrip;
                        else if(!bInterior) ++OutsideTissue;
                        else
                        {
                            // With valid band + interior tissue, the selected
                            // footprint query's final false return is a delivery lane.
                            ++DeliveryRejected;
                            if(!bLoggedDelivery)
                            {
                                UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC DELIVERY_REJECT pass=%d query_policy=%s jam_depth=%.1f tooth=%s candidate=%s floor=%s margin=%.3f far=%.3f near=%.3f"),
                                    ContactPass,QueryPolicy,JamDepth,*Tooth->GetName(),*Candidate.ToString(),*Interior.ImpactPoint.ToString(),Margin,DiagnosticFar,DiagnosticNear);
                                bLoggedDelivery=true;
                            }
                        }
                        if(!bLoggedFootprint)
                        {
                            FHitResult Surface,UnrestrictedInterior;
                            const bool bSurface=Tongue->SurfacePoint(Candidate,Surface);
                            const bool bTissue=Tongue->InteriorSurfacePoint(Candidate,Margin,UnrestrictedInterior);
                            UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC FOOTPRINT_REJECT pass=%d query_policy=%s jam_depth=%.1f tooth=%s candidate=%s contact=%s margin=%.3f surface_hit=%d surface=%s normal_z=%.3f band_pass=%d interior=%d far_gap=%.3f near_gap=%.3f"),
                                ContactPass,QueryPolicy,JamDepth,*Tooth->GetName(),*Candidate.ToString(),*Contact.ToString(),Margin,bSurface,*Surface.ImpactPoint.ToString(),Surface.ImpactNormal.Z,bBandFootprint,bTissue,
                                Candidate.X-Margin-DiagnosticFar,DiagnosticNear-Candidate.X-Margin);
                            bLoggedFootprint=true;
                        }
                    }
                    continue;
                }
                if(bDiagnostic) ++SupportedCount;
                const FVector Position=Floor.ImpactPoint+FVector(0,0,Extent.Z+5.f);
                const FVector Nearest=MCFoodDirectorPrivate::ToothBoxContact(Tooth->Body,Position);
                const FVector LocalContact=Pose.GetRotation().UnrotateVector(Nearest-Position).GetAbs();
                // Floor-supported height must still intersect the tooth, rather than float above it.
                if(LocalContact.X>Extent.X+1 || LocalContact.Y>Extent.Y+1 || LocalContact.Z>Extent.Z+1)
                {
                    if(bDiagnostic)
                    {
                        ++NoContact;
                        if(!bLoggedContact)
                        {
                            UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC CONTACT_REJECT pass=%d tooth=%s position=%s floor=%s nearest=%s local_contact=%s food_extent=%s gap=%s"),
                                ContactPass,*Tooth->GetName(),*Position.ToString(),*Floor.ImpactPoint.ToString(),*Nearest.ToString(),
                                *LocalContact.ToString(),*Extent.ToString(),*(LocalContact-Extent).ToString());
                            bLoggedContact=true;
                        }
                    }
                    continue;
                }
                bool Occupied=false;
                for(TActorIterator<AMCFoodActor> It(World);It;++It)
                {
                    if(*It==Food || It->IsDisposed() || It->bBrushTool || It->IsActorBeingDestroyed()) continue;
                    const FVector OtherExtent=It->Body->GetScaledBoxExtent();
                    if(FMath::Abs(It->GetActorLocation().Z-Position.Z)>Extent.Z+OtherExtent.Z+20) continue;
                    if(FVector::DistSquared2D(It->GetActorLocation(),Position)<FMath::Square(Margin+OtherExtent.Size2D()+20.f))
                        {Occupied=true;break;}
                }
                if(Occupied) {if(bDiagnostic) ++OccupiedCount;continue;}
                Pose.SetLocation(Position);Anchor=Tooth;Inward=Direction;SelectedContactPass=ContactPass;
                if(bAllowBrushCap)
                {
                    // Both queries use the same bands and tissue test. A normal
                    // refusal here proves the chosen pose needed only the brush exemption.
                    FHitResult OrdinaryFloor;
                    bBrushCapUsed=!Tongue->GameplaySpawnFootprint(Candidate,Margin,OrdinaryFloor);
                }
                break;
            }
            if(Anchor) break;
        }
        if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC TOOTH_RESULT pass=%d query_policy=%s name=%s candidates=%d wrong_zone=%d no_footprint=%d outside_strip=%d outside_tissue=%d delivery_rejected=%d supported=%d no_contact=%d occupied=%d selected=%d"),
            ContactPass,QueryPolicy,*Tooth->GetName(),Candidates-BeforeCandidates,WrongZone-BeforeWrongZone,NoFootprint-BeforeNoFootprint,
            OutsideStrip-BeforeOutsideStrip,OutsideTissue-BeforeOutsideTissue,DeliveryRejected-BeforeDelivery,
            SupportedCount-BeforeSupported,NoContact-BeforeNoContact,OccupiedCount-BeforeOccupied,Anchor==Tooth);
        if(Anchor) break;
    }
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC SEARCH candidates=%d zero_direction=%d wrong_zone=%d no_footprint=%d outside_strip=%d outside_tissue=%d delivery_rejected=%d supported=%d no_contact=%d occupied=%d anchor=%s"),
        Candidates,ZeroDirection,WrongZone,NoFootprint,OutsideStrip,OutsideTissue,DeliveryRejected,SupportedCount,NoContact,OccupiedCount,*GetNameSafe(Anchor));
    if(!Anchor) {Food->Destroy();return Failed(TEXT("no_legal_tooth_contact"));}
    Food->Batch=Batch;Food->Initialize(true,Inward);
    Food->Phase=EMCFoodPhase::Stuck;Food->StuckTooth=Anchor;
    UGameplayStatics::FinishSpawningActor(Food,Pose);
    if(!IsValid(Food) || Food->IsActorBeingDestroyed()) return Failed(TEXT("finish_spawning_failed"));
    Food->Body->SetSimulatePhysics(false);Food->ForceNetUpdate();
    if(!MCFoodDirectorPrivate::CommitServedDirt(Food,Plan,DirtServices)) {Food->Destroy();return Failed(TEXT("served_dirt_commit_failed"));}
    if(bDiagnostic) UE_LOG(LogTemp,Display,TEXT("MC_STUCK_DIAGNOSTIC SUCCESS food=%s tooth=%s position=%s zone=%d pass=%d query_policy=%s brush_cap_used=%d"),
        *Food->GetName(),*Anchor->GetName(),*Food->GetActorLocation().ToString(),Zone,SelectedContactPass,
        SelectedContactPass>=4?TEXT("stuck_brush_cap"):TEXT("ordinary"),bBrushCapUsed);
    return Food;
}

int32 MCCountDirectedFoodDirt(UWorld* World,int32 Batch)
{
    int32 Count=0;if(!World) return Count;
    for(TActorIterator<AMCMouthSurface> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && It->Batch==Batch && It->Tags.Contains(MCFoodDirectorPrivate::ServedDirtTag)
            && !It->bUlcer && !It->IsClean()) ++Count;
    return Count;
}
void MCDestroyDirectedFoodDirt(const AMCFoodActor* Food)
{
    if(!Food || !Food->HasAuthority()) return;
    for(TActorIterator<AMCMouthSurface> It(Food->GetWorld());It;++It)
        if(It->GetOwner()==Food && It->Tags.Contains(MCFoodDirectorPrivate::ServedDirtTag)) It->Destroy();
}
void MCDestroyDirectedFoodDirt(UWorld* World,int32 Batch)
{
    if(!World || World->GetNetMode()==NM_Client) return;
    for(TActorIterator<AMCMouthSurface> It(World);It;++It)
        if(It->Batch==Batch && It->Tags.Contains(MCFoodDirectorPrivate::ServedDirtTag)) It->Destroy();
}
FMCFoodPipelineTuning MCResolveFoodPipelineTuning(UWorld* World,const FMCFoodPipelineTuning& Base)
{
    auto Out=Base;if(!Base.bUseLivePlayerStats) return Out;
    const auto Team=MCFoodDirectorPrivate::Workers(World);if(Team.IsEmpty()) return Out;
    float Knife=0,Pickaxe=0,Speed=0,Cleaning=0;int32 Capacity=0;
    for(const auto& Worker:Team) {
        const auto* Inventory=Worker.Hero->Inventory.Get();const auto* Profile=Inventory?Inventory->Profile.Get():nullptr;
        const float BasePickaxe=Profile?Profile->PickaxeDamage:Base.PickaxeDamage;
        const float BaseKnife=Profile?Profile->KnifeDamage:Base.KnifeDamage;
        Knife+=Inventory && Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)?2.f*FMath::Max(1.f,BasePickaxe):BaseKnife;
        Pickaxe+=Inventory && Inventory->HasUpgrade(EMCToolUpgrade::Buffer)?75.f:BasePickaxe;
        Cleaning+=Inventory?Inventory->CleaningSpeedMultiplier():1.f;
        Speed+=Worker.Speed;Capacity+=Worker.Capacity;
    }
    Out.KnifeDamage=Knife/Team.Num();Out.PickaxeDamage=Pickaxe/Team.Num();
    Out.CarrySpeed=Speed/Team.Num();Out.PepperCarrySpeed=Out.CarrySpeed*Base.PepperCarrySpeed/FMath::Max(1.f,Base.CarrySpeed);
    Out.CleaningSpeedMultiplier=Cleaning/Team.Num();Out.StackCapacity=FMath::Max(1,FMath::RoundToInt(float(Capacity)/Team.Num()));
    return Out;
}

float MCForecastDirectedFoodWork(UWorld* World,const UMCDayPlan* Plan,FName RowName,bool bStuck,
    const FMCFoodPipelineTuning& BaseTuning,float CleaningWorkerSeconds)
{
    if(!World || World->GetNetMode()==NM_Client || !Plan || RowName.IsNone()) return MAX_flt;
    const UDataTable* Menu=Plan->Menu.LoadSynchronous();
    const FMCFoodRow* SavedRow=Menu?Menu->FindRow<FMCFoodRow>(RowName,TEXT("Directed food forecast"),false):nullptr;
    if(!SavedRow) return MAX_flt;
    const auto Tuning=MCResolveFoodPipelineTuning(World,BaseTuning);
    FMCFoodRow Row=*SavedRow;Row.Sanitize();
    using namespace MCFoodDirectorPrivate;
    const auto Team=BaseTuning.bUseLivePlayerStats?Workers(World):TArray<FWorker>();
    // A footprint estimate only: loaded authored meshes take precedence over the row fallback.
    // Use the smaller horizontal half-extent so an arbitrary entry yaw cannot make the
    // forecast assume contact through the long side of a thin food variant.
    float FoodHalfSize=MAX_flt;
    for(const auto& Mesh:Row.WholeMeshes) if(const UStaticMesh* Loaded=Mesh.Get()) {
        const FVector Extent=Loaded->GetBounds().BoxExtent*Row.Scale.GetAbs();
        FoodHalfSize=FMath::Min(FoodHalfSize,float(FMath::Min(Extent.X,Extent.Y)));
    }
    if(FoodHalfSize==MAX_flt) {
        const FVector Extent=Row.HalfExtent*Row.Scale.GetAbs();
        FoodHalfSize=float(FMath::Min(Extent.X,Extent.Y));
    }
    FoodHalfSize=FMath::IsFinite(FoodHalfSize)?FMath::Max(0.f,FoodHalfSize):0.f;
    float MaximumDistance=0,MaximumApproach=0;bool FoundTongue=false;
    for(TActorIterator<AMCTongue> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed() || !It->Surface || !It->Surface->IsRegistered() || It->CurrentVertices().IsEmpty()) continue;
        FBox Bounds=It->Surface->Bounds.GetBox();
        if(!Bounds.IsValid || Bounds.GetSize().X<=KINDA_SMALL_NUMBER || Bounds.GetSize().Y<=KINDA_SMALL_NUMBER) continue;
        const double Near=FMath::Clamp(double(FMath::IsFinite(It->GameplaySpawnNearDepth)?It->GameplaySpawnNearDepth:.18f),0.,.95);
        const double Far=FMath::Clamp(double(FMath::IsFinite(It->GameplaySpawnFarDepth)?It->GameplaySpawnFarDepth:.82f),Near+.05,1.);
        const double MaxX=Bounds.Max.X-Bounds.GetSize().X*Near;
        const double MinX=Bounds.Max.X-Bounds.GetSize().X*Far;
        if(Row.Kind==EMCFoodKind::Food) {
            // Ordinary work ends at the item, not at a throat/discard zone. One available
            // worker approaches it; additional workers are accounted for by the caller.
            // Bound each worker's entire rectangle first, then choose the best worker;
            // choosing a different worker per corner would miss a gap between them.
            float BestWorkerBound=MAX_flt;
            for(const auto& Worker:Team) {
                float Worst=0;
                for(const FVector Corner:{FVector(MinX,Bounds.Min.Y,Bounds.Max.Z),FVector(MinX,Bounds.Max.Y,Bounds.Max.Z),
                    FVector(MaxX,Bounds.Min.Y,Bounds.Max.Z),FVector(MaxX,Bounds.Max.Y,Bounds.Max.Z)})
                    Worst=FMath::Max(Worst,WorkerApproachWork(FBox(Corner-FVector(FoodHalfSize,FoodHalfSize,0),
                        Corner+FVector(FoodHalfSize,FoodHalfSize,0)),Tuning,Worker));
                BestWorkerBound=FMath::Min(BestWorkerBound,Worst);
            }
            if(BestWorkerBound!=MAX_flt) MaximumApproach=FMath::Max(MaximumApproach,BestWorkerBound);
            FoundTongue=true;continue;
        }
        // The legal footprint is a subset of this rectangle. For each valid exit,
        // its farthest rectangle corner bounds every delivery distance. Taking
        // the best matching discard exit retains that upper bound.
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
    // A hit also dislodges stuck ordinary food; both variants finish through HP destruction.
    (void)bStuck;
    return DestructionWork(Row.Health,HardRow(RowName,Row),Tuning,Row.Health,World)+MaximumApproach
        +Positive(CleaningWorkerSeconds,8.f)/Positive(Tuning.CleaningSpeedMultiplier,1.f);
}

FMCFoodPipelineLoad MCMeasureFoodPipeline(UWorld* World,int32 Batch,const FMCFoodPipelineTuning& BaseTuning)
{
    FMCFoodPipelineLoad Out;
    if(!World || World->GetNetMode()==NM_Client) return Out;
    const auto Tuning=MCResolveFoodPipelineTuning(World,BaseTuning);
    using namespace MCFoodDirectorPrivate;
    const float Efficiency=FMath::Clamp(Positive(Tuning.EffectiveWorkFraction,.84f),.1f,1.f);
    const float Path=Positive(Tuning.PathFactor,1.25f);
    const float Pickup=Positive(Tuning.PickupPieceSeconds,.275f);
    const float Delivery=Positive(Tuning.DeliveryTripSeconds,.4f);
    const auto Team=BaseTuning.bUseLivePlayerStats?Workers(World):TArray<FWorker>();
    for(const auto& Worker:Team) {
        Out.TeamStackCapacity+=Worker.Capacity;Out.FreeStackSlots+=Worker.Capacity-Worker.Held;
        Out.FullStacks+=Worker.Held>=Worker.Capacity;
    }
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
        if(!Ordinary)
        {
            const float Distance=DeliveryDistance(World,Food->WorkPosition(),Food);
            ++Out.EstimatedTrips;
            Out.EstimatedWorkerSeconds+=(Carried?0.f:Pickup/Efficiency)
                +Distance*Path/Positive(Tuning.PepperCarrySpeed,288.57f)+Delivery;
            continue;
        }
        // Existing absorption is an automatic alternative; it does not reserve a
        // second worker to destroy the same ingredient while that action finishes.
        if(Food->Phase==EMCFoodPhase::Absorbing) continue;
        const float Health=FMath::IsFinite(Food->Health)?FMath::Max(0.f,Food->Health):Food->FoodData.Health;
        Out.EstimatedWorkerSeconds+=DestructionWork(Health,HardRow(Food->ItemName,Food->FoodData),Tuning,Food->FoodData.Health,World);
        // Entry flight itself needs no work. Estimate contact at its actual intended
        // landing while preserving the authored visible size and orientation.
        const FBox VisibleBounds=Food->Visual->Bounds.GetBox().ShiftBy(Food->WorkPosition()-Food->GetActorLocation());
        Out.EstimatedWorkerSeconds+=ApproachWork(VisibleBounds,Tuning,Team);
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
