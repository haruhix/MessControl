#include "MCPassiveFeedbackCue.h"
#include "MCCartoonFXGeometry.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
struct FCueGeometry {
    TArray<FVector> Points,Normals;TArray<int32> Indices;TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    FVector View,Right,Up,Origin=FVector::ZeroVector;
    int32 Add(FVector Point,FLinearColor Color) {
        const int32 I=Points.Add(Point+Origin);Normals.Add(View);UV.Add(FVector2D::ZeroVector);
        Colors.Add(Color);Tangents.Add(FProcMeshTangent(Right,false));return I;
    }
    void Puff(FVector Center,float Radius,FLinearColor Color,int32 Sides=16) {
        const int32 First=Add(Center,Color);Color.A=0;
        for(int32 I=0;I<=Sides;++I) {
            const float Angle=I*2.f*PI/Sides;Add(Center+(Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle))*Radius,Color);
            if(I) Indices.Append({First,First+I,First+I+1});
        }
    }
    void Stroke(const TArray<FVector>& Path,float Width,FLinearColor Color) {
        if(Path.Num()<2) return;const int32 First=Points.Num();
        for(int32 I=0;I<Path.Num();++I) {
            const FVector Along=(Path[FMath::Min(I+1,Path.Num()-1)]-Path[FMath::Max(I-1,0)]).GetSafeNormal();
            const FVector Side=FVector::CrossProduct(View,Along).GetSafeNormal(UE_SMALL_NUMBER,Right);
            const float T=I/float(Path.Num()-1),Taper=.2f+.8f*FMath::Sin(T*PI);
            auto Edge=Color;Edge.A=0;Add(Path[I]-Side*Width*Taper,Edge);Add(Path[I],Color);Add(Path[I]+Side*Width*Taper,Edge);
            if(I) for(int32 J=0;J<2;++J) {const int32 A=First+(I-1)*3+J;Indices.Append({A,A+1,A+4,A,A+4,A+3});}
        }
    }
    void Capsule(FVector A,FVector B,float Width,FLinearColor Color) {
        const FVector Along=(B-A).GetSafeNormal();const FVector Side=FVector::CrossProduct(View,Along).GetSafeNormal(UE_SMALL_NUMBER,Right);
        const int32 First=Add((A+B)*.5f,Color);TArray<FVector> Outline;
        for(int32 I=0;I<=8;++I) {const float T=I*PI/8;Outline.Add(B+(Along*FMath::Sin(T)+Side*FMath::Cos(T))*Width);}
        for(int32 I=0;I<=8;++I) {const float T=I*PI/8;Outline.Add(A+(-Along*FMath::Sin(T)-Side*FMath::Cos(T))*Width);}
        for(int32 I=0;I<=Outline.Num();++I) {Add(Outline[I%Outline.Num()],Color);if(I) Indices.Append({First,First+I,First+I+1});}
    }
    void Plus(FVector Center,float Size,FLinearColor Color,float Stretch=1.f) {
        Capsule(Center-Right*Size,Center+Right*Size,Size*.32f,Color);
        Capsule(Center-Up*(Size*Stretch),Center+Up*(Size*Stretch),Size*.32f,Color);
        const FLinearColor Highlight(.5f,.95f,.68f,Color.A*.8f);
        Capsule(Center-Right*(Size*.7f)+Up*2,Center+Right*(Size*.7f)+Up*2,Size*.10f,Highlight);
    }
    void Arc(FVector Center,float Radius,float Start,float Sweep,float Width,FLinearColor Color) {
        TArray<FVector> Path;for(int32 I=0;I<=18;++I) {const float A=Start+Sweep*I/18.f;Path.Add(Center+(Right*FMath::Cos(A)+Up*FMath::Sin(A))*Radius);}
        Stroke(Path,Width,Color);
    }
    void Upload(UProceduralMeshComponent* Target,int32 Section) {
        const auto* Previous=Target->GetProcMeshSection(Section);
        if(Points.IsEmpty()) {
            if(Previous && !Previous->ProcVertexBuffer.IsEmpty()) Target->ClearMeshSection(Section);
            return;
        }
        bool Stable=Previous && Previous->ProcVertexBuffer.Num()==Points.Num() && Previous->ProcIndexBuffer.Num()==Indices.Num();
        if(Stable) for(int32 I=0;I<Indices.Num();++I) if(Previous->ProcIndexBuffer[I]!=uint32(Indices[I])) {Stable=false;break;}
        // Keep the existing soft material's sRGB conversion, equally on create/update.
        if(Stable) Target->UpdateMeshSection_LinearColor(Section,Points,Normals,UV,Colors,Tangents,true);
        else Target->CreateMeshSection_LinearColor(Section,Points,Indices,Normals,UV,Colors,Tangents,false,true);
    }
};
}
AMCPassiveFeedbackCue::AMCPassiveFeedbackCue()
{
    bReplicates=true;PrimaryActorTick.bCanEverTick=true;SetNetCullDistanceSquared(FMath::Square(3500.f));
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PassiveFeedback"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCastShadow(false);Mesh->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Soft(TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackSoft.MI_PassiveFeedbackSoft"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackGlow.MI_PassiveFeedbackGlow"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Solid(TEXT("/Game/Gameplay/VFX/M_CartoonReceiptSolid.M_CartoonReceiptSolid"));
    SoftMaterial=Soft.Object;GlowMaterial=Glow.Object;SolidMaterial=Solid.Object;
    Mesh->SetMaterial(0,SoftMaterial);Mesh->SetMaterial(1,GlowMaterial);Mesh->SetMaterial(2,SolidMaterial);
}
double AMCPassiveFeedbackCue::CueTime() const
{return GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}
float AMCPassiveFeedbackCue::Duration() const
{return Kind==EMCPassiveFeedbackKind::Heal?1.05f:Kind==EMCPassiveFeedbackKind::Neighbor?.75f:.65f;}
AMCPassiveFeedbackCue* AMCPassiveFeedbackCue::Spawn(AActor* InSubject,EMCPassiveFeedbackKind Type,float Intensity,FVector InDirection)
{
    if(!IsValid(InSubject) || InSubject->IsActorBeingDestroyed() || !InSubject->HasAuthority() || !FMath::IsFinite(Intensity) || Intensity<=0) return nullptr;
    UWorld* World=InSubject->GetWorld();if(!World || World->GetNetMode()==NM_Client) return nullptr;
    int32 Total=0,Owned=0;
    for(TActorIterator<AMCPassiveFeedbackCue> It(World);It;++It) {
        if(It->IsActorBeingDestroyed()) continue;++Total;
        if(It->Subject!=InSubject) continue;++Owned;
        if(It->Kind==Type && It->CueTime()-It->StartedAt<.15) {
            It->Strength=FMath::Max(It->Strength,FMath::Clamp(Intensity,.4f,1.6f));It->ForceNetUpdate();return *It;
        }
    }
    if(Total>=32 || Owned>=3) return nullptr;
    const FTransform Pose(InSubject->GetActorLocation());auto* Cue=World->SpawnActorDeferred<AMCPassiveFeedbackCue>(StaticClass(),Pose);
    if(!Cue) return nullptr;Cue->Subject=InSubject;Cue->Kind=Type;Cue->Strength=FMath::Clamp(Intensity,.4f,1.6f);
    Cue->Direction=InDirection.IsNearlyZero() || InDirection.ContainsNaN()?InSubject->GetActorForwardVector():InDirection.GetSafeNormal();
    Cue->StartedAt=Cue->CueTime();Cue->FinishSpawning(Pose);return Cue;
}
void AMCPassiveFeedbackCue::SpawnLink(AActor* InSubject,FVector From,FVector To)
{
    if(From.ContainsNaN() || To.ContainsNaN()) return;
    if(auto* Cue=Spawn(InSubject,EMCPassiveFeedbackKind::Neighbor)) {Cue->SetActorLocation(From+FVector(0,0,12));Cue->LinkEnd=To+FVector(0,0,12);Cue->ForceNetUpdate();}
}
void AMCPassiveFeedbackCue::BeginPlay()
{
    Super::BeginPlay();Mesh->SetMaterial(0,SoftMaterial);Mesh->SetMaterial(1,GlowMaterial);Mesh->SetMaterial(2,SolidMaterial);
    if(HasAuthority()) SetLifeSpan(1.6f);
    if(GetNetMode()==NM_DedicatedServer) SetActorTickEnabled(false);
}
void AMCPassiveFeedbackCue::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(GetNetMode()==NM_DedicatedServer) return;
    const float Age=float(CueTime()-StartedAt),T=Age/Duration();
    if(T<0 || T>=1 || !IsValid(Subject)) {Mesh->SetVisibility(false);return;}
    Mesh->SetVisibility(true);
    if(Kind!=EMCPassiveFeedbackKind::Neighbor && Kind!=EMCPassiveFeedbackKind::Vanish) SetActorLocation(Subject->GetActorLocation());
    FVector Eye=GetActorLocation()+FVector(-300,-400,250);
    if(const auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager) Eye=PC->PlayerCameraManager->GetCameraLocation();
    if(FVector::DistSquared(Eye,GetActorLocation())>FMath::Square(4000.f)) {Mesh->SetVisibility(false);return;}
    FCueGeometry Soft,Glow;Soft.View=(Eye-GetActorLocation()).GetSafeNormal();Soft.Right=FVector::CrossProduct(FVector::UpVector,Soft.View).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);Soft.Up=FVector::CrossProduct(Soft.View,Soft.Right).GetSafeNormal();Glow.View=Soft.View;Glow.Right=Soft.Right;Glow.Up=Soft.Up;
    MCCartoonFX::FGeometry Solid;
    Solid.Right=Soft.Right;Solid.Up=Soft.Up;Solid.View=Soft.View;
    const FVector R=Soft.Right,U=Soft.Up;
    float HalfHeight=38,BodyRadius=35;if(const auto* Hero=Cast<ACharacter>(Subject.Get())) {HalfHeight=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();BodyRadius=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();}
    // Camera-facing particles must sit outside the opaque body, without disabling depth tests.
    if(Kind!=EMCPassiveFeedbackKind::Neighbor && Kind!=EMCPassiveFeedbackKind::Vanish) Soft.Origin=Glow.Origin=Soft.View*(BodyRadius+8.f);
    Solid.Origin=Soft.Origin;
    const float Fade=FMath::SmoothStep(0.f,.12f,T)*(1-FMath::SmoothStep(.45f,1.f,T));
    const float Ease=1-FMath::Pow(1-T,3.f),S=Strength;
    const FLinearColor Mint(.10f,.72f,.38f,.85f*Fade),Blue(.10f,.46f,.82f,.78f*Fade),Cream(.85f,.58f,.15f,.76f*Fade);
const float DFH=2.f*FMath::Max(1.f,HalfHeight);
const float DFK=FMath::Clamp(S,.7f,1.2f);
const auto DFPop=[](float A,float Rise,float Settle,float FadeAt,float End) {
    if(A<0.f || A>=End) return 0.f;
    if(A<Rise) {const float P=A/Rise;return 1.18f*(1.f-FMath::Pow(1.f-P,3.f));}
    if(A<Settle) return FMath::Lerp(1.18f,1.f,FMath::SmoothStep(0.f,1.f,(A-Rise)/(Settle-Rise)));
    return 1.f-FMath::SmoothStep(FadeAt,End,A);
};
const auto DFTail=[](float A,float End) {
    return A<0.f?0.f:FMath::SmoothStep(0.f,.035f,A)*(1.f-FMath::SmoothStep(End*.45f,End,A));
};
if(Kind==EMCPassiveFeedbackKind::Heal)
{
    const auto HealBackOut=[](float Progress) {
        const float Q=FMath::Clamp(Progress,0.f,1.f)-1.f;
        return 1.f+2.70158f*Q*Q*Q+1.70158f*Q*Q;
    };
    const float HealArtScale=.8f+.3f*FMath::Clamp(S,.4f,1.6f);
    const float HealEnter=FMath::Max(.001f,HealBackOut(Age/.18f));
    const float HealLeave=1.f-FMath::SmoothStep(.67f,1.05f,Age);
    const float HealScale=FMath::Max(.001f,HealEnter*HealLeave)*HealArtScale;
    const float HealSquash=FMath::Lerp(.58f,1.18f,FMath::SmoothStep(0.f,.12f,Age));
    const float HealStretch=FMath::Lerp(HealSquash,1.f,FMath::SmoothStep(.12f,.32f,Age));
    const float HealRise=1.f-FMath::Pow(1.f-FMath::Clamp(Age/.86f,0.f,1.f),2.f);
    const FVector HealPrimaryAt=R*(BodyRadius+27.f*HealArtScale)
        +FVector::UpVector*(-HalfHeight*.24f+HalfHeight*.72f*HealRise);
    const float HealTilt=.23f+.05f*FMath::Sin(FMath::Clamp(Age/.55f,0.f,1.f)*PI);
    const FVector HealOriginalView=Solid.View;
    // A small true 3D tilt exposes the bevel/side wall. Basis remains orthonormal.
    Solid.Right=R*FMath::Cos(HealTilt)+HealOriginalView*FMath::Sin(HealTilt);
    Solid.View=HealOriginalView*FMath::Cos(HealTilt)-R*FMath::Sin(HealTilt);
    Solid.RoundedPlus(HealPrimaryAt,19.f*HealScale/FMath::Sqrt(HealStretch),
        9.f*HealScale,FLinearColor(.10f,.69f,.37f,1.f),-.10f+.10f*HealRise,HealStretch);
    Solid.Right=R;Solid.Up=U;Solid.View=HealOriginalView;

    // One smaller accent, on the opposite LOW shoulder; never over the face.
    const float HealAccentAge=Age-.105f;
    const float HealAccentEnvelope=FMath::Max(.001f,
        FMath::SmoothStep(0.f,.10f,HealAccentAge)*(1.f-FMath::SmoothStep(.40f,.73f,HealAccentAge)));
    const FVector HealAccentAt=-R*(BodyRadius+20.f*HealArtScale)
        +FVector::UpVector*(-HalfHeight*.30f+HalfHeight*.24f*HealRise);
    Solid.Star(HealAccentAt,9.5f*HealArtScale*HealAccentEnvelope,
        4.5f*HealArtScale*HealAccentEnvelope,FLinearColor(.81f,.94f,.62f,1.f),4,-.13f);

    // Fixed three-volume secondary, delayed independently of the main mass.
    for(int32 HealDropIndex=0;HealDropIndex<3;++HealDropIndex)
    {
        const float HealDropAge=Age-(.09f+.055f*HealDropIndex);
        const float HealDropProgress=FMath::Clamp(HealDropAge/.76f,0.f,1.f);
        const float HealDropEnvelope=FMath::Max(.001f,
            FMath::SmoothStep(0.f,.075f,HealDropAge)*(1.f-FMath::SmoothStep(.52f,.76f,HealDropAge)));
        const float HealDropRise=1.f-FMath::Pow(1.f-HealDropProgress,2.f);
        const float HealDropStretch=1.f+.45f*FMath::Sin(HealDropProgress*PI);
        const float HealDropRadius=(6.8f-HealDropIndex)*HealArtScale*HealDropEnvelope;
        const FVector HealDropAt=R*(BodyRadius+27.f+HealDropIndex*10.f+9.f*FMath::Sin(HealDropProgress*PI))
            +FVector::UpVector*(-HalfHeight*.56f+HalfHeight*.95f*HealDropRise);
        Solid.Ball(HealDropAt,FVector(HealDropRadius/FMath::Sqrt(HealDropStretch),
            HealDropRadius*HealDropStretch,HealDropRadius/FMath::Sqrt(HealDropStretch)),
            FLinearColor(.18f+.045f*HealDropIndex,.76f,.44f,1.f));
    }
    // Two compact cream glints support the mass, not a large emissive disk.
    Glow.Puff(HealPrimaryAt-R*7.f+U*8.f,2.8f*HealScale,
        FLinearColor(.83f,1.f,.75f,.22f*Fade));
    Glow.Puff(HealAccentAt+U*2.f,1.8f*HealAccentEnvelope,
        FLinearColor(.9f,1.f,.78f,.18f*Fade));
}
else if(Kind==EMCPassiveFeedbackKind::Neighbor)
{
    const FVector NeighborEnd=FVector(LinkEnd)-GetActorLocation();
    const float NeighborArch=FMath::Clamp(NeighborEnd.Size2D()*.24f,38.f,78.f);
    const FVector NeighborOriginalView=Solid.View;
    // One lead bean and two smaller followers, all from confirmed source to End.
    for(int32 NeighborDropIndex=0;NeighborDropIndex<3;++NeighborDropIndex)
    {
        const float NeighborFlightAge=Age-(.045f+.045f*NeighborDropIndex);
        const float NeighborProgress=FMath::Clamp(NeighborFlightAge/.34f,0.f,1.f);
        const float NeighborEnvelope=FMath::Max(.001f,
            FMath::SmoothStep(0.f,.045f,NeighborFlightAge)
            *(1.f-FMath::SmoothStep(.285f,.35f,NeighborFlightAge)));
        const FVector NeighborAt=NeighborEnd*NeighborProgress
            +FVector::UpVector*(12.f+4.f*NeighborArch*NeighborProgress*(1.f-NeighborProgress));
        const FVector NeighborTangent=NeighborEnd
            +FVector::UpVector*(4.f*NeighborArch*(1.f-2.f*NeighborProgress));
        const FVector NeighborScreenTravel=FVector::VectorPlaneProject(NeighborTangent,NeighborOriginalView)
            .GetSafeNormal(UE_SMALL_NUMBER,R);
        Solid.Right=NeighborScreenTravel;
        Solid.Up=FVector::CrossProduct(NeighborOriginalView,NeighborScreenTravel).GetSafeNormal(UE_SMALL_NUMBER,U);
        const float NeighborStretch=1.f+.68f*FMath::Sin(NeighborProgress*PI);
        const float NeighborRadius=(16.f-4.6f*NeighborDropIndex)*NeighborEnvelope;
        Solid.Ball(NeighborAt,FVector(NeighborRadius*NeighborStretch,
            NeighborRadius/FMath::Sqrt(NeighborStretch),NeighborRadius/FMath::Sqrt(NeighborStretch)),
            FLinearColor(.10f+.035f*NeighborDropIndex,.70f,.39f,1.f));
    }
    Solid.Right=R;Solid.Up=U;Solid.View=NeighborOriginalView;
    const float NeighborArrivalAge=Age-.365f;
    const float NeighborArrivalP=FMath::Clamp(NeighborArrivalAge/.12f,0.f,1.f)-1.f;
    const float NeighborArrivalPop=1.f+2.70158f*NeighborArrivalP*NeighborArrivalP*NeighborArrivalP
        +1.70158f*NeighborArrivalP*NeighborArrivalP;
    const float NeighborArrivalScale=FMath::Max(.001f,NeighborArrivalPop
        *(1.f-FMath::SmoothStep(.16f,.36f,NeighborArrivalAge)));
    Solid.Star(NeighborEnd+FVector::UpVector*12.f,17.f*NeighborArrivalScale,
        7.5f*NeighborArrivalScale,FLinearColor(.75f,.95f,.54f,1.f),4,.13f);
    Glow.Puff(NeighborEnd+FVector::UpVector*12.f+U*3.f,3.f*NeighborArrivalScale,
        FLinearColor(.8f,1.f,.7f,.18f*Fade*FMath::SmoothStep(0.f,.08f,NeighborArrivalAge)));
}
else if(Kind==EMCPassiveFeedbackKind::Speed)
{
    const FVector SpeedOriginalView=Solid.View;
    const FVector SpeedWorldForward=FVector(Direction).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector SpeedScreenForward=FVector::VectorPlaneProject(SpeedWorldForward,SpeedOriginalView)
        .GetSafeNormal(UE_SMALL_NUMBER,R);
    const FVector SpeedScreenSide=FVector::CrossProduct(SpeedOriginalView,SpeedScreenForward)
        .GetSafeNormal(UE_SMALL_NUMBER,U);
    const auto AddSpeedVolume=[&](FVector Center,FVector Along,FVector Radii,FLinearColor Color) {
        Solid.Right=Along.GetSafeNormal(UE_SMALL_NUMBER,SpeedScreenForward);
        Solid.Up=FVector::CrossProduct(SpeedOriginalView,Solid.Right).GetSafeNormal(UE_SMALL_NUMBER,SpeedScreenSide);
        Solid.Ball(Center,Radii,Color);
    };
    // Three blunt arrowlets, each made from three joined closed ellipsoids.
    // They remain below the torso; no ring, face overlay or Tick-based spawn.
    for(int32 SpeedArrowIndex=0;SpeedArrowIndex<3;++SpeedArrowIndex)
    {
        const float SpeedArrowAge=Age-SpeedArrowIndex*.045f;
        const float SpeedArrowProgress=FMath::Clamp(SpeedArrowAge/.44f,0.f,1.f);
        const float SpeedArrowQ=FMath::Clamp(SpeedArrowAge/.11f,0.f,1.f)-1.f;
        const float SpeedArrowPop=1.f+2.70158f*SpeedArrowQ*SpeedArrowQ*SpeedArrowQ
            +1.70158f*SpeedArrowQ*SpeedArrowQ;
        const float SpeedArrowScale=FMath::Max(.001f,SpeedArrowPop
            *(1.f-FMath::SmoothStep(.31f,.54f,SpeedArrowAge)));
        const float SpeedArrowTravel=1.f-FMath::Pow(1.f-SpeedArrowProgress,3.f);
        const FVector SpeedArrowAt=-SpeedWorldForward*(43.f+SpeedArrowIndex*10.f)
            +SpeedScreenForward*(30.f*SpeedArrowTravel)
            +R*((SpeedArrowIndex-1)*21.f)
            +FVector::UpVector*(-HalfHeight*.58f+4.f*FMath::Sin(SpeedArrowProgress*PI));
        const FLinearColor SpeedBody(.10f+.025f*SpeedArrowIndex,.67f,.43f,1.f);
        AddSpeedVolume(SpeedArrowAt-SpeedScreenForward*(2.f*SpeedArrowScale),SpeedScreenForward,
            FVector(10.5f,3.8f,3.2f)*SpeedArrowScale,SpeedBody);
        for(int32 SpeedWingIndex=0;SpeedWingIndex<2;++SpeedWingIndex)
        {
            const float SpeedWingSide=SpeedWingIndex?1.f:-1.f;
            const FVector SpeedWingAxis=(SpeedScreenForward*4.f-SpeedScreenSide*(SpeedWingSide*3.f))
                .GetSafeNormal(UE_SMALL_NUMBER,SpeedScreenForward);
            const FVector SpeedWingAt=SpeedArrowAt+SpeedScreenForward*(3.f*SpeedArrowScale)
                +SpeedScreenSide*(SpeedWingSide*3.f*SpeedArrowScale);
            AddSpeedVolume(SpeedWingAt,SpeedWingAxis,
                FVector(6.8f,3.6f,3.2f)*SpeedArrowScale,SpeedBody);
        }
    }
    Solid.Right=R;Solid.Up=U;Solid.View=SpeedOriginalView;
}
else if(Kind==EMCPassiveFeedbackKind::Shield) {
    // Elastic guard: one thick side plate, a curved rolled rim, two late beads.
    // It is intentionally camera-relative; no invented incoming-hit normal.
    const float P=DFPop(Age,.045f,.13f,.23f,.53f)*DFK;
    const FVector C=R*(BodyRadius+.10f*DFH)-U*(.12f*DFH);
    const FVector ShieldView=Solid.View;
    const float ShieldTilt=.22f;
    Solid.Right=R*FMath::Cos(ShieldTilt)+ShieldView*FMath::Sin(ShieldTilt);
    Solid.View=ShieldView*FMath::Cos(ShieldTilt)-R*FMath::Sin(ShieldTilt);
    if(P>.001f) {
        const float Dent=1.f-.28f*FMath::Exp(-FMath::Square((Age-.075f)/.04f));
        Solid.Ball(C,FVector(.12f*Dent,.28f/FMath::Sqrt(Dent),.075f)*(DFH*P),FLinearColor(.25f,.49f,.52f,1));
        Solid.Arc(C,.265f*DFH*P,-PI*.5f,PI,.095f*DFH*P,.075f*DFH*P,FLinearColor(.62f,.80f,.76f,1));
        Solid.Ball(C+R*(.09f*DFH*P)+U*(.21f*DFH*P),FVector(.053f,.075f,.042f)*(DFH*P),FLinearColor(.88f,.91f,.72f,1));
    }
    for(int32 I=0;I<2;++I) {
        const float A=Age-(.075f+I*.045f),Q=FMath::Clamp(A/.31f,0.f,1.f);
        const float Pop=DFPop(A,.03f,.075f,.10f,.31f)*DFK;
        if(Pop<=.001f) continue;
        const float Sign=I?1.f:-1.f;
        const FVector At=C+R*(DFH*(.22f+.19f*Q))+U*(DFH*(Sign*.17f-.10f*Q*Q));
        Solid.Ball(At,FVector(.053f,.068f,.05f)*(DFH*Pop),FLinearColor(.58f,.77f,.71f,1));
    }
    Solid.Right=R;Solid.Up=U;Solid.View=ShieldView;
    const float A=DFTail(Age-.035f,.30f);
    if(A>0.f) Soft.Puff(C+R*(.08f*DFH),.18f*DFH,FLinearColor(.22f,.45f,.43f,A*.13f));
}
else if(Kind==EMCPassiveFeedbackKind::Enamel) {
    // Hard ricochet: a compact, thick four-point ping, then two rounded beads.
    // No shield outline, orbit, floor marker or persistent protection bubble.
    const float Q=FMath::Clamp(Age/.17f,0.f,1.f);
    const float P=DFPop(Age,.025f,.065f,.09f,.23f)*DFK;
    const FVector C=R*(BodyRadius+DFH*(.15f+.10f*Q))+U*(.08f*DFH);
    if(P>.001f) {
        Solid.Star(C,.17f*DFH*P,.075f*DFH*P,FLinearColor(.88f,.77f,.49f,1),4,.20f+.28f*Q);
        Solid.Ball(C+U*(.035f*DFH*P),FVector(.050f,.065f,.045f)*(DFH*P),FLinearColor(.98f,.94f,.75f,1));
    }
    for(int32 I=0;I<2;++I) {
        const float A=Age-(.065f+I*.045f),Puff=DFPop(A,.025f,.065f,.095f,.30f)*DFK;
        if(Puff<=.001f) continue;
        const float Pct=FMath::Clamp(A/.30f,0.f,1.f),Sign=I?1.f:-1.f;
        const FVector At=R*(BodyRadius+DFH*(.24f+.28f*Pct))+U*(DFH*(.08f+Sign*.16f*Pct-.07f*Pct*Pct));
        Solid.Ball(At,FVector(.055f,.070f,.05f)*(DFH*Puff),FLinearColor(.76f,.74f,.53f,1));
    }
    const float A=DFTail(Age-.015f,.16f);
    if(A>0.f) Soft.Puff(C,.14f*DFH,FLinearColor(.77f,.70f,.44f,A*.13f));
}
else if(Kind==EMCPassiveFeedbackKind::Wind) {
    // Recovery breath at one side of the chest/cheek, never at the feet.
    // Three unequal lobes arrive in sequence and stretch while slowing down.
    const FVector C=R*(BodyRadius+.10f*DFH)+U*(.045f*DFH);
    for(int32 I=0;I<3;++I) {
        const float A=Age-I*.055f,Q=FMath::Clamp(A/.43f,0.f,1.f);
        const float P=DFPop(A,.045f,.105f,.17f,.43f)*DFK*(1.f-I*.12f);
        if(P<=.001f) continue;
        const float Stretch=1.f+.50f*FMath::Sin(Q*PI),Cross=1.f/FMath::Sqrt(Stretch);
        const FVector At=C+R*(DFH*(.05f+Q*.36f-I*.10f))+U*(DFH*(I*.11f+Q*.17f));
        const FLinearColor Color=I==1?FLinearColor(.82f,.88f,.73f,1):FLinearColor(.54f,.77f,.66f,1);
        Solid.Ball(At,FVector(.14f*Stretch,.105f*Cross,.085f*Cross)*(DFH*P),Color);
    }
    for(int32 I=0;I<3;++I) {
        const float A=Age-(.14f+I*.045f),Q=FMath::Clamp(A/.30f,0.f,1.f);
        const float P=DFPop(A,.03f,.07f,.11f,.30f)*DFK;
        if(P<=.001f) continue;
        const FVector At=C+R*(DFH*(.12f+.31f*Q))+U*(DFH*(-.05f+I*.08f+.16f*Q));
        Solid.Ball(At,FVector(.05f,.06f,.045f)*(DFH*P),FLinearColor(.60f,.80f,.72f,1));
    }
    const float A=DFTail(Age-.07f,.43f);
    if(A>0.f) Soft.Puff(C+R*(.23f*DFH)+U*(.12f*DFH),.17f*DFH,FLinearColor(.45f,.69f,.58f,A*.12f));
}
else if(Kind==EMCPassiveFeedbackKind::Vanish || Kind==EMCPassiveFeedbackKind::Return) {
    // Root MUST anchor Vanish at its spawn world position. Return may follow
    // the now-visible Subject. These puffs do not track an invisible dash.
    const bool Returning=Kind==EMCPassiveFeedbackKind::Return;
    const FVector2D Offsets[5]={FVector2D(-.30f,-.12f),FVector2D(.26f,-.22f),FVector2D(-.15f,.20f),FVector2D(.20f,.12f),FVector2D(0.f,-.35f)};
    const float Size[5]={.22f,.18f,.17f,.20f,.16f};
    const float Delay[5]={0.f,.035f,.07f,.02f,.09f};
    for(int32 I=0;I<5;++I) {
        const float A=Age-Delay[I],Q=FMath::Clamp(A/.34f,0.f,1.f);
        const float P=DFPop(A,.045f,.10f,.14f,.34f)*DFK;
        if(P<=.001f) continue;
        const float Sign=I%2?1.f:-1.f;
        const float X=Returning?Sign*(FMath::Abs(Offsets[I].X)+.25f+.20f*Q):Offsets[I].X+Sign*.15f*Q;
        const float Y=Offsets[I].Y+(Returning?-.28f:0.f)+.10f*Q;
        const FVector Center=(R*X+U*Y)*DFH;
        const float Stretch=1.f+.20f*FMath::Sin(Q*PI);
        const FLinearColor Color=I%2?FLinearColor(.66f,.79f,.74f,1):FLinearColor(.78f,.85f,.77f,1);
        Solid.Ball(Center,FVector(1.1f/Stretch,Stretch,.82f)*(DFH*Size[I]*P),Color);
    }
    for(int32 I=0;I<2;++I) {
        const float A=Age-(.13f+I*.045f),Q=FMath::Clamp(A/.29f,0.f,1.f);
        const float P=DFPop(A,.025f,.06f,.10f,.29f)*DFK;
        if(P<=.001f) continue;
        const float Sign=I?1.f:-1.f;
        const FVector At=R*(Sign*DFH*((Returning?.58f:.36f)+.18f*Q))-U*(DFH*(.15f+.10f*Q));
        Solid.Ball(At,FVector(.065f,.055f,.06f)*(DFH*P),FLinearColor(.58f,.73f,.70f,1));
    }
    const float A=DFTail(Age-.06f,.39f);
    if(A>0.f) {
        const float Side=Returning?.55f:.30f;
        for(float Sign:{-1.f,1.f}) Soft.Puff(R*(Sign*Side*DFH)-U*(.13f*DFH),.17f*DFH,FLinearColor(.44f,.62f,.58f,A*.12f));
    }
}

else if(Kind==EMCPassiveFeedbackKind::PowerHit)
{
    // A single warm, thick star leads the confirmed dash-hit. The open crescent
    // and delayed small stars have separate envelopes, never a white disk.
    const float P=DFPop(Age,.025f,.075f,.13f,.35f)*FMath::Clamp(S,.75f,1.3f);
    const FVector C=FVector(Direction)*(BodyRadius*.85f)+R*(BodyRadius*.45f)-U*4.f;
    Solid.Star(C,29.f*P,11.f*P,FLinearColor(.91f,.49f,.12f,1),5,-.20f+.30f*Ease);
    const float Crescent=DFPop(Age-.025f,.035f,.09f,.13f,.32f);
    Solid.Arc(C,36.f*Crescent,-1.1f+.16f*Ease,PI*.84f,11.f*Crescent,8.f*Crescent,FLinearColor(.98f,.78f,.38f,1));
    for(int32 I=0;I<2;++I) {
        const float A=Age-(.075f+I*.055f),Q=FMath::Clamp(A/.30f,0.f,1.f);
        const float K=DFPop(A,.025f,.07f,.12f,.30f);
        const FVector At=C+R*((I?1.f:-1.f)*(25.f+22.f*Q))+U*(10.f+15.f*Q-18.f*Q*Q);
        Solid.Star(At,(9.f-I*2.f)*K,5.f*K,FLinearColor(.88f,.71f,.31f,1),4,.35f+Q*.4f);
    }
    Soft.Puff(C,13.f*P,FLinearColor(.76f,.35f,.08f,.10f*Fade));
}
else if(Kind==EMCPassiveFeedbackKind::Billiards)
{
    // Two rounded masses meet, recoil, then release three blunt chips. This
    // narrow secondary collision is distinct from the primary five-tip star.
    const FVector V=Solid.View;
    const FVector Along=FVector::VectorPlaneProject(FVector(Direction),V).GetSafeNormal(UE_SMALL_NUMBER,R);
    Solid.Right=Along;Solid.Up=FVector::CrossProduct(V,Along).GetSafeNormal(UE_SMALL_NUMBER,U);
    const float Q=FMath::Clamp(Age/.19f,0.f,1.f),P=DFPop(Age,.025f,.07f,.11f,.28f);
    const float Offset=8.f+14.f*(1-FMath::Pow(1-Q,3.f));
    const FVector C=-U*5.f;
    Solid.Ball(C-Along*Offset,FVector(15.f,8.f,6.f)*P,FLinearColor(.08f,.60f,.55f,1));
    Solid.Ball(C+Along*Offset,FVector(13.f,9.f,6.f)*P,FLinearColor(.96f,.69f,.28f,1));
    Solid.Right=R;Solid.Up=U;
    for(int32 I=0;I<3;++I) {
        const float A=Age-(.045f+I*.035f),Toss=FMath::Clamp(A/.30f,0.f,1.f),K=DFPop(A,.025f,.07f,.12f,.30f);
        const FVector At=C+Along*((I-1)*(20.f+16.f*Toss))+U*(12.f+24.f*Toss-20.f*Toss*Toss);
        Solid.Star(At,(I==1?10.f:7.f)*K,4.5f*K,I==1?FLinearColor(.96f,.78f,.36f,1):FLinearColor(.12f,.69f,.56f,1),4,I*.4f+Toss*.35f);
    }
}
    Soft.Upload(Mesh,0);Glow.Upload(Mesh,1);Solid.Upload(Mesh,2);
}
void AMCPassiveFeedbackCue::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCPassiveFeedbackCue,Kind);DOREPLIFETIME(AMCPassiveFeedbackCue,Subject);DOREPLIFETIME(AMCPassiveFeedbackCue,StartedAt);DOREPLIFETIME(AMCPassiveFeedbackCue,Strength);DOREPLIFETIME(AMCPassiveFeedbackCue,Direction);DOREPLIFETIME(AMCPassiveFeedbackCue,LinkEnd);
}
