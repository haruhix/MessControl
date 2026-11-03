#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCWorldPresentationComponent.generated.h"

class UMCVFXPresentationComponent;
class UMCVFXPresentationLibrary;

/** Adapts existing replicated/local native owner state to the ONE prepared Composer player. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCWorldPresentationComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCWorldPresentationComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    /** Explicit source contracts declared by this native owner before BeginPlay. */
    UPROPERTY(EditDefaultsOnly,Category="Presentation") TArray<FName> Sources;
    /** Preparation is allowed to load; accepting/advancing state is never allowed to load. */
    bool PrepareSources(FString& Error,bool bAllowLoad=false);
    bool UpdateState(FName SourceID,FName Event,bool bActive,const FTransform& Frame,
        const FMCPerkVFXGameplayValues& Values,UActorComponent* TickPrerequisite,FString& Error);
    /** One existing native accepted pulse; local lifecycle only, never another RPC or gameplay action. */
    bool Pulse(FName SourceID,FName Event,const FTransform& Frame,const FMCPerkVFXGameplayValues& Values,
        float NativeLifeSeconds,FString& Error);
    void StopStates(FName SourceID=NAME_None);
    bool UsesNativeVisuals(FName SourceID,FName Event) const;
    bool UsesOriginalAudio(FName SourceID,FName Event) const;
    UMCVFXPresentationComponent* GetSource(FName SourceID) const;
    void GetPresentationComponents(TArray<UMCVFXPresentationComponent*>& Out) const;
    const FString& GetLastError() const {return LastError;}
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UMCVFXPresentationComponent>> Channels;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCVFXPresentationLibrary>> RetainedLibraries;
    FString LastError;
    TSet<FString> FailedStateEvents;
    struct FNativePulse
    {
        FName SourceID,Event;
        FTransform Frame;
        FMCPerkVFXGameplayValues Values;
        double Started=0;
        float Life=0;
    };
    TMap<FString,FNativePulse> Pulses;
};

namespace MCWorldPresentation
{
    /** Lifecycle/preparation only. Never call from an accepted event or per-frame update. */
    MESSCONTROL_API UMCWorldPresentationComponent* PrepareOwner(AActor* Owner,const TArray<FName>& Sources);
    MESSCONTROL_API bool Pulse(AActor* Owner,FName SourceID,FName Event,const FTransform& Frame,
        const FMCPerkVFXGameplayValues& Values,float NativeLifeSeconds);
    /** Null/unprepared/unmapped sources keep the exact original producer behavior. */
    MESSCONTROL_API bool UsesOriginalAudio(const AActor* Owner,FName SourceID,FName Event);
}
