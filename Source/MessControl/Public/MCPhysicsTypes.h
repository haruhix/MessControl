#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "MCPhysicsTypes.generated.h"

UENUM(BlueprintType)
enum class EMCBodyState : uint8 { Standing, Ragdoll, Recovering };

USTRUCT(BlueprintType)
struct FMCPhysicsSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="150",ClampMax="1100")) float Knockback = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="80",ClampMax="650")) float Lift = 340.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="100",ClampMax="600")) float FallThreshold = 240.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="0.5",ClampMax="6")) float RagdollSeconds = 2.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="0.25",ClampMax="2")) float GetUpSeconds = 0.75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="2",ClampMax="35")) float MuscleStrength = 14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="0.3",ClampMax="2")) float Damping = 0.9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics", meta=(ClampMin="3",ClampMax="20")) float Mass = 8.f;
    void Sanitize();
};

UCLASS(BlueprintType)
class MESSCONTROL_API UMCPhysicsProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Physics") FMCPhysicsSettings Settings;
};

USTRUCT()
struct FMCNetBonePose
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Position = FVector::ZeroVector;
    UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
    {
        bool bPositionOK=true;
        Position.NetSerialize(Ar,Map,bPositionOK); Rotation.SerializeCompressedShort(Ar);
        bOutSuccess=bPositionOK && !Ar.IsError(); return true;
    }
    FTransform Transform() const { return FTransform(Rotation,Position); }
};
template<> struct TStructOpsTypeTraits<FMCNetBonePose> : TStructOpsTypeTraitsBase2<FMCNetBonePose> { enum { WithNetSerializer=true }; };

USTRUCT()
struct FMCRagdollFrame
{
    GENERATED_BODY()
    UPROPERTY() EMCBodyState State = EMCBodyState::Standing;
    UPROPERTY() uint16 Revision = 0;
    UPROPERTY() float StateStartedAt = 0.f;
    UPROPERTY() FVector_NetQuantize10 MeshLocation = FVector::ZeroVector;
    UPROPERTY() FRotator MeshRotation = FRotator::ZeroRotator;
    UPROPERTY() FVector_NetQuantize10 CapsuleLocation = FVector::ZeroVector;
    UPROPERTY() float CapsuleYaw = 0.f;
    UPROPERTY() TArray<FMCNetBonePose> Bones;
};
