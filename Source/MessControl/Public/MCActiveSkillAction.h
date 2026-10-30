#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "MCActiveSkillAction.generated.h"

class AMCPlayerState;
class AMCToothCharacter;
class UMCPerkComponent;
class USceneComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UNiagaraSystem;
class UNiagaraComponent;

UENUM(BlueprintType)
enum class EMCActiveSkillEndReason : uint8
{
    Completed, InputCanceled, Replaced, Removed, OwnerDied, PawnChanged, RunReset, WorldEnded, TimedOut
};

/** Immutable authored values; arrays replicate while native TMaps do not. */
USTRUCT()
struct MESSCONTROL_API FMCActiveSkillParameter
{
    GENERATED_BODY()
    UPROPERTY() FName Key;
    UPROPERTY() float Value=0;
};

/** One physical press. Release never re-reads the item currently occupying this slot. */
USTRUCT()
struct MESSCONTROL_API FMCActiveSkillInputHandle
{
    GENERATED_BODY()
    UPROPERTY() int32 SlotIndex=INDEX_NONE;
    UPROPERTY() FGuid SlotInstanceID;
    UPROPERTY() uint32 PressSequence=0;
    bool IsValid() const { return SlotIndex>=0 && SlotIndex<4 && SlotInstanceID.IsValid() && PressSequence!=0; }
    bool operator==(const FMCActiveSkillInputHandle& Other) const
    { return SlotIndex==Other.SlotIndex && SlotInstanceID==Other.SlotInstanceID && PressSequence==Other.PressSequence; }
    bool operator!=(const FMCActiveSkillInputHandle& Other) const { return !(*this==Other); }
};

/** Prepared without gameplay side effects; committed actions outlive their depleted slot/effect. */
UCLASS(Abstract, Blueprintable)
class MESSCONTROL_API AMCActiveSkillAction : public AActor
{
    GENERATED_BODY()
public:
    AMCActiveSkillAction();
    virtual bool CanStart() const;
    /** Override must call Super first. Cannot reject after the cost has committed. */
    virtual void StartCommitted();
    virtual void ReleaseInput();
    /** True consumes a fresh repeat press before the new-use cooldown gate. */
    virtual bool RepeatInput();
    /** A launched hazard may retain its fuse on input loss/death. Reset/world teardown must finish. */
    virtual void Cancel(EMCActiveSkillEndReason Reason);
    void Finish(EMCActiveSkillEndReason Reason=EMCActiveSkillEndReason::Completed);
    UFUNCTION(BlueprintPure,Category="Skill") AMCToothCharacter* Hero() const;
    UFUNCTION(BlueprintPure,Category="Skill") AMCPlayerState* SourceState() const;
    UMCPerkComponent* Perks() const;
    UFUNCTION(BlueprintPure,Category="Skill|Tuning") float Param(FName Key,float Fallback) const;
    double Now() const;
    bool GetAim(FVector& Origin,FVector& Direction) const;
    UFUNCTION(BlueprintPure,Category="Skill") FGuid GetActionID() const { return ActionID; }
    UFUNCTION(BlueprintPure,Category="Skill") int32 GetSlotIndex() const { return InputHandle.SlotIndex; }
    UFUNCTION(BlueprintPure,Category="Skill") FGuid GetSlotInstanceID() const { return InputHandle.SlotInstanceID; }
    uint32 GetPressSequence() const { return InputHandle.PressSequence; }
    /** Blueprint/Python cannot expose uint32; this preserves its full positive range for observation. */
    UFUNCTION(BlueprintPure,Category="Skill|Review") int64 GetPressSequenceForReview() const { return int64(InputHandle.PressSequence); }
    UFUNCTION(BlueprintPure,Category="Skill") FName GetPerkID() const { return PerkID; }
    const FMCActiveSkillInputHandle& GetInputHandle() const { return InputHandle; }
    UFUNCTION(BlueprintPure,Category="Skill") bool IsCommitted() const { return bCommitted; }
    UFUNCTION(BlueprintPure,Category="Skill") bool IsFinished() const { return bFinished; }
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") float GetPresentationAge() const;
    /** -1 until the held action fires/releases. Independent of projectile lifetime. */
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") virtual float GetPresentationReleaseAge() const;
    virtual bool GetPresentationHandTarget(bool bLeft,FTransform& WorldPose) const;
    void ClaimPresentation();
    void ReleasePresentation();
    /** Negative Radius retains the artist's asset default. Values are applied before activation. */
    void PlaySkillCue(FName Stage,FVector Location,FVector Direction=FVector::UpVector,float Radius=-1.f);
    void StartLoop();
    void StopLoop();
    void UpdateLoopVFX(FVector Direction,float Fill=-1.f);
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill") TObjectPtr<UStaticMeshComponent> PropMesh;
    /** Reflected CDO roots are followed by the loading manifest through EffectClass -> ActionClass. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") TSoftObjectPtr<UStaticMesh> PropAsset;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") FTransform PropTransform=FTransform::Identity;
    /** Prop-local skeletal wrist frames, including rotation. No hand at an unspecified anchor. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") bool bHasLeftGrip=false;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") bool bHasRightGrip=false;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") FTransform LeftGripTransform=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation|Grip") FTransform RightGripTransform=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> StartVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> LoopVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> ImpactVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> EndVFX;
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> WarningVFX;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill|VFX") TObjectPtr<UNiagaraComponent> LoopComponent;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Exactly once, including external destruction/world teardown; release only this action's leases. */
    virtual void OnFinished(EMCActiveSkillEndReason Reason);
    void MarkPresentationReleased();

private:
    friend class UMCActiveSkillPerkEffect;
    void InitializeAction(UMCPerkComponent* OwnerPerks,AMCToothCharacter* Pawn,FName ID,
        const FMCActiveSkillInputHandle& Handle,const TMap<FName,float>& Values);
    UFUNCTION() void OnRep_Committed();
    UFUNCTION() void OnRep_Presentation();
    UFUNCTION() void OnRep_Loop();
    UFUNCTION(NetMulticast,Reliable) void MulticastSkillCue(FName Stage,FVector_NetQuantize Location,FVector_NetQuantizeNormal Direction,float Radius);
    void ApplyPresentation();
    void ApplyPresentationLease();
    void ReleaseAppliedPresentationLease();
    void RefreshPresentationTickPrerequisite(USkeletalMeshComponent* Mesh);
    void ReleasePresentationTickPrerequisite();
    void ApplyVFXParameters(UNiagaraComponent* Component,FVector Direction,float Radius,float Fill=-1.f) const;
    UPROPERTY(Replicated) TObjectPtr<AMCPlayerState> SourcePlayerState;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) TObjectPtr<AMCToothCharacter> SourcePawn;
    UPROPERTY(Replicated) FName PerkID;
    UPROPERTY(Replicated) FGuid ActionID;
    UPROPERTY(Replicated) FMCActiveSkillInputHandle InputHandle;
    UPROPERTY() TMap<FName,float> Parameters;
    UPROPERTY(Replicated) TArray<FMCActiveSkillParameter> InitialParameters;
    UPROPERTY(ReplicatedUsing=OnRep_Committed) bool bCommitted=false;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bWantsPresentation=false;
    UPROPERTY(ReplicatedUsing=OnRep_Loop) bool bLoopRunning=false;
    TWeakObjectPtr<AMCToothCharacter> AppliedPresentationPawn;
    TWeakObjectPtr<USkeletalMeshComponent> AppliedPresentationTickMesh;
    bool bOwnsPresentationTickPrerequisite=false;
    bool bReportedPresentationTickCycle=false;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) double PresentationReleasedAt=-1;
    bool bInitialized=false;
    bool bFinished=false;
};
