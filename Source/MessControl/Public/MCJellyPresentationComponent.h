#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/SoftObjectPtr.h"
#include "MCJellyPresentationComponent.generated.h"

class UMCWorldPresentationComponent;
class UNiagaraSystem;
class USceneComponent;
struct FMCPerkVFXGameplayValues;

/** Cosmetic projection of the real jelly's replicated contact/rupture state.
 * Dirt coverage, movement, splits, damage and RNG remain on the native owner. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCJellyPresentationComponent final:public UActorComponent
{
    GENERATED_BODY()
public:
    UMCJellyPresentationComponent();
    static FName SourceID() {return TEXT("World.Cataclysm.Jelly");}
    // Runtime resources are retained by the canonical scoped World profile;
    // the component has no references to future/unassigned Niagara assets.
    void PreparePresentation();
    void UpdateNativeState(USceneComponent* BodyFrame,USceneComponent* ActualVisual,bool bContact,
        double ServerNow,double RupturedAt,float BodyRadius,float TrailRadius,float RuptureRadius,
        float SizeScale,int32 Tier);
    void Stop();
    bool KeepNativeContact() const {return bKeepContact;}
    bool KeepNativeDroplets() const {return bKeepDroplets;}
    uint64 GetAcceptedContactSerial() const {return AcceptedContactSerial;}
    uint64 GetAcceptedRuptureSerial() const {return AcceptedRuptureSerial;}
    UMCWorldPresentationComponent* GetPreparedPresentation() const {return Presentation.Get();}
    /** Public typed contract used by asset recipes and focused tests. */
    static void SetNativeGeometry(FMCPerkVFXGameplayValues& Values,float BodyRadius,float TrailRadius,
        float RuptureRadius,float SizeScale,int32 Tier);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    double ObservedRupturedAt=-100.;
    uint64 AcceptedContactSerial=0,AcceptedRuptureSerial=0;
    bool bContactWasActive=false,bKeepContact=true,bKeepDroplets=true;
};
