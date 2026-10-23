#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubEconomyTypes.h"
#include "MCHubGrillStation.generated.h"

class APawn;
class APlayerController;
class AMCPlayerState;
class AMCToothCharacter;
class UMCHubEconomyComponent;
class UBoxComponent;
class UWidgetComponent;
class UMCHubGrillPresentationComponent;

UENUM(BlueprintType)
enum class EMCHubGrillStage : uint8 { Idle, Closing, Rolling, Revealing, Opening };

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCHubGrillChanged);

/** Interaction and animation driver for the existing five-piece grill/slot-machine assembly. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCHubGrillStation : public AActor
{
    GENERATED_BODY()
public:
    AMCHubGrillStation();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Grill") TObjectPtr<UBoxComponent> InteractionVolume;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Grill") TObjectPtr<UWidgetComponent> InteractionPrompt;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Grill") TObjectPtr<UMCHubGrillPresentationComponent> Presentation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill") bool bEnabled=true;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Grill|Assembly") TObjectPtr<AActor> LidActor;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Grill|Assembly") TArray<TObjectPtr<AActor>> ReelActors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") FVector LidHingeLocal=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") FVector LidHingeAxisLocal=FVector(0,1,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") float LidOpenDegrees=38.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") FVector ReelSpinAxisLocal=FVector(1,0,0);
    /** Measured stop angles from the actual mesh/UV symbol order; never guessed by gameplay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") TMap<FName,float> SymbolStopAngles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grill|Assembly") TArray<float> ReelAngleOffsets;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") EMCHubGrillStage Stage=EMCHubGrillStage::Idle;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") double StageStartedAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") double StageEndsAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") TObjectPtr<AMCPlayerState> ActivePlayer;
    /** One tuple drives both physical reels and any later presentation copy. */
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") FMCGrillReward CurrentReward;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Grill") TArray<float> RollStartAngles;
    UPROPERTY(BlueprintAssignable, Category="Grill") FMCHubGrillChanged OnGrillStateChanged;
    UFUNCTION(BlueprintPure, Category="Grill") bool ContainsPawn(const APawn* Pawn) const;
    UFUNCTION(BlueprintPure, Category="Grill") bool IsBusy() const { return Stage!=EMCHubGrillStage::Idle || bAcceptingRequest; }
    UFUNCTION(BlueprintPure, Category="Grill") TArray<float> GetReelDisplayAngles() const;
    UFUNCTION(BlueprintCallable, Category="Grill|Prompt") void SetLocalPromptVisible(APlayerController* Player,bool bVisible);
    /** Authority-only. Controller supplies its actual possessed hero, never a client reward/price. */
    bool TryStartRoll(AMCToothCharacter* Hero,FText& Error);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void SetStage(EMCHubGrillStage NewStage,float Duration);
    void AdvanceAuthority();
    void UpdateAssembly();
    double ServerNow() const;
    bool IsAssemblyConfigured(const UMCHubEconomyComponent* Economy,FText& Error) const;
    UFUNCTION() void OnRep_State();
    TWeakObjectPtr<UMCHubEconomyComponent> ActiveEconomy;
    FTransform ClosedLidTransform=FTransform::Identity;
    TArray<FQuat> InitialReelRotations;
    FMCHubEconomyRulesRow ActiveRules;
    bool bAcceptingRequest=false;
    bool bAssemblyCaptured=false;
};
