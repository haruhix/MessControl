#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MCDayPlan.h"
#include "Engine/NetSerialization.h"
#include "MCFoodActor.generated.h"
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMCWorldStatusComponent;
class AMCToothCharacter;
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
enum class EMCFoodPhase : uint8 { Falling, Stuck, Free, Disposed, Equipped, Carried, Swallowing, Absorbing };

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
    // Config/DefaultEngine.ini: distinguish fragments from player ragdoll PhysicsBody shapes.
    static constexpr ECollisionChannel FragmentCollisionChannel=ECC_GameTraceChannel2;
    static AMCFoodActor* FindNearbySpoiledFood(UWorld* World,FVector Point,float Radius=280);
    bool TryGrab(AMCToothCharacter* Hero);
    bool FindGripSurface(FVector From,FHitResult& Hit) const;
    // Query the visible food surface independently of its simplified physics hulls.
    bool FindToolContact(const AMCToothCharacter* Hero,float Reach,FHitResult& Hit) const;
    bool BeginCarry(AMCToothCharacter* Hero);
    void UpdateCarryPresentation(float Dt);
    void Release(AMCToothCharacter* Hero);
    void Dispose();
    void AwardDelivery();
    class AMCPlayerState* GetLastHandledBy() const { return LastHandledBy.Get(); }
    /** Tracks handling intent; physical throat entry also accepts loose bounced food. */
    bool WasManuallyThrown() const { return bManuallyThrown; }
    void MarkManualThrow(AMCToothCharacter* Hero);
    /** Shared physical food launch for hand-stack and legacy grip throws. */
    void LaunchManualThrow(AMCToothCharacter* Hero,FVector Direction,float Speed);
    FVector IntakeSweepStart() const { return PreviousPhysicsLocation; }
    bool BeginSwallow();
    void CancelSwallow();
    void ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment=false);
    void ConfigureBrush();
    bool HitFood(float Damage,FVector Direction,AMCToothCharacter* Worker=nullptr);
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
    bool IsStackPickupActive() const;
    float StackPickupDuration() const { return (.075f+StackPickup.FlightSeconds+.18f)/FMCStackPickup::PlayRate; }
    FTransform StackPickupPose(const FTransform& Goal) const;
    UPROPERTY(Replicated) FMCStackPickup StackPickup;
    UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Collection") TObjectPtr<AMCToothCharacter> StackCarrier;
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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GripSurface;
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
    bool SpawnFragments(FVector Direction);
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
