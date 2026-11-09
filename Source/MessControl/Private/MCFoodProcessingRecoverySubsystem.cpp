#include "MCFoodProcessingRecoverySubsystem.h"
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCFoodCutGeometry.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "KismetProceduralMeshLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr int32 MaxRecoveryAttempts=3,MaxCandidates=8;
constexpr double RetrySeconds=1.,EndpointScanSeconds=.5;
const TCHAR* FailureName(EMCFoodProcessingFailure Reason)
{
    switch(Reason){case EMCFoodProcessingFailure::UnsupportedGeometry:return TEXT("unsupported_geometry");case EMCFoodProcessingFailure::InvalidGeometry:return TEXT("invalid_geometry");case EMCFoodProcessingFailure::CollisionPreparation:return TEXT("collision_preparation");case EMCFoodProcessingFailure::MissingDestination:return TEXT("missing_destination");default:return TEXT("spawn_failure");}
}
bool ResidentMeshUsable(const UStaticMesh* Mesh)
{
    if(!IsValid(Mesh)||!Mesh->bAllowCPUAccess)return false;
    const auto* Data=Mesh->GetRenderData();
    return Data&&!Data->LODResources.IsEmpty()&&!Data->LODResources[0].Sections.IsEmpty()
        &&Data->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices()>=3&&Data->LODResources[0].IndexBuffer.GetNumIndices()>=3;
}
bool FreshEndpoint(const AMCThroat* Throat)
{
    return IsValid(Throat)&&!Throat->IsActorBeingDestroyed()&&!Throat->GetActorTransform().ContainsNaN()
        &&Throat->GateSize.X>0&&Throat->GateSize.Y>0&&!Throat->VacuumInlet().ContainsNaN();
}
bool SpoiledEndpoint(const AMCFoodDisposal* Exit)
{
    if(!IsValid(Exit)||Exit->IsActorBeingDestroyed()||!Exit->bBrushBin)return false;
    FTransform Pose;FVector Extent;bool Circular=false;Exit->GetDeliveryZoneGeometry(Pose,Extent,Circular);
    return !Pose.ContainsNaN()&&!Extent.ContainsNaN()&&Extent.GetMin()>0&&!Exit->GetDeliveryDirection().IsNearlyZero();
}
struct FEndpointValue
{
    FProperty* Property=nullptr;
    void* Data=nullptr;
    TStrongObjectPtr<UObject> Asset;
    ~FEndpointValue(){if(Data){Property->DestroyValue(Data);FMemory::Free(Data);}}
};
struct FEndpointRecipe
{
    TStrongObjectPtr<UClass> Class;
    TArray<TUniquePtr<FEndpointValue>> Values;
    TWeakObjectPtr<AMCFoodDisposal> Original;
    TWeakObjectPtr<AActor> Owner;
    FTransform Pose=FTransform::Identity,VolumePose=FTransform::Identity;
    FVector Extent=FVector::ZeroVector;
    bool bFresh=false;
    FTransform ZonePose=FTransform::Identity;
    FVector ZoneExtent=FVector::ZeroVector,Inlet=FVector::ZeroVector;
    bool bCircular=false;
    int32 FoodSwallowed=0,MealSequence=0,SwallowCount=0,SpasmCount=0,VomitCount=0;
    void CaptureCompleted(AMCFoodDisposal* Endpoint){if(auto* T=Cast<AMCThroat>(Endpoint)){FoodSwallowed=T->FoodSwallowed;MealSequence=T->MealSequence;SwallowCount=T->SwallowCount;SpasmCount=T->SpasmCount;VomitCount=T->VomitCount;}}
    void ApplyCompleted(AMCFoodDisposal* Endpoint)const{if(auto* T=Cast<AMCThroat>(Endpoint)){T->FoodSwallowed=FoodSwallowed;T->MealSequence=MealSequence;T->SwallowCount=SwallowCount;T->SpasmCount=SpasmCount;T->VomitCount=VomitCount;}}
    bool Capture(AMCFoodDisposal* Endpoint,bool Fresh)
    {
        if(!Endpoint||!Endpoint->Volume)return false;
        Class.Reset(Endpoint->GetClass());Values.Reset();Original=Endpoint;Owner=Endpoint->GetOwner();bFresh=Fresh;CaptureCompleted(Endpoint);
        Pose=Endpoint->GetActorTransform();VolumePose=Endpoint->Volume->GetRelativeTransform();Extent=Endpoint->Volume->GetUnscaledBoxExtent();
        Endpoint->GetDeliveryZoneGeometry(ZonePose,ZoneExtent,bCircular);if(auto* Throat=Cast<AMCThroat>(Endpoint))Inlet=Throat->VacuumInlet();
        for(TFieldIterator<FProperty> It(Endpoint->GetClass());It;++It){
            FProperty* P=*It;
            if(!P->HasAnyPropertyFlags(CPF_Edit)||P->HasAnyPropertyFlags(CPF_Transient|CPF_EditorOnly|CPF_Deprecated|CPF_EditConst|CPF_DisableEditOnInstance|CPF_InstancedReference|CPF_ContainsInstancedReference))continue;
            auto* Object=CastField<FObjectPropertyBase>(P);
            TArray<const FStructProperty*> Seen;
            // Never retain an opaque nested owner/component reference. Known
            // endpoint geometry/assets are scalar structs and direct assets.
            if((P->ContainsObjectReference(Seen)||P->ContainsObjectReference(Seen,EPropertyObjectReferenceType::Weak))&&!Object)continue;
            UObject* Reference=Object?Object->GetObjectPropertyValue_InContainer(Endpoint):nullptr;
            if(Object&&(Object->PropertyClass->IsChildOf(AActor::StaticClass())||Object->PropertyClass->IsChildOf(UActorComponent::StaticClass())||Object->PropertyClass->IsChildOf(UWorld::StaticClass())))continue;
            if(Reference&&(Reference->IsA<AActor>()||Reference->IsA<UActorComponent>()||Reference->IsA<UWorld>()))continue;
            auto Value=MakeUnique<FEndpointValue>();Value->Property=P;Value->Data=FMemory::Malloc(P->GetSize(),P->GetMinAlignment());
            P->InitializeValue(Value->Data);P->CopyCompleteValue(Value->Data,P->ContainerPtrToValuePtr<void>(Endpoint));
            if(Reference)Value->Asset.Reset(Reference);
            Values.Add(MoveTemp(Value));
        }
        return !Pose.ContainsNaN()&&!VolumePose.ContainsNaN()&&!Extent.ContainsNaN()&&Extent.GetMin()>0;
    }
    AMCFoodDisposal* Restore(UWorld* World)
    {
        if(!Class.IsValid()||!World)return nullptr;
        auto* Previous=Original.Get();auto* PreviousForBinding=Original.GetEvenIfUnreachable();
        if(IsValid(Previous))CaptureCompleted(Previous);
        FActorSpawnParameters Params;Params.bDeferConstruction=true;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Endpoint=World->SpawnActor<AMCFoodDisposal>(Class.Get(),Pose,Params);if(!Endpoint)return nullptr;
        Endpoint->SetOwner(Owner.Get());
        for(const auto& Value:Values)Value->Property->CopyCompleteValue(Value->Property->ContainerPtrToValuePtr<void>(Endpoint),Value->Data);
        Endpoint->Volume->SetRelativeTransform(VolumePose);Endpoint->Volume->SetBoxExtent(Extent);
        Endpoint->FinishSpawning(Pose);
        // Construction may normalize the box; retain the captured authored volume.
        Endpoint->Volume->SetRelativeTransform(VolumePose);Endpoint->Volume->SetBoxExtent(Extent);
        FTransform ActualZone;FVector ActualExtent;bool ActualCircular=false;Endpoint->GetDeliveryZoneGeometry(ActualZone,ActualExtent,ActualCircular);
        const bool GeometryMatches=ActualZone.Equals(ZonePose,.01)&&ActualExtent.Equals(ZoneExtent,.01)&&ActualCircular==bCircular
            &&(!bFresh||(Cast<AMCThroat>(Endpoint)&&Cast<AMCThroat>(Endpoint)->VacuumInlet().Equals(Inlet,1.)));
        if(!GeometryMatches||(bFresh?!FreshEndpoint(Cast<AMCThroat>(Endpoint)):!SpoiledEndpoint(Endpoint))){Endpoint->Destroy();return nullptr;}
        if(IsValid(Previous)&&!Previous->IsActorBeingDestroyed()&&(bFresh?!FreshEndpoint(Cast<AMCThroat>(Previous)):!SpoiledEndpoint(Previous)))Previous->Destroy();
        if(bFresh)if(auto* Mode=World->GetAuthGameMode<AMCGameMode>())if(auto* Reference=FindFProperty<FObjectPropertyBase>(Mode->GetClass(),TEXT("Throat"))){
            UObject* Existing=Reference->GetObjectPropertyValue_InContainer(Mode);
            if(Existing==PreviousForBinding||!IsValid(Existing))Reference->SetObjectPropertyValue_InContainer(Mode,Endpoint);
        }
        ApplyCompleted(Endpoint);Original=Endpoint;Endpoint->ForceNetUpdate();return Endpoint;
    }
};
struct FRecoveryRecord
{
    TWeakObjectPtr<AMCFoodActor> Food;
    TWeakObjectPtr<UStaticMesh> Mesh;
    TArray<FMCFoodCutPlane> History;
    FGuid Epoch;
    TWeakObjectPtr<AMCGameState> GameState;
    int32 Day=0;
    FName ItemName;
    FSoftObjectPath Profile;
    FVector Scale;
    EMCFoodProcessingFailure Reason=EMCFoodProcessingFailure::InvalidGeometry;
    double ReportedAt=0,NextAttemptAt=0;
    int32 Attempts=0,CandidateCursor=0;
    uint32 ResourceRevision=0;
};
bool SameHistory(const TArray<FMCFoodCutPlane>& A,const TArray<FMCFoodCutPlane>& B)
{
    if(A.Num()!=B.Num())return false;
    for(int32 I=0;I<A.Num();++I)if(A[I].bEnabled!=B[I].bEnabled||A[I].IslandIndex!=B[I].IslandIndex||A[I].Position!=B[I].Position||A[I].Normal!=B[I].Normal)return false;
    return true;
}
uint32 ResourceRevision(const AMCFoodActor* Food)
{
    uint32 H=GetTypeHash(Food->Settings.MaxAutoConsumeDimensions);
    const auto Add=[&H](const UStaticMesh* Mesh){
        H=HashCombine(H,PointerHash(Mesh));if(!Mesh)return;
        H=HashCombine(H,GetTypeHash(Mesh->bAllowCPUAccess));H=HashCombine(H,PointerHash(Mesh->GetRenderData()));
        if(const auto* Setup=Mesh->GetBodySetup())H=HashCombine(H,GetTypeHash(Setup->BodySetupGuid));
    };
    Add(Food->ItemMesh);
    for(const auto& Ref:Food->FoodData.FragmentMeshes)Add(Ref.Get());
    for(const auto& Ref:Food->FoodData.WholeMeshes)Add(Ref.Get());
    if(const auto* GS=Food->GetWorld()->GetGameState<AMCGameState>();GS&&GS->DayPlan)if(const auto* Menu=GS->DayPlan->Menu.Get()){
        H=HashCombine(H,PointerHash(Menu));auto Names=Menu->GetRowNames();Names.Sort(FNameLexicalLess());int32 Seen=0;
        for(FName Name:Names){
            if(++Seen>64)break;
            if(const auto* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Resident processing retry revision"),false);Row&&Row->Kind==EMCFoodKind::Food){
                for(const auto& Ref:Row->FragmentMeshes)Add(Ref.Get());for(const auto& Ref:Row->WholeMeshes)Add(Ref.Get());
            }
        }
    }
    bool Fresh=false,Spoiled=false;for(TActorIterator<AMCFoodDisposal> It(Food->GetWorld());It;++It){Fresh|=FreshEndpoint(Cast<AMCThroat>(*It));Spoiled|=SpoiledEndpoint(*It);}
    return HashCombine(H,GetTypeHash(uint8(Fresh)|(uint8(Spoiled)<<1)));
}
struct FCandidate
{
    UStaticMesh* Mesh=nullptr;
    FMCFoodRow Row;
    FName Name;
    FVector Scale=FVector::OneVector;
};
bool ImportResidentMesh(const FCandidate& Candidate,UProceduralMeshComponent* Scratch)
{
    if(!ResidentMeshUsable(Candidate.Mesh)||Candidate.Scale.ContainsNaN()||Candidate.Scale.GetAbs().GetMin()<SMALL_NUMBER)return false;
    for(int32 S=0;S<Candidate.Mesh->GetNumSections(0);++S){
        TArray<FVector> Positions,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;TArray<FLinearColor> Colors;
        UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Candidate.Mesh,0,S,Positions,Indices,Normals,UV,Tangents);
        if(Indices.IsEmpty())continue;
        Scratch->CreateMeshSection_LinearColor(S,Positions,Indices,Normals,UV,Colors,Tangents,false);Scratch->SetMaterial(S,Candidate.Mesh->GetMaterial(S));
    }
    return Scratch->GetNumSections()>0;
}
bool AnalyzeCandidate(FCandidate& Candidate,FVector ActorScale,double DesiredWorldVolume,double& LocalVolume,FBox& LocalBounds,
    TUniquePtr<MCFoodCutGeometry::FPreparedCollision>& Collision)
{
    auto Scratch=TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
    if(!ImportResidentMesh(Candidate,Scratch.Get()))return false;
    FTransform Local(FQuat::Identity,-Candidate.Mesh->GetBounds().Origin*Candidate.Scale,Candidate.Scale);
    TArray<MCFoodCutGeometry::FIsland> Islands;
    if(!MCFoodCutGeometry::FindIslands(Scratch.Get(),Islands,nullptr,256,Local))return false;
    LocalVolume=0;for(const auto& Part:Islands)LocalVolume+=Part.Volume;
    const double Determinant=FMath::Abs(ActorScale.X*ActorScale.Y*ActorScale.Z);
    if(!FMath::IsFinite(LocalVolume)||LocalVolume<=0||!FMath::IsFinite(Determinant)||Determinant<SMALL_NUMBER)return false;
    if(DesiredWorldVolume>0){
        const double Ratio=FMath::Pow(DesiredWorldVolume/(LocalVolume*Determinant),1./3.);
        if(!FMath::IsFinite(Ratio)||Ratio<.0001||Ratio>10000)return false;
        Candidate.Scale*=Ratio;LocalVolume*=Ratio*Ratio*Ratio;
        Local=FTransform(FQuat::Identity,-Candidate.Mesh->GetBounds().Origin*Candidate.Scale,Candidate.Scale);
    }
    LocalBounds=FBox(ForceInit);TArray<FVector> Points;
    for(const auto& Part:Islands)for(const FVector& P:Part.Vertices){const FVector V=Local.TransformPosition(P);Points.Add(V);LocalBounds+=V;}
    if(!LocalBounds.IsValid)return false;
    // A fresh empty history must support a real central knife plane, not merely
    // draw/collide. No asset loading, component registration or actor is needed.
    bool Cuttable=false;
    for(const FVector Axis:{FVector::ForwardVector,FVector::RightVector}){
        bool Both=true;
        for(const float Sign:{1.f,-1.f}){
            auto Half=TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
            for(int32 S=0;S<Scratch->GetNumSections();++S)if(const auto* Section=Scratch->GetProcMeshSection(S))Half->SetProcMeshSection(S,*Section);
            TArray<MCFoodCutGeometry::FIsland> Halves;
            Both&=MCFoodCutGeometry::SliceAndCap(Half.Get(),Candidate.Mesh->GetBoundingBox().GetCenter(),Axis*Sign,Candidate.Mesh->GetMaterial(0))
                &&MCFoodCutGeometry::FindIslands(Half.Get(),Halves,nullptr,256,Local)&&!Halves.IsEmpty();
        }
        if(Both){Cuttable=true;break;}
    }
    if(!Cuttable)return false;
    TArray<MCFoodCutGeometry::FConvexRequest> Requests;auto& Request=Requests.AddDefaulted_GetRef();Request.Vertices=MoveTemp(Points);Request.bAllowPlanarProxy=true;
    TArray<TUniquePtr<MCFoodCutGeometry::FPreparedCollision>> Prepared;
    if(!MCFoodCutGeometry::PrepareConvexBatch(Requests,Prepared)||Prepared.Num()!=1)return false;
    Collision=MoveTemp(Prepared[0]);return Collision.IsValid()&&Collision->IsValid();
}
}

struct FMCFoodProcessingRecoveryState
{
    TArray<FRecoveryRecord> Records;
    TUniquePtr<FEndpointRecipe> Fresh,Spoiled;
    FGuid Epoch;
    TWeakObjectPtr<AMCGameState> GameState;
    int32 Day=0;
    double NextEndpointScanAt=-1;
    int32 Replacements=0,Blocked=0;
    double LastRecoveryWorldTime=-1;
};

bool UMCFoodProcessingRecoverySubsystem::IsWorldReady() const
{
    const UWorld* World=GetWorld();if(bDeinitialized||!World||!World->HasBegunPlay()||World->GetNetMode()==NM_Client||World->bIsTearingDown)return false;
    const auto* GS=World->GetGameState<AMCGameState>();
    if(GS&&(GS->bLobbyWaiting||GS->Phase!=EMCShiftPhase::Working))return false;
    const FString Map=UGameplayStatics::GetCurrentLevelName(World,true);
    const bool Arena=Map==TEXT("L_Mouth")||Map==TEXT("L_Tutorial");
    if(Arena&&(!GS||!GS->GameplayLoadId.IsValid()))return false;
    if(const auto* Preload=World->GetSubsystem<UMCWorldPreloadSubsystem>())
        if((Arena||Preload->GetPhase()!=EMCWorldPreloadPhase::Idle)&&!Preload->IsReady())return false;
    return true;
}
bool UMCFoodProcessingRecoverySubsystem::IsTickable() const
{return !bDeinitialized&&!IsTemplate()&&GetWorld()&&GetWorld()->GetNetMode()!=NM_Client&&Super::IsTickable();}
TStatId UMCFoodProcessingRecoverySubsystem::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UMCFoodProcessingRecoverySubsystem,STATGROUP_Tickables);}
int32 UMCFoodProcessingRecoverySubsystem::GetPendingRecordCount() const{return State.IsValid()?State->Records.Num():0;}
int32 UMCFoodProcessingRecoverySubsystem::GetReplacementCount() const{return State.IsValid()?State->Replacements:0;}
int32 UMCFoodProcessingRecoverySubsystem::GetBlockedCount() const{return State.IsValid()?State->Blocked:0;}
void UMCFoodProcessingRecoverySubsystem::CaptureHealthyEndpoints(bool bForce)
{
    if(!IsWorldReady())return;
    if(!State.IsValid())State=MakeShared<FMCFoodProcessingRecoveryState>();
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();const FGuid Epoch=GS?GS->GameplayLoadId:FGuid();
    if(State->Epoch!=Epoch||State->GameState.Get()!=GS||State->Day!=(GS?GS->Day:0)){State->Records.Reset();State->Fresh.Reset();State->Spoiled.Reset();State->Epoch=Epoch;State->GameState=const_cast<AMCGameState*>(GS);State->Day=GS?GS->Day:0;State->NextEndpointScanAt=-1;}
    const double Now=GetWorld()->GetTimeSeconds();if(!bForce&&Now<State->NextEndpointScanAt)return;State->NextEndpointScanAt=Now+EndpointScanSeconds;
    for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It){
        const bool Fresh=FreshEndpoint(Cast<AMCThroat>(*It)),Spoiled=SpoiledEndpoint(*It);if(!Fresh&&!Spoiled)continue;
        auto& Recipe=Fresh?State->Fresh:State->Spoiled;
        if(Recipe.IsValid()&&Recipe->Original.Get()==*It&&Recipe->Pose.Equals(It->GetActorTransform(),0)
            &&Recipe->VolumePose.Equals(It->Volume->GetRelativeTransform(),0)&&Recipe->Extent==It->Volume->GetUnscaledBoxExtent()) {
            bool Same=true;for(const auto& Value:Recipe->Values)if(!Value->Property->Identical(Value->Property->ContainerPtrToValuePtr<void>(*It),Value->Data,0)){Same=false;break;}
            if(Same){Recipe->CaptureCompleted(*It);continue;}
        }
        auto Candidate=MakeUnique<FEndpointRecipe>();if(Candidate->Capture(*It,Fresh)){
            if(Recipe.IsValid())if(auto* Previous=Recipe->Original.Get())Previous->OnDestroyed.RemoveDynamic(this,&UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed);
            Recipe=MoveTemp(Candidate);It->OnDestroyed.AddUniqueDynamic(this,&UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed);
        }
    }
}
bool UMCFoodProcessingRecoverySubsystem::EnsureDestinations()
{
    CaptureHealthyEndpoints();if(!State.IsValid())return false;
    bool Fresh=false,Spoiled=false;
    for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It){Fresh|=FreshEndpoint(Cast<AMCThroat>(*It));Spoiled|=SpoiledEndpoint(*It);}
    if(!Fresh&&State->Fresh.IsValid()){
        auto* Restored=State->Fresh->Restore(GetWorld());Fresh=Restored!=nullptr;
        if(Restored)Restored->OnDestroyed.AddUniqueDynamic(this,&UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed);
        if(Fresh)UE_LOG(LogMCFoodCuts,Warning,TEXT("MC_FOOD_ENDPOINT_RESTORED kind=fresh run=%s"),*State->Epoch.ToString());
    }
    if(!Spoiled&&State->Spoiled.IsValid()){
        auto* Restored=State->Spoiled->Restore(GetWorld());Spoiled=Restored!=nullptr;
        if(Restored)Restored->OnDestroyed.AddUniqueDynamic(this,&UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed);
        if(Spoiled)UE_LOG(LogMCFoodCuts,Warning,TEXT("MC_FOOD_ENDPOINT_RESTORED kind=spoiled run=%s"),*State->Epoch.ToString());
    }
    return Fresh&&Spoiled;
}
bool UMCFoodProcessingRecoverySubsystem::CanAdmit(const AMCFoodActor* Food)
{
    check(IsInGameThread());if(!IsValid(Food)||!Food->GetWorld()||!Food->HasAuthority())return false;
    FMCFoodSettings Settings=Food->Settings;
    if(!Food->bStagedCutPortion)if(const auto* Profile=Food->Profile.Get())Settings=Profile->Settings;
    if(!Settings.bAutoConsumeSmallCuts||Food->FoodData.Kind!=EMCFoodKind::Food||Food->IsNutEncounterFragment())return true;
    if(!Food->ItemMesh)return false;
    if(Food->bFragment&&!ResidentMeshUsable(Food->ItemMesh))return false;
    const bool WholeHard=!Food->bFragment&&Food->IsHardFood();
    if(WholeHard||!ResidentMeshUsable(Food->ItemMesh)){
        bool Any=false;
        for(const auto& Ref:Food->FoodData.FragmentMeshes)if(!Ref.IsNull()){Any=true;if(!ResidentMeshUsable(Ref.Get()))return false;}
        if(!Any)return false;
    }
    auto* Recovery=Food->GetWorld()->GetSubsystem<UMCFoodProcessingRecoverySubsystem>();
    return Recovery&&Recovery->IsWorldReady()&&Recovery->EnsureDestinations();
}
void UMCFoodProcessingRecoverySubsystem::ReportFailure(AMCFoodActor* Food,EMCFoodProcessingFailure Reason)
{
    check(IsInGameThread());
    if(!IsValid(Food)||!Food->HasAuthority()||!Food->HasActorBegunPlay()||!Food->GetWorld()||Food->IsActorBeingDestroyed()||Food->IsDisposed()
        ||Food->IsCutTransactionStaged()||Food->bCutIntakeReceipt||!Food->UsesAutomaticCutIntake())return;
    auto* Recovery=Food->GetWorld()->GetSubsystem<UMCFoodProcessingRecoverySubsystem>();if(!Recovery||Recovery->bDeinitialized)return;
    Recovery->CaptureHealthyEndpoints();if(!Recovery->State.IsValid())Recovery->State=MakeShared<FMCFoodProcessingRecoveryState>();
    const auto* GS=Food->GetWorld()->GetGameState<AMCGameState>();const FGuid Epoch=GS?GS->GameplayLoadId:FGuid();
    if(Recovery->State->Epoch!=Epoch||Recovery->State->GameState.Get()!=GS||Recovery->State->Day!=(GS?GS->Day:0)){Recovery->State->Records.Reset();Recovery->State->Fresh.Reset();Recovery->State->Spoiled.Reset();Recovery->State->Epoch=Epoch;Recovery->State->GameState=const_cast<AMCGameState*>(GS);Recovery->State->Day=GS?GS->Day:0;Recovery->State->NextEndpointScanAt=-1;}
    const uint32 Revision=ResourceRevision(Food);
    auto* Existing=Recovery->State->Records.FindByPredicate([Food](const auto& R){return R.Food.Get()==Food;});
    if(Existing&&Existing->Epoch==Epoch&&Existing->Mesh.Get()==Food->ItemMesh&&Existing->Scale==Food->GetActorScale3D()&&SameHistory(Existing->History,Food->CutPlanes)
        &&(Existing->Attempts<MaxRecoveryAttempts||Existing->ResourceRevision==Revision))return;
    if(Existing)Recovery->State->Records.RemoveAll([Food](const auto& R){return R.Food.Get()==Food;});
    FRecoveryRecord Record;Record.GameState=const_cast<AMCGameState*>(GS);Record.Day=GS?GS->Day:0;Record.ItemName=Food->ItemName;Record.Profile=Food->Profile.ToSoftObjectPath();Record.Food=Food;Record.Reason=Reason;Record.Mesh=Food->ItemMesh;Record.History=Food->CutPlanes;Record.Epoch=Epoch;Record.Scale=Food->GetActorScale3D();
    Record.ResourceRevision=Revision;Record.ReportedAt=Record.NextAttemptAt=Food->GetWorld()->GetTimeSeconds();Recovery->State->Records.Add(MoveTemp(Record));
    UE_LOG(LogMCFoodCuts,Warning,TEXT("MC_FOOD_PROCESSING_FAILURE source=%s item=%s batch=%d reason=%s"),*Food->GetName(),*Food->ItemName.ToString(),Food->Batch,FailureName(Reason));
}

bool UMCFoodProcessingRecoverySubsystem::RecoverFood(AMCFoodActor* Food,EMCFoodProcessingFailure Reason,int32& CandidateCursor,bool& bExhausted)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodRecoverReportedProcessingFailure);
    bExhausted=true;
    if(!IsValid(Food)||Food->IsDisposed()||Food->IsActorBeingDestroyed()||!Food->UsesAutomaticCutIntake()||!EnsureDestinations())return false;
    TStrongObjectPtr<AMCFoodActor> KeepSourceAlive(Food);
    TGuardValue<bool> ProcessingGuard(Food->bProcessingAutomaticCutDamage,true);
    const float Mass=Food->FoodData.Mass;
    const int32 Units=Food->bFragment?Food->CutAwardUnits:Food->FoodData.FragmentCount();
    const FVector ActorScale=Food->GetActorScale3D();
    if(!FMath::IsFinite(Mass)||Mass<=0||Units<0||ActorScale.ContainsNaN()||ActorScale.GetAbs().GetMin()<SMALL_NUMBER)return false;
    double DesiredWorldVolume=Food->AutomaticIntakeVolume();
    if(DesiredWorldVolume<=0&&Food->CutVisual){
        TArray<MCFoodCutGeometry::FIsland> Remaining;
        if(MCFoodCutGeometry::FindIslands(Food->CutVisual,Remaining,nullptr,256,Food->PortionSourceTransform())){
            DesiredWorldVolume=0;for(const auto& Part:Remaining)DesiredWorldVolume+=Part.Volume;
            DesiredWorldVolume*=FMath::Abs(ActorScale.X*ActorScale.Y*ActorScale.Z);
        }
    }
    TArray<FCandidate> Candidates;
    const auto* ReadyResources=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    const auto AddCandidate=[&Candidates,ReadyResources](UStaticMesh* Mesh,const FMCFoodRow& Row,FName Name,FVector Scale){
        if(Candidates.Num()>=MaxCandidates||!ResidentMeshUsable(Mesh)||Candidates.ContainsByPredicate([&](const auto& C){return C.Mesh==Mesh&&C.Scale==Scale;}))return;
        // Geometry/convex preflight below validates the final prepared body.
        // The row may intentionally use external authored collision data.
        // Live knife replay uses these existing location-prepared cap resources.
        const FString MeshName=Mesh->GetName();const TCHAR* Color=MeshName.Contains(TEXT("Carrot"))?TEXT("Orange"):
            MeshName.Contains(TEXT("Tomato"))?TEXT("Tomato"):MeshName.Contains(TEXT("Strawberry"))?TEXT("Strawberry"):
            (MeshName.Contains(TEXT("Broccoli"))||MeshName.Contains(TEXT("Cabbage")))?TEXT("Green"):TEXT("Cream");
        const FString CapPath=FString::Printf(TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_%s.MI_FoodCut_%s"),Color,Color);
        auto* Cap=FindObject<UMaterialInterface>(nullptr,*CapPath);if(!Cap)return;
        if(ReadyResources&&ReadyResources->IsReady()&&(!ReadyResources->IsVisualResourceRetained(Mesh)||!ReadyResources->IsVisualResourceRetained(Cap)))return;
        Candidates.Add({Mesh,Row,Name,Scale});
    };
    AddCandidate(Food->ItemMesh,Food->FoodData,Food->ItemName,Food->bFragment?Food->FoodData.FragmentScale:Food->FoodData.Scale);
    for(const auto& Ref:Food->FoodData.FragmentMeshes)AddCandidate(Ref.Get(),Food->FoodData,Food->ItemName,Food->FoodData.FragmentScale);
    for(const auto& Ref:Food->FoodData.WholeMeshes)AddCandidate(Ref.Get(),Food->FoodData,Food->ItemName,Food->FoodData.Scale);
    if(const auto* GS=GetWorld()->GetGameState<AMCGameState>();GS&&GS->DayPlan){
        if(const auto* Menu=GS->DayPlan->Menu.Get()){
            auto Names=Menu->GetRowNames();Names.Sort(FNameLexicalLess());
            for(FName Name:Names)if(const auto* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Resident food processing recovery"),false);Row&&Row->Kind==EMCFoodKind::Food){
                for(const auto& Ref:Row->FragmentMeshes)AddCandidate(Ref.Get(),*Row,Name,Row->FragmentScale);
                for(const auto& Ref:Row->WholeMeshes)AddCandidate(Ref.Get(),*Row,Name,Row->Scale);
                if(Candidates.Num()>=MaxCandidates)break;
            }
        }
    }
    if(Candidates.IsEmpty())return false;
    const int32 First=FMath::Clamp(CandidateCursor,0,Candidates.Num()-1);CandidateCursor=First+1;bExhausted=CandidateCursor>=Candidates.Num();
    // At most one immutable candidate is analyzed in this world tick.
    for(int32 Index=First;Index<=First;++Index){
        auto Candidate=Candidates[Index];
        if(const auto* Preload=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();Preload&&Preload->IsReady()&&!Preload->IsVisualResourceRetained(Candidate.Mesh))continue;
        double Volume=-1;FBox Bounds(ForceInit);TUniquePtr<MCFoodCutGeometry::FPreparedCollision> Collision;
        if(!AnalyzeCandidate(Candidate,ActorScale,DesiredWorldVolume,Volume,Bounds,Collision))continue;
        FHitResult Floor;bool FoundFloor=false;
        const FTransform ShapePose(Food->GetActorQuat(),Food->GetActorLocation(),ActorScale);
        const FBox WorldBounds=Bounds.TransformBy(ShapePose);const float Margin=FMath::Max(35.f,float(WorldBounds.GetExtent().Size2D())+15.f);
        FRandomStream Random(HashCombine(GetTypeHash(Food->Batch),GetTypeHash(Food->GetFName())));
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It){
            if(It->IsActorBeingDestroyed()||!It->Surface||!It->Surface->IsRegistered()||It->CurrentVertices().IsEmpty())continue;
            FoundFloor=It->GameplaySpawnFootprint(WorldBounds.GetCenter(),Margin,Floor);
            if(!FoundFloor&&Food->LastAccessiblePosition.IsSet())FoundFloor=It->GameplaySpawnFootprint(Food->LastAccessiblePosition.GetValue(),Margin,Floor);
            if(!FoundFloor&&Food->EntryRecoveryLanding.IsSet())FoundFloor=It->GameplaySpawnFootprint(Food->EntryRecoveryLanding.GetValue(),Margin,Floor);
            if(!FoundFloor)FoundFloor=It->RandomGameplaySpawnPoint(Random,Margin,Margin*2,TConstArrayView<FVector>(),Floor);
            if(FoundFloor)break;
        }
        if(!FoundFloor)continue;
        FTransform Pose(Food->GetActorQuat(),FVector::ZeroVector,ActorScale);
        const FBox Rotated=Bounds.TransformBy(Pose);
        Pose.SetLocation(Floor.ImpactPoint+FVector(0,0,-Rotated.Min.Z+8.f)-FVector(Rotated.GetCenter().X,Rotated.GetCenter().Y,0));
        if(Pose.ContainsNaN())continue;
        FActorSpawnParameters Params;Params.bDeferConstruction=true;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.CustomPreSpawnInitialization=[](AActor* Actor){
            auto* Part=CastChecked<AMCFoodActor>(Actor);Part->bCutTransactionStaged=true;
            Part->SetReplicates(false);Part->SetActorHiddenInGame(true);Part->SetActorTickEnabled(false);
            Part->Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->GripSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->Visual->SetStaticMesh(nullptr);
        };
        auto* Replacement=GetWorld()->SpawnActor<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,Params);if(!Replacement)continue;
        Replacement->FoodData=Candidate.Row;
        Replacement->FoodData.Kind=EMCFoodKind::Food;Replacement->FoodData.Resistance=EMCFoodResistance::Soft;
        Replacement->FoodData.Scale=Replacement->FoodData.FragmentScale=Candidate.Scale;
        Replacement->FoodData.Mass=Mass;Replacement->FoodData.Health=25;
        Replacement->FoodData.SpoilSeconds=Food->FoodData.SpoilSeconds;
        Replacement->ItemName=Food->ItemName;Replacement->ItemMesh=Candidate.Mesh;Replacement->bFragment=true;Replacement->Health=25;
        Replacement->Settings=Food->Settings;Replacement->Settings.Mass=Mass;Replacement->Profile=Food->Profile;
        Replacement->Batch=Food->Batch;Replacement->CutAwardUnits=Units;Replacement->LastHandledBy=Food->LastHandledBy;
        Replacement->bSpoiled=Food->bSpoiled;Replacement->SpoilAt=Food->SpoilAt;Replacement->SetOwner(Food->GetOwner());
        Replacement->Tags=Food->Tags;
        Replacement->bStagedCutPortion=true;Replacement->Phase=EMCFoodPhase::Free;
        Replacement->EntryRecoveryLanding=Floor.ImpactPoint;Replacement->LastAccessiblePosition=Floor.ImpactPoint;
        Replacement->FinishSpawning(Pose);
        if(!Replacement->HasActorBegunPlay())Replacement->OnRep_Item();
        Replacement->bSpoiled=Food->bSpoiled;Replacement->SpoilAt=Food->SpoilAt;
        bool Valid=IsValid(Replacement)&&!Replacement->IsActorBeingDestroyed()&&Replacement->PrepareCutGeometry()
            &&CastChecked<UMCFoodBodyComponent>(Replacement->Body)->InstallPreparedCutCollision(*Collision);
        if(Valid){
            Replacement->MeasuredPortionLocalVolume=Volume;Replacement->CutStackLocalBounds=Bounds;
            Replacement->Settings.Mass=Mass;Replacement->FoodData.Mass=Mass;
            Replacement->Body->SetMassOverrideInKg(NAME_None,Mass,true);
            // Freshness belongs to the original work, including expiry during recovery.
            if(Replacement->HasFreshnessTimer()&&!Replacement->bSpoiled&&Replacement->SpoilAt>0&&Replacement->HazardNow()>=Replacement->SpoilAt)Replacement->bSpoiled=true;
            FVector Target;AMCThroat* Throat=nullptr;
            Valid=Replacement->ResolveAutomaticCutDestination(Pose.TransformPosition(Bounds.GetCenter()),Target,Throat);
        }
        Valid&=IsValid(Food)&&!Food->IsActorBeingDestroyed()&&!Food->IsDisposed()&&Food->FoodData.Mass==Mass
            &&(Food->bFragment?Food->CutAwardUnits:Food->FoodData.FragmentCount())==Units;
        if(!Valid){if(IsValid(Replacement))Replacement->Destroy();continue;}
        // No awards, intake, meal counters or fresh-success presentation occur.
        // Only after every replacement invariant passed can the broken source retire.
        const FCollisionResponseContainer Responses=Replacement->StagedCollisionResponses;
        Food->CancelPendingCut(false);Food->Dispose();
        Replacement->bCutTransactionStaged=false;Replacement->Body->SetCollisionResponseToChannels(Responses);
        Replacement->GripSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Replacement->SetReplicates(true);Replacement->SetActorTickEnabled(true);Replacement->OnRep_Phase();Replacement->ForceNetUpdate();
        ++State->Replacements;
        UE_LOG(LogMCFoodCuts,Warning,TEXT("MC_FOOD_PROCESSING_REPLACED source=%s replacement=%s item=%s batch=%d reason=%s mass=%.6f units=%d spoiled=%d volume_cm3=%.6f"),
            *Food->GetName(),*Replacement->GetName(),*Replacement->ItemName.ToString(),Replacement->Batch,FailureName(Reason),Mass,Units,Replacement->bSpoiled,Replacement->AutomaticIntakeVolume());
        return true;
    }
    return false;
}
void UMCFoodProcessingRecoverySubsystem::TickProcessRecords()
{
    check(IsInGameThread());
    if(bDeinitialized)return;
    if(!GetWorld()||GetWorld()->bIsTearingDown){if(State.IsValid())State->Records.Reset();return;}
    if(const auto* CurrentGS=GetWorld()->GetGameState<AMCGameState>();CurrentGS&&CurrentGS->Phase!=EMCShiftPhase::Working){if(State.IsValid())State->Records.Reset();return;}
    if(!IsWorldReady())return;
    CaptureHealthyEndpoints();if(!State.IsValid())return;
    const double Now=GetWorld()->GetTimeSeconds();const auto* GS=GetWorld()->GetGameState<AMCGameState>();const FGuid Epoch=GS?GS->GameplayLoadId:FGuid();
    State->Records.RemoveAll([&](const auto& R){
        const auto* Food=R.Food.Get();return !IsValid(Food)||Food->IsActorBeingDestroyed()||Food->IsDisposed()||!Food->UsesAutomaticCutIntake()||R.Epoch!=Epoch||R.GameState.Get()!=GS||R.Day!=(GS?GS->Day:0)
            ||R.ItemName!=Food->ItemName||R.Profile!=Food->Profile.ToSoftObjectPath()||R.Mesh.Get()!=Food->ItemMesh||R.Scale!=Food->GetActorScale3D()||!SameHistory(R.History,Food->CutPlanes);
    });
    if(Now<=State->LastRecoveryWorldTime)return;
    for(int32 I=0;I<State->Records.Num();++I){
        auto& Record=State->Records[I];auto* Food=Record.Food.Get();
        if((Record.Attempts>=MaxRecoveryAttempts&&Record.CandidateCursor==0)||Now<=Record.ReportedAt||Now<Record.NextAttemptAt)continue;
        if(!Food||Food->bProcessingAutomaticCutDamage||Food->IsCutTransactionPending()||Food->IsCutTransactionStaged()
            ||Food->StackCarrier||Food->EquippedBy||!Food->Holders.IsEmpty()||Food->IsInActiveContainer()
            ||Food->Phase==EMCFoodPhase::Swallowing||Food->Phase==EMCFoodPhase::Absorbing)continue;
        State->LastRecoveryWorldTime=Now;
        // A repaired route is healthy work again; do not replace it merely
        // because an earlier callback reported the now-resolved defect.
        bool RepairedUnsupported=false;
        if(Record.Reason==EMCFoodProcessingFailure::UnsupportedGeometry&&!Food->IsHidden()&&CanAdmit(Food)){
            if(!Food->bFragment&&!ResidentMeshUsable(Food->ItemMesh))RepairedUnsupported=true; // The valid authored-fragment route is available again.
            else {
                TArray<MCFoodCutGeometry::FIsland> Parts;
                if(!Food->CutPlanes.IsEmpty())RepairedUnsupported=Food->bCutVisualReady&&Food->CutVisual
                    &&MCFoodCutGeometry::FindIslands(Food->CutVisual,Parts,nullptr,256,Food->PortionSourceTransform());
                else {
                    FCandidate Current{Food->ItemMesh,Food->FoodData,Food->ItemName,Food->bFragment?Food->FoodData.FragmentScale:Food->FoodData.Scale};
                    auto Scratch=TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
                    RepairedUnsupported=ImportResidentMesh(Current,Scratch.Get())&&MCFoodCutGeometry::FindIslands(Scratch.Get(),Parts,nullptr,256,Food->PortionSourceTransform());
                }
            }
        }
        if((Record.Reason==EMCFoodProcessingFailure::MissingDestination&&EnsureDestinations())||RepairedUnsupported){
            UE_LOG(LogMCFoodCuts,Log,TEXT("MC_FOOD_PROCESSING_RECOVERED_IN_PLACE source=%s reason=%s"),*Food->GetName(),FailureName(Record.Reason));
            State->Records.RemoveAt(I);break;
        }
        if(Record.CandidateCursor==0)++Record.Attempts;
        bool Exhausted=false;const bool Recovered=RecoverFood(Food,Record.Reason,Record.CandidateCursor,Exhausted);
        if(Exhausted){Record.CandidateCursor=0;Record.NextAttemptAt=Now+RetrySeconds;}else Record.NextAttemptAt=Now;
        if(Recovered)State->Records.RemoveAt(I);
        else if(Exhausted&&Record.Attempts==MaxRecoveryAttempts){
            ++State->Blocked;
            UE_LOG(LogMCFoodCuts,Error,TEXT("MC_FOOD_PROCESSING_RECOVERY_BLOCKED source=%s item=%s batch=%d reason=%s attempts=%d action=source_preserved_no_valid_resident_replacement_or_endpoint"),
                *Food->GetName(),*Food->ItemName.ToString(),Food->Batch,FailureName(Record.Reason),Record.Attempts);
        }
        break;
    }
}
void UMCFoodProcessingRecoverySubsystem::Tick(float)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MCFoodProcessingRecoveryTick);TickProcessRecords();
}
void UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed(AActor* Endpoint)
{
    if(bDeinitialized||!State.IsValid()||!GetWorld()||GetWorld()->bIsTearingDown)return;
    for(auto* Recipe:{State->Fresh.Get(),State->Spoiled.Get()})
        if(Recipe&&Recipe->Original.GetEvenIfUnreachable()==Endpoint)Recipe->CaptureCompleted(Cast<AMCFoodDisposal>(Endpoint));
}
void UMCFoodProcessingRecoverySubsystem::Deinitialize()
{
    if(bDeinitialized)return;bDeinitialized=true;
    if(State.IsValid())for(auto* Recipe:{State->Fresh.Get(),State->Spoiled.Get()})
        if(Recipe)if(auto* Endpoint=Recipe->Original.Get())Endpoint->OnDestroyed.RemoveDynamic(this,&UMCFoodProcessingRecoverySubsystem::OnEndpointDestroyed);
    State.Reset();Super::Deinitialize();
}
