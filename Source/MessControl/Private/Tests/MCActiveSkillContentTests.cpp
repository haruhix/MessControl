// Prerequisite tests: run AFTER the coordinated build, canonical table authoring,
// props import and all21 Niagara assets (including NS_Air_Rupture) are saved.
// These inspect loaded saved content/CDOs only; no actors, PIE, asset saves or index refresh.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkComponent.h"
#include "MCPerkTypes.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPreloadManifest.h"
#include "MCSpongeSkillAction.h"
#include "MCHookSkillAction.h"
#include "MCGrenadeSkillAction.h"
#include "MCAirSkillAction.h"
#include "MCRocketSkillAction.h"
#include "MCDriverSkillAction.h"
#include "MCSuperCarVisualComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"

namespace
{
struct FExpectedActiveSkill
{
    FName ID;
    EMCPerkKind Kind;
    int32 Charges;
    UClass* Effect;
    UClass* Action;
};
TArray<FExpectedActiveSkill> ExpectedActiveSkills()
{
    return {
        {TEXT("Active_Sponge"),EMCPerkKind::Active,-1,UMCSpongeSkillEffect::StaticClass(),AMCSpongeSkillAction::StaticClass()},
        {TEXT("Active_Hook"),EMCPerkKind::Active,-1,UMCHookSkillEffect::StaticClass(),AMCHookSkillAction::StaticClass()},
        {TEXT("Consumable_Grenade"),EMCPerkKind::Consumable,3,UMCGrenadeSkillPerkEffect::StaticClass(),AMCGrenadeSkillAction::StaticClass()},
        {TEXT("Consumable_Air"),EMCPerkKind::Consumable,2,UMCAirSkillEffect::StaticClass(),AMCAirSkillAction::StaticClass()},
        {TEXT("Consumable_Rocket"),EMCPerkKind::Consumable,3,UMCRocketSkillPerkEffect::StaticClass(),AMCRocketSkillAction::StaticClass()},
        {TEXT("Consumable_Driver"),EMCPerkKind::Consumable,3,UMCDriverSkillEffect::StaticClass(),AMCDriverSkillAction::StaticClass()}
    };
}
UDataTable* SavedActiveTable()
{
    return LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
}
const AMCActiveSkillAction* ActionDefaults(const FMCPerkDefinition* Row)
{
    if(!Row || !Row->EffectClass || !Row->EffectClass->IsChildOf(UMCActiveSkillPerkEffect::StaticClass())) return nullptr;
    const auto* Factory=Row->EffectClass->GetDefaultObject<UMCActiveSkillPerkEffect>();
    return Factory && Factory->ActionClass ? Factory->ActionClass->GetDefaultObject<AMCActiveSkillAction>() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveSkillSavedCatalogTest,
    "MessControl.Skills.Content.SavedCatalogBoundary",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveSkillSavedCatalogTest::RunTest(const FString&)
{
    auto* Table=SavedActiveTable();
    if(!TestNotNull(TEXT("Post-author canonical DT_Perks is saved and loadable"),Table)) return false;
    if(!TestTrue(TEXT("Canonical row type is FMCPerkDefinition"),Table->GetRowStruct()==FMCPerkDefinition::StaticStruct())) return false;
    TestEqual(TEXT("27 passives +6 actives +100 retained retired rows"),Table->GetRowMap().Num(),133);
    const auto Expected=ExpectedActiveSkills();
    int32 Passives=0,Actives=0,Retired=0,FirstPassiveBatch=0,SecondPassiveBatch=0;
    for(const auto& Pair:Table->GetRowMap()) {
        const FName ID=Pair.Key;const auto* Row=reinterpret_cast<const FMCPerkDefinition*>(Pair.Value);
        const FString Name=ID.ToString();
        const auto* Spec=Expected.FindByPredicate([ID](const auto& E){return E.ID==ID;});
        const bool CurrentPassive=Name.StartsWith(TEXT("B1_")) || Name.StartsWith(TEXT("B2_"));
        if(!Spec && !CurrentPassive) {
            ++Retired;
            TestFalse(Name+TEXT(" remains disabled; no retired row was reactivated"),Row->bEnabled);
            continue;
        }
        TestTrue(Name+TEXT(" is enabled and usable"),UMCPerkComponent::IsDefinitionUsable(*Row));
        if(CurrentPassive) {
            ++Passives;FirstPassiveBatch+=Name.StartsWith(TEXT("B1_"));SecondPassiveBatch+=Name.StartsWith(TEXT("B2_"));
            TestTrue(Name+TEXT(" remains a separate unique passive"),Row->Kind==EMCPerkKind::Passive && Row->MaxStacks==1);
            continue;
        }
        ++Actives;
        TestTrue(Name+TEXT(" has the explicit current Kind"),Row->Kind==Spec->Kind && UMCPerkComponent::ResolvePerkKind(*Row,ID)==Spec->Kind);
        TestTrue(Name+TEXT(" cooldown is finite and positive authored tuning"),FMath::IsFinite(Row->CooldownSeconds) && Row->CooldownSeconds>0);
        TestTrue(Name+TEXT(" has its actual native factory class"),Row->EffectClass.Get()==Spec->Effect);
        TestTrue(Name+TEXT(" uses no retired activation adapter/tool channel"),Row->ActivationAdapterID.IsNone() && Row->ToolUpgrade==EMCToolUpgrade::None);
        if(Spec->Kind==EMCPerkKind::Consumable) TestEqual(Name+TEXT(" has approved production charges"),Row->InitialCharges,Spec->Charges);
        const auto* CDO=ActionDefaults(Row);
        if(TestNotNull(Name+TEXT(" resolves a real action CDO from the saved factory CDO"),CDO))
            TestTrue(Name+TEXT(" resolves the correct concrete action class"),CDO->GetClass()==Spec->Action && !CDO->GetClass()->HasAnyClassFlags(CLASS_Abstract));
    }
    TestEqual(TEXT("27 current unique passive definitions"),Passives,27);
    TestEqual(TEXT("Nine first-batch passives"),FirstPassiveBatch,9);
    TestEqual(TEXT("Eighteen second-batch passives"),SecondPassiveBatch,18);
    TestEqual(TEXT("Exactly the six approved active IDs"),Actives,6);
    TestEqual(TEXT("All100 historical rows retained and off"),Retired,100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveSkillPreloadRootsTest,
    "MessControl.Skills.Content.SavedCDOResourcesReachPreload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveSkillPreloadRootsTest::RunTest(const FString&)
{
    auto* Table=SavedActiveTable();
    if(!TestNotNull(TEXT("Run only after current DT_Perks authoring"),Table)) return false;
    if(!TestTrue(TEXT("Canonical row struct is current"),Table->GetRowStruct()==FMCPerkDefinition::StaticStruct())) return false;
    TArray<FSoftObjectPath> ManifestPaths;
    FMCPreloadManifest::CollectSoftReferences(Table,ManifestPaths);
    TSet<FSoftObjectPath> PropRoots,NiagaraRoots,OptionalProjectileRoots;
    const auto RequireRoot=[this,&ManifestPaths](const FString& Label,const FSoftObjectPath& Path) {
        TestFalse(Label+TEXT(" has a non-null reflected reference"),Path.IsNull());
        if(!Path.IsNull()) TestTrue(Label+TEXT(" is discovered from the real saved DT/factory/action CDO chain"),ManifestPaths.Contains(Path));
    };
    for(const auto& Spec:ExpectedActiveSkills()) {
        const FString Name=Spec.ID.ToString();
        const auto* Row=Table->FindRow<FMCPerkDefinition>(Spec.ID,TEXT("Active content prerequisite"),false);
        if(!TestNotNull(Name+TEXT(" exists in the authored table"),Row)) continue;
        if(!TestNotNull(Name+TEXT(" has a factory class"),Row->EffectClass.Get())) continue;
        RequireRoot(Name+TEXT(" factory"),FSoftObjectPath(Row->EffectClass.Get()));
        const auto* CDO=ActionDefaults(Row);
        if(!TestNotNull(Name+TEXT(" actual factory CDO supplies an action CDO"),CDO)) continue;
        RequireRoot(Name+TEXT(" action class"),FSoftObjectPath(CDO->GetClass()));
        RequireRoot(Name+TEXT(" prop"),CDO->PropAsset.ToSoftObjectPath());PropRoots.Add(CDO->PropAsset.ToSoftObjectPath());
        const TSoftObjectPtr<UNiagaraSystem>* Cues[]={&CDO->StartVFX,&CDO->LoopVFX,&CDO->ImpactVFX,&CDO->EndVFX,&CDO->WarningVFX};
        for(const auto* Cue:Cues) if(!Cue->IsNull()) {
            RequireRoot(Name+TEXT(" Niagara cue"),Cue->ToSoftObjectPath());NiagaraRoots.Add(Cue->ToSoftObjectPath());
        }
        if(const auto* Driver=Cast<AMCDriverSkillAction>(CDO)) {
            TestEqual(TEXT("SuperCar preserves eight moving parts plus its chassis"),Driver->PartAssets.Num(),8);
            TestTrue(TEXT("Driver selects the new SuperCar chassis"),Driver->PropAsset.ToSoftObjectPath()==FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_SuperCar_Chassis.SM_SuperCar_Chassis")));
            const auto* Visual=Driver->SuperCarVisual.Get();
            if(!TestNotNull(TEXT("Driver owns a reflected reusable SuperCar default component"),Visual)) continue;
            TestFalse(TEXT("Visual component relies on caller updates and never ticks"),Visual->PrimaryComponentTick.bCanEverTick);
            TestEqual(TEXT("Reusable visual has all nine authored mesh references"),Visual->PartAssets.Num(),9);
            TArray<FSoftObjectPath> ComponentPaths;FMCPreloadManifest::CollectSoftReferences(Driver->SuperCarVisual,ComponentPaths);
            for(FName Part:{FName(TEXT("Chassis")),FName(TEXT("WheelFrontLeft")),FName(TEXT("WheelFrontRight")),FName(TEXT("WheelRearLeft")),FName(TEXT("WheelRearRight")),FName(TEXT("BrushFrontRight")),FName(TEXT("BrushFrontLeft")),FName(TEXT("BrushRearRight")),FName(TEXT("BrushRearLeft"))}) {
                const auto* Asset=Visual->PartAssets.Find(Part);
                if(!TestNotNull(Part.ToString()+TEXT(" has an authored driver part CDO reference"),Asset)) continue;
                TestTrue(Part.ToString()+TEXT(" is discovered directly from the real reflected component"),ComponentPaths.Contains(Asset->ToSoftObjectPath()));
                RequireRoot(Part.ToString(),Asset->ToSoftObjectPath());PropRoots.Add(Asset->ToSoftObjectPath());
                TestTrue(Part.ToString()+TEXT(" retains its separate assembly transform"),Visual->PartTransforms.Contains(Part));
            }
            const FTransform* Left=Visual->PartTransforms.Find(FName(TEXT("WheelFrontLeft")));
            const FTransform* Right=Visual->PartTransforms.Find(FName(TEXT("WheelFrontRight")));
            TestTrue(TEXT("Front wheel pivots are individually separated across the car"),Left && Right && Left->GetLocation().Y<0 && Right->GetLocation().Y>0);
        }
        if(const auto* Air=Cast<AMCAirSkillAction>(CDO)) {
            const FSoftObjectPath Rupture(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Air_Rupture.NS_Air_Rupture"));
            TestTrue(TEXT("Actual Air EndVFX is the distinct rupture asset, not its full-bag warning"),Air->EndVFX.ToSoftObjectPath()==Rupture);
            TestTrue(TEXT("Air rupture reaches the table manifest"),ManifestPaths.Contains(Rupture));
        }
        if(const auto* Rocket=Cast<AMCRocketSkillAction>(CDO);Rocket && !Rocket->ProjectileAsset.IsNull()) {
            RequireRoot(TEXT("Optional authored rocket projectile"),Rocket->ProjectileAsset.ToSoftObjectPath());
            OptionalProjectileRoots.Add(Rocket->ProjectileAsset.ToSoftObjectPath());
        }
    }
    PropRoots.Remove(FSoftObjectPath());NiagaraRoots.Remove(FSoftObjectPath());
    TestEqual(TEXT("Fourteen separate prop meshes including all nine SuperCar meshes"),PropRoots.Num(),14);
    TestEqual(TEXT("All21 distinct Niagara systems including Air rupture"),NiagaraRoots.Num(),21);

    // Mimic the real finite expansion waves only for the six skills' roots.
    // This is no AssetRegistry scan and never manufactures expected paths.
    TArray<FSoftObjectPath> TargetRoots=PropRoots.Array();TargetRoots.Append(NiagaraRoots.Array());TargetRoots.Append(OptionalProjectileRoots.Array());
    TSet<UMaterialInterface*> UsedMaterials;
    for(const auto& Root:TargetRoots) {
        UObject* Object=Root.TryLoad();
        if(!TestNotNull(Root.ToString()+TEXT(" resolves saved content after author/import"),Object)) continue;
        FMCPreloadManifest::CollectSoftReferences(Object,ManifestPaths);
        if(const auto* Mesh=Cast<UStaticMesh>(Object)) {
            TestTrue(Root.ToString()+TEXT(" contains renderable triangles"),Mesh->GetNumTriangles(0)>0);
            for(const auto& Slot:Mesh->GetStaticMaterials()) if(Slot.MaterialInterface) UsedMaterials.Add(Slot.MaterialInterface);
        }
        else if(const auto* System=Cast<UNiagaraSystem>(Object)) {
            TestTrue(Root.ToString()+TEXT(" contains editable emitter data"),System->GetNumEmitters()>0);
            for(const auto& Handle:System->GetEmitterHandles()) if(const auto* Data=Handle.GetEmitterData())
                for(const auto* Renderer:Data->GetRenderers()) if(Renderer) {
                    TArray<UMaterialInterface*> Materials;Renderer->GetUsedMaterials(nullptr,Materials);
                    for(auto* Material:Materials) if(Material) UsedMaterials.Add(Material);
                }
        }
        else AddError(Root.ToString()+TEXT(" must resolve to its actual StaticMesh/NiagaraSystem class"));
    }
    TestTrue(TEXT("Saved renderers expose actual material objects"),UsedMaterials.Num()>=2);
    for(auto* Material:UsedMaterials) {
        const FSoftObjectPath MaterialPath(Material);
        TestNotNull(MaterialPath.ToString()+TEXT(" resolves the actual saved material"),MaterialPath.ResolveObject());
        TestNotNull(MaterialPath.ToString()+TEXT(" has a usable base material"),Material->GetMaterial());
        // Deliberately reveal a material-root gap instead of appending a known
        // path in the test and falsely certifying the production collector.
        TestTrue(MaterialPath.ToString()+TEXT(" reaches the real manifest expansion waves before first use"),ManifestPaths.Contains(MaterialPath));
    }
    return true;
}
#endif
