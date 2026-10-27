#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MCToothCareFeedbackComponent.generated.h"

class AMCArenaTooth;
class UMaterialInterface;
class UProceduralMeshComponent;

UENUM()
enum class EMCToothCareFeedbackKind : uint8 { Cleaning, Repair };

/** Accepted work only. Applying coffee, changing a damage mesh or loading a tooth is not work. */
USTRUCT()
struct FMCToothCareFeedbackState
{
    GENERATED_BODY()
    UPROPERTY() int32 WorkSerial=0;
    UPROPERTY() EMCToothCareFeedbackKind Kind=EMCToothCareFeedbackKind::Cleaning;
    UPROPERTY() double WorkAt=-100.;
    UPROPERTY() FVector_NetQuantize10 ContactLocal=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal NormalLocal=FVector::ForwardVector;
    UPROPERTY() int32 CompletionSerial=0;
    UPROPERTY() EMCToothCareFeedbackKind CompletionKind=EMCToothCareFeedbackKind::Cleaning;
    UPROPERTY() double CompletedAt=-100.;
    UPROPERTY() FVector_NetQuantize10 CompletionLocal=FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal CompletionNormalLocal=FVector::ForwardVector;
};

/** One immutable material/one section; bounded, local cosmetic vertices follow accepted server work. */
UCLASS(ClassGroup=(Care),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCToothCareFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCToothCareFeedbackComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    /** Native authority entry after a real mask/HP change, never a client reward request. */
    void ReportAccepted(EMCToothCareFeedbackKind Kind,FVector Point,FVector Normal,bool bCompleted=false);
    UPROPERTY(EditDefaultsOnly,Category="Care|Presentation") TSoftObjectPtr<UMaterialInterface> CareMaterial;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Care|Presentation") TObjectPtr<UProceduralMeshComponent> Presentation;
private:
    struct FParticle
    {
        FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
        FLinearColor Color=FLinearColor::White;
        double Born=0;
        float Life=.5f,Size=10.f;
        uint8 Shape=0;
        bool bOnSurface=false;
    };
    double Clock() const;
    void PreparePresentation();
    void EmitProcess(double Now);
    void EmitCompletion();
    bool ProjectSurface(FVector Candidate,FVector Normal,FVector& Point,FVector& HitNormal) const;
    void AddParticle(const FParticle& Particle);
    void Draw(double Now);
    UFUNCTION() void OnRep_Presentation();
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FMCToothCareFeedbackState State;
    UPROPERTY() TObjectPtr<AMCArenaTooth> Tooth;
    UPROPERTY() TObjectPtr<UMaterialInterface> RetainedMaterial;
    TArray<FParticle> Particles;
    TArray<FVector> DrawVertices,DrawNormals;
    TArray<FVector2D> DrawUV,DrawVariants;
    TArray<FColor> DrawPackedColors;
    FRandomStream Random;
    int32 SeenCompletion=0;
    float EmitRemainder=0;
    double LastPublishedAt=-100.;
};
