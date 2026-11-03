#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "SMCNativeArtPartsPanel.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXNativeSources.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPassiveFeedbackCue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativePassiveArtistControls,
    "MessControl.VFX.NativeFeedback.ComposerControlsAndOriginalSourcesAreReal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativePassiveArtistControls::RunTest(const FString&)
{
    FString Error;const FName ID(TEXT("B2_CleanBreath")),Event(TEXT("Proc"));
    auto* Draft=NewObject<UMCVFXNativePresentationSettings>();if(!TestTrue(TEXT("Real passive native defaults"),Draft->InitFromNativeDefaults(ID,Error)))return false;
    FMCNativeArtScope Scope;Scope.Event=Event;const auto Views=MCNativeArtParts::BuildViews(Draft,Scope);
    const auto* Solid=Views.FindByPredicate([](const auto& P){return P.PartID==TEXT("FeedbackSolid");});
    if(!TestNotNull(TEXT("Actual volume section has an ordinary existing native-parts card"),Solid))return false;
    TestTrue(TEXT("Real size control is exposed"),Solid->Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("Feedback.Scale");}));
    TestTrue(TEXT("Real cosmetic duration control is exposed"),Solid->Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("Feedback.DurationScale");}));
    TestFalse(TEXT("Opaque material does not expose nonfunctional alpha controls"),Solid->Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("OpacityScale") || C.ID==TEXT("OpacityCurve");}));
    FMCNativeArtEdit Edit;Edit.SourceID=ID;Edit.Scope=Scope;Edit.PartID=TEXT("FeedbackSolid");Edit.ControlID=TEXT("Feedback.Scale");Edit.Kind=EMCNativeArtControlKind::Scalar;Edit.Scalar=1.25;
    FMCVFXNativePartSettings Changed;
    TestTrue(TEXT("Existing native UI writer consumes the exact reflected field"),MCNativeArtParts::ApplyEdit(Edit,Draft,Changed,Error));
    TestEqual(TEXT("Size is actually written"),Changed.Feedback.Scale,1.25f);
    Edit.ControlID=TEXT("Feedback.DurationScale");Edit.Scalar=1.5;
    TestTrue(TEXT("Duration field has a real UI writer"),MCNativeArtParts::ApplyEdit(Edit,Draft,Changed,Error));
    TestEqual(TEXT("Cosmetic clock is actually written"),Changed.Feedback.DurationScale,1.5f);
    const auto Sources=MCVFXNativeSources::DescribePassiveFeedback(ID,Event);
    TestEqual(TEXT("Heal exposes its two used materials plus native generator"),Sources.Num(),3);
    TestFalse(TEXT("No original Niagara is fabricated for procedural feedback"),Sources.ContainsByPredicate([](const auto& S){return S.Kind==EMCVFXNativeSourceKind::Niagara;}));
    const auto* Entry=MCPerkPreviewAdapters::Find(ID);
    TestTrue(TEXT("Shared existing real passive provider supplies native sources"),Entry && Entry->DescribeNativeSources && !Entry->DescribeNativeSources(Event).IsEmpty());
    TestTrue(TEXT("Unused acquisition event has no invented original drawing"),MCVFXNativeSources::DescribePassiveFeedback(ID,TEXT("Equipped")).IsEmpty());
    auto* Link=NewObject<UMCVFXNativePresentationSettings>();if(!Link->InitFromNativeDefaults(TEXT("B2_NeatFreak"),Error))return false;
    Scope.Event=TEXT("Link");const auto LinkViews=MCNativeArtParts::BuildViews(Link,Scope);
    for(const auto& Part:LinkViews)TestFalse(TEXT("Actual endpoint path is protected in UI too"),Part.Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("Feedback.Scale") || C.ID==TEXT("ExpansionCurve");}));
    auto* Lightning=NewObject<UMCVFXNativePresentationSettings>();if(!Lightning->InitFromNativeDefaults(TEXT("B2_ThunderGod"),Error))return false;
    const auto LightningViews=MCNativeArtParts::BuildViews(Lightning,Scope);
    TestEqual(TEXT("One real material section, not fake separate core/halo slots"),LightningViews.Num(),1);
    if(LightningViews.Num()!=1)return false;
    TestTrue(TEXT("Width-only native scalar is an actual UI control"),LightningViews[0].Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("Feedback.Scale");}));
    TestFalse(TEXT("No artist path expansion can move accepted lightning endpoints"),LightningViews[0].Controls.ContainsByPredicate([](const auto& C){return C.ID==TEXT("ExpansionCurve");}));
    Edit.SourceID=TEXT("B2_ThunderGod");Edit.Scope=Scope;Edit.PartID=TEXT("LightningStroke");Edit.ControlID=TEXT("Feedback.Scale");Edit.Scalar=1.5;
    TestTrue(TEXT("Existing shared UI writer consumes actual thickness field"),MCNativeArtParts::ApplyEdit(Edit,Lightning,Changed,Error));
    TestEqual(TEXT("Written thickness field is the runtime field"),Changed.Feedback.Scale,1.5f);
    const auto LightningSources=MCVFXNativeSources::DescribePassiveFeedback(TEXT("B2_ThunderGod"),TEXT("Link"));
    TestEqual(TEXT("Original material and real procedural source are openable"),LightningSources.Num(),2);
    TestTrue(TEXT("Original source points to the real lightning generator"),LightningSources.ContainsByPredicate([](const auto& S){return S.SourceFile==TEXT("Source/MessControl/Private/MCPassiveCombatCue.cpp");}));
    const auto* ThunderEntry=MCPerkPreviewAdapters::Find(TEXT("B2_ThunderGod"));
    TestTrue(TEXT("Existing shared provider discovers the actual native sources"),ThunderEntry && ThunderEntry->DescribeNativeSources && ThunderEntry->DescribeNativeSources(TEXT("Link")).Num()==2);
    return true;
}
#endif
