#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewRunner.h"
#include "MCPerkPreviewAdapter.h"
#include "Editor.h"

namespace
{
// A temporary mechanic-only provider tests the public Session boundary without
// making a real editable passive pretend that its binding is unavailable.
class FReadOnlySessionFixture final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString&)override{return C.World && C.Hero;}
    void Tick(FMCPerkPreviewContext&,float)override{}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return TEXT("Read-only Session regression fixture");}
};
struct FScopedReadOnlyProvider
{
    const FName ID;
    explicit FScopedReadOnlyProvider(bool Editable=false)
        :ID(Editable?TEXT("Tests.UnboundEditableSessionFixture"):TEXT("Tests.ReadOnlySessionFixture"))
    {
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;
        E.bSupportsProfileOverride=Editable;E.Stages={TEXT("Observed")};E.Scenarios={EMCPerkPreviewScenario::Target};
        E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FReadOnlySessionFixture>();};
        MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
    ~FScopedReadOnlyProvider(){MCPerkPreviewAdapters::Unregister(ID);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCMechanicOnlySessionPlay,
    "MessControl.VFX.ActionSession.MechanicOnlyProviderCanPlay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCMechanicOnlySessionPlay::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld) {AddError(TEXT("Run this isolated Composer regression outside PIE."));return false;}
    FScopedReadOnlyProvider Fixture;FMCVFXActionSession Session;FString Error;
    const bool bOpened=Session.Initialize(Fixture.ID,Error);
    if(!TestTrue(*FString::Printf(TEXT("Read-only native context opens: %s"),*Error),bOpened))return false;
    TestFalse(TEXT("Mechanic-only preview does not invent an editable VFX binding"),Session.CanEditProfile());
    TestTrue(TEXT("Mechanic-only provider has no invented canonical asset owner"),Session.GetCanonicalOwner().IsNull());
    auto* Working=MCPerkActionPreviewPanel::GetWorkingProfile();
    TestTrue(TEXT("Reusing the same mechanic-only context needs no canonical binding"),MCPerkActionPreviewPanel::EnsureContext(Fixture.ID,Error));
    TestTrue(TEXT("Read-only context reuse preserves the shared transient working object"),Working==MCPerkActionPreviewPanel::GetWorkingProfile());
    FMCPerkVFXSlot Disabled;Disabled.Mode=EMCPerkVFXSlotMode::Disabled;
    TestFalse(TEXT("Shared authoring gate still refuses a read-only event write"),MCPerkActionPreviewPanel::SetWorkingSlot(TEXT("Observed"),Disabled,Error));
    TestFalse(TEXT("Session still refuses authoring a mechanic-only event"),Session.NewCompound(TEXT("Observed"),Error));
    TestFalse(TEXT("Session still refuses applying a mechanic-only profile"),Session.ApplyAll(Error));
    TestFalse(TEXT("Rejected authoring did not create a dirty draft"),Session.IsDirty());
    // The previous PrepareProfile path rejected its empty AllowedEvents here,
    // even though the native scenario and direct runner API both worked.
    const bool bStarted=Session.PlayFullAction(Error);
    TestTrue(*FString::Printf(TEXT("Public Session Play starts the real mechanic: %s"),*Error),bStarted);
    TestTrue(TEXT("Native shared runner exists for the same context"),MCPerkActionPreviewPanel::GetActiveRunner().IsValid());
    if(const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner())
        TestNull(TEXT("Mechanic-only playback does not inject an editable presentation override"),Runner->GetPresentationOverride());
    {
        FScopedReadOnlyProvider UnboundEditable(true);FMCVFXActionSession EditableSession;
        TestFalse(TEXT("Editable provider still requires its real canonical profile owner"),EditableSession.Initialize(UnboundEditable.ID,Error));
        TestTrue(TEXT("Rejected editable initialization preserves the previous read-only context"),Session.IsContextCurrent());
        TestTrue(TEXT("Rejected editable initialization does not replace shared working data"),Working==MCPerkActionPreviewPanel::GetWorkingProfile());
    }
    Session.Stop();Session.Clear();return bStarted;
}
#endif
