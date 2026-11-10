#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "Engine/NetSerialization.h"
#include "SharedMCNetworkFrames.h"
#include "MCFoodActor.generated.h"
class UBoxComponent;
class UStaticMeshComponent;
class UMCFoodBodyComponent;
class UTextRenderComponent;
class UMCWorldStatusComponent;
class USceneComponent;
class AMCToothCharacter;
struct FMCPreparedFoodCutState;
struct FMCFoodCutTransaction;
DECLARE_MULTICAST_DELEGATE_OneParam(FMCFoodEntryLanding,const FHitResult&);

USTRUCT(BlueprintType)
struct FMCFoodSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drop") float DropHeight=650;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drop") float Mass=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float ImpactSpeed=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float DamagePerSpeed=0.035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float MaxDamage=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float Knockback=560;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Impact") float HitCooldown=0.75f;
    /** Whole Nutageddon nuts only. Energy uses kg and m/s; ordinary food keeps its impact rules. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="1",DisplayName="Nut Tooth Min Energy (J)")) float NutToothMinEnergy=70;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="0")) float NutToothDamagePerJoule=.08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="0")) float NutToothMaxDamage=35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="0",ClampMax="1")) float NutRestitution=.28f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="0",ClampMax="5")) float NutLinearDamping=.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nut Physics",meta=(ClampMin="0",ClampMax="10")) float NutAngularDamping=.45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float GrabReach=145;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grab") float BreakDistance=340;
    /** Full dimensions in the flat hand pose, in cm; independent of fragment identity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portions",meta=(ClampMin="1")) FVector MaxHandDimensions=FVector(110);
    /** Product of these cm values is the automatic intake volume budget; no per-axis acceptance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portions",meta=(ClampMin="1",Units="cm")) FVector MaxAutoConsumeDimensions=FVector(110);
    /** Disable to restore the retained hand-stack and manual throat-delivery pattern. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portions") bool bAutoConsumeSmallCuts=true;
    /** Both cut results must retain this much physical convex-hull volume (cm cubed). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portions",meta=(ClampMin="1")) float MinCutVolume=1000;
    /** Minimum depth of either result along the new cut normal, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Portions",meta=(ClampMin="1")) float MinCutThickness=12;
    // Serialized legacy fields; grip tuning lives in DA_Grip.
    UPROPERTY() float Spring=14;
    UPROPERTY() float Damping=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float PullSeconds=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float PullConeDegrees=35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pull") float CooperationMultiplier=1.5f;
    void Sanitize();
};
UCLASS(BlueprintType)
class MESSCONTROL_API UMCFoodProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Food") FMCFoodSettings Settings;
};
UENUM(BlueprintType)
enum class EMCFoodPhase : uint8 { Falling, Stuck, Free, Disposed, Equipped, Carried, Swallowing, Absorbing, Contained };

/** Server physics pose relative to its carrier, rebased onto the predicted/smoothed character. */
USTRUCT()
struct FMCCarryPresentation
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<AMCToothCharacter> Holder;
    UPROPERTY() FVector_NetQuantize10 Location=FVector::ZeroVector;
    UPROPERTY() FRotator Rotation=FRotator::ZeroRotator;
};

/** One replicated pickup cue; every peer evaluates the same timed hop locally. */
USTRUCT()
struct FMCStackPickup
{
    GENERATED_BODY()
    static constexpr float PlayRate=2.f;
    UPROPERTY() FVector_NetQuantize10 StartLocation=FVector::ZeroVector;
    UPROPERTY() FRotator StartRotation=FRotator::ZeroRotator;
    UPROPERTY() double StartedAt=-100;
    UPROPERTY() float FlightSeconds=.32f;
    UPROPERTY() float SlotHeight=0;
    UPROPERTY() FVector_NetQuantize10 SlotOffset=FVector::ZeroVector;
    UPROPERTY() FRotator SlotRotation=FRotator::ZeroRotator;
    UPROPERTY() float ArcHeight=50;
};

/** One accepted food ownership generation; all client render modes read this snapshot. */
USTRUCT()
struct FMCFoodInteractionState
{
    GENERATED_BODY()
    UPROPERTY() FGuid LoadEpoch;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() EMCFoodPhase Phase=EMCFoodPhase::Falling;
    UPROPERTY() TObjectPtr<AMCToothCharacter> StackCarrier;
    UPROPERTY() TObjectPtr<AActor> Container;
    UPROPERTY() TArray<TObjectPtr<AMCToothCharacter>> Participants;
    UPROPERTY() FMCCarryPresentation Carry;
    UPROPERTY() FMCStackPickup Pickup;
    // A null unresolved NetGUID must not be mistaken for an accepted release.
    UPROPERTY() bool bRequiresStackCarrier=false;
    UPROPERTY() bool bRequiresCarrier=false;
    UPROPERTY() bool bRequiresContainer=false;
    UPROPERTY() double AcceptedAt=0;
    bool CanAdvanceFrom(const FMCFoodInteractionState& Previous,const FGuid& ExpectedEpoch) const
    {
        if(!LoadEpoch.IsValid() || !Revision || (ExpectedEpoch.IsValid() && LoadEpoch!=ExpectedEpoch)
            || uint8(Phase)>uint8(EMCFoodPhase::Contained)) return false;
        if((bRequiresStackCarrier && !StackCarrier.Get()) || ((bRequiresCarrier || Phase==EMCFoodPhase::Carried) && !Carry.Holder.Get())
            || ((bRequiresContainer || Phase==EMCFoodPhase::Contained) && !IsValid(Container))) return false;
        for(const auto& Participant:Participants) if(!Participant.Get()) return false;
        if(!Previous.Revision) return true;
        // A published world epoch is the trust boundary; its revisions restart.
        if(LoadEpoch!=Previous.LoadEpoch) return ExpectedEpoch.IsValid();
        if(int32(Revision-Previous.Revision)<0) return false;
        // Same-generation pose updates cannot change interaction ownership.
        return Revision!=Previous.Revision || (Phase==Previous.Phase && StackCarrier==Previous.StackCarrier
            && Container==Previous.Container && Participants==Previous.Participants && Carry.Holder==Previous.Carry.Holder
            && bRequiresStackCarrier==Previous.bRequiresStackCarrier && bRequiresCarrier==Previous.bRequiresCarrier && bRequiresContainer==Previous.bRequiresContainer
            && Pickup.StartedAt==Previous.Pickup.StartedAt);
    }
};

/** Local-space description of one planar cut half. */
USTRUCT()
struct FMCFoodCutPlane
{
    GENERATED_BODY()
    UPROPERTY() bool bEnabled=false;
    UPROPERTY() FVector Position=FVector::ZeroVector;
    UPROPERTY() FVector Normal=FVector::ZeroVector;
    /** Deterministic connected part after this step. A disabled plane selects an existing part only. */
    UPROPERTY() int32 IslandIndex=INDEX_NONE;
};

/** Server-simulated rigid food. Clients receive motion and interaction state. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCFoodActor : public AActor
{
    GENERATED_BODY()
public:
    AMCFoodActor();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRep_ReplicatedMovement() override;
    virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Initialize(bool bJam,FVector ExtractionDirection);
    /** Launch a newly spawned piece through the mouth; call after FinishSpawning. */
    void BeginMouthEntry(FVector LaunchVelocity,float PushSpeed,TOptional<FVector> ExpectedLanding=TOptional<FVector>());
    bool IsMouthEntryActive() const { return bMouthEntry; }
    /** Call after ConfigureItem/FinishSpawning, before the rain launch. */
    void EnableNutKinetics();
    bool IsNutKineticsActive() const;
    /** Server-only external impact; converts velocity change to an impulse using the real body mass. */
    bool ApplyNutImpulse(FVector VelocityChange,FVector WorldPoint);
    /** Break an eligible whole encounter nut into the authored physical food fragments. */
    bool FractureKnightNut(FVector Direction,float Speed,bool bPlayFoodBreak=true);
    UPROPERTY(ReplicatedUsing=OnRep_NutKinetics,BlueprintReadOnly,Category="Nut Physics") bool bNutKineticsEnabled=false;
    UPROPERTY(BlueprintReadOnly,Category="Nut Physics") int32 NutToothImpacts=0;
    UPROPERTY(BlueprintReadOnly,Category="Nut Physics") float LastNutImpactEnergy=0;
    /** Optional server hazard owns the entry impact instead of physical player contact. */
    void ManageEntryImpact() { if(HasAuthority()) bEntryImpactManaged=true; }
    FMCFoodEntryLanding OnEntryLanding;
    /** Entry flight is automatic transport, so work is measured at its planned landing. */
    FVector WorkPosition() const { return bMouthEntry && MouthEntryLanding.IsSet()?MouthEntryLanding.GetValue():GetActorLocation(); }
    // Cut fragments use the retained hand stack. Tools and hazards retain the old grip.
    bool UsesLegacyGrip() const { return bBrushTool || FoodData.Kind!=EMCFoodKind::Food; }
    /** Broken boss-encounter nuts use one hand slot as ammunition, never a food tower. */
    bool IsNutEncounterFragment() const;
    // Whole nuts and their pieces never rot, including the boss encounter's ammunition.
    bool HasFreshnessTimer() const;
    UFUNCTION(BlueprintPure,Category="Food|Portions") bool FitsHandPortion() const;
    UFUNCTION(BlueprintPure,Category="Food|Portions") FVector GetHandPortionSize() const;
    UFUNCTION(BlueprintPure,Category="Food|Portions") bool FitsAutomaticIntakePortion() const;
    /** Fresh/rotten ordinary food uses automatic routing; hazards and boss ammunition keep their own rules. */
    UFUNCTION(BlueprintPure,Category="Food|Portions") bool UsesAutomaticCutIntake() const;
    /** Server-only, accepted once after a knife cut. The VFX never owns delivery/accounting. */
    bool TryConsumeCutPortion(FVector Direction);
    bool WasAutomaticallyConsumed() const { return bAutomaticallyConsumed; }
    bool WasAutomaticallyDiscarded() const { return bAutomaticallyDiscarded; }
    // Config/DefaultEngine.ini: distinguish fragments from player ragdoll PhysicsBody shapes.
    static constexpr ECollisionChannel FragmentCollisionChannel=ECC_GameTraceChannel2;
    /** Query-only frozen walking proxy; tools and physical bodies ignore this channel. */
    static constexpr ECollisionChannel WalkingObstacleChannel=ECC_GameTraceChannel3;
    FBox GetWalkingCollisionBounds() const;
    bool IsSubstantialWalkingObstacle() const;
    bool HasWalkingObstacle() const;
    void RefreshWalkingCollision();
    void UpdateWalkingCollision(float Dt);
    void ResetWalkingObstacle();
    static AMCFoodActor* FindNearbySpoiledFood(UWorld* World,FVector Point,float Radius=280);
    bool TryGrab(AMCToothCharacter* Hero);
    bool FindGripSurface(FVector From,FHitResult& Hit) const;
    // Query the visible food surface independently of its simplified physics hulls.
    bool FindToolContact(const AMCToothCharacter* Hero,float Reach,FHitResult& Hit) const;
    bool BeginCarry(AMCToothCharacter* Hero);
    void UpdateCarryPresentation(float Dt);
    FMCActorSimulationFrame GetSimulationFrame() const;
    FMCActorPresentationFrame GetPresentationFrame() const;
    FTransform GetPresentedActorTransform() const;
    FTransform GetCanonicalModelTransform() const;
    FBox GetCanonicalModelBounds() const;
    bool FindPresentedGripSurface(FVector From,FHitResult& Hit) const;
    bool ResolveCanonicalPawnContact(AMCToothCharacter* Hero,FVector FoodContactNormal,FVector ContactPoint);
    void RefreshAcceptedInteraction();
    bool MatchesInteractionGeneration(FGuid Epoch,uint32 Revision,const AMCToothCharacter* Participant) const;
    void Release(AMCToothCharacter* Hero);
    void Dispose();
    void AwardDelivery();
    class AMCPlayerState* GetLastHandledBy() const { return LastHandledBy.Get(); }
    /** Tracks handling intent; physical throat entry also accepts loose bounced food. */
    bool WasManuallyThrown() const { return bManuallyThrown; }
    void MarkManualThrow(AMCToothCharacter* Hero);
    /** Shared physical food launch for hand-stack and legacy grip throws. */
    void LaunchManualThrow(AMCToothCharacter* Hero,FVector Direction,float Speed);
    /** Authority-only exclusive bag claim. Existing food identity/freshness are retained. */
    bool TryEnterActiveContainer(AActor* Container,AMCToothCharacter* Hero,FGuid Claim);
    /** Only the original container+claim may release. Invalid/duplicate releases are inert. */
    bool ExitActiveContainer(AActor* Container,FGuid Claim,FVector Location,FVector Direction,float Speed,bool bThrown);
    bool IsInActiveContainer() const { return Phase==EMCFoodPhase::Contained; }
    AMCToothCharacter* GetActiveContainerCarrier() const;
    /** External blast preserves source PlayerState attribution without primary-tool procs. */
    bool ReceiveActiveSkillImpact(float Damage,FVector Direction,class AMCPlayerState* SourcePS);
    FVector IntakeSweepStart() const { return PreviousPhysicsLocation; }
    bool BeginSwallow();
    void CancelSwallow();
    void ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment=false);
    void ConfigureBrush();
    bool HitFood(float Damage,FVector Direction,AMCToothCharacter* Worker=nullptr, FVector CutPoint=FVector::ZeroVector, FVector CutNormal=FVector::ZeroVector);
    /** Accepted large cuts reserve their source until all hidden children are prepared. */
    bool IsCutTransactionPending() const {return bCutTransactionPending;}
    bool IsCutTransactionStaged() const {return bCutTransactionStaged;}
    bool IsHardFood() const;
    void AttendFood();
    /** A real river sweep preserves the new position even after the water leaves. */
    void MarkRiverSwept(float EscapeZ);
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Food|Flood") bool bRiverSwept=false;
    void SetStackCarrier(AMCToothCharacter* Hero);
    /** A dropped stack settles without attacking players; deliberate throws remain dangerous. */
    void ProtectPlayersOnStackRelease(bool bThrown,float GraceSeconds=.75f);
    void BeginStackPickup(AMCToothCharacter* Hero,float SlotHeight);
    float PrepareHorizontalStackPose(const FMCFoodStackSettings* StackSettings,int32 Slot);
    FQuat StackRestRotation(const FQuat& BaseRotation) const;
    float StackHalfHeight() const;
    void StackVerticalBounds(float& MinHeight,float& MaxHeight) const;
    float StackGapBefore(const AMCFoodActor* Above) const;
    bool IsStackPickupActive() const;
    float StackPickupDuration() const { return (.075f+StackPickup.FlightSeconds+.18f)/FMCStackPickup::PlayRate; }
    FTransform StackPickupPose(const FTransform& Goal) const;
    UPROPERTY(Replicated) FMCStackPickup StackPickup;
    UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Collection") TObjectPtr<AMCToothCharacter> StackCarrier;
    UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Collection") TObjectPtr<AActor> ActiveContainer;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Collection") TObjectPtr<AMCToothCharacter> ActiveContainerCarrier;
    void ReactToImpact(float Strength=1);
    void UpdateReaction(float Dt);
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Reaction") double ImpactAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Reaction") float ImpactStrength=0;
    void UpdateAbsorption(float Dt);
    float AbsorptionProgress() const;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") double AbsorbStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") bool bAbsorbed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Ulcer") TObjectPtr<class AMCMouthSurface> AbsorbedUlcer;
    void Throw(AMCToothCharacter* Hero,FVector AimDirection=FVector::ZeroVector);
    float DragSpeed() const;
    bool IsDisposed() const { return Phase==EMCFoodPhase::Disposed; }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UMCFoodBodyComponent> WalkingBlocker;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> PresentationRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GripSurface;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UProceduralMeshComponent> CutVisual;
    UPROPERTY(ReplicatedUsing=OnRep_Item) TArray<FMCFoodCutPlane> CutPlanes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="UI") TObjectPtr<UMCWorldStatusComponent> WorldStatus;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Food") TSoftObjectPtr<UMCFoodProfile> Profile;
    UPROPERTY(Replicated, BlueprintReadOnly) FMCFoodSettings Settings;
    UPROPERTY(ReplicatedUsing=OnRep_Phase, BlueprintReadOnly) EMCFoodPhase Phase=EMCFoodPhase::Falling;
    UPROPERTY(Replicated, BlueprintReadOnly) float PullProgress=0;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector PullDirection=FVector(0,1,0);
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<TObjectPtr<AMCToothCharacter>> Holders;
    UPROPERTY(ReplicatedUsing=OnRep_Item, BlueprintReadOnly) TObjectPtr<UStaticMesh> ItemMesh;
    UPROPERTY(ReplicatedUsing=OnRep_Item, BlueprintReadOnly) FMCFoodRow FoodData;
    UPROPERTY(Replicated, BlueprintReadOnly) FName ItemName;
    UPROPERTY(Replicated, BlueprintReadOnly) float Health=75;
    UPROPERTY(ReplicatedUsing=OnRep_Item, BlueprintReadOnly) bool bFragment=false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bBrushTool=false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bSpoiled=false;
    UPROPERTY(Replicated, BlueprintReadOnly) double SpoilAt=0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Batch=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") double FuseEndsAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") float PausedFuse=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") bool bFusePaused=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") int32 HazardRound=1;
    bool IsWrongIngredient() const;
    float FuseRemaining() const;
    float DetonationRadius() const;
    void ArmSpicy();
    void PauseFuse(class AMCThroat* Throat);
    void ResumeFuse(class AMCThroat* Throat);
    void Detonate();
    bool IsHazardResolved() const;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") TObjectPtr<class AMCMouthSurface> BurnLesion;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Hazard") TArray<TObjectPtr<class AMCFirePatch>> FireTrail;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCToothCharacter> EquippedBy;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AActor> StuckTooth;
    int32 ConfirmedImpacts=0;
private:
    /** Authority publishes a fixed query pose, never a moving Chaos support. */
    UPROPERTY(Transient,ReplicatedUsing=OnRep_WalkingCollision) bool bWalkingObstacle=false;
    UPROPERTY(Transient,ReplicatedUsing=OnRep_WalkingCollision) FTransform WalkingObstacleTransform=FTransform::Identity;
    UFUNCTION() void OnRep_WalkingCollision();
    bool CanUseWalkingCollision() const;
    bool IsWalkingBoundsClearOfPlayers(const FBox& Bounds) const;
    bool SeparateWalkingFoodFromPlayers(const FBox& Bounds);
    void NudgeSmallWalkingDebris(float Dt);
    float WalkingSettleSeconds=0.f,WalkingNudgeScanSeconds=0.f,WalkingClearanceScanSeconds=0.f;
    double NextWalkingNudgeAt=-100;
    bool bWalkingSettleCandidate=false;
    FTransform WalkingSettleTransform=FTransform::Identity;
    bool bWalkingSeparationActive=false;
    FVector WalkingSeparationGoal=FVector::ZeroVector;
    friend class FMCFoodIslandActorAccounting;
    friend class FMCFoodAutomaticSliverVolume;
    friend class FMCFoodAutomaticSpoiledCutTransaction;
    friend class FMCFoodPreparedCutReuse;
    friend class UMCFoodCutStagingSubsystem;
    friend class UMCFoodProcessingRecoverySubsystem;
    friend class FMCFoodProcessingRecoveryTest;
    UPROPERTY(ReplicatedUsing=OnRep_InteractionState) FMCFoodInteractionState InteractionState;
    UFUNCTION() void OnRep_InteractionState();
    UPROPERTY(Transient) FMCFoodInteractionState PresentedInteraction;
    FGuid FallbackFoodEpoch;
    FTransform RenderedFoodPose=FTransform::Identity;
    FTransform CanonicalModelLocal=FTransform::Identity;
    bool bHasRenderedFoodPose=false;
    double LastFoodPresentationTick=-DBL_MAX;
    uint32 FoodPresentationSerial=0;
    void UpdateFoodPresentation(float Dt);
    void ApplyCanonicalProxyPose(FVector Location,FQuat Rotation);
    UPROPERTY(ReplicatedUsing=OnRep_ActorScale) FVector ReplicatedActorScale=FVector::OneVector;
    UFUNCTION() void OnRep_ActorScale();
    UPROPERTY(Replicated) FMCCarryPresentation CarryPresentation;
    TWeakObjectPtr<AMCToothCharacter> PresentationCarrier;
    FTransform SmoothedCarryRelative=FTransform::Identity;
    bool bReceivedMotion=false;
    FVector NetworkLocation=FVector::ZeroVector;
    FQuat NetworkRotation=FQuat::Identity;
    UFUNCTION() void OnRep_Phase();
    UFUNCTION() void OnRep_Item();
    UFUNCTION() void OnRep_NutKinetics();
    UFUNCTION() void OnHit(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    void HandleNutToothImpact(class AMCArenaTooth* Tooth,FVector Impulse,const FHitResult& Hit);
    UPROPERTY(Transient) TObjectPtr<class UPhysicalMaterial> NutPhysicsMaterial;
    bool bJamOnLanding=false;
    float RiverEscapeZ=-250;
    mutable TWeakObjectPtr<class AMCTongue> EscapeTongue;
    float OutOfArenaZ() const;
    bool bLandingPending=false;
    float LastPullTime=0;
    float CarryBlockedSeconds=0;
    TWeakObjectPtr<AMCToothCharacter> CollisionIgnoredCarrier;
    // Sampled in the actor's PrePhysics tick, before contact impulses change velocity.
    FVector PrePhysicsVelocity=FVector::ZeroVector;
    FVector PreviousPhysicsLocation=FVector::ZeroVector;
    bool bManualThrowInFlight=false;
    TWeakObjectPtr<AMCToothCharacter> ManualThrower;
    FVector NutThrowSweepStart=FVector::ZeroVector;
    /** Query-only nut enemies need a sweep along the real server physics travel. */
    void UpdateNutFragmentThrowImpact();
    void EndManualThrowFlight();
    bool bMouthEntry=false;
    bool bEntryImpactManaged=false;
    double EntryImpactSafeUntil=0;
    TOptional<FVector> MouthEntryLanding;
    float MouthEntryPushSpeed=0;
    double MouthEntryEndsAt=0;
    void EndMouthEntry();
    double StackReleaseSafeUntil=-100;
    TMap<TWeakObjectPtr<AActor>,double> LastHit;
    UPROPERTY(Replicated) TObjectPtr<class AMCTongue> AbsorptionTongue;
    UPROPERTY(Replicated) FVector AbsorptionAnchor=FVector::ZeroVector;
    bool FindAbsorptionFloor(FHitResult& Hit,class AMCTongue*& Tongue) const;
    void FinishAbsorption();
    void Spoil();
    void UpdateSmell();
    bool SpawnFragments(FVector Direction,TArray<AMCFoodActor*>* OutPieces=nullptr);
    bool SupportsPlanarCut() const;
    /** A refusal preserves the parent; callers must not fall back to legacy fragments. */
    bool SpawnCutFragments(FVector Direction,FVector WorldPoint,FVector WorldNormal,TArray<AMCFoodActor*>* OutPieces=nullptr);
    /** 0 = already connected, 1 = all independent portions staged, -1 = atomic refusal. */
    int32 SpawnDisconnectedPortions(FVector Direction,TArray<AMCFoodActor*>& OutPieces);
    bool SpawnPortionActors(const TArray<TArray<FMCFoodCutPlane>>& Histories,const TArray<double>& Weights,
        const TArray<FBox>& LocalBounds,const TArray<double>& LocalVolumes,FVector Direction,TArray<AMCFoodActor*>& OutPieces,
        const TArray<TSharedPtr<const FMCPreparedFoodCutState>>& Prepared={});
    bool BuildCutVisual();
    bool PrepareCutGeometry();
    void ApplyPreparedSourceMaterials();
    FTransform PortionSourceTransform() const;
    FVector PortionExtent() const;
    bool FitsAutomaticIntakeMetrics(double LocalVolume) const;
    double AutomaticIntakeVolume() const;
    bool ResolveAutomaticCutDestination(FVector Start,FVector& OutTarget,class AMCThroat*& OutThroat) const;
    /** Actual selected geometry in actor-local cubic cm; negative means not measured. */
    double MeasuredPortionLocalVolume=-1;
    FVector PendingCutPoint=FVector::ZeroVector,PendingCutNormal=FVector::ZeroVector;
    bool bCutVisualReady=false;
    TSharedPtr<const FMCPreparedFoodCutState> PreparedCutState;
    /** Parent tuning/geometry was staged before FinishSpawning; BeginPlay must not overwrite it. */
    bool bStagedCutPortion=false;
    /** Server-only receipt: accepted small geometry never needs a physical replicated actor. */
    bool bCutIntakeReceipt=false;
    FBox CutIntakeReceiptBounds=FBox(ForceInit);
    bool bProcessingAutomaticCutDamage=false;
    double CutContactStartedSeconds=0;
    UPROPERTY(ReplicatedUsing=OnRep_Phase) bool bCutTransactionPending=false;
    bool bCutTransactionStaged=false,bHasStagedCollisionResponses=false;
    FCollisionResponseContainer StagedCollisionResponses;
    TSharedPtr<FMCFoodCutTransaction> PendingCutTransaction;
    AMCFoodActor* SpawnPreparedPortion(const FMCFoodCutTransaction& Plan,int32 Index,bool bQueued);
    bool AdvancePendingCut();
    void CancelPendingCut(bool bRestoreSource=true);
    void SetPendingCutCompletion(AMCToothCharacter* Worker,bool bWeaponPassives,bool bPlayBreak,class AMCPlayerState* PreviousHandler);
    void CompleteCutPortions(const TArray<AMCFoodActor*>& Pieces,FVector Direction,AMCToothCharacter* Worker,bool bWeaponPassives,bool bPlayBreak);
    FBox CutStackLocalBounds=FBox(ForceInit);
    int32 CutAwardUnits=1;
    bool ApplyFoodDamage(float Damage,FVector Direction,AMCToothCharacter* Worker,class AMCPlayerState* SourcePS,bool bWeaponPassives);
    FGuid ActiveContainerClaim;
    void RecoverUnreachableFood(float Dt);
    UPROPERTY() TObjectPtr<class AMCReactionVFX> SmellVFX;
    float AccessCheckElapsed=0,UnreachableSeconds=0;
    TOptional<FVector> LastAccessiblePosition;
    TOptional<FVector> EntryRecoveryLanding;
    void UpdateHazard(float Dt);
    double HazardNow() const;
    TWeakObjectPtr<class AMCThroat> FuseOwner;
    UPROPERTY() TObjectPtr<class AMCPlayerState> LastHandledBy;
    bool bDeliveryScored=false;
    bool bAutomaticallyConsumed=false;
    bool bAutomaticallyDiscarded=false;
    bool bManuallyThrown=false;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> HazardMaterials;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> ReactionMaterial;
    FString LastLabelCaption;
};

/** Active delivery marker: food enters the throat, rejected ingredients and tools leave at the front. */
UCLASS()
class MESSCONTROL_API AMCFoodDisposal : public AActor
{
    GENERATED_BODY()
public:
    AMCFoodDisposal();
    virtual void Tick(float Dt) override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Volume;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Delivery") TObjectPtr<class UMCDeliveryZoneVisualComponent> DeliveryZoneVisual;
    UPROPERTY(EditAnywhere, Replicated) bool bBrushBin=false;
    /** Legacy floor delivery/discard caps are retired; explicit throat throws own intake. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Delivery") bool bEnableLegacyDeliveryZone=false;
    /** Shared footprint for gameplay intake and the animated floor guide. */
    virtual void GetDeliveryZoneGeometry(FTransform& OutTransform,FVector& OutHalfExtent,bool& bOutCircular) const;
    /** Paired world-space rim and inner fade curves; false keeps the fixture's authored box/circle. */
    bool GetDeliveryZoneOutline(TArray<FVector>& OutOuter,TArray<FVector>& OutInner) const;
    virtual bool ContainsDeliveryPosition(FVector Position) const;
    FVector GetDeliveryDirection() const;
    virtual bool CanAcceptDelivery(const AMCFoodActor* Food) const;
    virtual bool AcceptDelivery(AMCFoodActor* Food);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    bool ContainsDeliveryCap(FVector Position,bool& bOutHasCap) const;
    bool DeliverySurfaceFloorZ(FVector Position,double& OutFloorZ) const;
private:
    bool CacheDeliveryZoneOutline() const;
    mutable TWeakObjectPtr<class AMCTongue> DeliveryOutlineTongue;
    mutable TWeakObjectPtr<UStaticMesh> DeliveryOutlineSource;
    mutable FTransform DeliveryOutlineSurfaceTransform=FTransform::Identity;
    mutable FTransform DeliveryOutlineZoneTransform=FTransform::Identity;
    mutable FVector DeliveryOutlineExtent=FVector::ZeroVector;
    mutable int32 DeliveryOutlineVertexCount=INDEX_NONE,DeliveryOutlineIndexCount=INDEX_NONE;
    mutable bool bDeliveryOutlineBrush=false,bDeliveryOutlineCircular=false,bDeliveryOutlineCached=false;
    mutable TArray<FVector> DeliveryOutlineOuter,DeliveryOutlineInner;
    bool IsFoodInDeliveryZone(const AMCFoodActor* Food) const;
    void ReturnFreshFood(AMCFoodActor* Food);
    bool bExitConfigured=false;
};
