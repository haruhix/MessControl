#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCPassiveCombatCue.h"
#include "MCCombatPassiveSubsystem.generated.h"

class AMCToothCharacter;
class AMCNutEnemy;
class UMCPerkComponent;

/** One authoritative contact episode. Its lifetime ends after every target has resolved damage. */
class MESSCONTROL_API FMCCombatAttackScope
{
public:
    explicit FMCCombatAttackScope(AMCToothCharacter* Hero,bool bOrdinaryMelee=true);
    ~FMCCombatAttackScope();
    FMCCombatAttackScope(const FMCCombatAttackScope&)=delete;
    FMCCombatAttackScope& operator=(const FMCCombatAttackScope&)=delete;
private:
    TWeakObjectPtr<AMCToothCharacter> Source;
    uint64 ID=0;
};

/** Server-only transient combat work. No delayed attack survives its original pawn or world. */
UCLASS()
class MESSCONTROL_API UMCCombatPassiveSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    virtual void Deinitialize() override;

    static void NotifyDashStarted(AMCToothCharacter* Hero);
    static float ModifyWeaponDamage(const UMCPerkComponent* Perks,AActor* Target,float Damage);
    static void NotifyWeaponHit(const UMCPerkComponent* Perks,AActor* Target);
    static void NotifyPhysicalHit(AMCToothCharacter* Hero,AMCToothCharacter* Target,float Damage);
    static void ResetOwner(const UMCPerkComponent* Perks);
    static void PruneOwner(const UMCPerkComponent* Perks);
    static bool TickBilliards(AMCNutEnemy* Enemy,float Dt);
    /** Only secondary snapshot contact overrides the live pawn's origin/direction. */
    static bool GetSnapshotContact(const AMCToothCharacter* Hero,FVector& Origin,FVector& Forward,float& Reach);

    // Narrow deterministic inspection for native validation, without changing global random state.
#if WITH_DEV_AUTOMATION_TESTS
    void SetNextThunderRollForTesting(float Roll) { NextThunderRoll=FMath::Clamp(Roll,0.f,1.f); }
    int32 PendingEchoCountForTesting() const { return Echoes.Num(); }
    int32 LaunchCountForTesting() const { return Launches.Num(); }
#endif

private:
    friend class FMCCombatAttackScope;
    enum class ETargetGroup : uint8 { None,Enemy,Food,Ally };
    struct FAttack
    {
        uint64 ID=0;
        uint64 Frame=0;
        uint8 Tool=0;
        TWeakObjectPtr<AMCToothCharacter> Hero;
        FVector Origin=FVector::ZeroVector,Forward=FVector::ForwardVector;
        float Reach=180,Damage=0,DashScale=1;
        bool bMelee=false,bAccepted=false,bSecondary=false,bImplicit=false;
        ETargetGroup Group=ETargetGroup::None;
        TSet<TWeakObjectPtr<AActor>> HitTargets;
        TMap<TWeakObjectPtr<AActor>,float> PendingDamage;
        TWeakObjectPtr<AActor> FirstTarget;
        FVector FirstPoint=FVector::ZeroVector;
        TArray<FMCEchoToolPart> ToolParts;
    };
    struct FOwner
    {
        TWeakObjectPtr<AMCToothCharacter> Hero;
        double DashUntil=-100;
        FAttack Attack;
    };
    struct FEcho { FAttack Attack; double At=0; TWeakObjectPtr<UMCPerkComponent> Owner; TWeakObjectPtr<AMCPassiveCombatCue> Cue; };
    struct FLaunch
    {
        uint64 ID=0;
        TWeakObjectPtr<AMCNutEnemy> Enemy;
        TWeakObjectPtr<AMCToothCharacter> Hero;
        TWeakObjectPtr<UMCPerkComponent> Perks;
        FVector Velocity=FVector::ZeroVector;
        float Damage=0;
        double MoveUntil=0,ContactUntil=0;
        TSet<TWeakObjectPtr<AActor>> HitTargets;
    };
    TMap<TWeakObjectPtr<UMCPerkComponent>,FOwner> Owners;
    TArray<FEcho> Echoes;
    TArray<FLaunch> Launches;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> RetainedEchoMeshes;
    uint64 NextAttackID=1;
    float NextThunderRoll=-1;

    static UMCCombatPassiveSubsystem* For(const UObject* Object);
    static AMCToothCharacter* HeroFor(const UMCPerkComponent* Perks);
    static bool Live(const AMCToothCharacter* Hero);
    static bool Owns(const UMCPerkComponent* Perks,FName ID);
    static float Param(const UMCPerkComponent* Perks,FName ID,FName Key,float Default,float Min,float Max);
    static ETargetGroup GroupFor(AActor* Target);
    static FVector TargetPoint(AActor* Target,FVector Origin);
    bool Visible(FVector Origin,AActor* Target,const AMCToothCharacter* Hero,AActor* Ignore=nullptr) const;
    bool InAttack(const FAttack& Attack,AActor* Target) const;
    FOwner* StateFor(const UMCPerkComponent* Perks,bool bCreate);
    uint64 BeginAttack(AMCToothCharacter* Hero,bool bMelee);
    void EndAttack(AMCToothCharacter* Hero,uint64 ID);
    bool Apply(FAttack& Attack,AActor* Target,float Damage,bool bSecondary);
    void Repeat(FAttack& Attack,bool bSecondary);
    void Chain(const UMCPerkComponent* Perks,const FAttack& Attack);
    void Launch(const UMCPerkComponent* Perks,const FAttack& Attack,AActor* Target,float Damage);
    bool AdvanceLaunch(AMCNutEnemy* Enemy,float Dt);
    void Prune(const UMCPerkComponent* Perks);
};
