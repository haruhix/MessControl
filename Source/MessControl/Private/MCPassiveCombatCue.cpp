#include "MCPassiveCombatCue.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshComponentLODInfo.h"
#include "Rendering/ColorVertexBuffer.h"
#include "RenderResource.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
double CueNow(const UWorld* World) {return World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds();}
}
AMCPassiveCombatCue::AMCPassiveCombatCue()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(true);PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Lightning"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);Mesh->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Gameplay/VFX/MI_PassiveLightning.MI_PassiveLightning"));
    LinkMaterial=Material.Object;Mesh->SetMaterial(0,LinkMaterial);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ghost(TEXT("/Game/Gameplay/VFX/MI_PassiveGhost.MI_PassiveGhost"));
    GhostMaterial=Ghost.Object;
}
void AMCPassiveCombatCue::Spawn(UWorld* World,FVector From,FVector To,uint32 Seed)
{
    if(!World || World->GetNetMode()==NM_Client || From.ContainsNaN() || To.ContainsNaN()) return;
    const FTransform Pose(From);auto* Cue=World->SpawnActorDeferred<AMCPassiveCombatCue>(StaticClass(),Pose);
    if(!Cue) return;
    Cue->LinkEnd=To;Cue->NoiseSeed=Seed;Cue->StartedAt=CueNow(World);Cue->FinishSpawning(Pose);
}
AMCPassiveCombatCue* AMCPassiveCombatCue::SpawnEcho(UWorld* World,FVector Origin,FVector Forward,const TArray<FMCEchoToolPart>& Parts,bool bOverhead,double ContactAt)
{
    if(!World || World->GetNetMode()==NM_Client || Parts.IsEmpty() || Origin.ContainsNaN() || Forward.ContainsNaN()) return nullptr;
    const FTransform Pose(Forward.Rotation(),Origin);
    auto* Cue=World->SpawnActorDeferred<AMCPassiveCombatCue>(StaticClass(),Pose);
    if(!Cue) return nullptr;
    Cue->Kind=EMCPassiveCombatCueKind::EchoTool;Cue->ToolParts=Parts;Cue->bEchoOverhead=bOverhead;
    // Tool passes through its captured contact pose at the existing gameplay echo deadline.
    Cue->StartedAt=CueNow(World)+(ContactAt-World->GetTimeSeconds())-.1;
    Cue->FinishSpawning(Pose);return Cue;
}
void AMCPassiveCombatCue::BeginPlay()
{
    Super::BeginPlay();Mesh->SetMaterial(0,LinkMaterial);RefreshGhost();
    if(HasAuthority()) SetLifeSpan(Kind==EMCPassiveCombatCueKind::EchoTool?.3f:.24f);
}
void AMCPassiveCombatCue::RefreshGhost()
{
    if(!HasActorBegunPlay() || GetNetMode()==NM_DedicatedServer || Kind!=EMCPassiveCombatCueKind::EchoTool || !GhostMeshes.IsEmpty()) return;
    for(int32 Part=0;Part<FMath::Min(ToolParts.Num(),3);++Part)
        if(!ToolParts[Part].Mesh || !ToolParts[Part].Mesh->GetRenderData()) return;
    for(int32 Part=0;Part<FMath::Min(ToolParts.Num(),3);++Part) {
        auto* Source=ToolParts[Part].Mesh.Get();if(!Source) continue;
        auto* Ghost=NewObject<UStaticMeshComponent>(this);Ghost->SetupAttachment(Mesh);
        Ghost->SetMobility(EComponentMobility::Movable);
        Ghost->SetStaticMesh(Source);Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ghost->SetSimulatePhysics(false);
        Ghost->SetGenerateOverlapEvents(false);Ghost->SetCastShadow(false);Ghost->SetCanEverAffectNavigation(false);
        Ghost->bReceivesDecals=false;Ghost->bDisallowNanite=true;
        for(int32 Slot=0;Slot<FMath::Max(1,Source->GetStaticMaterials().Num());++Slot) Ghost->SetMaterial(Slot,GhostMaterial);
        // M_ReactionSoft already reads vertex alpha. Uniform override colors need only
        // vertex counts, not CPU mesh positions, and never create a gameplay-time MID.
        if(const auto* RenderData=Source->GetRenderData()) {
            Ghost->SetLODDataCount(RenderData->LODResources.Num(),RenderData->LODResources.Num());
            for(int32 LOD=0;LOD<RenderData->LODResources.Num();++LOD) {
                const uint32 Count=RenderData->LODResources[LOD].GetNumVertices();if(!Count) continue;
                auto& Data=Ghost->LODData[LOD];Data.OverrideVertexColors=new FColorVertexBuffer;
                // Dedicated saved MIC supplies the cool tint before readiness;
                // vertex alpha keeps the captured geometry visibly translucent.
                Data.OverrideVertexColors->InitFromSingleColor(FColor(255,255,255,100),Count);
                BeginInitResource(Data.OverrideVertexColors);
            }
        }
        Ghost->SetRelativeTransform(ToolParts[Part].ContactPose);AddInstanceComponent(Ghost);Ghost->RegisterComponent();GhostMeshes.Add(Ghost);
    }
}
void AMCPassiveCombatCue::TickGhost(float Age)
{
    RefreshGhost();Mesh->SetVisibility(false);
    const bool Visible=Age>=0 && Age<.24f;
    const float Angle=Age<.1f?FMath::Lerp(-38.f,0.f,FMath::SmoothStep(0.f,.1f,Age))
        :FMath::Lerp(0.f,50.f,FMath::SmoothStep(.1f,.24f,Age));
    const FTransform Motion(FQuat(bEchoOverhead?FVector::RightVector:FVector::UpVector,FMath::DegreesToRadians(Angle)));
    for(int32 I=0;I<GhostMeshes.Num();++I) {
        GhostMeshes[I]->SetVisibility(Visible);GhostMeshes[I]->SetRelativeTransform(ToolParts[I].ContactPose*Motion);
    }
}
void AMCPassiveCombatCue::Tick(float Dt)
{
    Super::Tick(Dt);if(GetNetMode()==NM_DedicatedServer) return;
    if(Kind==EMCPassiveCombatCueKind::EchoTool) {TickGhost(float(CueNow(GetWorld())-StartedAt));return;}
    const float Age=FMath::Max(0.,CueNow(GetWorld())-StartedAt),Alpha=1-FMath::Clamp(Age/.2f,0.f,1.f);
    Mesh->SetVisibility(Alpha>0);if(Alpha<=0) return;
    const FVector End=FVector(LinkEnd)-GetActorLocation(),Axis=End.GetSafeNormal();
    FVector Eye=GetActorLocation()+FVector(0,-500,300);
    if(const auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager) Eye=PC->PlayerCameraManager->GetCameraLocation();
    const FVector View=(Eye-GetActorLocation()).GetSafeNormal(),Side=FVector::CrossProduct(View,Axis).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    const FVector Up=FVector::CrossProduct(Axis,Side).GetSafeNormal();
    TArray<FVector> P,N;TArray<int32> Tri;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    FRandomStream Random(int32(NoiseSeed)+FMath::FloorToInt(Age*20));TArray<FVector> Points;
    constexpr int32 Segments=10;
    for(int32 I=0;I<=Segments;++I) {
        const float T=I/float(Segments),Taper=FMath::Sin(T*PI);
        Points.Add(End*T+(Side*Random.FRandRange(-11.f,11.f)+Up*Random.FRandRange(-6.f,6.f))*Taper);
    }
    for(int32 Layer=0;Layer<2;++Layer) {
        const int32 First=P.Num();const float Width=(Layer?1.2f:4.f)*(.5f+.5f*Alpha);
        const FLinearColor Tint=Layer?FLinearColor(.85f,1.f,1.f,Alpha):FLinearColor(.05f,.55f,1.f,Alpha*.7f);
        for(int32 I=0;I<=Segments;++I) {
            const float T=I/float(Segments),W=Width*(.35f+.65f*FMath::Sin(T*PI));
            for(int32 Edge=0;Edge<2;++Edge) {
                P.Add(Points[I]+Side*(Edge?W:-W));N.Add(View);UV.Add(FVector2D(Edge,T));Colors.Add(Tint);Tangents.Add(FProcMeshTangent(Axis,false));
            }
            if(I) {const int32 A=First+(I-1)*2;Tri.Append({A,A+1,A+3,A,A+3,A+2});}
        }
    }
    Mesh->CreateMeshSection_LinearColor(0,P,Tri,N,UV,Colors,Tangents,false);
}
void AMCPassiveCombatCue::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCPassiveCombatCue,LinkEnd);DOREPLIFETIME(AMCPassiveCombatCue,StartedAt);DOREPLIFETIME(AMCPassiveCombatCue,NoiseSeed);
    DOREPLIFETIME(AMCPassiveCombatCue,Kind);DOREPLIFETIME(AMCPassiveCombatCue,ToolParts);DOREPLIFETIME(AMCPassiveCombatCue,bEchoOverhead);
}
