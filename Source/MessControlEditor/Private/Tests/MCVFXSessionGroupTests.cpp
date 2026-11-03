#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXRecipePreset.h"
#include "MCVFXProfileBinding.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "Editor.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCSessionGroupTests
{
class FLayoutOnlyAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString&)override{return C.World && C.Hero;}
    void Tick(FMCPerkPreviewContext&,float)override{}
    FString GetPhaseName(const FMCPerkPreviewContext&)const override{return TEXT("Layout-only Session fixture");}
};
struct FLayoutOnlyRegistration
{
    const FName ID=TEXT("Tests.ComposerLayoutOnly");
    FLayoutOnlyRegistration()
    {
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.bSupportsProfileOverride=false;
        E.Stages={TEXT("Observed")};E.Scenarios={EMCPerkPreviewScenario::Target};
        E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FLayoutOnlyAdapter>();};MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
    ~FLayoutOnlyRegistration(){MCPerkPreviewAdapters::Unregister(ID);}
};
FString CachedComposition(const FMCVFXActionSession& Session,FName Event)
{
    TSharedPtr<FJsonObject> Root;auto Reader=TJsonReaderFactory<>::Create(Session.GetSnapshotJson());
    if(!FJsonSerializer::Deserialize(Reader,Root) || !Root)return FString();
    for(const auto& Value:Root->GetArrayField(TEXT("stages")))
    {const auto Object=Value->AsObject();if(Object && Object->GetStringField(TEXT("stage"))==Event.ToString())return Object->GetStringField(TEXT("cached_composition"));}
    return FString();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionLayoutRuntimeSeparation,
    "MessControl.VFX.ActionSession.LayoutPreservesEmptyDefaultAndPlayback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionLayoutRuntimeSeparation::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    MCSessionGroupTests::FLayoutOnlyRegistration Provider;FMCVFXActionSession Session;FString Error;
    if(!TestTrue(*Error,Session.Initialize(Provider.ID,Error)))return false;
    const FName Event(TEXT("Observed"));const FGuid Token=Session.GetDraftToken();const auto Playback=Session.GetPlaybackRevision();
    TestEqual(TEXT("Empty provider starts as UseDefault"),Session.GetPreviewSlotMode(Event),EMCPerkVFXSlotMode::UseDefault);
    TestTrue(TEXT("Move a metadata-only event"),Session.SetEventCardPosition(Token,Event,FVector2D(115,230),Error));
    TestTrue(TEXT("Move action hub independently"),Session.SetActionHubPosition(Token,FVector2D(600,250),Error));
    FMCComposerGroupLayout DefaultFrame;DefaultFrame.Position=FVector2D(150,320);
    TestTrue(TEXT("Implicit default frame has portable layout without a new runtime group"),Session.SetEventGroupLayout(Token,Event,FGuid(),DefaultFrame,Error));
    TestTrue(TEXT("Layout participates in the close guard"),Session.IsDirty());
    TestTrue(TEXT("Layout has its own unsaved baseline"),Session.IsMasterLayoutDirty());
    TestFalse(TEXT("Layout does not invent runtime data"),Session.IsRuntimeDirty());
    TestEqual(TEXT("No Disabled inference from an empty moved card"),Session.GetPreviewSlotMode(Event),EMCPerkVFXSlotMode::UseDefault);
    TestEqual(TEXT("Layout does not invalidate playback"),Session.GetPlaybackRevision(),Playback);
    GEditor->UndoTransaction();GEditor->UndoTransaction();GEditor->UndoTransaction();
    TestFalse(TEXT("Undo all layout operations returns clean"),Session.IsDirty());
    TestEqual(TEXT("Layout Undo also leaves the playback revision alone"),Session.GetPlaybackRevision(),Playback);
    GEditor->RedoTransaction();GEditor->RedoTransaction();GEditor->RedoTransaction();FVector2D Position;
    TestTrue(TEXT("Redo retains the same event position"),Session.GetEventCardPosition(Event,Position) && Position==FVector2D(115,230));
    Session.MarkMasterLayoutSaved();TestFalse(TEXT("Successful master checkpoint clears layout only"),Session.IsDirty());
    Session.Clear();TestFalse(TEXT("Old graph callback is refused"),Session.SetEventCardPosition(Token,Event,FVector2D(0,0),Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionSharedGroupIdentity,
    "MessControl.VFX.ActionSession.SharedGroupBothLanesIdentityUndoAndLimits",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionSharedGroupIdentity::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FMCVFXActionSession Session;FString Error;const FName Source(TEXT("EditorOnly.NutEnemyDeath")),Event(TEXT("Death"));
    if(!TestTrue(TEXT("Open the real bound source"),Session.Initialize(Source,Error)))return false;
    FMCVFXCanonicalProfileState Before;if(!MCVFXProfileBindings::ReadCanonical(Source,Before,Error)){AddError(Error);return false;}
    auto* NS=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));
    if(!TestNotNull(TEXT("Real Niagara source"),NS) || !TestNotNull(TEXT("Real sound source"),Sound))return false;
    const FGuid Token=Session.GetDraftToken();const FGuid Group=Session.CreateEventGroup(Token,Event,TEXT("Test group"),Error);
    if(!TestTrue(TEXT("Group identity created"),Group.IsValid()))return false;
    const int32 BeforeV=Session.GetLayerCount(Event);
    const FGuid Visual=Session.AddSystemToGroup(Token,Event,NS,Group,Error);
    TestTrue(TEXT("A drop creates one grouped source instance"),Visual.IsValid());
    TestEqual(TEXT("Membership belongs to the shared model"),Session.GetRecipe(Event)->Layers[Session.GetRecipe(Event)->FindNode(Visual)].GroupID,Group);
    GEditor->UndoTransaction();TestEqual(TEXT("One drop is one Undo, with no ungrouped ghost"),Session.GetLayerCount(Event),BeforeV);
    TestNotNull(TEXT("The preceding group survives Undo of the drop"),Session.GetEventGroups(Event)->Find(Group));
    GEditor->RedoTransaction();TestTrue(TEXT("Redo restores the exact visual GUID"),Session.GetRecipe(Event)->FindNode(Visual)!=INDEX_NONE);
    const FGuid Audio=Session.AddSFXLayer(Token,Event,Sound,Group,Error);
    if(!TestTrue(TEXT("Audio instance uses the same group"),Audio.IsValid()))return false;
    FMCComposerGroupLayout Frame;Frame.GroupID=Group;Frame.Position=FVector2D(250,350);
    TestTrue(TEXT("Group layout separate from runtime controls"),Session.SetEventGroupLayout(Token,Event,Group,Frame,Error));
    TestTrue(TEXT("Sound node position stored by exact layer GUID"),Session.SetLayerGraphPosition(Token,Event,EMCComposerLayerLane::SFX,Audio,FVector2D(300,450),Error));
    const int32 AudioBefore=Session.GetSFXSlot(Event)->Layers.Num();
    const FGuid Copy=Session.DuplicateEventGroup(Token,Event,Group,Error);
    if(!TestTrue(TEXT("Duplicate gets a new group GUID"),Copy.IsValid() && Copy!=Group))return false;
    TestEqual(TEXT("Both visual instances retained"),Session.GetLayerCount(Event),BeforeV+2);
    TestEqual(TEXT("Both audio instances retained"),Session.GetSFXSlot(Event)->Layers.Num(),AudioBefore+1);
    const auto* Recipe=Session.GetRecipe(Event);FGuid CopyV,CopyA;
    for(int32 I=0;I<Recipe->Layers.Num();++I)if(Recipe->Layers[I].GroupID==Copy){CopyV=Recipe->Nodes[I].ID;TestTrue(TEXT("Visual source is shared, not modified/copied"),Recipe->Layers[I].System.Get()==NS);}
    for(const auto& L:Session.GetSFXSlot(Event)->Layers)if(L.GroupID==Copy){CopyA=L.ID;TestTrue(TEXT("Audio source shared"),L.Sound.Get()==Sound);}
    TestTrue(TEXT("Both child GUIDs are remapped"),CopyV.IsValid() && CopyV!=Visual && CopyA.IsValid() && CopyA!=Audio);
    GEditor->UndoTransaction();TestNull(TEXT("One Undo removes the whole duplicated group"),Session.GetEventGroups(Event)->Find(Copy));
    TestEqual(TEXT("Both lanes reverted atomically"),Session.GetLayerCount(Event),BeforeV+1);
    TestEqual(TEXT("Audio lane reverted atomically"),Session.GetSFXSlot(Event)->Layers.Num(),AudioBefore);
    GEditor->RedoTransaction();TestTrue(TEXT("Redo retains duplicated visual GUID"),Session.GetRecipe(Event)->FindNode(CopyV)!=INDEX_NONE);
    TestTrue(TEXT("Redo retains duplicated audio GUID"),Session.GetSFXSlot(Event)->Layers.ContainsByPredicate([&](const auto& L){return L.ID==CopyA;}));
    FMCComposerGroupLayout CopyLayout;TestTrue(TEXT("Duplicate carries a distinct layout projection"),Session.GetEventGroupLayout(Event,Copy,CopyLayout) && CopyLayout.Position==Frame.Position+FVector2D(40,40));
    TestTrue(TEXT("A layer can move to another group without copying"),Session.AssignLayerToGroup(Token,Event,EMCComposerLayerLane::SFX,CopyA,Group,Error));
    TestEqual(TEXT("Move retains the number of source instances"),Session.GetSFXSlot(Event)->Layers.Num(),AudioBefore+1);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Single Undo restores membership and identity"),Session.GetSFXSlot(Event)->Layers.ContainsByPredicate([&](const auto& L){return L.ID==CopyA && L.GroupID==Copy;}));
    TestTrue(TEXT("Delete-children is explicit and affects both lanes"),Session.RemoveEventGroup(Token,Event,Copy,EMCComposerGroupRemoval::DeleteChildren,Error));
    TestTrue(TEXT("Only the duplicated visual is removed"),Session.GetRecipe(Event)->FindNode(CopyV)==INDEX_NONE && Session.GetRecipe(Event)->FindNode(Visual)!=INDEX_NONE);
    TestFalse(TEXT("Only the duplicated audio is removed"),Session.GetSFXSlot(Event)->Layers.ContainsByPredicate([&](const auto& L){return L.ID==CopyA;}));
    GEditor->UndoTransaction();TestTrue(TEXT("Delete Undo restores exact children"),Session.GetRecipe(Event)->FindNode(CopyV)!=INDEX_NONE && Session.GetSFXSlot(Event)->Layers.ContainsByPredicate([&](const auto& L){return L.ID==CopyA;}));
    TestTrue(TEXT("Mute is independent from enable"),Session.SetEventGroupAudioMuted(Token,Event,Group,true,Error));
    TestTrue(TEXT("Mute keeps visual group enabled"),Session.GetEventGroups(Event)->Find(Group)->bEnabled);
    TestTrue(TEXT("Disable affects shared group"),Session.SetEventGroupEnabled(Token,Event,Copy,false,Error));
    TestTrue(TEXT("Ungroup removes membership, not children"),Session.RemoveEventGroup(Token,Event,Group,EMCComposerGroupRemoval::UngroupChildren,Error));
    TestFalse(TEXT("Visual becomes implicit default"),Session.GetRecipe(Event)->Layers[Session.GetRecipe(Event)->FindNode(Visual)].GroupID.IsValid());
    TestTrue(TEXT("Audio becomes implicit default"),Session.GetSFXSlot(Event)->Layers.ContainsByPredicate([&](const auto& L){return L.ID==Audio && !L.GroupID.IsValid();}));
    GEditor->UndoTransaction();TestEqual(TEXT("Undo restores visual membership"),Session.GetRecipe(Event)->Layers[Session.GetRecipe(Event)->FindNode(Visual)].GroupID,Group);
    const uint64 Playback=Session.GetPlaybackRevision();
    TestTrue(TEXT("Rename group"),Session.RenameEventGroup(Token,Event,Group,TEXT("Renamed group"),Error));
    TestEqual(TEXT("Rename does not reprepare systems"),Session.GetPlaybackRevision(),Playback);
    Session.MarkMasterLayoutSaved();TestTrue(TEXT("Saving a layout baseline cannot clear runtime edits"),Session.IsRuntimeDirty());
    TestFalse(TEXT("Layout baseline itself is now saved"),Session.IsMasterLayoutDirty());
    while(Session.GetEventGroups(Event)->Groups.Num()<32)
        if(!Session.CreateEventGroup(Token,Event,TEXT("Limit test"),Error).IsValid()){AddError(Error);return false;}
    const uint64 Revision=Session.GetRevision();
    TestFalse(TEXT("33rd group refused before a transaction"),Session.CreateEventGroup(Token,Event,TEXT("Overflow"),Error).IsValid());
    TestEqual(TEXT("Rejected create keeps revision unchanged"),Session.GetRevision(),Revision);
    FMCVFXCanonicalProfileState After;TestTrue(TEXT("Canonical owner still readable"),MCVFXProfileBindings::ReadCanonical(Source,After,Error));
    TestEqual(TEXT("Authoring commands never assign the game owner"),After.OwnerDigest,Before.OwnerDigest);
    TestEqual(TEXT("Saved profile source unchanged"),After.ProfileValueDigest,Before.ProfileValueDigest);
    Session.Clear();TestFalse(TEXT("Old popup token cannot create a group"),Session.CreateEventGroup(Token,Event,TEXT("Stale"),Error).IsValid());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionGroupSoloCopy,
    "MessControl.VFX.ActionSession.SoloCopyAndLayoutPreservePreparedComposition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionGroupSoloCopy::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FMCVFXActionSession Session;FString Error;const FName Event(TEXT("Death"));
    if(!TestTrue(TEXT("Open real native source"),Session.Initialize(TEXT("EditorOnly.NutEnemyDeath"),Error)))return false;
    auto* NS=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("Real source available"),NS))return false;
    const FGuid Token=Session.GetDraftToken();const FGuid A=Session.CreateEventGroup(Token,Event,TEXT("A"),Error),B=Session.CreateEventGroup(Token,Event,TEXT("B"),Error);
    if(!TestTrue(TEXT("Two real groups"),A.IsValid() && B.IsValid()))return false;
    const FGuid VA=Session.AddSystemToGroup(Token,Event,NS,A,Error),VB=Session.AddSystemToGroup(Token,Event,NS,B,Error);
    if(!TestTrue(TEXT("Both source instances exist"),VA.IsValid() && VB.IsValid()))return false;
    TestTrue(TEXT("Set a preview-only group filter"),Session.SetPreviewSoloGroups(Token,Event,{A},Error));
    TestTrue(TEXT("Persistent B remains enabled"),Session.GetEventGroups(Event)->Find(B)->bEnabled);
    TestTrue(TEXT("Authored B layer remains enabled"),Session.GetRecipe(Event)->Layers[Session.GetRecipe(Event)->FindNode(VB)].bEnabled);
    if(!TestTrue(*FString::Printf(TEXT("Real scenario accepts filtered snapshot: %s"),*Error),Session.PlayFullAction(Error)))return false;
    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();const auto* Profile=Runner?Runner->GetPresentationOverride():nullptr;
    if(TestNotNull(TEXT("Real runner owns filtered profile"),Profile))
    {
        TestFalse(TEXT("Only temporary profile disables B"),Profile->EventGroups.FindChecked(Event).Find(B)->bEnabled);
        const auto* Slot=Profile->Slots.Find(Event);const auto* Composition=Slot?Slot->Composition.Get():nullptr;
        if(TestNotNull(TEXT("Temporary runtime composition present"),Composition))
        {
            TestTrue(TEXT("Selected group has enabled output"),Composition->Layers.ContainsByPredicate([&](const auto& L){return L.GroupID==A && L.bEnabled;}));
            TestFalse(TEXT("Other group has no preview output"),Composition->Layers.ContainsByPredicate([&](const auto& L){return L.GroupID==B && L.bEnabled;}));
        }
    }
    Session.Stop();const FString Cached=MCSessionGroupTests::CachedComposition(Session,Event);const auto Playback=Session.GetPlaybackRevision();
    TestTrue(TEXT("Native preparation produced a retained composition cache"),!Cached.IsEmpty() && Cached!=TEXT("None"));
    TestTrue(TEXT("Move visual node"),Session.SetLayerGraphPosition(Token,Event,EMCComposerLayerLane::VFX,VA,FVector2D(750,150),Error));
    TestEqual(TEXT("Move does not invalidate playback revision"),Session.GetPlaybackRevision(),Playback);
    TestEqual(TEXT("Move retains the exact prepared composition"),MCSessionGroupTests::CachedComposition(Session,Event),Cached);
    GEditor->UndoTransaction();TestEqual(TEXT("Layout Undo also retains exact prepared composition"),MCSessionGroupTests::CachedComposition(Session,Event),Cached);
    Session.ClearPreviewSolo();TestTrue(TEXT("Solo reset leaves authored B enabled"),Session.GetEventGroups(Event)->Find(B)->bEnabled);
    Session.Clear();return true;
}
#endif
