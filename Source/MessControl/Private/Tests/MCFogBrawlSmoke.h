#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCFogBrawlSmoke.generated.h"

class AMCToothCharacter;
class AMCPlayerController;
class AMCArenaTooth;
class AMCFogBrawlEvent;
class AMCTongue;
class ACameraActor;
class UExponentialHeightFogComponent;

/** Opt-in integration on the saved map. The remote peer supplies its own ordinary RMB input. */
UCLASS()
class UMCFogBrawlSmoke : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool IsTickable() const override { return IsInitialized() && !bFinished; }
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCFogBrawlSmoke,STATGROUP_Tickables); }
private:
    void Fail(const TCHAR* Reason);
    void Enter(int32 Next,const TCHAR* Label);
    bool RefreshCrew();
    bool PlaceNear(AMCToothCharacter* Worker,int32 Side);
    bool PlaceFar(AMCToothCharacter* Worker);
    bool Place(AMCToothCharacter* Worker,FVector FloorPoint,FVector Facing);
    bool SmokeCleared(const AMCFogBrawlEvent* Event) const;
    bool IsSmokeVisible(const AMCFogBrawlEvent* Event) const;
    void TickClient();
    void FrameCamera();
    void Capture();
    void CheckStrike();
    void SetBraceKey(bool Held);
    UPROPERTY() TObjectPtr<AMCPlayerController> Host;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Remote;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<AMCFogBrawlEvent> Fog;
    UPROPERTY() TObjectPtr<AMCArenaTooth> PendingTarget;
    UPROPERTY() TObjectPtr<AMCArenaTooth> ConsumedTarget;
    UPROPERTY() TObjectPtr<ACameraActor> CaptureCamera;
    TMap<TWeakObjectPtr<UExponentialHeightFogComponent>,bool> OriginalFogVisibility;
    FString CaptureFolder,StageLabel;
    double StartedAt=0,StageAt=0,NextCaptureAt=0,CaptureStartedAt=0,RetargetDeadline=0;
    float BeforeHealth=0,RemoteBeforeHealth=0,TargetBeforeHealth=0,ClientInitialHealth=0;
    int32 Stage=0,ExpectedPlayers=1,Frame=0,CheckedStrikes=0,InitialKnockdowns=0,InitialRecoveries=0;
    int32 InitialReserve=0,WarningSerial=-1,ClientInputSerial=-1,ClientCheckedStrikes=0;
    uint32 ClientSeen=0;
    bool bPrepared=false,bFinished=false,bCapture=false,bCaptureActive=false,bPanelOpen=false;
    bool bFarChecked=false,bNearChecked=false,bRetargetRequested=false,bRetargetChecked=false,bSawPush=false,bSawRecovery=false;
    bool bSawSmoke=false,bLocalBraceKeyHeld=false,bCompletionChecked=false,bSawBraceRelease=false,bSawHeldReassert=false;
    int32 InputPresses=0;
    FVector BeforeLocation=FVector::ZeroVector;
};
