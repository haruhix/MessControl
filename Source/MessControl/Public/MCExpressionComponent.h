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
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.5",ClampMax="10")) float Duration=3;
    float Length() const;
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
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Emotes") void ServerPlayEmote(FName Id);
    bool CanPlay(const FMCEmoteEntry& Entry) const;
    const FMCEmoteEntry* ActiveEntry() const;
    float EmoteAlpha() const;
    float BodyAlpha() const;
    void BuildBodyPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref) const;
    void BuildFacePose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);
    float Squint() const { return EyeSquint; }
    // Call on the avatar whose voice is being played locally. Timeout returns the mouth to rest.
    // Envelope-only VOIP can use Open; a later lip-sync provider supplies actual visemes.
    UFUNCTION(BlueprintCallable,Category="Speech") void SetSpeechInput(float Envelope,MCViseme Viseme=MCViseme::Open);
    float SpeechAmount() const { return Voice; }
    static FName VisemeShape(MCViseme Viseme);
    static TConstArrayView<FName> MouthShapes();
private:
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    double Now() const;
    double VoiceAt=-100,NextEmoteAt=0;
    float Voice=0,EyeSquint=0,Jaw=0,Smile=0,Brows=0,BrowTilt=0,Round=0,LipClosure=0;
    MCViseme VoiceViseme=MCViseme::Rest;
    TMap<FName,float> MouthWeights;
    bool UpdateMouthShapes(float Dt,float EmotionStrength,bool bPain);
};
