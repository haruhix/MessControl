#include "MCToothCalculusComponent.h"

#include "MCArenaTooth.h"
#include "MCBrushContactComponent.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/Material.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace MCCalculus
{
    constexpr int32 PiecesPerDeposit=13,MaxDeposits=6,MaxShards=16,RimCount=8;
    constexpr uint8 FullStage=3;
    FVector WorldNormal(const FTransform& Transform,FVector Normal)
    {
        const FVector Scale=Transform.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
        return Transform.TransformVectorNoScale(Normal/Scale).GetSafeNormal();
    }
    FVector LocalNormal(const FTransform& Transform,FVector Normal)
    {
        return (Transform.InverseTransformVectorNoScale(Normal)*Transform.GetScale3D()).GetSafeNormal();
    }
    double Time(const UWorld* World)
    {
        const AGameStateBase* GS=World?World->GetGameState():nullptr;
        return GS?GS->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;
    }
    FLinearColor StoneColor(float Variation)
    {
        return FMath::Lerp(FLinearColor(.57f,.43f,.19f),FLinearColor(.92f,.82f,.57f),Variation);
    }
    // Flat faces make every removed corner readable without pixel noise or extra material passes.
    void Triangle(FVector A,FVector B,FVector C,FVector Outward,FLinearColor Color,
        TArray<FVector>& Vertices,TArray<int32>& Indices,TArray<FVector>& Normals,
        TArray<FVector2D>& UV,TArray<FLinearColor>& Colors,TArray<FProcMeshTangent>& Tangents)
    {
        FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        if(FVector::DotProduct(N,Outward)<0) { Swap(B,C); N=-N; }
        const int32 Base=Vertices.Num();
        // PMC uses clockwise front faces: its generated-box top has a +Z
        // shading normal but a -Z edge cross product. Keep the outward normal
        // for lighting and reverse only the indices, otherwise the crust's
        // front shell is culled and only its recessed rear cap is visible.
        Vertices.Append({A,B,C}); Indices.Append({Base,Base+2,Base+1});
        Normals.Append({N,N,N}); UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(.5,1)});
        Colors.Append({Color,Color,Color});
        const FProcMeshTangent Tangent((B-A).GetSafeNormal(),false);
        Tangents.Append({Tangent,Tangent,Tangent});
    }
}

UMCToothCalculusComponent::UMCToothCalculusComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetIsReplicatedByDefault(true);
    CalculusMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Care/M_ToothCalculus.M_ToothCalculus")));
}

void UMCToothCalculusComponent::BeginPlay()
{
    Super::BeginPlay();
    PlayedHit=State.HitSerial;
    RebuildForSurface();
    if(PendingGrowFrames>0) SetComponentTickEnabled(true);
}

void UMCToothCalculusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Shards.Reset(); SurfacePieces.Reset(); SetComponentTickEnabled(false);
    if(IsValid(Deposits)) Deposits->DestroyComponent();
    if(IsValid(Debris)) Debris->DestroyComponent();
    Deposits=nullptr; Debris=nullptr;
    Super::EndPlay(Reason);
}

void UMCToothCalculusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCToothCalculusComponent,State);
}

void UMCToothCalculusComponent::EnsureMeshes()
{
    auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth || !Tooth->Visual || !GetWorld() || !FApp::CanEverRender()) return;
    if(!LoadedMaterial)
    {
        LoadedMaterial=CalculusMaterial.LoadSynchronous();
        if(!LoadedMaterial) LoadedMaterial=UMaterial::GetDefaultMaterial(MD_Surface);
    }
    auto Create=[&](FName Name,USceneComponent* Parent)
    {
        auto* Mesh=NewObject<UProceduralMeshComponent>(Tooth,Name);
        Mesh->SetupAttachment(Parent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false); Mesh->bUseAsyncCooking=false;
        Mesh->SetMaterial(0,LoadedMaterial); Mesh->SetCullDistance(4500.f);
        Mesh->RegisterComponent();
        return Mesh;
    };
    if(!Deposits) Deposits=Create(TEXT("CalculusDeposits"),Tooth->Visual);
    if(!Debris)
    {
        Debris=Create(TEXT("CalculusChips"),Tooth->GetRootComponent());
        // Detached chips keep their world trajectory while the crown squashes, falls or plays a piano note.
        Debris->SetAbsolute(true,true,true); Debris->SetWorldTransform(FTransform::Identity);
    }
}

bool UMCToothCalculusComponent::HasCalculus() const { return RemainingPieces()>0; }
int32 UMCToothCalculusComponent::RemainingPieces() const
{
    int32 Count=0;
    for(uint8 Stage:State.Pieces) Count+=Stage>0;
    return Count;
}
float UMCToothCalculusComponent::RemainingFraction() const
{
    int32 Remaining=0;
    for(uint8 Stage:State.Pieces) Remaining+=FMath::Min(Stage,MCCalculus::FullStage);
    return State.Pieces.IsEmpty()?0.f:float(Remaining)/(State.Pieces.Num()*MCCalculus::FullStage);
}

void UMCToothCalculusComponent::GrowCalculus(int32 Seed,int32 PatchCount)
{
    auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth || !Tooth->HasAuthority() || !Tooth->IsAvailable() || !Tooth->Visual || !Tooth->BrushSurface) return;
    PatchCount=FMath::Clamp(PatchCount,0,MCCalculus::MaxDeposits);
    if(PatchCount==0) { ClearCalculus(); return; }
    if(!Tooth->Visual->GetStaticMesh() || !Tooth->BrushSurface->IsPhysicsStateCreated())
    {
        PendingSeed=Seed; PendingPatches=PatchCount; PendingGrowFrames=60;
        SetComponentTickEnabled(true); return;
    }
    PendingGrowFrames=0;
    const FTransform T=Tooth->Visual->GetComponentTransform();
    const FBoxSphereBounds Bounds=Tooth->Visual->GetStaticMesh()->GetBounds();
    FRandomStream Random(Seed);
    FMCCalculusState Next;
    Next.Seed=Seed; Next.GrowthSerial=State.GrowthSerial+1;
    Next.GrowthScale=T.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
    Next.HitSerial=State.HitSerial;
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    FVector Inward=((Tongue?Tongue->GetActorLocation():FVector::ZeroVector)-Tooth->GetActorLocation()).GetSafeNormal2D();
    if(Inward.IsNearlyZero()) Inward=-Tooth->GetActorForwardVector();
    const FVector Center=T.TransformPosition(Bounds.Origin);
    FVector Floor=FVector::ZeroVector;
    const bool bFloor=Tongue && Tongue->RestSurfacePoint(Center+Inward*(Bounds.BoxExtent*Next.GrowthScale).Size2D(),Floor);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCalculusGrowth),true);
    const float HalfWidth=FMath::Max(18.f,float((Bounds.BoxExtent*Next.GrowthScale).Size2D())*.32f);
    for(int32 I=0;I<PatchCount;++I)
    {
        // Nearby windows join into a mineral cluster instead of a repeated
        // horizontal necklace around the crown.
        const float Angle=FMath::Lerp(-9.f,9.f,PatchCount>1?float(I)/(PatchCount-1):.5f)+Random.FRandRange(-3.f,3.f);
        const FVector Facing=Inward.RotateAngleAxis(Angle,FVector::UpVector);
        FVector Aim=Center;
        const float MinZ=Tooth->Visual->Bounds.GetBox().Min.Z+28;
        const float MaxZ=FMath::Max(MinZ,Tooth->Visual->Bounds.GetBox().Max.Z-28);
        Aim.Z=FMath::Clamp(bFloor?Floor.Z+65.f+(I%3)*27.f+Random.FRandRange(-5.f,5.f):float(Center.Z-Bounds.BoxExtent.Z*Next.GrowthScale.Z*.25f),MinZ,MaxZ);
        FHitResult Hit;
        const float Span=FMath::Max(600.f,float(Tooth->Visual->Bounds.SphereRadius)*2.f);
        if(!Tooth->BrushSurface->LineTraceComponent(Hit,Aim+Facing*Span,Aim-Facing*Span,Query)) continue;
        const FVector N=Hit.ImpactNormal.GetSafeNormal();
        FVector Up=FVector::VectorPlaneProject(FVector::UpVector,N).GetSafeNormal();
        if(Up.IsNearlyZero()) Up=FVector::VectorPlaneProject(FVector::ForwardVector,N).GetSafeNormal();
        FMCCalculusDepositAnchor Anchor;
        Anchor.Center=T.InverseTransformPosition(Hit.ImpactPoint);
        Anchor.Normal=MCCalculus::LocalNormal(T,N);
        Anchor.Up=T.InverseTransformVectorNoScale(Up).GetSafeNormal();
        Anchor.Width=FMath::Clamp(HalfWidth,35.f,58.f)*Random.FRandRange(.85f,1.08f);
        Anchor.Height=Random.FRandRange(28.f,38.f);
        Next.Anchors.Add(Anchor);
    }
    Next.Pieces.Init(MCCalculus::FullStage,Next.Anchors.Num()*MCCalculus::PiecesPerDeposit);
    State=MoveTemp(Next);
    RebuildForSurface();
    Tooth->ForceNetUpdate();
}

void UMCToothCalculusComponent::ClearCalculus()
{
    if(!GetOwner() || !GetOwner()->HasAuthority()) return;
    State.Anchors.Reset(); State.Pieces.Reset(); ++State.GrowthSerial;
    State.HitAt=-100;
    SurfacePieces.Reset(); CachedGrowth=State.GrowthSerial;
    RetryFrames=0; PendingGrowFrames=0; Shards.Reset();
    if(Deposits) Deposits->ClearAllMeshSections();
    if(Debris) Debris->ClearAllMeshSections();
    SetComponentTickEnabled(false);
    GetOwner()->ForceNetUpdate();
}

void UMCToothCalculusComponent::RebuildForSurface()
{
    CachedGrowth=INDEX_NONE; CachedMesh=nullptr; SurfacePieces.Reset();
    EnsureMeshes();
    if(State.Anchors.IsEmpty()) { if(Deposits) Deposits->ClearAllMeshSections(); return; }
    if(BuildSurfacePieces()) { RetryFrames=0; BuildDepositMesh(); }
    else { RetryFrames=12; SetComponentTickEnabled(true); }
}

bool UMCToothCalculusComponent::BuildSurfacePieces()
{
    auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth || !Tooth->Visual || !Tooth->Visual->GetStaticMesh() || !Tooth->BrushSurface
        || !Tooth->BrushSurface->IsPhysicsStateCreated()) return false;
    const FTransform T=Tooth->Visual->GetComponentTransform();
    const FVector GrowthScale=FVector(State.GrowthScale).GetAbs().ComponentMax(FVector(.001f));
    FRandomStream Random(State.Seed);
    SurfacePieces.Reset();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCalculusSurface),true);
    for(const auto& Anchor:State.Anchors)
    {
        const FVector N=FVector(Anchor.Normal).GetSafeNormal();
        const FVector Up=FVector::VectorPlaneProject(FVector(Anchor.Up),N).GetSafeNormal();
        const FVector ProbeN=MCCalculus::WorldNormal(T,N);
        const FVector Side=T.InverseTransformVectorNoScale(FVector::CrossProduct(T.TransformVectorNoScale(Up),ProbeN).GetSafeNormal());
        for(int32 Piece=0;Piece<MCCalculus::PiecesPerDeposit;++Piece)
        {
            const int32 Ring=Piece==0?0:Piece<=6?1:2;
            const float Angle=Piece==0?0.f:(Piece-1-(Ring==2?6:0))*TWO_PI/6.f+(Ring==2?PI/6.f:0.f);
            const FVector2D Offset=FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(Ring==0?0.f:Ring==1?.34f:.66f)
                +FVector2D(Random.FRandRange(-.065f,.065f),Random.FRandRange(-.065f,.065f));
            const FVector Plane=FVector(Anchor.Center)+(Side*(Offset.X*Anchor.Width)+Up*(Offset.Y*Anchor.Height))/GrowthScale;
            FHitResult CenterHit;
            const FVector WorldPlane=T.TransformPosition(Plane);
            FPiece P;
            const bool Projected=Tooth->BrushSurface->LineTraceComponent(CenterHit,WorldPlane+ProbeN*75,WorldPlane-ProbeN*100,Query);
            P.Center=Projected?T.InverseTransformPosition(CenterHit.ImpactPoint):Plane;
            P.Normal=Projected?MCCalculus::LocalNormal(T,CenterHit.ImpactNormal):N;
            const FVector PieceNormal=MCCalculus::WorldNormal(T,P.Normal);
            P.Up=T.InverseTransformVectorNoScale(FVector::VectorPlaneProject(T.TransformVectorNoScale(Up),PieceNormal).GetSafeNormal());
            P.Side=T.InverseTransformVectorNoScale(FVector::CrossProduct(T.TransformVectorNoScale(P.Up),PieceNormal).GetSafeNormal());
            P.Radius=Random.FRandRange(.27f,.35f)*Anchor.Width;
            P.Depth=Random.FRandRange(12.f,22.f);
            P.Variation=Random.FRandRange(.35f,.96f);
            for(int32 V=0;V<MCCalculus::RimCount;++V)
            {
                const float A=V*TWO_PI/MCCalculus::RimCount;
                const float Radius=P.Radius*Random.FRandRange(.86f,1.14f);
                const FVector Local=P.Center+(P.Side*(FMath::Cos(A)*Radius)+P.Up*(FMath::Sin(A)*Radius*.8f))/GrowthScale;
                const FVector World=T.TransformPosition(Local);
                FHitResult RimHit;
                P.Rim.Add(Tooth->BrushSurface->LineTraceComponent(RimHit,World+ProbeN*75,World-ProbeN*100,Query)
                    ?T.InverseTransformPosition(RimHit.ImpactPoint):Local);
            }
            SurfacePieces.Add(MoveTemp(P));
        }
    }
    CachedGrowth=State.GrowthSerial; CachedMesh=Tooth->Visual->GetStaticMesh();
    return true;
}

void UMCToothCalculusComponent::BuildDepositMesh()
{
    EnsureMeshes();
    if(!Deposits) return;
    TArray<FVector> Vertices,Normals; TArray<int32> Indices; TArray<FVector2D> UV;
    TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
    const FVector Scale=FVector(State.GrowthScale).GetAbs().ComponentMax(FVector(.001f));
    for(int32 I=0;I<SurfacePieces.Num() && I<State.Pieces.Num();++I)
    {
        const int32 Stage=FMath::Clamp(int32(State.Pieces[I]),0,int32(MCCalculus::FullStage));
        if(Stage==0) continue;
        const FPiece& P=SurfacePieces[I];
        const float Fraction=float(Stage)/MCCalculus::FullStage;
        const float Radius=.45f+.55f*Fraction,Depth=P.Depth*(.35f+.65f*Fraction);
        const FVector NormalOffset=(P.Normal/Scale).GetSafeNormal()/Scale;
        const FVector Peak=P.Center+NormalOffset*Depth
            +(P.Side*(P.Radius*.15f*FMath::Sin(I*2.17f+P.Variation*8))
                +P.Up*(P.Radius*.13f*FMath::Cos(I*1.91f+P.Variation*9)))/Scale;
        const FLinearColor Color=MCCalculus::StoneColor(P.Variation);
        const FLinearColor Fracture=FMath::Lerp(Color,FLinearColor(.98f,.91f,.72f),Stage<3?.65f:0.f);
        for(int32 V=0;V<P.Rim.Num();++V)
        {
            const int32 Next=(V+1)%P.Rim.Num();
            // A chipped stage removes a seeded side of the piece, exposing a new uneven cross-section.
            auto Chip=[&](int32 Corner)
            {
                return (Stage<3 && (Corner+I)%8<3)?(Stage==2?.7f:.35f):1.f;
            };
            auto Rim=[&](int32 Corner)
            {
                return P.Center+(P.Rim[Corner]-P.Center)*(Radius*Chip(Corner))+NormalOffset*.5f;
            };
            auto Shoulder=[&](int32 Corner)
            {
                const float Grain=.5f+.5f*FMath::Sin(Corner*4.13f+I*2.37f+P.Variation*11);
                const float Width=.64f+Grain*.14f,Height=.56f+Grain*.2f;
                return P.Center+(P.Rim[Corner]-P.Center)*(Radius*Width*Chip(Corner))
                    +NormalOffset*(Depth*Height*((Stage<3 && (Corner+I)%8<3)?(.45f+.55f*Fraction):1.f));
            };
            const FVector A=Rim(V),B=Rim(Next),C=Shoulder(Next),D=Shoulder(V);
            const FLinearColor FaceColor=(Stage<3 && (V+I)%8<3)?Fracture:Color;
            // A thick irregular shoulder breaks up each lobe's silhouette;
            // the top is offset and the base remains embedded in real enamel.
            const FVector SideNormal=(A+B+C+D)*.25f-P.Center;
            MCCalculus::Triangle(A,B,C,SideNormal,FaceColor,Vertices,Indices,Normals,UV,Colors,Tangents);
            MCCalculus::Triangle(A,C,D,SideNormal,FaceColor,Vertices,Indices,Normals,UV,Colors,Tangents);
            MCCalculus::Triangle(Peak,D,C,P.Normal,FaceColor,Vertices,Indices,Normals,UV,Colors,Tangents);
            // The rear cap is tucked into enamel, closing the rock without an extra transparent coating.
            MCCalculus::Triangle(P.Center-NormalOffset*.4f,B,A,-P.Normal,Color*.83f,Vertices,Indices,Normals,UV,Colors,Tangents);
        }
    }
    if(Indices.IsEmpty()) Deposits->ClearAllMeshSections();
    else Deposits->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
    Deposits->SetVisibility(!Indices.IsEmpty());
}

FVector UMCToothCalculusComponent::PieceContact(int32 Index,FVector& Normal) const
{
    const auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth || !Tooth->Visual || !SurfacePieces.IsValidIndex(Index) || !State.Pieces.IsValidIndex(Index)) return FVector::ZeroVector;
    const FPiece& P=SurfacePieces[Index];
    const float Fraction=FMath::Clamp(float(State.Pieces[Index])/MCCalculus::FullStage,0.f,1.f);
    const FVector Scale=FVector(State.GrowthScale).GetAbs().ComponentMax(FVector(.001f));
    const FTransform T=Tooth->Visual->GetComponentTransform();
    Normal=MCCalculus::WorldNormal(T,P.Normal);
    const FVector NormalOffset=(P.Normal/Scale).GetSafeNormal()/Scale;
    const FVector Peak=P.Center+NormalOffset*(P.Depth*(.35f+.65f*Fraction))
        +(P.Side*(P.Radius*.15f*FMath::Sin(Index*2.17f+P.Variation*8))
            +P.Up*(P.Radius*.13f*FMath::Cos(Index*1.91f+P.Variation*9)))/Scale;
    return T.TransformPosition(Peak);
}

bool UMCToothCalculusComponent::CanReachContact(const AMCToothCharacter* Worker,FVector Point,FVector Normal,bool bImpact) const
{
    const auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!IsValid(Worker) || !Tooth || !Tooth->IsAvailable() || !Worker->Status || !Worker->Status->IsAlive()
        || !Worker->CanWork() || !Worker->BrushContact || Point.ContainsNaN() || Normal.ContainsNaN()
        || !Worker->BrushContact->CanAcquireSurface(Tooth) || !Worker->BrushContact->CanBrushToward(Point)) return false;
    const FVector D=Point-Worker->GetActorLocation();
    if(D.Size2D()>Worker->BrushContact->SurfaceReach || FMath::Abs(D.Z)>Worker->BrushContact->MaxHandVerticalTravel+40
        || FVector::DotProduct(Normal,(Worker->GetActorLocation()+FVector(0,0,55)-Point).GetSafeNormal())<.05f
        || (bImpact && FVector::DotProduct(D.GetSafeNormal2D(),Worker->GetActorForwardVector())<.75f)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCalculusOcclusion),false,Worker);
    Query.AddIgnoredActor(Tooth);
    FHitResult Block;
    return !GetWorld()->LineTraceSingleByChannel(Block,Worker->GetActorLocation()+FVector(0,0,40),Point-Normal*2,ECC_Visibility,Query);
}

bool UMCToothCalculusComponent::FindContact(AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const
{
    if(!HasCalculus() || !IsValid(Worker) || SurfacePieces.Num()!=State.Pieces.Num()) return false;
    const FVector Aim=Worker->GetActorLocation()+Worker->GetActorForwardVector()*65+FVector(0,0,65);
    float Best=MAX_flt;
    for(int32 I=0;I<SurfacePieces.Num();++I)
    {
        if(State.Pieces[I]==0) continue;
        FVector N;
        const FVector P=PieceContact(I,N);
        const float Score=FVector::DistSquared(P,Aim);
        if(Score>=Best || !CanReachContact(Worker,P,N,false)) continue;
        Point=P; Normal=N; Best=Score;
    }
    return Best<MAX_flt;
}

bool UMCToothCalculusComponent::ApplyPickaxeHit(AMCToothCharacter* Worker,FVector Point,FVector Normal,float Damage)
{
    auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    if(!Tooth || !Tooth->HasAuthority() || !IsValid(Worker) || !Worker->HasAuthority()
        || !Worker->Inventory || Worker->Inventory->Selected!=EMCToolSlot::Pickaxe || !HasCalculus()
        || Point.ContainsNaN() || Normal.ContainsNaN() || !FMath::IsFinite(Damage) || Damage<=0
        || !CanReachContact(Worker,Point,Normal.GetSafeNormal(),true)) return false;
    int32 Closest=INDEX_NONE;
    float Best=MAX_flt;
    FVector ActualNormal;
    for(int32 I=0;I<SurfacePieces.Num() && I<State.Pieces.Num();++I)
    {
        if(State.Pieces[I]==0) continue;
        FVector N;
        const FVector P=PieceContact(I,N);
        const float Distance=FVector::DistSquared(P,Point);
        if(Distance<Best && Distance<FMath::Square(SurfacePieces[I].Radius+12.f))
        { Closest=I; Best=Distance; ActualNormal=N; }
    }
    if(Closest==INDEX_NONE || FVector::DotProduct(ActualNormal,Normal.GetSafeNormal())<.4f) return false;
    const int32 DamageStages=FMath::Clamp(FMath::CeilToInt(FMath::Min(Damage,75.f)/25.f),1,3);
    const FVector ActualPoint=PieceContact(Closest,ActualNormal);
    for(int32 I=0;I<SurfacePieces.Num() && I<State.Pieces.Num();++I)
    {
        if(State.Pieces[I]==0) continue;
        FVector N;
        const FVector P=PieceContact(I,N);
        const float Distance=FVector::Dist(P,ActualPoint);
        const int32 Chip=I==Closest?DamageStages:Distance<23.f?1:0;
        State.Pieces[I]=uint8(FMath::Max(0,int32(State.Pieces[I])-Chip));
    }
    State.HitPoint=Tooth->Visual->GetComponentTransform().InverseTransformPosition(Point);
    State.HitNormal=MCCalculus::LocalNormal(Tooth->Visual->GetComponentTransform(),ActualNormal);
    State.HitAt=MCCalculus::Time(GetWorld()); ++State.HitSerial;
    BuildDepositMesh();
    PlayedHit=State.HitSerial;
    EmitShards(Point,ActualNormal,State.HitSerial);
    Tooth->ForceNetUpdate();
    return true;
}

void UMCToothCalculusComponent::OnRep_State()
{
    auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
    EnsureMeshes();
    if(!Tooth || !Tooth->Visual) return;
    if(CachedGrowth!=State.GrowthSerial || CachedMesh!=Tooth->Visual->GetStaticMesh()) RebuildForSurface();
    else BuildDepositMesh();
    // State reconstructs old holes; only a recent new impact emits cosmetic fragments.
    if(HasBegunPlay() && State.HitSerial!=PlayedHit && MCCalculus::Time(GetWorld())-State.HitAt<.7)
    {
        const FTransform T=Tooth->Visual->GetComponentTransform();
        EmitShards(T.TransformPosition(State.HitPoint),MCCalculus::WorldNormal(T,State.HitNormal),State.HitSerial);
    }
    PlayedHit=State.HitSerial;
}

void UMCToothCalculusComponent::EmitShards(FVector Point,FVector Normal,int32 Serial)
{
    if(!FApp::CanEverRender() || GetNetMode()==NM_DedicatedServer) return;
    EnsureMeshes();
    FRandomStream Random(State.Seed^Serial*7919);
    const FVector Side=FVector::CrossProduct(Normal,FVector::UpVector).GetSafeNormal(.001f,FVector::RightVector);
    for(int32 I=0;I<6;++I)
    {
        if(Shards.Num()>=MCCalculus::MaxShards) Shards.RemoveAt(0);
        FShard Shard;
        Shard.Point=Point+Normal*3+Side*Random.FRandRange(-4.f,4.f);
        Shard.Velocity=Normal*Random.FRandRange(90.f,170.f)+Side*Random.FRandRange(-85.f,85.f)+FVector(0,0,Random.FRandRange(70.f,150.f));
        Shard.Rotation=FRotator(Random.FRandRange(-180.f,180.f),Random.FRandRange(-180.f,180.f),Random.FRandRange(-180.f,180.f));
        Shard.Spin=FRotator(Random.FRandRange(-500.f,500.f),Random.FRandRange(-500.f,500.f),Random.FRandRange(-500.f,500.f));
        Shard.Size=Random.FRandRange(2.f,5.f); Shard.Lifetime=Random.FRandRange(.55f,.8f);
        Shard.Color=MCCalculus::StoneColor(Random.FRandRange(.4f,.95f));
        Shards.Add(Shard);
    }
    UpdateShardMesh(); SetComponentTickEnabled(true);
}

void UMCToothCalculusComponent::UpdateShardMesh()
{
    if(!Debris) return;
    if(Shards.IsEmpty()) { Debris->ClearAllMeshSections(); return; }
    TArray<FVector> Vertices,Normals; TArray<int32> Indices; TArray<FVector2D> UV;
    TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
    const FVector Corners[]={FVector(1,0,-.6),FVector(-.7,.8,-.5),FVector(-.7,-.8,-.5),FVector(0,0,1)};
    constexpr int32 Faces[][3]={{0,1,2},{0,3,1},{1,3,2},{2,3,0}};
    for(const FShard& S:Shards)
    {
        const float Fade=1-FMath::SmoothStep(S.Lifetime-.18f,S.Lifetime,S.Age);
        FVector Points[4];
        for(int32 I=0;I<4;++I) Points[I]=S.Point+S.Rotation.RotateVector(Corners[I]*S.Size*Fade);
        for(const auto& F:Faces)
        {
            const FVector Outside=(Points[F[0]]+Points[F[1]]+Points[F[2]])/3-S.Point;
            MCCalculus::Triangle(Points[F[0]],Points[F[1]],Points[F[2]],Outside,S.Color,Vertices,Indices,Normals,UV,Colors,Tangents);
        }
    }
    Debris->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}

void UMCToothCalculusComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(PendingGrowFrames>0)
    {
        --PendingGrowFrames;
        const auto* Tooth=Cast<AMCArenaTooth>(GetOwner());
        if(Tooth && Tooth->Visual && Tooth->Visual->GetStaticMesh() && Tooth->BrushSurface && Tooth->BrushSurface->IsPhysicsStateCreated())
            GrowCalculus(PendingSeed,PendingPatches);
    }
    if(RetryFrames>0)
    {
        --RetryFrames;
        if(BuildSurfacePieces()) { RetryFrames=0; BuildDepositMesh(); }
    }
    const float Step=FMath::Clamp(Dt,0.f,.05f);
    for(int32 I=Shards.Num()-1;I>=0;--I)
    {
        auto& S=Shards[I]; S.Age+=FMath::Max(0.f,Dt);
        if(S.Age>=S.Lifetime) { Shards.RemoveAtSwap(I); continue; }
        S.Velocity.Z-=680.f*Step; S.Point+=S.Velocity*Step; S.Rotation+=S.Spin*Step;
    }
    UpdateShardMesh();
    if(RetryFrames==0 && PendingGrowFrames==0 && Shards.IsEmpty()) SetComponentTickEnabled(false);
}
