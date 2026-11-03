#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXProfileBinding.h"

#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainProfile.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCNutBossVFXPreview
{
const FName SourceID(TEXT("Boss.NutKnight.Shield"));

FBox VisibleFixtureBounds(AActor* Actor)
{
    FBox Bounds(ForceInit);
    if(!IsValid(Actor))return Bounds;
    TInlineComponentArray<UMeshComponent*> Meshes(Actor);
    for(auto* Mesh:Meshes)if(IsValid(Mesh) && Mesh->IsRegistered() && Mesh->IsVisible() && !Mesh->bHiddenInGame
        && (Cast<UStaticMeshComponent>(Mesh) || Cast<USkeletalMeshComponent>(Mesh)))
    {
        if(auto* Rig=Cast<USkeletalMeshComponent>(Mesh))Rig->RefreshBoneTransforms();
        Mesh->UpdateBounds();const FBox Box=Mesh->Bounds.GetBox();
        if(Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN() && Box.GetExtent().GetMax()<2000.f)Bounds+=Box;
    }
    return Bounds;
}

/** Isolated shield contact, with the actual saved knight rig and damage resolver.
 * The flat native tongue is only query support; this is not a complete encounter
 * or a knight-to-mage run. Native block/stun animation and cracks remain owned by
 * the boss, while only the accepted Hit/Break cosmetic can be overridden. */
class FShieldAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority() || !C.Hero->Status || !C.Hero->Status->IsAlive()
            || C.PerkID!=SourceID || (C.Scenario!=EMCPerkPreviewScenario::Target && C.Scenario!=EMCPerkPreviewScenario::NearWall))
        {Error=TEXT("Щит рыцаря требует отдельный временный Game-мир, живого зубика и сценарий удара/разрушения.");return false;}
        auto* Data=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        if(!Data || !Plane || !C.AddBox){Error=TEXT("Не найдены сохранённые настройки рыцаря или поддержка временного полигона.");return false;}
        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,-.1f),FVector(35,35,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Не удалось создать native tongue для запросов точки контакта.");return false;}
        C.Targets.Add(Floor);Floor->SetFlags(RF_Transient);Floor->SourceMesh=Plane;Floor->bAutomaticYawns=false;
        Floor->SetActorTickEnabled(false);Floor->FinishSpawning(FloorPose);Floor->SetActorTickEnabled(false);
        Floor->Surface->SetVisibility(false);Floor->Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        FHitResult Ground;
        FMCNutBossSettings Settings=Data->Settings.Boss;Settings.Sanitize();
        const float Distance=FMath::Clamp(Settings.MeleeRange*.6f,220.f,300.f);
        const FVector XY=C.Hero->GetActorLocation()+FVector(Distance,0,0);
        if(!Floor->SurfacePoint(XY,Ground)){Error=TEXT("Временная native tongue не подтверждает поверхность под рыцарем.");return false;}
        const FVector Point=Ground.ImpactPoint+FVector(0,0,Settings.BodyRadiusForRole(EMCNutBossRole::Tank)+4.f);
        const FTransform Pose(FRotator(0,180,0),Point);
        auto* Target=C.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Target){Error=TEXT("Не удалось создать настоящий AMCNutBoss.");return false;}
        Boss=Target;C.Targets.Add(Target);Target->SetFlags(RF_Transient);
        Target->ConfigureEncounter(Floor,nullptr,EMCNutBossRole::Tank,Settings,1,137);
        // ConfigureEncounter normally starts an entrance. This fixed fixture
        // starts at the already-landed state; no Hit/Break is manufactured.
        Target->State=EMCNutBossState::Idle;Target->Attack=EMCNutBossAttack::None;Target->SetActorLocation(Point);
        Target->FinishSpawning(Pose);
        // Capsule spacing alone does not describe this authored rig's shield
        // and club. Keep original proportions and move only this isolated
        // fixture until the evaluated visible meshes have a clear gap.
        const FBox HeroBounds=VisibleFixtureBounds(C.Hero),BossBounds=VisibleFixtureBounds(Target);
        if(HeroBounds.IsValid && BossBounds.IsValid)
        {
            const float Shift=FMath::Max(0.f,float(HeroBounds.Max.X+80.f-BossBounds.Min.X));
            if(Shift>0.f)
            {
                const FVector Shifted=Target->GetActorLocation()+FVector(Shift,0,0);FHitResult FinalGround;
                if(!Floor->SurfacePoint(Shifted,FinalGround)){Error=TEXT("Модель рыцаря не помещается на поверхности preview.");return false;}
                Target->SetActorLocation(FVector(Shifted.X,Shifted.Y,FinalGround.ImpactPoint.Z+Settings.BodyRadiusForRole(EMCNutBossRole::Tank)+4.f));
            }
        }
        C.PreviewFront=AMCNutCombatEffect::GetShieldContactFrame(Target).GetRotation().GetForwardVector();
        // BeginPlay also creates the native entrance tell. Remove only that
        // startup cue before readiness; the shield's actual damage cue remains.
        for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It)
            if(IsValid(*It) && It->GetOwner()==Target && It->Cue.Type==EMCNutCombatCue::JumpTell)It->Destroy(false,false);
        const double Later=C.World->GetTimeSeconds()+3600.;
        Target->NextMeleeAt=Target->NextChargeAt=Target->NextJumpAt=Target->NextRollAt=Later;
        Target->NextFireballAt=Target->NextSummonAt=Target->NextRainAt=Target->NextTeleportAt=Target->NextSpecialAt=Later;
        if(!Target->ShieldPresentation || !Target->TankModel || !Target->TankModel->GetSkeletalMeshAsset()
            || Target->ShieldHealth<=0.f || Target->BossRole!=EMCNutBossRole::Tank || Target->bHasMagePhase)
        {Error=TEXT("Сохранённый рыцарь, щит или изолированная конфигурация не готовы; подмена мешем-примитивом запрещена.");return false;}
        Presentation=Target->ShieldPresentation;
        const auto* Defaults=GetDefault<AMCNutCombatEffect>();
        LegacyDust.Reset(Defaults->DustImpactSystem.LoadSynchronous());LegacyShield.Reset(Defaults->ShieldHitSystem.LoadSynchronous());
        const TSoftObjectPtr<UMaterialInterface> Materials[]={Defaults->TelegraphMaterial,Defaults->ShadowMaterial,
            Defaults->FlameMaterial,Defaults->MagicMaterial,Defaults->ShieldFractureMaterial,Defaults->StormMaterial};
        for(int32 Index=0;Index<UE_ARRAY_COUNT(Materials);++Index)
        {
            auto* Material=Materials[Index].LoadSynchronous();
            auto* Anchor=C.AddBox(FName(*FString::Printf(TEXT("KnightShieldResource_%d"),Index)),FVector(0,0,-10000-Index*10),FVector(.001f));
            if(!Material || !Anchor){Error=TEXT("Не удалось подготовить родные материалы щита.");return false;}
            Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);
            Anchor->GetStaticMeshComponent()->SetMaterial(0,Material);C.Targets.Add(Anchor);
        }
        if(!LegacyDust.IsValid() || !LegacyShield.IsValid()){Error=TEXT("Не найдены native Niagara-системы контакта щита.");return false;}
        bBreak=C.Scenario==EMCPerkPreviewScenario::NearWall;
        ExpectedEvent=bBreak?FName(TEXT("Break")):FName(TEXT("Hit"));
        Age=0;bCommitted=bDamageAccepted=bReceiptObserved=bFailed=false;
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящий рыцарь: урон по щиту → native Hit/Break; HP, cracks и анимация не заменяются VFX."));
        return true;
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Component=Presentation.Get();
        if(!Component || !Boss.IsValid()){Error=TEXT("Рыцарь и компонент щита ещё не подготовлены.");return false;}
        if(ReceiptHandle.IsValid())Component->OnEventAccepted.Remove(ReceiptHandle);
        ReceiptHandle=Component->OnEventAccepted.AddLambda([this,Record=C.RecordEvent](FName Event,EMCVFXPresentationResult Result)
        {
            if(!bCommitted)return;
            if(Event==TEXT("Durability")){if(Record)Record(Event);return;}
            if(Event!=ExpectedEvent || Result==EMCVFXPresentationResult::Rejected)
            {bFailed=true;if(Record)Record(TEXT("NativeShieldPresentationRejected"));return;}
            bReceiptObserved=true;LastResult=Result;
            if(Record)Record(Event); // only the actual runtime owner's accepted receipt
        });
        return C.PresentationOverride?Component->PrepareProfileOverride(C.PresentationOverride,Error):Component->Prepare(Error);
    }

    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {
        if(auto* Component=Presentation.Get())Component->GetPreparedResources(Out);
        if(LegacyDust.IsValid())Out.AddUnique(LegacyDust.Get());
        if(LegacyShield.IsValid())Out.AddUnique(LegacyShield.Get());
    }
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;Age=0;auto* Target=Boss.Get();
        if(!Target || !IsValid(C.Hero) || !Target->CanReceiveToolHit() || Target->ShieldHealth<=0.f)
        {bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeShieldFixtureInvalid"));return;}
        const float ShieldBefore=Target->ShieldHealth,HPBefore=Target->Health;
        const float AppliedHP=Target->ReceiveToolDamage(bBreak?ShieldBefore*2.f:FMath::Max(.1f,ShieldBefore*.2f),C.Hero);
        bDamageAccepted=AppliedHP==0.f && FMath::IsNearlyEqual(Target->Health,HPBefore) && !Target->bDefeated
            && (bBreak?(Target->ShieldHealth==0.f && Target->State==EMCNutBossState::Stunned)
                      :(Target->ShieldHealth>0.f && Target->ShieldHealth<ShieldBefore));
        if(!bDamageAccepted)bFailed=true;
        if(C.RecordEvent)C.RecordEvent(bDamageAccepted?FName(TEXT("NativeShieldDamageAccepted")):FName(TEXT("NativeShieldDamageRejected")));
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted)return;Age+=FMath::Max(0.f,Dt);
        if(Age>=1.f && !bReceiptObserved && !bFailed)
        {bFailed=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeShieldReceiptMissing"));}
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override
    {return bCommitted && Age>=1.8f && (bFailed || (bDamageAccepted && bReceiptObserved));}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {
        if(bFailed)return TEXT("Щит рыцаря · проверка не пройдена");
        if(!bCommitted)return TEXT("Подготовка настоящего рыцаря и VFX");
        if(!bReceiptObserved)return TEXT("Родной урон по щиту");
        if(LastResult==EMCVFXPresentationResult::Suppressed)return TEXT("Щит · косметика отключена, native реакция сохранена");
        return bBreak?TEXT("Break · настоящий stun и остаток эффекта"):TEXT("Hit · native block, cracks и остаток эффекта");
    }
    float GetTailSeconds() const override {return 2.f;}
    void Cancel(FMCPerkPreviewContext&) override
    {if(auto* Component=Presentation.Get())Component->StopPresentation(true);}
    void Reset(FMCPerkPreviewContext&) override
    {
        if(auto* Component=Presentation.Get())
        {if(ReceiptHandle.IsValid())Component->OnEventAccepted.Remove(ReceiptHandle);Component->StopPresentation(true);}
        ReceiptHandle.Reset();Presentation.Reset();Boss.Reset();LegacyDust.Reset();LegacyShield.Reset();
        Age=0;bCommitted=bDamageAccepted=bReceiptObserved=bFailed=false;
    }
private:
    TWeakObjectPtr<AMCNutBoss> Boss;
    TWeakObjectPtr<UMCVFXPresentationComponent> Presentation;
    TStrongObjectPtr<UNiagaraSystem> LegacyDust,LegacyShield;
    FDelegateHandle ReceiptHandle;
    FName ExpectedEvent;
    float Age=0;
    bool bBreak=false,bCommitted=false,bDamageAccepted=false,bReceiptObserved=false,bFailed=false;
    EMCVFXPresentationResult LastResult=EMCVFXPresentationResult::UseDefault;
};

const bool bRegistered=[]
{
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=SourceID;Entry.SourceRowID=NAME_None;
    Entry.Label=FText::FromString(TEXT("Ореховый рыцарь · щит"));Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.bSupportsProfileOverride=true;Entry.Stages={TEXT("Hit"),TEXT("Break"),TEXT("Durability")};
    Entry.DescribeNativeSources=[](FName Event) {
        if(Event!=TEXT("Durability"))return MCVFXNativeSources::DescribeKnightShield(Event);
        TArray<FMCVFXNativeSourcePart> Parts;
        FMCVFXNativeSourcePart Material;Material.ID=TEXT("Durability.Material");Material.Kind=EMCVFXNativeSourceKind::Material;
        Material.Label=FText::FromString(TEXT("Родной материал трещин"));Material.Asset=GetDefault<AMCNutCombatEffect>()->ShieldFractureMaterial.ToSoftObjectPath();Parts.Add(Material);
        FMCVFXNativeSourcePart Geometry;Geometry.ID=TEXT("Durability.Geometry");Geometry.Kind=EMCVFXNativeSourceKind::Geometry;
        Geometry.Label=FText::FromString(TEXT("Настоящие изогнутые трещины · форма исходника"));Geometry.SourceFile=TEXT("Source/MessControl/Private/MCNutShieldCrackLayout.h");Parts.Add(Geometry);return Parts;
    };
    Entry.StageLabels.Add(TEXT("Hit"),FText::FromString(TEXT("Удар по щиту")));
    Entry.StageLabels.Add(TEXT("Break"),FText::FromString(TEXT("Разрушение щита")));
    Entry.StageLabels.Add(TEXT("Durability"),FText::FromString(TEXT("Повреждение щита · постоянные трещины")));
    Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::NearWall};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий удар · щит цел")));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,FText::FromString(TEXT("Настоящее разрушение · stun")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FShieldAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    MCVFXProfileBindings::RegisterLibrarySource(SourceID,UMCVFXPresentationLibrary::MouthLibraryPath(),SourceID,{TEXT("Hit"),TEXT("Break"),TEXT("Durability")});
    FMCVFXActionCatalogueEntry Source;Source.ID=SourceID;Source.Label=FText::FromString(TEXT("Ореховый рыцарь · щит"));
    Source.Category=TEXT("Boss");Source.SourceOwner=TEXT("AMCNutBoss::ReceiveToolDamage / ReceiveExternalImpact → ShieldPresentation");
    Source.Description=FText::FromString(TEXT("Реальный контакт Hit/Break щита. Можно заменить косметику события; native block/stun, cracks и HP остаются у босса. Default сохраняет исходный procedural+Niagara эффект целиком."));
    Source.Stages={TEXT("Hit"),TEXT("Break"),TEXT("Durability")};MCVFXActionCatalog::RegisterSource(MoveTemp(Source));return true;
}();
}
