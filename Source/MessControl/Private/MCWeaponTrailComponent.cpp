#include "MCWeaponTrailComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCWeaponImpact.h"
#include "MCMeleeCombo.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
TAutoConsoleVariable<int32>& Enabled()
{
    static TAutoConsoleVariable<int32> Value(TEXT("mc.WeaponTrail.Enable"),1,TEXT("Show ordinary knife and pickaxe cutting trails."));
    return Value;
}
constexpr int32 MaxSamples=24,Subdivisions=3,Columns=8;
FVector Curve(const FVector& A,const FVector& B,const FVector& C,const FVector& D,float T)
{ return FMath::CubicInterp(B,(C-A)*.5f,C,(D-B)*.5f,T); }
}

UMCWeaponTrailComponent::UMCWeaponTrailComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    TrailMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/WeaponTrail/M_WeaponSwingTrail.M_WeaponSwingTrail"));
}
void UMCWeaponTrailComponent::BeginPlay()
{
    Super::BeginPlay();Hero=Cast<AMCToothCharacter>(GetOwner());
    if(!Hero || GetNetMode()==NM_DedicatedServer) return;
    PrimaryComponentTick.AddPrerequisite(Hero->GetMesh(),Hero->GetMesh()->PrimaryComponentTick);
    Ribbon=NewObject<UProceduralMeshComponent>(Hero,TEXT("WeaponSwingRibbon"));
    Ribbon->SetupAttachment(Hero->GetRootComponent());
    Ribbon->SetAbsolute(true,true,true);
    Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ribbon->SetGenerateOverlapEvents(false);Ribbon->SetCanEverAffectNavigation(false);
    Ribbon->SetCastShadow(false);Ribbon->bReceivesDecals=false;
    Ribbon->SetVisibility(false);Hero->AddInstanceComponent(Ribbon);Ribbon->RegisterComponent();
    if(auto* Material=TrailMaterial.LoadSynchronous()) Ribbon->SetMaterial(0,Material);
}
void UMCWeaponTrailComponent::BeginSwing()
{
    if(!Hero || !Ribbon) return;
    Samples.Reset();ActiveSamples=RenderedVertices=0;bEmitting=false;
    Ribbon->ClearAllMeshSections();Ribbon->SetVisibility(false);
    if(!Enabled().GetValueOnGameThread() || !MCMeleeCombo::Supports(Hero)
        || !Hero->Inventory->ShouldPresentTool() || Hero->ActorHasTag(TEXT("MC.LegacyCombatFeedback"))) {
        SetComponentTickEnabled(false);return;
    }
    Origin=Hero->GetActorLocation();Ribbon->SetWorldTransform(FTransform(Origin));
    SwingTool=uint8(Hero->Inventory->Selected);++PresentedSwings;
    SetComponentTickEnabled(true);
}
void UMCWeaponTrailComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt,TickType,ThisTickFunction);
    if(!Hero || !Ribbon || !Hero->Inventory) {SetComponentTickEnabled(false);return;}
    const double Now=GetWorld()->GetTimeSeconds();
    const float Visual=MCWeaponImpact::VisualSwingElapsed(Hero);
    FVector2D Window=MCMeleeCombo::TrailWindow(Hero);
    if(Window.X<0) {
        const float Rate=Hero->Inventory->RogueAttackRate(),Contact=Hero->Inventory->SwingContactTime();
        Window=FVector2D(FMath::Max(0.f,Contact-.10f/Rate),Contact+.10f/Rate);
    }
    const bool Eligible=Enabled().GetValueOnGameThread() && Hero->CanWork()
        && MCMeleeCombo::Supports(Hero) && uint8(Hero->Inventory->Selected)==SwingTool
        && Hero->Inventory->ShouldPresentTool();
    bEmitting=Eligible && Visual>=Window.X && Visual<=Window.Y;
    FVector Base,Tip;
    if(bEmitting && Hero->Inventory->MeleeTrailSpan(Base,Tip)) {
        const float Moved=Samples.IsEmpty()?1.f:FMath::Max(FVector::Dist(Base,Samples.Last().Base),FVector::Dist(Tip,Samples.Last().Tip));
        // Do not draw a bridge across teleports, tool changes or an interrupted pose.
        if(Moved>300.f) {Samples.Reset();bEmitting=false;}
        else if(Moved>.05f) Samples.Add({Base,Tip,Now});
    }
    const float Life=FMath::Clamp(HistorySeconds,.03f,.25f);
    Samples.RemoveAll([&](const FMCWeaponTrailSample& S){return Now-S.At>Life;});
    if(Samples.Num()>MaxSamples) Samples.RemoveAt(0,Samples.Num()-MaxSamples,EAllowShrinking::No);
    ActiveSamples=Samples.Num();Upload(Now);
    if(Samples.IsEmpty() && (!Eligible || Visual>Window.Y)) {bEmitting=false;SetComponentTickEnabled(false);}
}
void UMCWeaponTrailComponent::Upload(double Now)
{
    if(Samples.Num()<2) {RenderedVertices=0;Ribbon->ClearAllMeshSections();Ribbon->SetVisibility(false);return;}
    TArray<FVector> P,N;TArray<FVector2D> UV;TArray<int32> Tri;
    TArray<FLinearColor> Color;TArray<FProcMeshTangent> Tangents;
    const int32 Rows=(Samples.Num()-1)*Subdivisions+1,Stride=Columns+1;
    const float Life=FMath::Clamp(HistorySeconds,.03f,.25f);
    const auto* PC=GetWorld()->GetFirstPlayerController();
    const FVector Eye=PC && PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraLocation():Origin+FVector(-300,0,200);
    for(int32 Row=0;Row<Rows;++Row) {
        const int32 I=FMath::Min(Row/Subdivisions,Samples.Num()-2);
        const float T=(Row-I*Subdivisions)/float(Subdivisions);
        const auto& A=Samples[FMath::Max(0,I-1)];const auto& B=Samples[I];
        const auto& C=Samples[I+1];const auto& D=Samples[FMath::Min(I+2,Samples.Num()-1)];
        const FVector Base=Curve(A.Base,B.Base,C.Base,D.Base,T),Tip=Curve(A.Tip,B.Tip,C.Tip,D.Tip,T);
        const float Fade=FMath::Pow(FMath::Clamp(1.f-float(Now-FMath::Lerp(B.At,C.At,double(T)))/Life,0.f,1.f),1.35f);
        const FVector Center=(Base+Tip)*.5f;
        FVector Span=Tip-Base;
        FVector ScreenSide=FVector::CrossProduct((Eye-Center).GetSafeNormal(),C.Tip-B.Tip).GetSafeNormal();
        if(FVector::DotProduct(ScreenSide,Span)<0) ScreenSide=-ScreenSide;
        // Retain the blade's span, with a small luminous thickness when its
        // physical plane is edge-on to the gameplay camera.
        Span+=ScreenSide*FMath::Max(0.f,24.f-FMath::Abs(FVector::DotProduct(Span,ScreenSide)));
        Span*=FMath::Lerp(.12f,1.f,FMath::Sqrt(Fade));
        const FVector Normal=FVector::CrossProduct(Tip-Base,C.Tip-B.Tip).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        for(int32 J=0;J<=Columns;++J) {
            const float V=J/float(Columns);
            P.Add(Center+Span*(V-.5f)-Origin);N.Add(Normal);
            UV.Add(FVector2D(Row/float(Rows-1),V));Color.Add(FLinearColor(1,1,1,Fade));
            Tangents.Add(FProcMeshTangent(Span.GetSafeNormal(),false));
            if(Row>0 && J>0) {
                const int32 K=Row*Stride+J;
                Tri.Append({K-Stride-1,K-Stride,K,K-Stride-1,K,K-1});
            }
        }
    }
    RenderedVertices=P.Num();
    Ribbon->CreateMeshSection_LinearColor(0,P,Tri,N,UV,Color,Tangents,false);
    Ribbon->SetVisibility(true);
}
