#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "MCExpressionComponent.generated.h"
class UAnimSequence;
class AMCToothCharacter;
struct FReferenceSkeleton;

UENUM(BlueprintType)
enum class EMCEmotion : uint8 { Neutral, Happy, Angry, Sad, Surprise, Pain, Effort };
UENUM()
enum class EMCHookFaceReaction : uint8 { Success, Miss, Caught };
UENUM(BlueprintType)
// Keep the original six values stable for existing Blueprint callers.
enum class MCViseme : uint8 { Rest, Open, Wide, Round, Closed, LipBite, I, U, L, TH, CH, S, D };

USTRUCT(BlueprintType)
struct FMCEmoteEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Label;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UAnimSequence> Animation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EMCEmotion Emotion=EMCEmotion::Neutral;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName EyeMorph;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bFaceOnly=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bHoldFinalPose=false;
    // Short authored dance cycles repeat for Duration instead of ending mid-gesture.
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bLooping=false;
    // Continuous body dances stop through the ordinary movement/action rules.
    // Native dance1..dance5 retain this behavior for already-saved libraries.
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bLoopUntilMovement=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.5",ClampMax="10")) float Duration=3;
    float Length() const;
    bool LoopsUntilMovement() const;
};
UCLASS(BlueprintType)
class MESSCONTROL_API UMCEmoteLibrary : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FMCEmoteEntry> Entries;
};
USTRUCT()
struct FMCEmoteState
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() double StartedAt=0;
    UPROPERTY() double StoppedAt=-1;
    UPROPERTY() uint16 Serial=0;
};

USTRUCT()
struct FMCSprayReactionState
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<AMCToothCharacter> Source;
    UPROPERTY() double StartedAt=-100;
    UPROPERTY() double ContactAt=-100;
};

/** A short cosmetic lease; server time keeps the same reaction on every peer. */
USTRUCT(BlueprintType)
struct FMCHookFaceReactionState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Face|Hook") FGuid ActionId;
    UPROPERTY(BlueprintReadOnly,Category="Face|Hook") EMCHookFaceReaction Reaction=EMCHookFaceReaction::Success;
    UPROPERTY(BlueprintReadOnly,Category="Face|Hook") double StartedAt=-100;
    UPROPERTY(BlueprintReadOnly,Category="Face|Hook") double StoppedAt=-1;
    UPROPERTY(BlueprintReadOnly,Category="Face|Hook") double DamageAtStart=-100;
};

/** Cosmetic state only. Gameplay never depends on an emote or speech envelope. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCExpressionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCExpressionComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Emotes") TSoftObjectPtr<UMCEmoteLibrary> Profile;
    UPROPERTY(BlueprintReadOnly,Category="Emotes") TObjectPtr<UMCEmoteLibrary> Library;
    UPROPERTY(Replicated) FMCEmoteState State;
    UPROPERTY(BlueprintReadOnly,Category="Face") EMCEmotion CurrentEmotion=EMCEmotion::Neutral;
    /** Transient reaction derived locally from the replicated food inhale; never changes an emote. */
    UPROPERTY(Transient,BlueprintReadOnly,Category="Face") float FoodSuctionReaction=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Face") float SocialDisgust=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Face") float StinkReaction=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Face") float StinkGestureAlpha=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Face") float SprayReaction=0;
    // Authority records actual spray contact; peers derive the cosmetic envelope.
    UPROPERTY(Replicated) FMCSprayReactionState SprayState;
    void ReceiveSpray(AMCToothCharacter* Source);
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Face|Hook") FMCHookFaceReactionState HookFaceState;
    // Authority records contact/miss once. Completion leaves the bounded reaction
    // to finish; only a genuine interruption stops the matching action's lease.
    void PlayHookReaction(const FGuid& ActionId,EMCHookFaceReaction Reaction);
    void StopHookReaction(const FGuid& ActionId);
    UFUNCTION(BlueprintPure,Category="Face|Hook") float HookFaceAlpha() const;
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Emotes") void ServerPlayEmote(FName Id);
    // Authority-owned reward feedback uses the same replicated cosmetic state.
    void PlayChestCelebration();
    bool CanPlay(const FMCEmoteEntry& Entry) const;
    const FMCEmoteEntry* ActiveEntry() const;
    float EmoteAlpha() const;
    float BodyAlpha() const;
    void BuildBodyPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref) const;
    void BuildSocialPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref) const;
    void BuildFacePose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);
    bool ApplyMorphBlink(float Closure);
    float Squint() const { return EyeSquint; }
    float SocialEyeClosure() const { return FMath::Max3(SocialDisgust*.13f,StinkReaction*.3f,SprayReaction*.82f); }
    // Call on the avatar whose voice is being played locally. Timeout returns the mouth to rest.
    // Envelope-only VOIP can use Open; a later lip-sync provider supplies actual visemes.
    UFUNCTION(BlueprintCallable,Category="Speech") void SetSpeechInput(float Envelope,MCViseme Viseme=MCViseme::Open);
    float SpeechAmount() const { return Voice; }
    // Cosmetic weight for the same Surprise face used by the T menu, on both glide phases.
    float SlipperyFaceAlpha() const;
    static FName VisemeShape(MCViseme Viseme);
    static TConstArrayView<FName> MouthShapes();
private:
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    double Now() const;
    double EmoteStoppedAt() const;
    double PredictedEmoteStoppedAt=-1,PredictedEmoteStartedAt=-1;
    uint16 PredictedEmoteSerial=0;
    double VoiceAt=-100,NextEmoteAt=0;
    double FoodSuctionReactionAt=-100;
    double StinkCheckAt=0,StinkGestureAt=-100,NextStinkGestureAt=0;
    float StinkContact=0;
    float FoodSuctionFlinch=0;
    float SprayFlinch=0;
    bool CanReactSocially() const;
    bool CanReactToHook() const;
    EMCEmotion HookReactionEmotion() const;
    FGuid SuppressedHookActionId;
    double SuppressedHookStartedAt=-100;
    float Voice=0,EyeSquint=0,Jaw=0,Smile=0,Brows=0,BrowTilt=0,Round=0,LipClosure=0;
    MCViseme VoiceViseme=MCViseme::Rest;
    TMap<FName,float> MouthWeights;
    TMap<FName,float> EyeWeights;
    bool UpdateEyeShapes(float Dt,float EmotionStrength,bool bPain);
    bool UpdateMouthShapes(float Dt,float EmotionStrength,bool bPain,bool bEyeOnly,float HookAlpha=0);
};
