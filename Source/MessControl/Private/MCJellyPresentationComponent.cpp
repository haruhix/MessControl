#include "MCJellyPresentationComponent.h"

#include "MCPerkVFXCompositionPlayer.h"
#include "MCVFXNativeInputs.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCCataclysmEvent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

UMCJellyPresentationComponent::UMCJellyPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(false); // RupturedAt/bPendingRupture already replicate on the jelly.
}

void UMCJellyPresentationComponent::PreparePresentation()
{
    if(!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer || !IsValid(GetOwner()))return;
    if(!Presentation.IsValid())Presentation=MCWorldPresentation::PrepareOwner(GetOwner(),{SourceID()});
    else {FString Error;Presentation->PrepareSources(Error,false);}
    // The actual encounter can retain an immutable preview/library source
    // before spawning its real children. Copy preparation, never receipt/RNG.
    if(auto* Encounter=Cast<AMCCataclysmEvent>(GetOwner()->GetOwner()))
        if(auto* Parent=Encounter->FindComponentByClass<UMCWorldPresentationComponent>())
            if(auto* Source=Parent->GetSource(SourceID()))
                if(auto* Target=Presentation.IsValid()?Presentation->GetSource(SourceID()):nullptr)
                {FString Error;Source->CopyPreparedSourceTo(SourceID(),Target,Error);}
}

void UMCJellyPresentationComponent::SetNativeGeometry(FMCPerkVFXGameplayValues& Values,float BodyRadius,
    float TrailRadius,float RuptureRadius,float SizeScale,int32 Tier)
{
    const auto Float=[&](const TCHAR* Name,float Value)
    {
        auto& Entry=Values.NativeInputs.Entries.AddDefaulted_GetRef();Entry.Name=FName(Name);
        Entry.Type=EMCVFXNativeInputType::Float;Entry.FloatValue=Value;
    };
    Float(TEXT("User.Native.BodyRadius"),BodyRadius);
    Float(TEXT("User.Native.TrailRadius"),TrailRadius);
    Float(TEXT("User.Native.RuptureRadius"),RuptureRadius);
    Float(TEXT("User.Native.PresentationScale"),SizeScale);
    auto& Entry=Values.NativeInputs.Entries.AddDefaulted_GetRef();Entry.Name=TEXT("User.Native.JellyTier");
    Entry.Type=EMCVFXNativeInputType::Int;Entry.IntValue=Tier;
}

void UMCJellyPresentationComponent::UpdateNativeState(USceneComponent* BodyFrame,USceneComponent* ActualVisual,
    bool bContact,double ServerNow,double RupturedAt,float BodyRadius,float TrailRadius,float RuptureRadius,
    float SizeScale,int32 Tier)
{
    bKeepContact=bKeepDroplets=true;
    auto* Prepared=Presentation.Get();
    if(!Prepared || !IsValid(BodyFrame) || BodyFrame->GetWorld()!=GetWorld()
        || !FMath::IsFinite(ServerNow) || !FMath::IsFinite(RupturedAt))return;
    FMCPerkVFXGameplayValues Values;
    Values.VisualSubject=ActualVisual;Values.FrameComponent=BodyFrame;
    Values.SourcePosition=Values.TargetPosition=BodyFrame->GetComponentLocation();Values.bHasEndpoints=true;
    Values.Direction=BodyFrame->GetForwardVector();
    SetNativeGeometry(Values,BodyRadius,TrailRadius,RuptureRadius,SizeScale,Tier);
    const FTransform Frame=BodyFrame->GetComponentTransform();
    FString Error;
    // Existing Trail means the living wet contact footprint, not ownership of
    // the brush-cleanable paint deposited by RegisterJellyPaintStroke.
    Values.Radius=TrailRadius;
    const bool bContactAccepted=Prepared->UpdateState(SourceID(),TEXT("Trail"),bContact,Frame,Values,BodyFrame,Error);
    bKeepContact=!bContactAccepted || Prepared->UsesNativeVisuals(SourceID(),TEXT("Trail"));
    if(bContactAccepted && bContact && !bContactWasActive)++AcceptedContactSerial;
    bContactWasActive=bContact && bContactAccepted;
    if(RupturedAt>-99. && RupturedAt!=ObservedRupturedAt)
    {
        ObservedRupturedAt=RupturedAt;
        Values.Radius=RuptureRadius;
        // Seek the actual replicated rupture clock. Late relevance never
        // restarts a new 14-particle burst or repeats any paint/split decision.
        Values.LocalStateAgeSeconds=FMath::Max(0.f,float(ServerNow-RupturedAt));
        const bool bAccepted=Prepared->Pulse(SourceID(),TEXT("Impact"),Frame,Values,1.1f,Error);
        if(bAccepted)++AcceptedRuptureSerial;
    }
    bKeepDroplets=Prepared->UsesNativeVisuals(SourceID(),TEXT("Impact"));
}

void UMCJellyPresentationComponent::Stop()
{
    if(auto* Prepared=Presentation.Get())Prepared->StopStates(SourceID());
    bContactWasActive=false;bKeepContact=bKeepDroplets=true;
}

void UMCJellyPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{Stop();Super::EndPlay(Reason);}
