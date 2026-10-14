#include "MCFogBrawlPresentationComponent.h"
#include "MCFogBrawlEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCArenaTooth.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace MCFogSignals
{
void Quad(UProceduralMeshComponent* Mesh,int32 Section,FVector Center,FVector Right,FVector Up,FLinearColor Color)
{
    TArray<FVector> P={Center-Right-Up,Center+Right-Up,Center+Right+Up,Center-Right+Up};
    TArray<int32> T={0,1,2,0,2,3};
    TArray<FVector> N; N.Init(FVector::CrossProduct(Right,Up).GetSafeNormal(),4);
    TArray<FVector2D> UV={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
    TArray<FLinearColor> C; C.Init(Color,4);
    TArray<FProcMeshTangent> Tangents; Tangents.Init(FProcMeshTangent(Right.GetSafeNormal(),false),4);
    Mesh->CreateMeshSection_LinearColor(Section,P,T,N,UV,C,Tangents,false);
}

UProceduralMeshComponent* Mesh(AActor* Owner,FName Name)
{
    auto* Result=NewObject<UProceduralMeshComponent>(Owner,Name);
    Result->SetupAttachment(Owner->GetRootComponent()); Result->SetAbsolute(true,true,true);
    Result->SetCollisionEnabled(ECollisionEnabled::NoCollision); Result->SetCastShadow(false);
    Result->SetCanEverAffectNavigation(false); Result->SetVisibility(false); Result->RegisterComponent(); return Result;
}
}

UMCFogBrawlPresentationComponent::UMCFogBrawlPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=.1f;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Smoke(TEXT("/Game/Gameplay/VFX/M_ReactionSmoke.M_ReactionSmoke"));
    SmokeMaterial=Smoke.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Gameplay/FogBrawl/M_FogSearchGlow")); GlowMaterial=Glow.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Beacon(TEXT("/Game/Gameplay/FogBrawl/M_FogBeacon")); BeaconMaterial=Beacon.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tell(TEXT("/Game/Gameplay/FogBrawl/M_FogStrikeTell")); TellMaterial=Tell.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Windup(TEXT("/Game/Audio/FogBrawl/S_FogWindup")); WindupSound=Windup.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Knock(TEXT("/Game/Audio/FogBrawl/S_FogKnock")); KnockSound=Knock.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Final(TEXT("/Game/Audio/FogBrawl/S_FogFinal")); FinalSound=Final.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Mark(TEXT("/Game/Audio/FogBrawl/S_FogBeacon")); BeaconSound=Mark.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Impact(TEXT("/Game/Audio/FogBrawl/S_FogImpact")); ImpactSound=Impact.Object;
    KnockAttenuation=CreateDefaultSubobject<USoundAttenuation>(TEXT("FogKnockAttenuation"));
    KnockAttenuation->Attenuation.bAttenuate=true; KnockAttenuation->Attenuation.bSpatialize=true;
    KnockAttenuation->Attenuation.AttenuationShapeExtents=FVector(250,0,0);
    KnockAttenuation->Attenuation.FalloffDistance=3500;
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
    Signals=MCFogSignals::Mesh(Event,TEXT("FogSearchAndTeamBeacon"));
    Signals->SetMaterial(0,GlowMaterial); Signals->SetMaterial(1,BeaconMaterial); Signals->SetMaterial(2,GlowMaterial);
    Threat=MCFogSignals::Mesh(Event,TEXT("FogIncomingStrikeDirection")); Threat->SetMaterial(0,TellMaterial);
    KnockAudio=NewObject<UAudioComponent>(Event,TEXT("FogSpatialKnock"));
    KnockAudio->SetupAttachment(Event->GetRootComponent()); KnockAudio->bAutoActivate=false;
    KnockAudio->bAutoDestroy=false; KnockAudio->SetSound(KnockSound); KnockAudio->AttenuationSettings=KnockAttenuation;
    KnockAudio->RegisterComponent();
    CueAudio=NewObject<UAudioComponent>(Event,TEXT("FogPhaseCue"));
    CueAudio->SetupAttachment(Event->GetRootComponent()); CueAudio->bAutoActivate=false;
    CueAudio->bAutoDestroy=false; CueAudio->bAllowSpatialization=false; CueAudio->RegisterComponent();
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
    if(Signals) { Signals->ClearAllMeshSections(); Signals->SetVisibility(false); }
    if(Threat) { Threat->ClearAllMeshSections(); Threat->SetVisibility(false); }
    if(KnockAudio) KnockAudio->Stop();
    if(CueAudio) CueAudio->Stop();
    PresentedWarning=-1; PresentedImpact=-100; NextKnockAt=0; bFinalHeard=bBeaconHeard=false;
    PresentedSelection=-1;PresentedSelectionStep=INDEX_NONE;
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
    const float SmokeWeight=Event->Stage==EMCFogBrawlStage::SmokeIn
        ?1-FMath::Clamp(float((Event->StageEndsAt-Now)/FMath::Max(.1f,Event->SmokeInSeconds)),0.f,1.f):1.f;
    // The complete final warning is readable. Keep it clear briefly through contact, then close the smoke again.
    const bool bOpen=Event->IsFinalWarning() || (Event->Stage==EMCFogBrawlStage::Recovery && Now-Event->LastImpactAt<.35);
    const float Weight=bOpen?.025f:SmokeWeight;
    Fog->SetFogDensity(FMath::Clamp(Density,0.f,1.f)*Weight);
    Mood->BlendWeight=bOpen?.12f:SmokeWeight;
    Mood->Settings.bOverride_SceneColorTint=true;
    Mood->Settings.SceneColorTint=Event->Stage==EMCFogBrawlStage::Warning?FLinearColor(1,.83f,.72f):FLinearColor(.88f,.94f,1);
    UpdateBillows(Now,Weight);
    UpdateSignals(Now);
    UpdateAudio(Now);
}

void UMCFogBrawlPresentationComponent::UpdateSignals(double Now)
{
    if(!Signals || !Threat) return;
    Signals->ClearAllMeshSections();Threat->ClearAllMeshSections();
    Signals->SetVisibility(false);Threat->SetVisibility(false);
    const auto* PC=GetWorld()->GetFirstPlayerController();
    if(!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) return;
    const FVector Right=FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Y);
    const FVector Up=FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Z);
    if(Event->Stage==EMCFogBrawlStage::Selection) {
        if(auto* Tooth=Event->SelectionTooth();Tooth && Tooth->Body) {
            const auto Bounds=Tooth->Body->Bounds;
            MCFogSignals::Quad(Signals,0,Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z+65),Right*160,Up*160,FLinearColor(2,1,.05f,.65f));
            Signals->SetMaterial(0,GlowMaterial);Signals->SetVisibility(GlowMaterial!=nullptr);
        }
        return;
    }
    if(Event->Stage!=EMCFogBrawlStage::Warning && Event->Stage!=EMCFogBrawlStage::Recovery) return;
    int32 Section=0;
    for(const auto& Target:Event->Targets) {
        if(Target.bResolved || !IsValid(Target.Tooth) || !Target.Tooth->IsAvailable() || !Target.Tooth->Body) continue;
        const auto Bounds=Target.Tooth->Body->Bounds;
        const FVector Top=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z);
        const bool Current=Event->IsWaveTarget(Target.Tooth);
        const auto Color=Current?FLinearColor(3,.06f,.015f,.5f):FLinearColor(2,1.2f,.06f,.4f);
        MCFogSignals::Quad(Signals,Section,Top+FVector(0,0,90),Right*150,Up*130,Color);
        Signals->SetMaterial(Section++,GlowMaterial);
        if(Target.bMarked && BeaconMaterial) {
            const FVector Side=FVector::CrossProduct(FVector::UpVector,(PC->PlayerCameraManager->GetCameraLocation()-Top).GetSafeNormal()).GetSafeNormal(KINDA_SMALL_NUMBER,Right);
            MCFogSignals::Quad(Signals,Section,Top+FVector(0,0,550),Side*65,FVector(0,0,550),FLinearColor(.12f,2.8f,2.3f,.8f));
            Signals->SetMaterial(Section++,BeaconMaterial);
            MCFogSignals::Quad(Signals,Section,Top+FVector(0,0,1010),Right*130,Up*130,FLinearColor(.18f,3,2.4f,.7f));
            Signals->SetMaterial(Section++,GlowMaterial);
        }
    }
    Signals->SetVisibility(Section>0 && GlowMaterial!=nullptr);
    if(!Event->IsFinalWarning() || !TellMaterial) return;
    const FVector Direction=Event->StrikeDirection.GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector(0,1,0));
    const FVector Side=FVector::CrossProduct(FVector::UpVector,Direction);
    TArray<FVector> Points,Normals;TArray<int32> Triangles;TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    auto Ribbon=[&](FVector A,FVector B,float Width) {
        const FVector Across=FVector::CrossProduct(FVector::UpVector,(B-A).GetSafeNormal())*Width;
        const int32 I=Points.Num();Points.Append({A-Across,A+Across,B+Across,B-Across});
        Triangles.Append({I,I+1,I+2,I,I+2,I+3});
        UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        for(int32 J=0;J<4;++J) {Normals.Add(FVector::UpVector);Colors.Add(FLinearColor(3,.07f,.015f,.8f));Tangents.Add(FProcMeshTangent(Direction,false));}
    };
    for(const auto& Target:Event->Targets) {
        if(!Event->IsWaveTarget(Target.Tooth) || !Target.Tooth->Body) continue;
        const auto Bounds=Target.Tooth->Body->Bounds;
        const FVector Center=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z*.65f);
        const float Radius=FMath::Max(Bounds.BoxExtent.X,Bounds.BoxExtent.Y)+35;
        for(int32 I=0;I<48;++I) {
            const float A=I*2*PI/48,B=(I+1)*2*PI/48;
            Ribbon(Center+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius,Center+FVector(FMath::Cos(B),FMath::Sin(B),0)*Radius,7);
        }
        for(int32 I=0;I<3;++I) {
            const FVector Tip=Center-Direction*(Radius+100+I*145);
            Ribbon(Tip-Direction*75-Side*65,Tip,8);Ribbon(Tip-Direction*75+Side*65,Tip,8);
        }
    }
    Threat->CreateMeshSection_LinearColor(0,Points,Triangles,Normals,UV,Colors,Tangents,false);Threat->SetVisibility(!Points.IsEmpty());
}

void UMCFogBrawlPresentationComponent::UpdateAudio(double Now)
{
    if(!CueAudio || !KnockAudio) return;
    auto Cue=[&](USoundBase* Sound,float Volume)
    { if(Sound) {CueAudio->Stop();CueAudio->SetSound(Sound);CueAudio->SetVolumeMultiplier(Volume);CueAudio->Play();} };
    if(Event->Stage==EMCFogBrawlStage::Selection)
    {
        if(PresentedSelection!=Event->StageEndsAt) {
            PresentedSelection=Event->StageEndsAt;PresentedSelectionStep=INDEX_NONE;Cue(WindupSound,.6f);
        }
        const int32 Step=Event->SelectionStep();
        if(Step!=PresentedSelectionStep) {
            PresentedSelectionStep=Step;
            if(auto* Tooth=Event->SelectionTooth();Tooth && KnockSound) {
                KnockAudio->Stop();KnockAudio->SetWorldLocation(Tooth->GetActorLocation());
                KnockAudio->SetVolumeMultiplier(.5f);KnockAudio->SetPitchMultiplier(.9f+.5f*Event->SelectionProgress());KnockAudio->Play();
            }
        }
    }
    else if(Event->Stage==EMCFogBrawlStage::Warning && IsValid(Event->ActiveTarget))
    {
        if(PresentedWarning!=Event->WarningEndsAt)
        {
            PresentedWarning=Event->WarningEndsAt; bFinalHeard=bBeaconHeard=false; NextKnockAt=Now+.15;
            KnockAudio->Stop();
            if(Event->WarningRemaining()>Event->WarningSeconds) Cue(WindupSound,.6f);
        }
        if(Event->bTargetMarked && !bBeaconHeard) {bBeaconHeard=true;if(!Event->IsFinalWarning()) Cue(BeaconSound,.5f);}
        if(Event->IsFinalWarning() && !bFinalHeard) {bFinalHeard=true;Cue(FinalSound,.65f);}
        if(Now>=NextKnockAt && KnockSound)
        {
            float Distance=3500;
            for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
                if(const auto* Local=It->Get();Local && Local->IsLocalController() && Local->GetPawn())
                    Distance=FMath::Min(Distance,float(FVector::Dist(Local->GetPawn()->GetActorLocation(),Event->ActiveTarget->GetActorLocation())));
            const float Near=1-FMath::Clamp((Distance-250)/2400.f,0.f,1.f);
            const float Urgency=1-FMath::Clamp((Event->WarningRemaining()-Event->WarningSeconds)/FMath::Max(.1f,Event->SearchSeconds),0.f,1.f);
            NextKnockAt=Now+(Event->IsFinalWarning()?.23f:FMath::Lerp(1.1f,.32f,FMath::Max(Near,Urgency*.7f)));
            KnockAudio->SetWorldLocation(Event->ActiveTarget->GetActorLocation());
            KnockAudio->SetVolumeMultiplier(.35f+.4f*Near); KnockAudio->SetPitchMultiplier(1+.15f*Urgency); KnockAudio->Play();
        }
    }
    else KnockAudio->Stop();
    if(Event->LastImpactAt!=PresentedImpact && Now-Event->LastImpactAt<.4)
    {PresentedImpact=Event->LastImpactAt;Cue(ImpactSound,.7f);}
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
