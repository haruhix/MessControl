#include "MCFreezeDeathVFX.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace MCFreezeDeathPrivate
{
    constexpr int32 Across=3,High=4,Count=Across*Across*High;
    struct FMesh
    {
        TArray<FVector> Vertices,Normals;
        TArray<FVector2D> UV;
        TArray<int32> Indices;
        TArray<FProcMeshTangent> Tangents;
        void Triangle(FVector A,FVector B,FVector C,FVector NA,FVector NB,FVector NC,
            FVector2D UA,FVector2D UB,FVector2D UC)
        {
            const int32 I=Vertices.Num();Vertices.Append({A,B,C});Normals.Append({NA,NB,NC});UV.Append({UA,UB,UC});
            Indices.Append({I,I+1,I+2});
            const FVector T=(B-A).GetSafeNormal();Tangents.Append({FProcMeshTangent(T,false),FProcMeshTangent(T,false),FProcMeshTangent(T,false)});
        }
        void Quad(FVector A,FVector B,FVector C,FVector D,FVector N)
        {
            Triangle(A,B,C,N,N,N,FVector2D(0,0),FVector2D(1,0),FVector2D(1,1));
            Triangle(A,C,D,N,N,N,FVector2D(0,0),FVector2D(1,1),FVector2D(0,1));
        }
        void Box(FVector E)
        {
            const float X=E.X,Y=E.Y,Z=E.Z;
            Quad({X,-Y,-Z},{X,Y,-Z},{X,Y,Z},{X,-Y,Z},{1,0,0});
            Quad({-X,Y,-Z},{-X,-Y,-Z},{-X,-Y,Z},{-X,Y,Z},{-1,0,0});
            Quad({-X,Y,-Z},{-X,Y,Z},{X,Y,Z},{X,Y,-Z},{0,1,0});
            Quad({X,-Y,-Z},{X,-Y,Z},{-X,-Y,Z},{-X,-Y,-Z},{0,-1,0});
            Quad({-X,-Y,Z},{X,-Y,Z},{X,Y,Z},{-X,Y,Z},{0,0,1});
            Quad({-X,Y,-Z},{X,Y,-Z},{X,-Y,-Z},{-X,-Y,-Z},{0,0,-1});
        }
        void Apply(UProceduralMeshComponent* Component,int32 Section)
        {
            if(Indices.IsEmpty())return;
            const auto* Existing=Component->GetProcMeshSection(Section);
            if(Existing&&Existing->ProcVertexBuffer.Num()==Vertices.Num()&&Existing->ProcIndexBuffer.Num()==Indices.Num())
                Component->UpdateMeshSection_LinearColor(Section,Vertices,Normals,UV,{},Tangents);
            else Component->CreateMeshSection_LinearColor(Section,Vertices,Indices,Normals,UV,{},Tangents,false);
            // Native compatibility can still recreate sections/RHI when topology
            // changes. This is not the prepared NS allocation/readiness claim.
        }
    };
}

AMCFreezeDeathVFX::AMCFreezeDeathVFX()
{
    PrimaryActorTick.bCanEverTick=true;bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FrozenDeathRoot")));
    IceCube=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransparentIceCube"));IceCube->SetupAttachment(GetRootComponent());
    IceCube->SetCollisionEnabled(ECollisionEnabled::NoCollision);IceCube->SetCastShadow(false);IceCube->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glass(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathGlass"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Body(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathBody"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Bag(TEXT("/Game/Gameplay/Cold/Death/M_FrozenDeathBag"));
    GlassMaterial=Glass.Object;BodyMaterial=Body.Object;BagMaterial=Bag.Object;
}
AMCFreezeDeathVFX* AMCFreezeDeathVFX::SpawnLocal(AMCToothCharacter* Hero,double At)
{
    if(!IsValid(Hero)||Hero->GetNetMode()==NM_DedicatedServer||!Hero->GetMesh()->GetSkeletalMeshAsset())return nullptr;
    if(auto* Prepared=Hero->GetPreparedFreezeDeathVisual()) {Prepared->CommitPrepared(Hero,At);return Prepared;}
    // Preserve the original native artwork when opt-in preparation is absent.
    // This deliberately retains its old first-use CPU/RHI/MID cost, not a
    // migrated/readiness success. No new NS is guessed or loaded here.
    FActorSpawnParameters P;P.Owner=Hero;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* FX=Hero->GetWorld()->SpawnActor<AMCFreezeDeathVFX>(Hero->GetActorLocation(),FRotator(0,Hero->GetActorRotation().Yaw,0),P);
    if(FX){FX->Source=Hero;FX->SetActorHiddenInGame(true);FX->CommitPrepared(Hero,At);}
    return FX;
}
UProceduralMeshComponent* AMCFreezeDeathVFX::MakePart(FName Name)
{
    auto* Part=NewObject<UProceduralMeshComponent>(this,Name);Part->SetupAttachment(GetRootComponent());
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);Part->bReceivesDecals=false;
    Part->RegisterComponent();AddInstanceComponent(Part);return Part;
}
void AMCFreezeDeathVFX::Capture(AMCToothCharacter* Hero)
{
    using namespace MCFreezeDeathPrivate;
    auto* Mesh=Hero->GetMesh();auto* Data=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
    if(!Data || Data->LODRenderData.IsEmpty() || !Mesh->GetMeshObject()) return;
    // One capture per fatal freeze, including the facial morphs. Never evaluate CPU skinning per tick.
    TArray<FFinalSkinVertex> Skin;Mesh->GetCPUSkinnedVertices(Skin,0);
    if(Skin.IsEmpty()) return;
    const auto& LOD=Data->LODRenderData[0];const auto* Index=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    if(!Index) return;
    TArray<FVector> Positions,Normals;TArray<FVector2D> UV;FBox Bounds(ForceInit);
    const FTransform MeshWorld=Mesh->GetComponentTransform();const FTransform EffectWorld=GetActorTransform();
    for(const auto& V:Skin)
    {
        const FVector P=EffectWorld.InverseTransformPosition(MeshWorld.TransformPosition(FVector(V.Position)));
        Positions.Add(P);Bounds+=P;
        Normals.Add(EffectWorld.InverseTransformVectorNoScale(MeshWorld.TransformVectorNoScale(FVector(V.TangentZ.ToFVector3f()))).GetSafeNormal());
        UV.Add(V.TextureCoordinates[0]);
    }
    const FVector Center=Bounds.GetCenter();
    HalfSize=FMath::Max(70.f,Bounds.GetExtent().GetMax()+15.f);
    const FVector CubeCenter(Center.X,Center.Y,Bounds.Min.Z+HalfSize-4.f);
    IceCube->SetRelativeLocation(CubeCenter);
    FMesh Cube;Cube.Box(FVector(HalfSize));Cube.Apply(IceCube,0);
    if(!NativeGlass)NativeGlass=UMaterialInstanceDynamic::Create(GlassMaterial,IceCube);IceCube->SetMaterial(0,NativeGlass);
    TArray<UMaterialInstanceDynamic*> Materials;
    for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
    {
        auto* Material=NativeMaterials.IsValidIndex(Slot)?NativeMaterials[Slot].Get():UMaterialInstanceDynamic::Create(Slot==1?BagMaterial.Get():BodyMaterial.Get(),this);
        if(auto* Original=Mesh->GetMaterial(Slot)) Material->CopyMaterialUniformParameters(Original);
        Material->SetScalarParameterValue(TEXT("BodyStretch"),0);
        Material->SetScalarParameterValue(TEXT("Damage"),0);
        Material->SetScalarParameterValue(TEXT("Coffee"),0);
        Materials.Add(Material);
    }
    FString MotionError;
    if(!PoseCapture.IssueMotionForNativeFallback(Bounds,MotionError)){LastPresentationError=MotionError;return;}
    const auto Samples=PoseCapture.GetIssuedMotion();
    const FVector Cell(HalfSize*2/Across,HalfSize*2/Across,HalfSize*2/High);
    TArray<TArray<FMesh>> Groups;Groups.SetNum(Count);
    for(auto& Group:Groups) Group.SetNum(Materials.Num());
    for(int32 Z=0;Z<High;++Z) for(int32 Y=0;Y<Across;++Y) for(int32 X=0;X<Across;++X)
    {
        const int32 I=X+Across*(Y+Across*Z);
        const FVector Home=CubeCenter-FVector(HalfSize)+Cell*FVector(X+.5f,Y+.5f,Z+.5f);
        const auto& Sample=Samples[I];FShard M{Sample.Home,Sample.Velocity,Sample.Spin,Sample.Floor};Motion.Add(M);
        auto* Body=BodyFragments.IsValidIndex(I)?BodyFragments[I].Get():MakePart(*FString::Printf(TEXT("FrozenBodyFragment%d"),I));Body->SetRelativeLocation(Home);Body->SetVisibility(true);if(!BodyFragments.IsValidIndex(I))BodyFragments.Add(Body);
        auto* Ice=IceFragments.IsValidIndex(I)?IceFragments[I].Get():MakePart(*FString::Printf(TEXT("IceCubeFragment%d"),I));Ice->SetRelativeLocation(Home);Ice->SetCastShadow(false);
        FMesh Shard;Shard.Box(Cell*.47f);Shard.Apply(Ice,0);Ice->SetMaterial(0,GlassMaterial);Ice->SetVisibility(false);if(!IceFragments.IsValidIndex(I))IceFragments.Add(Ice);
    }
    for(const auto& Section:LOD.RenderSections)
    {
        const int32 Slot=Section.MaterialIndex;if(!Groups[0].IsValidIndex(Slot)) continue;
        for(uint32 T=0;T<Section.NumTriangles;++T)
        {
            const uint32 A=Index->Get(Section.BaseIndex+T*3),B=Index->Get(Section.BaseIndex+T*3+1),C=Index->Get(Section.BaseIndex+T*3+2);
            if(!Positions.IsValidIndex(A) || !Positions.IsValidIndex(B) || !Positions.IsValidIndex(C)) continue;
            const FVector P=((Positions[A]+Positions[B]+Positions[C])/3-CubeCenter+FVector(HalfSize))/Cell;
            const int32 I=FMath::Clamp(FMath::FloorToInt(P.X),0,Across-1)+Across*(FMath::Clamp(FMath::FloorToInt(P.Y),0,Across-1)+Across*FMath::Clamp(FMath::FloorToInt(P.Z),0,High-1));
            const FVector Home=Motion[I].Home;
            Groups[I][Slot].Triangle(Positions[A]-Home,Positions[B]-Home,Positions[C]-Home,Normals[A],Normals[B],Normals[C],UV[A],UV[B],UV[C]);
        }
    }
    for(int32 I=0;I<Count;++I) for(int32 Slot=0;Slot<Materials.Num();++Slot)
    {if(Groups[I][Slot].Indices.IsEmpty())BodyFragments[I]->ClearMeshSection(Slot);else Groups[I][Slot].Apply(BodyFragments[I],Slot);BodyFragments[I]->SetMaterial(Slot,Materials[Slot]);}
    bNativeGeometryWritten=true;NativeGeometryWrittenFrame=GFrameCounter;
}
void AMCFreezeDeathVFX::Tick(float Dt)
{
    Super::Tick(Dt);if(!IsValid(Source)){Destroy();return;}if(!bCommitted)return;
    const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Age=FMath::Max(0.f,float(Now-StartedAt));const float BreakAge=AMCToothCharacter::FreezeDeathHoldSeconds;
    const bool Shatter=Age>=BreakAge;
    if(Shatter&&!bShatterAccepted){FString Error;StartPreparedPhase(true,Now,Error);if(!Error.IsEmpty())LastPresentationError=Error;}
    if(Shatter&&!bShatterAudioIssued){bShatterAudioIssued=true;DispatchAcceptedPhaseAudio(true,Now);}
    const bool Native=GetPhaseMode(Shatter)==EMCPerkVFXSlotMode::UseDefault||CaptureFallback[Shatter?1:0];
    if(Native&&EnsureNativeGeometry()) {
        if(bNativePending) {
            if(!bNativeFenceQueued&&GFrameCounter>NativeGeometryWrittenFrame){NativeRenderFence.BeginFence();NativeFenceFrame=GFrameCounter;bNativeFenceQueued=true;}
            if(bNativeFenceQueued&&GFrameCounter>NativeFenceFrame&&NativeRenderFence.IsFenceComplete())bNativePending=false;
        }
        bNativeVisible=!bNativePending;SetActorHiddenInGame(!bNativeVisible);
    } else {bNativeVisible=false;SetActorHiddenInGame(true);}
    auto* Player=Shatter?ShatterPlayer.Get():HoldPlayer.Get();
    bNiagaraVisible=!Native&&PhaseHasCapture(Shatter)&&Player&&Player->HasVisibleCapturedFreezeBody();
    if(bNativeVisible||bNiagaraVisible){Source->GetMesh()->SetHiddenInGame(true,true);bOwnsHeroHide=true;}
    else if(bOwnsHeroHide){Source->GetMesh()->SetHiddenInGame(false,true);bOwnsHeroHide=false;}
    // Never infer a native fallback from no visible particles. Disabled groups,
    // artist delay/stop and ordinary-only replacements keep the gameplay body.
    if(bNativeGeometryWritten) {
        if(auto* Material=Cast<UMaterialInstanceDynamic>(IceCube->GetMaterial(0)))Material->SetScalarParameterValue(TEXT("Cracks"),FMath::SmoothStep(.75f,BreakAge,Age));
        if(Shatter) {
            if(!bShattered){bShattered=true;IceCube->SetVisibility(false);for(UProceduralMeshComponent* Ice:IceFragments)Ice->SetVisibility(true);}
            const float T=Age-BreakAge,Fade=1-FMath::SmoothStep(1.25f,2.15f,T);
            for(int32 I=0;I<Motion.Num();++I) {
                const auto& M=Motion[I];FVector P=M.Home+M.Velocity*T-FVector(0,0,290*T*T);
                if(P.Z<M.Floor){P.Z=M.Floor+FMath::Abs(FMath::Sin(T*8+I))*(1-FMath::Clamp(T/1.5f,0.f,1.f))*8;P.X=FMath::Lerp(M.Home.X,P.X,.75f);P.Y=FMath::Lerp(M.Home.Y,P.Y,.75f);}
                const FRotator R(M.Spin.X*T,M.Spin.Y*T,M.Spin.Z*T);
                for(auto* Part:{BodyFragments[I].Get(),IceFragments[I].Get()}){Part->SetRelativeLocationAndRotation(P,R);Part->SetRelativeScale3D(FVector(Fade));}
            }
        }
    }
    if(Age>=BreakAge+2.2f){bNaturalPresentationEnd=true;Destroy();}
}

bool AMCFreezeDeathVFX::PrepareNative(AMCToothCharacter* Hero,FString& Error)
{
    if(bNativePrepared)return Source==Hero;
    if(!IsValid(Hero)||Hero->GetWorld()!=GetWorld()||Hero->GetNetMode()==NM_DedicatedServer||!GlassMaterial||!BodyMaterial||!BagMaterial)
    {Error=TEXT("Exact native freeze assets/owner are unavailable during preparation.");return false;}
    Source=Hero;SetActorHiddenInGame(true);SetActorTickEnabled(false);
    NativeGlass=UMaterialInstanceDynamic::Create(GlassMaterial,this);IceCube->SetMaterial(0,NativeGlass);
    MCFreezeDeathPrivate::FMesh Cube;Cube.Box(FVector(1));Cube.Apply(IceCube,0);
    for(int32 Slot=0;Slot<Hero->GetMesh()->GetNumMaterials();++Slot)
    {auto* M=UMaterialInstanceDynamic::Create(Slot==1?BagMaterial.Get():BodyMaterial.Get(),this);NativeMaterials.Add(M);}
    for(int32 I=0;I<36;++I)
    {
        auto* Body=MakePart(*FString::Printf(TEXT("FrozenBodyFragment%d"),I));Body->SetVisibility(false);BodyFragments.Add(Body);
        auto* Ice=MakePart(*FString::Printf(TEXT("IceCubeFragment%d"),I));Ice->SetCastShadow(false);Cube.Apply(Ice,0);
        Ice->SetMaterial(0,GlassMaterial);Ice->SetVisibility(false);IceFragments.Add(Ice);
        for(int32 Slot=0;Slot<NativeMaterials.Num();++Slot)
        {MCFreezeDeathPrivate::FMesh Tiny;Tiny.Triangle({0,0,0},{.0001,0,0},{0,.0001,0},{0,0,1},{0,0,1},{0,0,1},{0,0},{1,0},{0,1});Tiny.Apply(Body,Slot);Body->SetMaterial(Slot,NativeMaterials[Slot]);}
    }
    RefreshPreparedUniforms();bNativePrepared=true;
    bPosePrepared=PoseCapture.Prepare(Hero->GetMesh(),Error);
    // A missing optional CPU conversion keeps the exact prepared native path.
    if(!bPosePrepared)LastPresentationError=Error;
    return true;
}

void AMCFreezeDeathVFX::RefreshPreparedUniforms()
{
    if(!Source||!Source->GetMesh())return;
    auto Update=[this](UMaterialInstanceDynamic* M,int32 Slot)
    {if(!M)return;if(auto* Original=Source->GetMesh()->GetMaterial(Slot))M->CopyMaterialUniformParameters(Original);
     M->SetScalarParameterValue(TEXT("BodyStretch"),0);M->SetScalarParameterValue(TEXT("Damage"),0);M->SetScalarParameterValue(TEXT("Coffee"),0);};
    for(int32 I=0;I<NativeMaterials.Num();++I)Update(NativeMaterials[I],I);
    for(int32 I=0;I<NiagaraMaterials.Num();++I)Update(NiagaraMaterials[I],I);
    for(auto* Player:{HoldPlayer.Get(),ShatterPlayer.Get()})if(IsValid(Player))Player->RefreshPreparedFreezeMaterialUniforms();
}

bool AMCFreezeDeathVFX::PrepareNiagara(AMCToothCharacter* Hero,FGuid Epoch,const FMCPreparedPerkVFXComposition& Hold,const FMCPreparedPerkVFXComposition& Shatter,
    UMaterialInterface* BodyMaster,UMaterialInterface* BagMaster,UMaterialInterface* GlassMaster,FString& Error)
{
    if(bCommitted||!IsValid(Hero)||Hero->GetWorld()!=GetWorld()||(Source&&Source!=Hero)||bNiagaraPrepared)
    {Error=TEXT("Prepare the actual uncommitted freeze event owner once before readiness.");return false;}
    Source=Hero;SetActorHiddenInGame(true);SetActorTickEnabled(false);
    HoldDefinition=Hold;ShatterDefinition=Shatter;bFreezePolicyPrepared=true;PreparedEpoch=Epoch;
    PreparedSubject=Hero->GetMesh();PreparedAppearance=Hero->GetMesh()->GetSkeletalMeshAsset();
    for(int32 Slot=0;Slot<Hero->GetMesh()->GetNumMaterials();++Slot)PreparedSourceMaterials.Add(Hero->GetMesh()->GetMaterial(Slot));
    int32 Captured=0;FString CapturePreparationError,NativePreparationError;
    for(const auto* D:{&HoldDefinition,&ShatterDefinition}) {
        if(D->Outer.Mode==EMCPerkVFXSlotMode::UseDefault||D->Outer.Mode==EMCPerkVFXSlotMode::Disabled)continue;
        if(!D->Snapshot||D->Systems.Num()!=D->Snapshot->Layers.Num()||D->Systems.Num()>64)
        {Error=TEXT("Custom freeze event requires its actual shared prepared snapshot.");return false;}
        for(int32 I=0;I<D->Systems.Num();++I)if(D->Snapshot->Layers[I].bEnabled&&FMCPreparedFreezeGeometryLayer::UsesCapture(D->Systems[I]))++Captured;
    }
    if(Captured>4)CapturePreparationError=TEXT("One fatal episode supports at most four captured-body layers; independent ordinary rows remain eligible.");
    const bool AnyDefault=Hold.Outer.Mode==EMCPerkVFXSlotMode::UseDefault||Shatter.Outer.Mode==EMCPerkVFXSlotMode::UseDefault;
    if(Captured||AnyDefault)PrepareNative(Hero,NativePreparationError); // Failure cannot erase another ordinary event plan.
    if(Captured&&CapturePreparationError.IsEmpty()) {
        if(!bPosePrepared||Geometry)
            CapturePreparationError=!NativePreparationError.IsEmpty()?NativePreparationError:
                TEXT("Enabled captured body rows lack the prepared actual pose.");
        else {
            Geometry=MakeShared<FMCFreezeCapturedGeometry>();
            if(!Geometry->Prepare(Source->GetMesh(),Epoch,PoseCapture.GetTriangleSlots(),Source->GetMesh()->GetNumMaterials(),CapturePreparationError))Geometry.Reset();
            if(Geometry&&BodyMaster&&BagMaster&&GlassMaster) {
                // Compatibility for existing explicit callers. Normal enrollment
                // passes null: each captured NS owns its exact saved defaults.
                for(int32 Slot=0;Slot<Source->GetMesh()->GetNumMaterials();++Slot)
                    NiagaraMaterials.Add(UMaterialInstanceDynamic::Create(Slot==1?BagMaster:BodyMaster,this));
                NiagaraGlass=UMaterialInstanceDynamic::Create(GlassMaster,this);RefreshPreparedUniforms();
            }
        }
    }
    TArray<UMaterialInterface*> Materials;for(UMaterialInstanceDynamic* M:NiagaraMaterials)Materials.Add(M);
    auto PrepareEvent=[&](bool ShatterEvent,TObjectPtr<AMCPerkVFXCompositionPlayer>& Player) {
        const auto& D=ShatterEvent?ShatterDefinition:HoldDefinition;
        if(D.Outer.Mode==EMCPerkVFXSlotMode::UseDefault||D.Outer.Mode==EMCPerkVFXSlotMode::Disabled)return true;
        bool Enabled=false;for(const auto& L:D.Snapshot->Layers)Enabled|=L.bEnabled;if(!Enabled)return true;
        FActorSpawnParameters P;P.ObjectFlags=RF_Transient;P.Owner=GetWorld()->GetWorldSettings();P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Player=GetWorld()->SpawnActor<AMCPerkVFXCompositionPlayer>(FVector::ZeroVector,FRotator::ZeroRotator,P);
        const TSharedPtr<FMCFreezeCapturedGeometry> EventGeometry=PhaseHasCapture(ShatterEvent)?Geometry:TSharedPtr<FMCFreezeCapturedGeometry>();
        if(!Player||!Player->PrepareFreezeCaptureLayers(D,Source->GetMesh(),EventGeometry,Epoch,Materials,NiagaraGlass,Error))return false;
        if(Player->HasRejectedCapturedFreezeLayers()&&CapturePreparationError.IsEmpty())CapturePreparationError=Player->GetFreezeCaptureAdmissionError();
        return true;
    };
    if(!PrepareEvent(false,HoldPlayer)||!PrepareEvent(true,ShatterPlayer)) {
        // A genuinely invalid ordinary plan remains an atomic profile error.
        // Capture-specific failure never reaches this branch.
        StopPreparedNiagara();Geometry.Reset();NiagaraMaterials.Reset();NiagaraGlass=nullptr;return false;
    }
    bNiagaraPrepared=true; // Event plan retained, even if capture readiness fails.
    if(!CapturePreparationError.IsEmpty()||!NativePreparationError.IsEmpty()) {
        LastPresentationError=CapturePreparationError.IsEmpty()?NativePreparationError:CapturePreparationError;
        Error=TEXT("Captured/native preparation is incomplete; independent ordinary event rows remain prepared: ")+LastPresentationError;
        return false; // Honest location readiness failure, not permission to drop the ordinary plan.
    }
    return true;
}

bool AMCFreezeDeathVFX::ValidatePreparation(const AMCToothCharacter* Hero,FGuid Epoch,FString& Error) const
{
    Error.Reset();
    const auto* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!IsValid(Hero)||Hero!=Source||Hero->GetWorld()!=GetWorld()||bCommitted||!bNiagaraPrepared
        ||!bFreezePolicyPrepared||Epoch!=PreparedEpoch||!Epoch.IsValid()||!Hero->PassivePresentation
        ||(State&&State->GameplayLoadId.IsValid()&&State->GameplayLoadId!=Epoch)
        ||PreparedSubject.Get()!=Hero->GetMesh()||PreparedAppearance.Get()!=Hero->GetMesh()->GetSkeletalMeshAsset()
        ||PreparedSourceMaterials.Num()!=Hero->GetMesh()->GetNumMaterials())
    {Error=TEXT("Freeze readiness is not bound to this uncommitted actual hero/world/epoch.");return false;}
    for(int32 Slot=0;Slot<PreparedSourceMaterials.Num();++Slot)if(PreparedSourceMaterials[Slot].Get()!=Hero->GetMesh()->GetMaterial(Slot))
    {Error=TEXT("Freeze readiness's actual source material identity changed; explicitly reprepare before the barrier.");return false;}
    for(bool Shatter:{false,true}) {
        const auto* Actual=Hero->PassivePresentation->GetPreparedSourceEvent(TEXT("Character.FreezeDeath"),Shatter?TEXT("Shatter"):TEXT("Hold"));
        const auto& Retained=Shatter?ShatterDefinition:HoldDefinition;
        if(!Actual||Actual->Snapshot!=Retained.Snapshot||Actual->Systems!=Retained.Systems||Actual->Outer.Mode!=Retained.Outer.Mode)
        {Error=TEXT("Actual Freeze shared event changed after owner preparation; explicitly reprepare before readiness.");return false;}
        if(Actual->Outer.Mode==EMCPerkVFXSlotMode::UseDefault&&!bNativePrepared)
        {Error=TEXT("Original native Freeze presentation resources are not prepared.");return false;}
        // Default explicitly retains the legacy native topology/CPU-capture
        // fallback if optional resident pose preparation was unavailable. This
        // validates its retained resources, NOT migration or zero-first-use cost.
        const auto* Player=Shatter?ShatterPlayer.Get():HoldPlayer.Get();
        if(PhaseHasCapture(Shatter)&&(!Geometry||!Geometry->ValidateAcceptedIdentity(Hero->GetMesh(),Epoch,Error)
            ||!Player||Player->HasRejectedCapturedFreezeLayers())) {
            if(Error.IsEmpty())Error=Player?Player->GetFreezeCaptureAdmissionError():TEXT("Enabled captured Freeze row has no prepared player.");
            return false;
        }
    }
    return true;
}

void AMCFreezeDeathVFX::GetPreparedResources(TArray<UObject*>& Out) const
{
    for(UMaterialInstanceDynamic* M:NativeMaterials)if(IsValid(M))Out.AddUnique(M);
    for(UMaterialInstanceDynamic* M:NiagaraMaterials)if(IsValid(M))Out.AddUnique(M);
    if(IsValid(NativeGlass))Out.AddUnique(NativeGlass);if(IsValid(NiagaraGlass))Out.AddUnique(NiagaraGlass);
    for(auto* Player:{HoldPlayer.Get(),ShatterPlayer.Get()})if(IsValid(Player))Player->GetPreparedFreezeResources(Out);
}

void AMCFreezeDeathVFX::StopPreparedNiagara()
{
    for(auto* Player:{HoldPlayer.Get(),ShatterPlayer.Get()})if(IsValid(Player))Player->StopEmission(true);
    HoldPlayer=nullptr;ShatterPlayer=nullptr;bNiagaraPrepared=false;bNiagaraVisible=false;
}

bool AMCFreezeDeathVFX::StartPreparedPhase(bool Shatter,double Now,FString& Error)
{
    if(Shatter&&ShatterAcceptedAt<0)ShatterAcceptedAt=Now;
    if(Shatter&&!bShatterAccepted&&Source&&Source->PassivePresentation)
        Source->PassivePresentation->RetireLocalSourceAudioEvent(TEXT("Character.FreezeDeath"),TEXT("Hold"));
    const auto Mode=GetPhaseMode(Shatter);auto* Player=Shatter?ShatterPlayer.Get():HoldPlayer.Get();
    const double EventAt=Shatter?ShatterAcceptedAt:StartedAt;const float LocalAge=FMath::Max(0.f,float(Now-EventAt));
    const bool Captured=PhaseHasCapture(Shatter),RelevantCapture=PhaseHasCapture(Shatter,LocalAge);bool Started=true;
    if(Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition) {
        if(Player) {
            FMCPerkVFXGameplayValues Values;if(Captured)Values.FreezeCapture=Geometry;
            Values.FreezeEventAcceptedAt=EventAt;Values.LocalStateAgeSeconds=LocalAge;Values.VisualSubject=Source->GetMesh();
            Started=Player->StartPrepared(Shatter?ShatterDefinition:HoldDefinition,GetActorTransform(),Source->GetMesh(),nullptr,false,Values,Error,LocalAge);
            if(RelevantCapture&&(!Started||Player->HasRejectedCapturedFreezeLayers())) {
                // A failed captured row must not erase ordinary rows or duplicate a
                // separately admitted captured group with an old native body.
                CaptureFallback[Shatter?1:0]=!Player->HasAdmittedCapturedFreezeLayers();
                LastPresentationError=Player->GetFreezeCaptureAdmissionError();
                if(CaptureFallback[Shatter?1:0])EnsureNativeGeometry();
            }
        } else if(RelevantCapture) {
            CaptureFallback[Shatter?1:0]=true;Started=false;
            Error=TEXT("Captured rows were not prepared; exact native compatibility is explicit, not readiness.");EnsureNativeGeometry();
        }
        // Ordinary-only events without the owned prepared player use the
        // existing shared local lane, dispatched once beside SFX below.
    }
    if(Shatter)bShatterAccepted=true;
    return Started;
}

void AMCFreezeDeathVFX::DispatchAcceptedPhaseAudio(bool Shatter,double Now)
{
    if(Shatter&&ShatterAcceptedAt<0)ShatterAcceptedAt=Now;
    if(!Source||!Source->PassivePresentation||!Source->PassivePresentation->IsSourcePrepared(TEXT("Character.FreezeDeath")))return;
    FMCPerkVFXGameplayValues Values;Values.LocalStateAgeSeconds=FMath::Max(0.f,float(Now-(Shatter?ShatterAcceptedAt:StartedAt)));Values.VisualSubject=Source->GetMesh();
    const auto Mode=GetPhaseMode(Shatter);const auto* Player=Shatter?ShatterPlayer.Get():HoldPlayer.Get();
    const bool DispatchOrdinaryVFX=!Player&&!PhaseHasCapture(Shatter)
        &&(Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition);
    Source->PassivePresentation->PlayLocalSourceEvent(TEXT("Character.FreezeDeath"),Shatter?TEXT("Shatter"):TEXT("Hold"),GetActorTransform(),Values,DispatchOrdinaryVFX);
}

bool AMCFreezeDeathVFX::CommitPrepared(AMCToothCharacter* Hero,double At)
{
    if(bCommitted)return Source==Hero;
    if(Source!=Hero||!FMath::IsFinite(At))return false;
    StartedAt=At;bCommitted=true;ReadPreparedPhasePolicy();RefreshPreparedUniforms();
    const FTransform Frame(FRotator(0,Hero->GetActorRotation().Yaw,0),Hero->GetActorLocation());SetActorTransform(Frame);
    const auto* State=GetWorld()->GetGameState();const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const bool AnyDefault=GetPhaseMode(false)==EMCPerkVFXSlotMode::UseDefault||GetPhaseMode(true)==EMCPerkVFXSlotMode::UseDefault;
    const bool AnyCapture=PhaseHasCapture(false,FMath::Max(0.f,float(Now-At)))||PhaseHasCapture(true,0);
    FString Error;
    if((AnyDefault||AnyCapture)&&bPosePrepared&&PoseCapture.Capture(Hero->GetMesh(),Frame,At,Error)) {
        if(Geometry&&!Geometry->Seal(PoseCapture.GetSnapshot(),Hero->GetMesh(),PreparedEpoch,Error))LastPresentationError=Error;
    }
    if(AnyDefault)EnsureNativeGeometry(); // Default keeps old art; mute/custom alone does not recreate it.
    StartPreparedPhase(false,Now,Error);DispatchAcceptedPhaseAudio(false,Now);
    if(Hero->FreezeDeathAge()>=AMCToothCharacter::FreezeDeathHoldSeconds) {
        bShatterAudioIssued=true;StartPreparedPhase(true,Now,Error);DispatchAcceptedPhaseAudio(true,Now);
    }
    if(!Error.IsEmpty())LastPresentationError=Error;
    SetActorTickEnabled(true);SetLifeSpan(5.f);return true;
}

void AMCFreezeDeathVFX::RenderNativeSnapshot(const MCFreezeDeathSnapshot::FBuffer& Snapshot)
{
    using namespace MCFreezeDeathPrivate;
    if(!Snapshot.IsSealed()||Snapshot.GetShards().Num()!=36||Snapshot.GetSlotCount()!=NativeMaterials.Num())return;
    HalfSize=Snapshot.GetHalfSize();IceCube->SetRelativeLocation(Snapshot.GetCubeCenter());
    FMesh Cube;Cube.Box(FVector(HalfSize));Cube.Apply(IceCube,0);IceCube->SetMaterial(0,NativeGlass);
    Motion.SetNum(36);TArray<TArray<FMesh>> Groups;Groups.SetNum(36);for(auto& Group:Groups)Group.SetNum(NativeMaterials.Num());
    const FVector Cell(HalfSize*2/3,HalfSize*2/3,HalfSize*2/4);
    for(int32 I=0;I<36;++I)
    {
        const auto& M=Snapshot.GetShards()[I];Motion[I]={M.Home,M.Velocity,M.Spin,M.Floor};
        BodyFragments[I]->SetRelativeLocation(M.Home);BodyFragments[I]->SetVisibility(true);
        IceFragments[I]->SetRelativeLocation(M.Home);FMesh Shard;Shard.Box(Cell*.47f);Shard.Apply(IceFragments[I],0);
        IceFragments[I]->SetVisibility(false);
    }
    for(const auto& T:Snapshot.GetTriangles())Groups[T.Cell][T.Slot].Triangle(FVector(T.A),FVector(T.B),FVector(T.C),FVector(T.NA),FVector(T.NB),FVector(T.NC),FVector2D(T.UA),FVector2D(T.UB),FVector2D(T.UC));
    for(int32 I=0;I<36;++I)for(int32 Slot=0;Slot<NativeMaterials.Num();++Slot)
    {if(Groups[I][Slot].Indices.IsEmpty())BodyFragments[I]->ClearMeshSection(Slot);else Groups[I][Slot].Apply(BodyFragments[I],Slot);}
    bNativeGeometryWritten=true;NativeGeometryWrittenFrame=GFrameCounter;bNativeFenceQueued=false;
}

void AMCFreezeDeathVFX::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bNaturalPresentationEnd) {
        // Native captured body has a short fixed episode. Ordinary gallery rows
        // keep their existing world-owned event schedule/tail and cleanup tag.
        for(auto* Player:{HoldPlayer.Get(),ShatterPlayer.Get()})if(IsValid(Player))Player->StopCapturedFreezeLayers();
        HoldPlayer=nullptr;ShatterPlayer=nullptr;
    } else StopPreparedNiagara();
    Geometry.Reset();Super::EndPlay(Reason);
}


EMCPerkVFXSlotMode AMCFreezeDeathVFX::GetPhaseMode(bool Shatter) const
{return bFreezePolicyPrepared?(Shatter?ShatterDefinition:HoldDefinition).Outer.Mode:EMCPerkVFXSlotMode::UseDefault;}


bool AMCFreezeDeathVFX::PhaseHasCapture(bool Shatter,float EventAge) const
{
    const auto& D=Shatter?ShatterDefinition:HoldDefinition;
    if(!bFreezePolicyPrepared||!D.Snapshot)return false;
    if(D.Outer.Mode!=EMCPerkVFXSlotMode::Replace&&D.Outer.Mode!=EMCPerkVFXSlotMode::Composition)return false;
    for(int32 I=0;I<D.Systems.Num();++I)
        if(D.Snapshot->Layers[I].bEnabled&&(EventAge<0||D.Snapshot->Layers[I].EmissionSeconds<=0
            ||EventAge<D.Snapshot->Layers[I].StartSeconds+D.Snapshot->Layers[I].EmissionSeconds)
            &&FMCPreparedFreezeGeometryLayer::UsesCapture(D.Systems[I]))return true;
    return false;
}


void AMCFreezeDeathVFX::ReadPreparedPhasePolicy()
{
    if(bFreezePolicyPrepared||!Source||!Source->PassivePresentation)return;
    const auto* Hold=Source->PassivePresentation->GetPreparedSourceEvent(TEXT("Character.FreezeDeath"),TEXT("Hold"));
    const auto* Shatter=Source->PassivePresentation->GetPreparedSourceEvent(TEXT("Character.FreezeDeath"),TEXT("Shatter"));
    if(Hold&&Shatter){HoldDefinition=*Hold;ShatterDefinition=*Shatter;bFreezePolicyPrepared=true;}
}


bool AMCFreezeDeathVFX::EnsureNativeGeometry()
{
    if(bNativeGeometryWritten)return true;
    if(PoseCapture.IsCaptured())RenderNativeSnapshot(PoseCapture.GetSnapshot());
    else if(!PoseCapture.HasIssuedMotion())Capture(Source); // Exact legacy compatibility, before any issued motion only.
    else LastPresentationError=TEXT("Native freeze fallback cannot reskin/reroll after motion issuance.");
    bNativePending=bNativeGeometryWritten;return bNativeGeometryWritten;
}
