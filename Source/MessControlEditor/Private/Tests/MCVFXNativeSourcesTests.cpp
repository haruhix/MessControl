#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCVFXNativeSources.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeSourceRowsUseActualRendererResources,
    "MessControl.VFX.Composer.NativeSourceRowsMatchActualCDODefaults",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeSourceRowsUseActualRendererResources::RunTest(const FString&)
{
    const auto* Cue=GetDefault<AMCNutCombatEffect>();const auto* Shot=GetDefault<AMCNutSpellProjectile>();
    auto Has=[&](const TArray<FMCVFXNativeSourcePart>& Parts,EMCVFXNativeSourceKind Kind,const FSoftObjectPath& Asset)
        {return Parts.ContainsByPredicate([&](const auto& P){return P.Kind==Kind && P.Asset==Asset;});};
    const auto Shield=MCVFXNativeSources::DescribeKnightShield(TEXT("Hit"));
    const auto Flight=MCVFXNativeSources::DescribeMageFireball(TEXT("Flight"));
    const auto Charge=MCVFXNativeSources::DescribeMageFireball(TEXT("Charge"));
    const auto Impact=MCVFXNativeSources::DescribeMageFireball(TEXT("Impact"));
    TestTrue(TEXT("Shield spark row is the current native Niagara reference"),Has(Shield,EMCVFXNativeSourceKind::Niagara,Cue->ShieldHitSystem.ToSoftObjectPath()));
    TestTrue(TEXT("Shield flash material is not mislabeled Niagara"),Has(Shield,EMCVFXNativeSourceKind::Material,Cue->MagicMaterial.ToSoftObjectPath()));
    TestTrue(TEXT("Mage flight row uses actual projectile trail reference"),Has(Flight,EMCVFXNativeSourceKind::Niagara,Shot->TrailSystem.ToSoftObjectPath()));
    TestTrue(TEXT("Mage charge row uses actual native charge system"),Has(Charge,EMCVFXNativeSourceKind::Niagara,Cue->CastChargeSystem.ToSoftObjectPath()));
    TestTrue(TEXT("Mage core row opens its actual original material"),Has(Flight,EMCVFXNativeSourceKind::Material,Shot->CoreMaterial.ToSoftObjectPath()));
    TestTrue(TEXT("Impact footprint row opens the actual native telegraph material"),Has(Impact,EMCVFXNativeSourceKind::Material,Cue->TelegraphMaterial.ToSoftObjectPath()));
    TestFalse(TEXT("Unused loaded Magic material is not presented as a rendered Impact part"),Has(Impact,EMCVFXNativeSourceKind::Material,Cue->MagicMaterial.ToSoftObjectPath()));
    for(const FName Source:{FName(TEXT("Boss.NutKnight.Shield")),FName(TEXT("Boss.NutMage.Fireball"))}) {
        const auto* Entry=MCPerkPreviewAdapters::Find(Source);
        if(!TestNotNull(TEXT("Native resource discovery belongs to the shared real provider"),Entry))return false;
        if(!TestTrue(TEXT("Provider declares a native-source capability"),bool(Entry->DescribeNativeSources)))return false;
        for(FName Event:Entry->Stages) {
            const auto Parts=Entry->DescribeNativeSources(Event);TSet<FName> Seen;
            TestFalse(TEXT("A real native event does not appear as an empty0/0 placeholder"),Parts.IsEmpty());
            for(const auto& Part:Parts) {
                TestTrue(TEXT("Native source identity is stable and unique within the event"),!Part.ID.IsNone()&&!Seen.Contains(Part.ID));Seen.Add(Part.ID);
                if(Part.Kind==EMCVFXNativeSourceKind::Geometry) {
                    TestTrue(TEXT("Procedural geometry is identified as code, not a fictitious Niagara asset"),Part.Asset.IsNull()&&!Part.SourceFile.IsEmpty());
                    TestTrue(TEXT("Geometry source link targets an actual checked project file"),FPaths::FileExists(FPaths::ProjectDir()/Part.SourceFile));
                } else TestFalse(TEXT("Actual NS/material resource paths are retained"),Part.Asset.IsNull());
            }
        }
    }
    TestTrue(TEXT("Unknown event does not borrow an unrelated effect"),MCVFXNativeSources::DescribeMageFireball(TEXT("NotAnEvent")).IsEmpty());
    return true;
}
#endif
