#pragma once

#include "CoreMinimal.h"
#include "MCDevCommands.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCIceEventSmoke.generated.h"

class AMCToothCharacter;
class AMCTongue;
class AMCIceEvent;
class AMCPlayerController;
class ACameraActor;

/** Opt-in saved-map integration probe. Damage, strikes and swings use production code. */
UCLASS()
class UMCIceEventSmoke : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool IsTickable() const override { return IsInitialized() && !bFinished; }
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCIceEventSmoke,STATGROUP_Tickables); }
private:
    void Fail(const TCHAR* Reason);
    void Enter(int32 Next,const TCHAR* Label);
    bool RefreshHero();
    bool Place(FVector Point,FVector Facing=FVector::ForwardVector);
    bool Warm();
    bool Unsafe();
    void ParkOtherPlayers();
    void StartF3Case();
    bool CheckF3Case() const;
    void TickClient();
    void Capture();
    void FrameCamera();
    void TickColdRework(float Dt);
    void TickColdClient();
    void ColdPrimaryKey(bool Held);
    bool ColdWarmPoint(FVector& Point,bool bAvoidStrikes=true) const;
    void ColdParkPeers();
    void ColdObserve();
    void ColdObserveVFX(const AMCIceEvent& Event);
    bool ColdShouldCapture() const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<AMCIceEvent> Ice;
    UPROPERTY() TObjectPtr<AMCPlayerController> Host;
    UPROPERTY() TObjectPtr<ACameraActor> CaptureCamera;
    TArray<EMCDevAction> Actions;
    TArray<int32> SavedSteps;
    FString CaptureFolder,StageLabel;
    FVector StrikeAnchor=FVector::ZeroVector;
    double StartedAt=0,StageAt=0,StrikeAt=0,NextCaptureAt=0;
    float InitialHealth=0,FreezePeak=0,ThawStart=0;
    int32 Stage=0,CaseIndex=0,StepCase=0,StrikeId=-1,ExpectedPlayers=1,InitialSwings=0,InitialHits=0,Frame=0;
    uint32 ClientSeen=0;
    int32 ColdSerial=0,ColdCircleIndex=-1,ColdCrystalZone=-1,ColdInputPresses=0;
    uint32 ColdCircleMask=0;
    int32 ColdBurstPeak=0;
    double ColdBurstAt=0,ColdPreviousWarningAt=0,ColdNextSpawn=0;
    bool bColdRework=false,bColdKeyHeld=false,bColdClientHadFeet=false,bColdClientOwnFeet=false;
    bool bColdDodge=false,bColdHit=false,bColdBlocker=false,bColdNova=false,bColdRescue=false;
    bool bColdOverlap=false,bColdMinimum=false,bColdSeries=false;
    bool bRequireColdVFX=false,bColdVFXSeen=false,bColdCompletionVFXSeen=false;
    bool bPrepared=false,bFinished=false,bCapture=false,bSawNextCircle=false,bSawTransfer=false,bResting=false,bPanelOpen=false;
};
