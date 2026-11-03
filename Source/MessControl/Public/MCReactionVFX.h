#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCReactionVFX.generated.h"
struct FMCVFXNativeInputs;
class UMCWorldPresentationComponent;

UENUM()
enum class EMCReactionEffect : uint8 { Impact, Suction, Yawn, Fire, Stars, Steam, BossWind, BlackClotImpact, WaterStream, WaterShot, WaterImpact, CombatHit, PlayerHit, FoodBreak, SpoiledSmell, ChiliExplosion };

/** Small replicated cosmetic bursts. Geometry is generated locally from the server clock. */
UCLASS()
class MESSCONTROL_API AMCReactionVFX : public AActor
{
    GENERATED_BODY()
public:
    AMCReactionVFX();
    static AMCReactionVFX* Spawn(UWorld* World,FVector Point,EMCReactionEffect Effect,float Seconds=1.4f,float Radius=100,FVector Direction=FVector::ForwardVector,float FlowLength=0);
    /** Existing FoodBreak clock/dust boundary; main art is accepted by the native enemy. */
    static AMCReactionVFX* SpawnFoodBreakDeath(AActor* NativeSource,FName SourceID,FGuid EpisodeID,
        FVector Point,float Seconds,float Radius,FVector Direction,bool bNativeBody,bool bNativeDust);
    /** Server-only presentation of the actual post-protection health loss. Never applies damage. */
    static AMCReactionVFX* SpawnHit(AActor* Victim,FVector Point,FVector Direction,float AppliedDamage,bool bShielded=false,bool bTextOnly=false);
    static AMCReactionVFX* SpawnForPresentation(UWorld* World,FName SourceID,FName Event,FVector Point,EMCReactionEffect Effect,float Seconds,float Radius,FVector Direction=FVector::ForwardVector,float FlowLength=0,AActor* PreparedOwner=nullptr);
    FName GetPresentationSourceID()const{return PresentationSourceID;}
    FName GetPresentationEvent()const{return PresentationEvent;}
    UMCWorldPresentationComponent* GetComposerPresentation()const{return ComposerPresentation;}
    bool HasPreparedSourceHandoff()const{return bPreparedSourceHandoff;}
    const FString& GetPreparedSourceHandoffError()const{return PreparedSourceHandoffError;}
    bool HasAcceptedComposerEvent(FName Event)const{return Event==PresentationEvent?bComposerStateAccepted:Event==TEXT("BreakDust")&&bComposerDustAccepted;}
    /** Shared exact accepted cue payload; never derives gameplay from presentation. */
    static void AppendNativeContext(FMCVFXNativeInputs& Inputs,float Age,float Seconds,float Radius,
        FVector Origin,FVector Axis,FVector View,FVector Right,FVector Up,FVector Scale,bool bLoop,bool bReady,
        bool bPlayer=false,bool bShielded=false,bool bLegacy=false,bool bCartoon=false,float AppliedDamage=0);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY(Replicated) EMCReactionEffect Effect=EMCReactionEffect::Stars;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Seconds=1.4f;
    UPROPERTY(Replicated) float Radius=100;
    UPROPERTY(Replicated) FVector Direction=FVector::ForwardVector;
    /** Mouth-source vacuum extent, measured from the inlet back across the tongue. */
    UPROPERTY(Replicated) float FlowLength=0;
    UPROPERTY(Replicated) bool bLoop=false;
    /** Combat feedback owns the NS lane; this actor may retain numeric UI only. */
    UPROPERTY(Replicated) bool bPresentationTextOnly=false;
    UPROPERTY(Replicated,BlueprintReadOnly) float AppliedDamage=0;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bShieldedHit=false;
    /** Opt-in comparison baseline, captured from the victim's MC.LegacyCombatFeedback actor tag. */
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Feedback") bool bLegacyCombatFeedback=false;
    /** Reuse the owned dust burst; reflected reference keeps it reachable by cooking. */
    UPROPERTY(EditDefaultsOnly,Category="Feedback") TSoftObjectPtr<class UNiagaraSystem> FoodBreakSystem;
    /** Preloaded opaque lit vertex-color material for the closed NPC contact solids. */
    UPROPERTY(EditDefaultsOnly,Category="Feedback") TObjectPtr<class UMaterialInterface> SolidMaterial;
private:
    UPROPERTY(Replicated) FName PresentationSourceID;
    UPROPERTY(Replicated) FName PresentationEvent;
    UPROPERTY(Replicated) FName FoodBreakDeathSourceID;
    UPROPERTY(Replicated) FGuid FoodBreakDeathEpisode;
    UPROPERTY(Replicated) bool bAllowNativeDeathDust=true;
    void UpdateFoodBreakDeathContext(float Age,FVector Axis,FVector View,FVector Right,FVector Up);
    UPROPERTY(Transient) TObjectPtr<class UMCWorldPresentationComponent> ComposerPresentation;
    FGuid ComposerEpisode=FGuid::NewGuid();
    bool UpdateComposerArtwork(float Age,float Progress,FVector Axis,FVector View,FVector Right,FVector Up);
    void UpdateFoodBreakDust(float Age);
    void UpdateDamageLabels(float Age,float Progress,FVector View,FVector Right,FVector Up);
    UPROPERTY() TObjectPtr<class UTextRenderComponent> DamageNumber;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> DamageBackdrop;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SoftMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FireMaterial;
    UPROPERTY() TObjectPtr<class UPointLightComponent> FireLight;
    bool bFoodBurstPresented=false;
    bool bPreparedSourceHandoff=false,bComposerStateAccepted=false,bComposerDustAccepted=false;
    FString PreparedSourceHandoffError;
    float PresentedDamage=-1;
    double Now() const;
};
