#include "MCFogBrawlPresentationComponent.h"
#include "MCFogBrawlEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

UMCFogBrawlPresentationComponent::UMCFogBrawlPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=.1f;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Smoke(TEXT("/Game/Gameplay/VFX/M_ReactionSmoke.M_ReactionSmoke"));
    SmokeMaterial=Smoke.Object;
}

void UMCFogBrawlPresentationComponent::BeginPlay()
{
    Super::BeginPlay();
    Event=Cast<AMCFogBrawlEvent>(GetOwner());
    if(!Event || GetNetMode()==NM_DedicatedServer) { SetComponentTickEnabled(false); return; }
    AddTickPrerequisiteActor(Event);
    Fog=NewObject<UExponentialHeightFogComponent>(Event,TEXT("BrawlHeightFog"));
    Fog->SetupAttachment(Event->GetRootComponent());
    Fog->SetVisibility(false); Fog->SetFogDensity(0); Fog->SetFogMaxOpacity(.88f);
    Fog->SetFogHeightFalloff(.18f); Fog->SetStartDistance(200);
    Fog->SetFogInscatteringColor(FLinearColor(.20f,.22f,.24f));
    Fog->RegisterComponent();
    Mood=NewObject<UPostProcessComponent>(Event,TEXT("BrawlSmokeMood"));
    Mood->SetupAttachment(Event->GetRootComponent());
    Mood->bUnbound=true; Mood->bEnabled=false; Mood->Priority=120; Mood->BlendWeight=0;
    Mood->Settings.bOverride_ColorSaturation=true; Mood->Settings.ColorSaturation=FVector4(.70f,.70f,.70f,1);
    Mood->Settings.bOverride_VignetteIntensity=true; Mood->Settings.VignetteIntensity=.42f;
    Mood->RegisterComponent();
    Billows=NewObject<UProceduralMeshComponent>(Event,TEXT("BrawlSmokeBillows"));
    Billows->SetupAttachment(Event->GetRootComponent()); Billows->SetAbsolute(true,true,true);
    Billows->SetCollisionEnabled(ECollisionEnabled::NoCollision); Billows->SetCastShadow(false);
    Billows->SetCanEverAffectNavigation(false);
    // The existing smoke material reads flipbook age from vertex green and opacity from alpha.
    if(SmokeMaterial) Billows->SetMaterial(0,SmokeMaterial);
    Billows->SetVisibility(false); Billows->RegisterComponent();
}

void UMCFogBrawlPresentationComponent::BeginSmoke()
{
    if(bPresenting || !Fog) return;
    bPresenting=true;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    // UE renders the first height-fog component. Restore the prior visible components on every exit.
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UExponentialHeightFogComponent*> Components(*It);
        for(auto* Other:Components) if(Other!=Fog && Other->IsVisible())
        {
            SuspendedFog.Add(Other); Other->SetVisibility(false);
        }
    }
    const FVector Center=Tongue && Tongue->Surface?Tongue->Surface->Bounds.Origin:Event->GetActorLocation();
    Fog->SetWorldLocation(Center+FVector(0,0,400));
    Fog->SetVisibility(true); Mood->bEnabled=true;
    Billows->SetVisibility(SmokeMaterial!=nullptr);
}

void UMCFogBrawlPresentationComponent::ResetSmoke()
{
    if(Fog) { Fog->SetFogDensity(0); Fog->SetVisibility(false); }
    if(Mood) { Mood->BlendWeight=0; Mood->bEnabled=false; }
    if(Billows) { Billows->ClearAllMeshSections(); Billows->SetVisibility(false); }
    for(auto& Previous:SuspendedFog) if(Previous.IsValid()) Previous->SetVisibility(true);
    SuspendedFog.Empty(); bPresenting=false;
}

void UMCFogBrawlPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ResetSmoke();
    Super::EndPlay(Reason);
}

void UMCFogBrawlPresentationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(!Event || !Event->IsActive()) { if(bPresenting) ResetSmoke(); return; }
    BeginSmoke();
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Weight=Event->Stage==EMCFogBrawlStage::SmokeIn
        ?1-FMath::Clamp(float((Event->StageEndsAt-Now)/FMath::Max(.1f,Event->SmokeInSeconds)),0.f,1.f):1.f;
    Fog->SetFogDensity(FMath::Clamp(Density,0.f,1.f)*Weight);
    Mood->BlendWeight=Weight;
    UpdateBillows(Now,Weight);
}

void UMCFogBrawlPresentationComponent::UpdateBillows(double Now,float Weight)
{
    if(!SmokeMaterial || !Tongue || !Tongue->Surface || !Billows) return;
    const auto* PC=GetWorld()->GetFirstPlayerController();
    if(!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) return;
    const auto* Hero=Cast<AMCToothCharacter>(PC->GetViewTarget());
    const FVector Focus=Hero?Hero->GetActorLocation():PC->PlayerCameraManager->GetCameraLocation();
    const FRotator Rotation=PC->PlayerCameraManager->GetCameraRotation();
    const FVector Right=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y),Up=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    TArray<FVector> Points,Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    Points.Reserve(252); Triangles.Reserve(378);
    for(int32 Y=0;Y<7;++Y) for(int32 X=0;X<9;++X)
    {
        const int32 Seed=Y*9+X;
        const float Phase=Seed*2.39996f;
        FVector Center(FMath::Lerp(Bounds.Min.X,Bounds.Max.X,(X+.5f)/9),FMath::Lerp(Bounds.Min.Y,Bounds.Max.Y,(Y+.5f)/7),Bounds.GetCenter().Z);
        Center+=FVector(FMath::Sin(Now*.12+Phase)*90,FMath::Cos(Now*.09+Phase)*90,0);
        FHitResult Floor;
        if(!Tongue->SurfacePoint(Center,Floor)) continue;
        Center=Floor.ImpactPoint+FVector(0,0,180+FMath::Sin(Now*.25+Phase)*50);
        const float NearFade=FMath::SmoothStep(250.f,850.f,float(FVector::Dist2D(Focus,Center)));
        if(NearFade<.01f) continue;
        const float Size=250+FMath::Sin(Phase)*60;
        const float Life=FMath::Frac(float(Now*.035+Seed*.137));
        const int32 First=Points.Num();
        for(int32 I=0;I<4;++I)
        {
            const float U=(I==1 || I==2)?1.f:-1.f,V=I>=2?1.f:-1.f;
            Points.Add(Center+(Right*U+Up*V)*Size); Normals.Add(-Rotation.Vector());
            UV.Add(FVector2D((U+1)*.5f,(V+1)*.5f)); Colors.Add(FLinearColor(0,Life,0,.40f*Weight*NearFade));
            Tangents.Add(FProcMeshTangent(Right,false));
        }
        Triangles.Append({First,First+1,First+2,First,First+2,First+3});
    }
    Billows->CreateMeshSection_LinearColor(0,Points,Triangles,Normals,UV,Colors,Tangents,false);
}
