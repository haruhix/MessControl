#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Templates/PimplPtr.h"
#include "MCNiagaraMaterialContextComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UMCNiagaraMaterialContextComponent;
struct FMCNiagaraMaterialContextState;

UENUM()
enum class EMCNiagaraMaterialContextOperation : uint8
{
    ExistingMeshOverlay,
    ExistingMIDScalar
};

/** Transient companion owned by the existing location/preview readiness service, not a subsystem.
 * Prepare and seal before the location barrier. Playback reserves existing resources only. */
UCLASS(Transient, NotBlueprintable)
class MESSCONTROL_API UMCNiagaraMaterialContextResources final : public UObject
{
    GENERATED_BODY()
public:
    UMCNiagaraMaterialContextResources();
    virtual ~UMCNiagaraMaterialContextResources() override;
    virtual UWorld* GetWorld() const override;

    static bool NeedsMaterialContext(const UNiagaraSystem* System);
    bool IsPreparedForEpoch(FGuid Epoch) const;
    /** Works for explicit private-world preparation scopes without a replicated GameState. */
    bool IsReadyForPlayback(const UWorld* World) const;
    bool IsSealed() const { return bSealed; }
    bool HasActiveActivations() const;
    FGuid GetPreparedEpoch() const { return PreparedEpoch; }
    /** One representative precreated MID per actual overlay template; numeric values do not change PSOs. */
    void GetPreparedOverlayMaterials(TArray<UMaterialInterface*>& Out) const;
    /** Certifies an actual supported factory signature after the preparation owner queued its PSOs.
     * Later instances may reuse exactly the same geometry/material/factory signature. */
    bool RegisterPreparedTarget(UMeshComponent* Target, FString& Error);
    bool IsTargetPrepared(UMeshComponent* Target) const;
    static bool ValidateTargetFactory(UMeshComponent* Target, FString& Error);
    /** Resolves only explicitly registered (subject,parameter) bindings. No actor lookup.
     * Before Activate and on each native input update; zero scalar renderers is a no-op. */
    bool BindPreparedScalarTarget(UNiagaraComponent* Niagara, FString& Error) const;

    bool Initialize(UWorld* World, FGuid Epoch, FString& Error);
    /** Inspects actual CPU ComponentRenderer templates; creates overlay MIDs here only.
     * A changed sealed template requires explicit Reset/reprepare. No asset loads. */
    bool PrepareSystem(UNiagaraSystem* System, int32 ConcurrentInstances, FString& Error);
    /** Explicit native-owned target; the requested scalar must already be an override on this MID.
     * This never creates/replaces a MID or promotes an inherited parameter at an interaction. */
    bool RegisterExistingScalarTarget(UMeshComponent* Subject, UMaterialInstanceDynamic* MID,
        FName AllowedParameter, FString& Error);
    /** Explicit late native identity handoff. Requires the sealed current epoch,
     * exact prepared factory/static-parent/scalar role and an already assigned
     * native MID with its own finite override. No load/MID/PSO/renderer creation.
     * This never prepares captured Freeze geometry or an unknown appearance. */
    bool AdoptPreparedScalarTarget(UMeshComponent* Subject,UMaterialInstanceDynamic* ExistingMID,
        FName AllowedParameter,FString& Error);
    bool SealResources(FString& Error);
    /** Call after native object inputs are filled, before Activate and before returning Played.
     * Reserves every context renderer atomically; writes only the read-only resource input. */
    bool ReserveActivation(UNiagaraComponent* Niagara, FString& Error);
    /** Immediate stop/reset/EndPlay must call this before Niagara destruction. Tails retain it
     * until normal completion. Component Deactivate/OnUnregister additionally release their lease. */
    void ReleaseActivation(UNiagaraComponent* Niagara);
    void Reset();
    void GetPreparedResources(TArray<UObject*>& Out) const;

private:
    friend class UMCNiagaraMaterialContextComponent;
    bool Apply(UMCNiagaraMaterialContextComponent* Client, uint64& Lease, FString& Error);
    void Release(uint64 Lease);
    void Recompute(int32 GroupIndex);
    bool IsCurrentScope() const;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Retained;
    TWeakObjectPtr<UWorld> PreparedWorld;
    FGuid PreparedEpoch;
    bool bSealed = false;
    // Never reuse handles across Reset: a late component teardown cannot release a new lease.
    uint64 NextLeaseID = 1;
    TPimplPtr<FMCNiagaraMaterialContextState> State;
};

/** Generic CPU Niagara ComponentRenderer bridge. No gameplay, actor lookup, asset loads,
 * MID creation, timers, art curves or source-specific colors. The NS owns all numeric artwork.
 * This is a component on the Niagara owner actor, attached directly to Niagara by the engine. */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent), NotBlueprintable)
class MESSCONTROL_API UMCNiagaraMaterialContextComponent final : public USceneComponent
{
    GENERATED_BODY()
public:
    UMCNiagaraMaterialContextComponent();

    /** Stable unique name within one NS, authored on its renderer template. */
    UPROPERTY(EditAnywhere, Category="Context") FName ContextID;
    UPROPERTY(EditAnywhere, Category="Context") EMCNiagaraMaterialContextOperation Operation = EMCNiagaraMaterialContextOperation::ExistingMeshOverlay;
    /** Arbitration is explicit: higher priority wins; equal priority uses latest acquisition.
     * A mesh has one global overlay; this does not promise multiple simultaneous overlay passes. */
    UPROPERTY(EditAnywhere, Category="Context") int32 Priority = 0;
    UPROPERTY(EditAnywhere, Category="Overlay") TObjectPtr<UMaterialInterface> OverlayMaterial;
    UPROPERTY(EditAnywhere, Category="Overlay") FName TintParameter = TEXT("Color");
    UPROPERTY(EditAnywhere, Category="Overlay") FName OpacityParameter = TEXT("Opacity");
    UPROPERTY(EditAnywhere, Category="Scalar") FName ScalarParameter;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetTint, Category="Artwork") FLinearColor Tint = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetOpacity, Category="Artwork", meta=(ClampMin="0", ClampMax="1")) float Opacity = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetScalarValue, Category="Artwork") float ScalarValue = 0.f;
    /** PauseResume scalability must not leave an external mesh overlay visible offscreen.
     * False suspends this lease while Niagara is paused; NS values stay frozen in the simulation. */
    UPROPERTY(EditAnywhere, Category="Context") bool bApplyWhilePaused = false;

    UFUNCTION(BlueprintSetter) void SetTint(FLinearColor Value);
    UFUNCTION(BlueprintSetter) void SetOpacity(float Value);
    UFUNCTION(BlueprintSetter) void SetScalarValue(float Value);
    virtual void Activate(bool bReset=false) override;
    virtual void Deactivate() override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    virtual void OnUnregister() override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnAttachmentChanged() override;
    virtual void OnVisibilityChanged() override;
    const FString& GetLastContextError() const { return LastError; }

private:
    void ApplyContext();
    void ReleaseContext();
    uint64 LeaseID = 0;
    TWeakObjectPtr<UMCNiagaraMaterialContextResources> LeaseResources;
    FString LastError;
};
