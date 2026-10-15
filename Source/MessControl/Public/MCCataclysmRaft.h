#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCataclysmRaft.generated.h"

class AMCCoffeeFlood;
class AMCToothCharacter;
class AMCTongue;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
struct FCollisionQueryParams;

/** Kinematic, walkable Cataclysm refuge. Only actual deck riders count as load. */
UCLASS()
class MESSCONTROL_API AMCCataclysmRaft : public AActor
{
    GENERATED_BODY()
public:
    AMCCataclysmRaft();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PostNetReceiveLocationAndRotation() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    /** GroundAnchor.Z is the floor under the raft, rather than its waterline. */
    void SetupForFlood(AMCCoffeeFlood* Flood, FVector GroundAnchor, float SurfaceOffset = 0.f);
    void UpdateSurface(float WaterWorldZ);
    UFUNCTION(BlueprintPure, Category="Cataclysm|Raft") bool IsBoardable() const;
    UFUNCTION(BlueprintPure, Category="Cataclysm|Raft") bool IsOverloaded() const;
    UFUNCTION(BlueprintPure, Category="Cataclysm|Raft") bool IsSubmerging() const;
    bool FindBoardingPoint(const AMCToothCharacter* Hero, FVector& DeckFootPoint) const;
    bool GetBoardingDestination(const AMCToothCharacter* Hero, FVector& DeckFootPoint) const
        { return FindBoardingPoint(Hero, DeckFootPoint); }
    UPrimitiveComponent* GetDeckComponent() const;
    bool IsRider(const AMCToothCharacter* Hero) const;
    bool IsHoldingRail(const AMCToothCharacter* Hero) const;
    /** Authority input in world XY; expires when the owning pawn stops refreshing it. */
    bool SetRowingInput(AMCToothCharacter* Hero, FVector2D Direction, bool Held);
    /** Two actual handle points on the player's existing brush used as a paddle. */
    bool GetRowingHandTargets(const AMCToothCharacter* Hero, FVector& Left, FVector& Right) const;
    /** Shared radians for the brush stroke and the passenger's body pose. */
    float RowingStrokePhase(const AMCToothCharacter* Hero = nullptr) const;
    /** Each passenger retains their own world-space aim, even when forces cancel. */
    FVector RowingDirectionFor(const AMCToothCharacter* Hero) const;
    FVector RowingVisualDirectionFor(const AMCToothCharacter* Hero) const;
    void AlignRowingBrushToHand(const AMCToothCharacter* Hero, const FTransform& HandWorld);
    const UStaticMeshComponent* RowingBrushFor(const AMCToothCharacter* Hero) const;
    float RowingSideFor(const AMCToothCharacter* Hero) const;
    /** A real hull contact supplies a bounded velocity change (cm/s), never health damage. */
    void ApplyDebrisImpulse(FVector WorldVelocityImpulse, FVector ContactPoint);

    static constexpr int32 Capacity = 2;
    static constexpr float DeckTop = 37.5f;
    static constexpr float WarningSeconds = .7f;
    static constexpr float SubmergeSeconds = 2.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft")
    TObjectPtr<UStaticMeshComponent> Deck;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") int32 OccupantCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") double WarningStartedAt = -1.;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") double SinkStartedAt = -1.;
    UPROPERTY(ReplicatedUsing=OnRep_Flood, BlueprintReadOnly, Category="Cataclysm|Raft") TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") int32 RowingCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") FVector2D RowingDirection = FVector2D::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") TArray<TObjectPtr<AMCToothCharacter>> RowingPlayers;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") TArray<FVector2D> RowingDirections;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Raft") TArray<double> RowingStartedAt;

    /** Mean acceleration per rower (cm/s²); the shared loaded stroke supplies its pulse. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="0",ClampMax="1000")) float RowAcceleration = 360.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="60",ClampMax="800")) float MaxTravelSpeed = 480.f;
    /** Linear coast damping per second, applied equally to current and all rowers. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="0.1",ClampMax="5")) float TravelDrag = 1.1f;
    /** Effective planar rotational inertia per unit mass (cm²). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="1000",ClampMax="100000")) float YawInertiaRadiusSquared = 18000.f;
    /** Blade contact lies beside the rower, across their chosen stroke direction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="0",ClampMax="100")) float PaddleLeverOffset = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="0.1",ClampMax="5")) float AngularDrag = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="1",ClampMax="180")) float MaxYawSpeed = 65.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cataclysm|Raft|Rowing", meta=(ClampMin="10",ClampMax="720")) float MaxYawAcceleration = 150.f;

private:
    double Clock() const;
    void CollectRiders(TArray<AMCToothCharacter*>& Riders) const;
    void UpdateLoad(double Now);
    void UpdatePose(float DeltaSeconds, double Now);
    void UpdateCues(double Now);
    float SinkAmount(double Now) const;
    void InitializeCues();
    void UpdateOars(double Now);
    void UpdateRowing(double Now);
    void UpdateDrift(float DeltaSeconds, double Now);
    bool SupportedFloor(FVector Position, float Yaw, FVector& FloorPoint) const;
    FCollisionQueryParams TravelQuery() const;
    bool ClearHullPose(FVector Location, FRotator Rotation, const FCollisionQueryParams& Query) const;
    UFUNCTION() void OnRep_Flood();

    UPROPERTY(Replicated) bool bConfigured = false;
    UPROPERTY(Replicated) FVector Anchor = FVector::ZeroVector;
    UPROPERTY(Replicated) float AnchorYaw = 0.f;
    UPROPERTY(Replicated) float SurfaceWorldZ = 0.f;
    UPROPERTY(Replicated) float WaterlineOffset = 0.f;
    UPROPERTY(Replicated) double FloatStartedAt = 0.;
    UPROPERTY(Replicated) float LoadPitch = 0.f;
    UPROPERTY(Replicated) float LoadRoll = 0.f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WarningOutline;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WaterWake;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LeftOar;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> RightOar;
    struct FRowingRequest
    {
        TWeakObjectPtr<AMCToothCharacter> Hero;
        FVector2D Direction = FVector2D::ZeroVector;
        double RefreshedAt = -1.;
        double StartedAt = 0.;
        int32 Oar = 0;
    };
    TArray<FRowingRequest> RowingRequests;
    FVector HorizontalVelocity = FVector::ZeroVector;
    float YawVelocity = 0.f;
    float ImpactPitch = 0.f;
    float ImpactRoll = 0.f;
    double NextSupportScanAt = 0.;
    FVector LastSupportPoint = FVector::ZeroVector;
    bool bHasSupportPoint = false;
    TWeakObjectPtr<AMCTongue> SupportTongue;
    double NextLoadScanAt = 0.;
    double GraceEndsAt = 0.;
    float VisualPitch = 0.f;
    float VisualRoll = 0.f;
    bool bPoseInitialized = false;
    TWeakObjectPtr<AMCCoffeeFlood> TickFlood;
    float VisualPaddleSide[2]={-1.f,1.f};
    FVector VisualRowingDirection[2]={FVector::ForwardVector,FVector::ForwardVector};
    TWeakObjectPtr<AMCToothCharacter> VisualRowers[2];
    double LastBrushVisualAt=-1.;
};
