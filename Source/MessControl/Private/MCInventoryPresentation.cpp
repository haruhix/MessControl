#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace MCInventoryPresentation
{
const FName ID(TEXT("Tool.Spray"));
UMCVFXPresentationComponent* Channel(const AMCToothCharacter* Hero)
{
    const auto* Owner=Hero?Hero->FindComponentByClass<UMCWorldPresentationComponent>():nullptr;
    return Owner?Owner->GetSource(ID):nullptr;
}
FMCPerkVFXGameplayValues Inputs(AMCToothCharacter* Hero,FVector Origin,FVector Direction,float Reach,float Width,
    bool WaterCare,FVector Contact,bool ContactKnown,double AcceptedAt,float Age)
{
    FMCPerkVFXGameplayValues V;V.VisualSubject=Hero?Hero->GetMesh():nullptr;V.Direction=Direction;
    V.Radius=Reach;V.LocalStateAgeSeconds=FMath::Max(0.f,Age);
    V.bHasEndpoints=true;V.SourcePosition=Origin;V.TargetPosition=ContactKnown?Contact:Origin+Direction*Reach;
    auto Position=[&](const TCHAR* Name,FVector Value){auto& R=V.NativeInputs.Entries.AddDefaulted_GetRef();R.Name=Name;R.Type=EMCVFXNativeInputType::Position;R.PositionValue=Value;};
    auto Float=[&](const TCHAR* Name,float Value){auto& R=V.NativeInputs.Entries.AddDefaulted_GetRef();R.Name=Name;R.Type=EMCVFXNativeInputType::Float;R.FloatValue=Value;};
    auto Bool=[&](const TCHAR* Name,bool Value){auto& R=V.NativeInputs.Entries.AddDefaulted_GetRef();R.Name=Name;R.Type=EMCVFXNativeInputType::Bool;R.BoolValue=Value;};
    Position(TEXT("User.Native.SprayOrigin"),Origin);Position(TEXT("User.Native.SprayContactPosition"),V.TargetPosition);
    auto& Axis=V.NativeInputs.Entries.AddDefaulted_GetRef();Axis.Name=TEXT("User.Native.SprayDirection");Axis.Type=EMCVFXNativeInputType::Vector;Axis.VectorValue=Direction;
    Float(TEXT("User.Native.SprayReach"),Reach);Float(TEXT("User.Native.SprayWidthMultiplier"),Width);
    Float(TEXT("User.Native.SprayAcceptedAt"),float(AcceptedAt));
    Bool(TEXT("User.Native.SprayWaterCare"),WaterCare);Bool(TEXT("User.Native.SprayContactKnown"),ContactKnown);
    return V;
}
}

bool UMCInventoryComponent::PresentAcceptedSprayContact(FVector Point)
{
    // Called only AFTER an existing native Extinguish/Treat/Repair succeeds.
    // This local observation never resolves contact or changes LastSprayAt.
    if(!Hero||Point.ContainsNaN())return false;
    const double Time=Now();
    if(Time-SprayContactPresentationAt>.15)SprayContactPresentationStarted=Time;
    SprayContactPresentationAt=Time;SprayContactPresentationPoint=Point;
    bSprayContactPointKnown=true;
    auto* Source=MCInventoryPresentation::Channel(Hero);if(!Source)return false;
    const FVector Origin=SprayOrigin(),Direction=SprayDirection();
    auto V=MCInventoryPresentation::Inputs(Hero,Origin,Direction,SprayReach(),SprayWidthMultiplier(),
        HasUpgrade(EMCToolUpgrade::Watergun),Point,true,Time,float(Time-SprayContactPresentationStarted));
    FString Error;
    const bool Accepted=Source->UpdateLocalSourceState(MCInventoryPresentation::ID,TEXT("Contact"),true,
        FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(),Point),V,this,Error);
    if(Accepted)SprayAcceptedContactAt=Time;
    return Accepted;
}

bool UMCInventoryComponent::UpdateSprayComposerPresentation(bool Spray,bool Water)
{
    const double Time=Now();
    const FName Event=Spray?FName(TEXT("Start")):Water?FName(TEXT("WaterCare")):NAME_None;
    auto* Source=MCInventoryPresentation::Channel(Hero);
    const FVector Origin=SprayOrigin(),Direction=SprayDirection();
    const float Reach=SprayReach(),Width=SprayWidthMultiplier();
    const float AuthoredReach=Water?(Settings?Settings->WatergunCareReach:1000.f):(Settings?Settings->SprayReach:235.f);
    const float BaseReach=FMath::IsFinite(AuthoredReach)?FMath::Clamp(AuthoredReach,10.f,3000.f):235.f;
    const float Range=Reach/BaseReach;
    // Match the exact existing component frame/scale, including watercare's
    // original unit scale. The pressure Shot/Impact action is independent.
    const FTransform Frame(FRotationMatrix::MakeFromZ(Direction).ToQuat(),Origin,Water?FVector::OneVector:FVector(Range*Width,Range*Width,Range));
    const FName Previous=SprayPresentationEvent;
    if(Event!=Previous) {
        if(Source&&!Previous.IsNone()) {
            FString Error;FMCPerkVFXGameplayValues Empty;
            Source->UpdateLocalSourceState(MCInventoryPresentation::ID,Previous,false,SprayPresentationFrame,Empty,this,Error);
            const auto End=Source->PlayLocalSourceEvent(MCInventoryPresentation::ID,TEXT("End"),SprayPresentationFrame,
                MCInventoryPresentation::Inputs(Hero,SprayPresentationFrame.GetLocation(),SprayPresentationFrame.GetRotation().GetAxisZ(),
                    Reach,Width,Previous==TEXT("WaterCare"),SprayContactPresentationPoint,bSprayContactPointKnown,
                    SprayContactPresentationAt,float(Time-SprayPresentationStarted)));
            if(End!=EMCVFXPresentationResult::Rejected)++SprayAcceptedEndSerial;
        }
        SprayPresentationEvent=Event;SprayAcceptedPresentationEvent=NAME_None;SprayPresentationStarted=Time;
    }
    if(!Event.IsNone())SprayPresentationFrame=Frame;

    // LastSprayAt is the existing replicated successful native-work stamp.
    // Remote peers use the already-replicated live target; no cosmetic RPC,
    // authority decision, additional trace or replay is introduced here.
    if(LastSprayAt>ObservedSprayAcceptedAt&&LastSprayAt>LastWaterShotAt) {
        if(Time-SprayContactPresentationAt>.15)SprayContactPresentationStarted=LastSprayAt;
        ObservedSprayAcceptedAt=LastSprayAt;SprayContactPresentationAt=LastSprayAt;
        if(IsValid(HealingTarget)){SprayContactPresentationPoint=HealingTarget->GetActorLocation();bSprayContactPointKnown=true;}
        else if(IsValid(FireTarget)){SprayContactPresentationPoint=FireTarget->GetActorLocation();bSprayContactPointKnown=true;}
        else if(!Hero||!Hero->HasAuthority())bSprayContactPointKnown=false;
    }
    const bool Contact=!Event.IsNone()&&Time>=SprayContactPresentationAt&&Time-SprayContactPresentationAt<.15;
    if(Source) {
        auto V=MCInventoryPresentation::Inputs(Hero,Origin,Direction,Reach,Width,Water,SprayContactPresentationPoint,
            bSprayContactPointKnown,SprayContactPresentationAt,float(Time-SprayContactPresentationStarted));
        FString Error;
        if(Source->UpdateLocalSourceState(MCInventoryPresentation::ID,TEXT("Contact"),Contact,
            FTransform(FRotationMatrix::MakeFromZ(Direction).ToQuat(),V.TargetPosition),V,this,Error)&&Contact)
            SprayAcceptedContactAt=SprayContactPresentationAt;
    }
    if(!Source||Event.IsNone())return true;
    auto V=MCInventoryPresentation::Inputs(Hero,Origin,Direction,Reach,Width,Water,SprayContactPresentationPoint,
        bSprayContactPointKnown,SprayContactPresentationAt,float(Time-SprayPresentationStarted));
    FString Error;
    const bool Accepted=Source->UpdateLocalSourceState(MCInventoryPresentation::ID,Event,true,Frame,V,this,Error);
    SprayAcceptedPresentationEvent=Accepted?Event:NAME_None;
    if(!Accepted)return true; // unprepared/rejected profile retains the original
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(!Source->GetPreparedEventMode(MCInventoryPresentation::ID,Event,Mode)||Mode==EMCPerkVFXSlotMode::UseDefault)return true;
    const auto* Native=Source->GetPreparedNativeSource(MCInventoryPresentation::ID);
    return Native&&Native->ShouldRenderNativeAccents(Event,Mode);
}

void UMCInventoryComponent::StopSprayComposerPresentation()
{
    if(auto* Source=MCInventoryPresentation::Channel(Hero))Source->StopLocalSourceStates(MCInventoryPresentation::ID);
    SprayPresentationEvent=SprayAcceptedPresentationEvent=NAME_None;bSprayContactPointKnown=false;
}
