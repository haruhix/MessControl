#include "MCWeaponTrailComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXNativeInputs.h"
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
    Composer=MCWorldPresentation::PrepareOwner(Hero,{TEXT("Tool.Knife"),TEXT("Tool.Pickaxe")});
    Ribbon=NewObject<UProceduralMeshComponent>(Hero,TEXT("WeaponSwingRibbon"));
    Ribbon->SetupAttachment(Hero->GetRootComponent());
    Ribbon->SetAbsolute(true,true,true);
    Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ribbon->SetGenerateOverlapEvents(false);Ribbon->SetCanEverAffectNavigation(false);
    Ribbon->SetCastShadow(false);Ribbon->bReceivesDecals=false;
    Ribbon->SetVisibility(false);Hero->AddInstanceComponent(Ribbon);Ribbon->RegisterComponent();
    if(auto* Material=TrailMaterial.LoadSynchronous()) Ribbon->SetMaterial(0,Material);
}
void UMCWeaponTrailComponent::StopComposerState()
{
    if(Composer && !PresentationSource.IsNone()) {
        FMCPerkVFXGameplayValues Values;FString Error;
        Composer->UpdateState(PresentationSource,TEXT("Swing"),false,FTransform::Identity,Values,nullptr,Error);
    }
    bPublished=false;
}
void UMCWeaponTrailComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopComposerState();Samples.Reset();Super::EndPlay(Reason);
}
void UMCWeaponTrailComponent::PublishAcceptedSwing(FName SourceID,double StartedAt,uint8 VisualStep)
{
    if(!Hero || !Composer || (SourceID!=TEXT("Tool.Knife") && SourceID!=TEXT("Tool.Pickaxe"))
        || !FMath::IsFinite(StartedAt))return;
    if(bPublished && PresentationSource==SourceID && AcceptedAt==StartedAt)return;
    if(bPublished)StopComposerState();
    PresentationSource=SourceID;AcceptedAt=StartedAt;AcceptedStep=VisualStep;
    SwingTool=uint8(SourceID==TEXT("Tool.Knife")?EMCToolSlot::Knife:EMCToolSlot::Pickaxe);
    PresentationEpisode=FGuid::NewGuid();PoseBreakSerial=0;bHasPreviousPose=false;bPublished=true;
    FVector Base=Hero->GetActorLocation(),Tip=Base;
    const bool HaveSpan=Hero->Inventory && uint8(Hero->Inventory->Selected)==SwingTool && Hero->Inventory->MeleeTrailSpan(Base,Tip);
    // Initial data is real (possibly resting) evaluated tool geometry. The NS
    // owns its emission window and cannot emit until its native-valid gate.
    PublishComposerState(true,0.f,HaveSpan,Base,Tip,false);SetComponentTickEnabled(true);
}
void UMCWeaponTrailComponent::PublishComposerState(bool Active,float PoseDelta,bool HaveSpan,FVector Base,FVector Tip,bool RenderAllowed)
{
    if(!bPublished || !Composer || !Hero || !Hero->Inventory)return;
    FMCPerkVFXGameplayValues Values;Values.VisualSubject=Hero->GetMesh();Values.EpisodeID=PresentationEpisode;
    Values.LocalStateAgeSeconds=FMath::Max(0.f,Hero->GetToolSwingElapsed());
    Values.Duration=Hero->Inventory->SwingDuration();
    const bool Valid=HaveSpan && !Base.ContainsNaN() && !Tip.ContainsNaN();
    const float Travel=Valid&&bHasPreviousPose?float(FMath::Max(FVector::Dist(Base,PreviousBase),FVector::Dist(Tip,PreviousTip))):0.f;
    FVector TipDelta=Valid&&bHasPreviousPose?Tip-PreviousTip:FVector::ZeroVector;
    if((Valid&&bHasPreviousPose&&Travel>300.f) || (!Valid&&bHasPreviousPose)) {
        PoseBreakSerial=PoseBreakSerial==MAX_int32?1:PoseBreakSerial+1;TipDelta=FVector::ZeroVector;
    }
    FVector Eye=Origin+FVector(-300,0,200);
    if(const auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager)Eye=PC->PlayerCameraManager->GetCameraLocation();
    Values.NativeInputs.Entries.Reserve(19);
    auto Add=[&](const TCHAR* Name,EMCVFXNativeInputType Type)->FMCVFXNativeInput& {
        auto& Input=Values.NativeInputs.Entries.AddDefaulted_GetRef();Input.Name=FName(Name);Input.Type=Type;return Input;
    };
    Add(TEXT("User.Native.BladeBase"),EMCVFXNativeInputType::Position).PositionValue=Valid?Base:Hero->GetActorLocation();
    Add(TEXT("User.Native.BladeTip"),EMCVFXNativeInputType::Position).PositionValue=Valid?Tip:Hero->GetActorLocation();
    Add(TEXT("User.Native.BladeCenter"),EMCVFXNativeInputType::Position).PositionValue=Valid?(Base+Tip)*.5:Hero->GetActorLocation();
    Add(TEXT("User.Native.BladeSpan"),EMCVFXNativeInputType::Vector).VectorValue=Valid?Tip-Base:FVector::ZeroVector;
    Add(TEXT("User.Native.BladeTipDelta"),EMCVFXNativeInputType::Vector).VectorValue=TipDelta;
    Add(TEXT("User.Native.BladeTravel"),EMCVFXNativeInputType::Float).FloatValue=Travel;
    Add(TEXT("User.Native.BladePoseValid"),EMCVFXNativeInputType::Bool).BoolValue=Valid;
    Add(TEXT("User.Native.BladeBreakSerial"),EMCVFXNativeInputType::Int).IntValue=PoseBreakSerial;
    Add(TEXT("User.Native.SwingAge"),EMCVFXNativeInputType::Float).FloatValue=Values.LocalStateAgeSeconds;
    Add(TEXT("User.Native.SwingVisualAge"),EMCVFXNativeInputType::Float).FloatValue=FMath::Max(0.f,MCWeaponImpact::VisualSwingElapsed(Hero));
    Add(TEXT("User.Native.SwingDuration"),EMCVFXNativeInputType::Float).FloatValue=Values.Duration;
    Add(TEXT("User.Native.SwingContactTime"),EMCVFXNativeInputType::Float).FloatValue=Hero->Inventory->SwingContactTime();
    const FVector2D NativeTrailWindow=MCMeleeCombo::TrailWindow(Hero);
    const bool HasComboWindow=NativeTrailWindow.X>=0.f&&NativeTrailWindow.Y>NativeTrailWindow.X;
    Add(TEXT("User.Native.SwingRate"),EMCVFXNativeInputType::Float).FloatValue=
        HasComboWindow?MCMeleeCombo::AnimationRate(Hero):Hero->Inventory->RogueAttackRate();
    Add(TEXT("User.Native.SwingStep"),EMCVFXNativeInputType::Int).IntValue=AcceptedStep;
    Add(TEXT("User.Native.TrailRenderAllowed"),EMCVFXNativeInputType::Bool).BoolValue=RenderAllowed&&Valid;
    Add(TEXT("User.Native.TrailViewPosition"),EMCVFXNativeInputType::Position).PositionValue=Eye;
    Add(TEXT("User.Native.PoseDeltaSeconds"),EMCVFXNativeInputType::Float).FloatValue=FMath::Max(0.f,PoseDelta);
    FString Error;const bool Updated=Composer->UpdateState(PresentationSource,TEXT("Swing"),Active,Hero->GetActorTransform(),Values,this,Error);
    bNativeTrail=!Updated||Composer->UsesNativeVisuals(PresentationSource,TEXT("Swing"));
    if(Valid){PreviousBase=Base;PreviousTip=Tip;}bHasPreviousPose=Valid;
    if(!Active)bPublished=false;
}
void UMCWeaponTrailComponent::BeginSwing()
{
    if(bPublished)StopComposerState();
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
    if(!Hero || !Ribbon || !Hero->Inventory) {StopComposerState();SetComponentTickEnabled(false);return;}
    const double Now=GetWorld()->GetTimeSeconds();
    const float Visual=MCWeaponImpact::VisualSwingElapsed(Hero);
    FVector2D Window=MCMeleeCombo::TrailWindow(Hero);
    if(Window.X<0) {
        const float Rate=Hero->Inventory->RogueAttackRate(),Contact=Hero->Inventory->SwingContactTime();
        Window=FVector2D(FMath::Max(0.f,Contact-.10f/Rate),Contact+.10f/Rate);
    }
    const bool Eligible=Enabled().GetValueOnGameThread() && Hero->CanWork()
        && MCMeleeCombo::Supports(Hero) && uint8(Hero->Inventory->Selected)==SwingTool
        && Hero->Inventory->ShouldPresentTool() && !Hero->ActorHasTag(TEXT("MC.LegacyCombatFeedback"));
    bEmitting=Eligible && Visual>=Window.X && Visual<=Window.Y;
    FVector Base=Hero->GetActorLocation(),Tip=Base;
    const bool HaveSpan=Eligible && Hero->Inventory->MeleeTrailSpan(Base,Tip);
    if(bEmitting && HaveSpan) {
        const float Moved=Samples.IsEmpty()?1.f:FMath::Max(FVector::Dist(Base,Samples.Last().Base),FVector::Dist(Tip,Samples.Last().Tip));
        // Do not draw a bridge across teleports, tool changes or an interrupted pose.
        if(Moved>300.f) {Samples.Reset();bEmitting=false;}
        else if(Moved>.05f) Samples.Add({Base,Tip,Now});
    }
    const float Life=FMath::Clamp(HistorySeconds,.03f,.25f);
    Samples.RemoveAll([&](const FMCWeaponTrailSample& S){return Now-S.At>Life;});
    if(Samples.Num()>MaxSamples) Samples.RemoveAt(0,Samples.Num()-MaxSamples,EAllowShrinking::No);
    ActiveSamples=Samples.Num();
    const bool ScopeActive=bPublished && Hero->CanWork() && uint8(Hero->Inventory->Selected)==SwingTool
        && Hero->GetToolSwingElapsed()>=0.f && Hero->GetToolSwingElapsed()<Hero->Inventory->SwingDuration();
    if(bPublished)PublishComposerState(ScopeActive,Dt,HaveSpan,Base,Tip,Eligible);
    Upload(Now);
    if(Samples.IsEmpty() && (!Eligible || Visual>Window.Y)) {bEmitting=false;if(!bPublished)SetComponentTickEnabled(false);}
}
void UMCWeaponTrailComponent::Upload(double Now)
{
    if(!bNativeTrail) {RenderedVertices=0;Ribbon->ClearAllMeshSections();Ribbon->SetVisibility(false);return;}
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
