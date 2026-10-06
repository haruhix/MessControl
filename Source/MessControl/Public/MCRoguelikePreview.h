#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCRoguelikePreview.generated.h"

class AMCToothCharacter;
class AMCRewardChest;
class AMCBossCharacter;
class ACameraActor;
class UMCPerkComponent;
class UAnimSequence;
enum class EMCDevAction : uint8;

/** Explicit command-line demonstration of the integrated foundation; never spawned by a normal match. */
UCLASS()
class MESSCONTROL_API AMCRoguelikePreview : public AActor
{
    GENERATED_BODY()
public:
    AMCRoguelikePreview();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void Step();
    void Finish(bool Passed,const FString& Detail);
    void Photograph(const TCHAR* Filename,FVector Focus);
    bool PlacePlayer(FVector FloorPoint);
    bool BeginSocialReview();
    void StepSocialReview(double Now);
    void StepBossPhase3Review(double Now);
    bool SetBossPhase3Camera();
    bool BeginBossPhase3Clip(int32 Index,double Now);
    void RequestBossPhase3Action(EMCDevAction Action,int32 Index=INDEX_NONE);
    void RestoreBossPhase3View();
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<UMCPerkComponent> Perks;
    UPROPERTY() TObjectPtr<AMCRewardChest> Chest;
    UPROPERTY() TObjectPtr<AMCBossCharacter> Boss;
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    UPROPERTY() TObjectPtr<AMCToothCharacter> SocialPartner;
    UPROPERTY() TObjectPtr<AMCBossCharacter> PhaseOneBoss;
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Phase3Clips;
    FTimerHandle StepTimer;
    int32 Stage=0,ExpectedPlayers=1,BeforeStacks=0,BeforeSpawned=0;
    double StartedAt=0,StageStartedAt=0;
    FName ChosenID;
    FVector BossStarted=FVector::ZeroVector;
    FVector SocialForward=FVector::ForwardVector,SocialRight=FVector::RightVector;
    float HeroHealthBefore=0,PartnerHealthBefore=0,PeakDisgust=0,PeakSprayReaction=0;
    bool bSawChase=false,bSawTarget=false,bSawTelegraph=false,bSawAttack=false,bFinished=false;
    bool bOpeningPhotographed=false;
    bool bChestSocialReview=false,bSocialPhotographed=false;
    bool bBossPhase3Review=false,bPhase3Photographed=false;
    int32 Phase3ClipIndex=0,Phase3PreviewSerial=0,Phase3PoseSamples=0,PhaseOnePreviewSerial=0;
    float Phase3ClipDuration=0,Phase3PeakBoneMotion=0,PhaseOneHealth=0;
    double Phase3PreviewStartedAt=0;
    FTransform PhaseOneTransform;
    FString PhaseOneProfilePath,PhaseOneMeshPath,PhaseOneSkeletonPath;
    TArray<FVector> Phase3FirstBonePositions;
    FVector Phase3ClipOrigin=FVector::ZeroVector;
};
