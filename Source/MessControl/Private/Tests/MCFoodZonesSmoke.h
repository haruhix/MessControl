#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCFoodZonesSmoke.generated.h"

class AMCFoodActor;
class AMCFoodDisposal;
class AMCToothCharacter;
class AMCThroat;
class AMCTongue;
class ACameraActor;

/** Opt-in L_Mouth integration probe. All interaction uses production carry/movement/throw paths. */
UCLASS()
class UMCFoodZonesSmoke : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool IsTickable() const override { return IsInitialized() && !bFinished; }
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCFoodZonesSmoke,STATGROUP_Tickables); }
private:
    void Fail(const TCHAR* Reason);
    bool PrepareCase();
    bool SpawnPiece(int32 Index,bool Wrong,bool Spoiled);
    void AdvanceCase();
    void Capture(const TCHAR* Name);
    void TickCloseup();
    bool PrepareCloseupView();
    bool AuditCloseupGuide(const TCHAR* PhaseName);
    void CaptureCloseup(const TCHAR* PhaseName);
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<AMCFoodDisposal> Red;
    UPROPERTY() TObjectPtr<AMCThroat> Green;
    UPROPERTY() TArray<TObjectPtr<AMCFoodActor>> Food;
    UPROPERTY() TObjectPtr<ACameraActor> CloseupCamera;
    FVector CloseupTarget=FVector::ZeroVector;
    bool bCloseup=false;
    FVector Start=FVector::ZeroVector,Goal=FVector::ZeroVector,Direction=FVector::ForwardVector;
    double StartedAt=0,StageAt=0,WindowAt=0,SwallowAt=0,PassedAt=0;
    float InitialHealth=0;
    int32 Case=0,Stage=0,InitialPoints=0,InitialSwallowed=0,InitialSwallowCount=0;
    bool bPrepared=false,bFinished=false,bSawSuction=false,bCapturedFirst=false,bCapturedSecond=false;
};
