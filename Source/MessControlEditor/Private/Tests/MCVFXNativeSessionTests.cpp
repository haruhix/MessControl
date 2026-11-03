#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXNativePresentationSettings.h"
#include "NiagaraSystem.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeSessionDraft,
    "MessControl.VFX.ActionSession.NativeDraftTransactionsAndStaleCallbacks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeSessionDraft::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run this isolated Composer regression outside PIE."));return false;}
    FMCVFXActionSession Session;FString Error;
    if(!TestTrue(*Error,Session.Initialize(TEXT("Boss.NutKnight.Shield"),Error)))return false;
    const auto* Draft=Session.GetNativeDraft();
    if(!TestNotNull(TEXT("Renderer's actual defaults are available without assigning a new profile"),Draft))return false;
    const auto* Part=Draft->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic"));
    if(!TestNotNull(TEXT("Actual contact part"),Part))return false;
    const auto Token=Session.GetNativeDraftToken();const auto Original=*Part;auto Edited=Original;
    const uint64 OriginalHash=Draft->ComputeSettingsHash();Edited.Tint=FLinearColor(.23f,.61f,.92f);
    TestTrue(TEXT("A real supported field can be edited"),Session.SetNativePart(Token,false,TEXT("Hit"),Original.PartID,Edited,Error));
    TestTrue(TEXT("Native-only edits participate in the ordinary dirty state"),Session.IsDirty());
    TestEqual(TEXT("Only the copied native part changed"),Session.GetNativeDraft()->FindPart(TEXT("Hit"),Original.PartID)->Tint,Edited.Tint);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores the exact native settings"),Session.GetNativeDraft()->ComputeSettingsHash(),OriginalHash);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Redo restores the authored tint"),Session.GetNativeDraft()->FindPart(TEXT("Hit"),Original.PartID)->Tint,Edited.Tint);
    TestTrue(TEXT("Reset uses actual renderer defaults"),Session.ResetNativePart(Token,false,TEXT("Hit"),Original.PartID,Error));
    TestEqual(TEXT("Reset restores the same exact native data"),Session.GetNativeDraft()->ComputeSettingsHash(),OriginalHash);
    TestTrue(TEXT("Reopen same source gets a new draft identity"),Session.Initialize(TEXT("Boss.NutKnight.Shield"),Error));
    TestFalse(TEXT("Old popup callback cannot edit a newly opened same-source draft"),Session.SetNativePart(Token,false,TEXT("Hit"),Original.PartID,Edited,Error));
    Session.Clear();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeSessionAdditive,
    "MessControl.VFX.ActionSession.FirstAddedLayerRetainsNativeBase",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeSessionAdditive::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run this isolated Composer regression outside PIE."));return false;}
    FMCVFXActionSession Session;FString Error;
    if(!TestTrue(*Error,Session.Initialize(TEXT("Boss.NutMage.Fireball"),Error)))return false;
    if(Session.GetOriginalSlot(TEXT("Impact")).Mode!=EMCPerkVFXSlotMode::UseDefault)
    {AddWarning(TEXT("Artist assigned a custom canonical profile; default-to-add case left unchanged."));Session.Clear();return true;}
    const auto* Part=Session.GetNativeDraft()?Session.GetNativeDraft()->FindPart(TEXT("Impact"),TEXT("ImpactSparks")):nullptr;
    auto* System=Part?Part->System.LoadSynchronous():nullptr;
    if(!TestNotNull(TEXT("The actual source Niagara exists"),System))return false;
    const FGuid Layer=Session.AddSystem(TEXT("Impact"),System,Error);
    TestTrue(TEXT("A layer is independently added"),Layer.IsValid());
    TestTrue(TEXT("First add explicitly keeps native art"),Session.GetNativeDraft()->Events.FindChecked(TEXT("Impact")).bKeepNativeAccentsWithComposition);
    TestTrue(TEXT("Additive choice is part of the saved draft"),Session.HasNativeOverride());
    TestTrue(TEXT("Removing only the custom layer succeeds"),Session.RemoveLayer(TEXT("Impact"),Layer,Error));
    TestEqual(TEXT("Native default remains after the added layer is removed"),Session.GetPreviewSlotMode(TEXT("Impact")),EMCPerkVFXSlotMode::UseDefault);
    auto Protected=*Session.GetNativeDraft()->FindPart(TEXT("Impact"),TEXT("ImpactRing"));Protected.bEnabled=false;
    TestFalse(TEXT("The real danger footprint cannot be hidden by an art override"),Session.SetNativePart(Session.GetNativeDraftToken(),false,TEXT("Impact"),Protected.PartID,Protected,Error));
    Session.Clear();return true;
}
#endif
