#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "MCVFXActionPacks.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXNativePresentationSettings.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"

namespace MCSessionPackEdgeTests
{
const FName Source(TEXT("Boss.NutKnight.Shield")),Hit(TEXT("Hit")),Break(TEXT("Break")),Warning(TEXT("Warning"));

FString Digest(const FSoftObjectPath& Asset)
{
    FString File;if(!FPackageName::DoesPackageExist(Asset.GetLongPackageName(),&File))return FString();
    const auto Hash=FMD5Hash::HashFile(*File);return Hash.IsValid()?LexToString(Hash):FString();
}
template<class T>T* NewFixtureAsset(const FString& PackageName)
{
    auto* Package=CreatePackage(*PackageName);
    auto* Object=NewObject<T>(Package,*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone|RF_Transactional);
    FAssetRegistryModule::AssetCreated(Object);Package->MarkPackageDirty();return Object;
}
bool Save(UObject* Object,FString& Error)
{
    Object->MarkPackageDirty();
    if(UEditorLoadingAndSavingUtils::SavePackages({Object->GetOutermost()},true))return true;
    Error=TEXT("Could not save isolated fixture package: ")+Object->GetOutermost()->GetName();return false;
}

/** Only new fixture assets are written. The real source registration is restored even on early failure. */
struct FShieldFixture
{
    FAutomationTestBase& Test;
    FMCVFXCanonicalProfileState Original;
    FString OriginalOwnerDigest,Root;
    UMCVFXPresentationLibrary* Owner=nullptr;
    UMCPerkVFXProfile* InitialProfile=nullptr;
    TArray<FName> Allowed;
    bool bCaptured=false,bRegistered=false;
    explicit FShieldFixture(FAutomationTestBase& InTest):Test(InTest){}
    ~FShieldFixture()
    {
        if(bRegistered)MCVFXProfileBindings::RegisterLibrarySource(Original.SourceID,Original.OwnerAsset,Original.OwnerKey,Original.AllowedEvents);
        if(bCaptured&&Digest(Original.OwnerAsset)!=OriginalOwnerDigest)
            Test.AddError(TEXT("Session pack edge fixture changed the real mouth presentation owner package."));
    }
    bool Initialize(const TArray<FName>& Events,FString& Error,const FMCPerkVFXSlot* DormantWarning=nullptr)
    {
        if(!MCVFXProfileBindings::ReadCanonical(Source,Original,Error))return false;
        OriginalOwnerDigest=Digest(Original.OwnerAsset);
        if(OriginalOwnerDigest.IsEmpty()){Error=TEXT("Could not fingerprint the real mouth presentation owner before isolation.");return false;}
        bCaptured=true;Allowed=Events;
        Root=TEXT("/Game/Tests/VFXPractice/Profiles/GenericBindingQA/SessionPackEdges_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        InitialProfile=NewFixtureAsset<UMCPerkVFXProfile>(Root/TEXT("DA_InitialProfile"));
        if(DormantWarning)InitialProfile->Slots.Add(Warning,*DormantWarning);
        if(!InitialProfile->ValidateForEvents(Allowed,Error)||!Save(InitialProfile,Error))return false;
        Owner=NewFixtureAsset<UMCVFXPresentationLibrary>(Root/TEXT("DA_IsolatedOwner"));Owner->Profiles.Add(Source,InitialProfile);
        if(!Save(Owner,Error))return false;
        MCVFXProfileBindings::RegisterLibrarySource(Source,FSoftObjectPath(Owner),Source,Allowed);bRegistered=true;return true;
    }
    bool Read(FMCVFXCanonicalProfileState& Out,FString& Error) const
    {return MCVFXProfileBindings::ReadCanonical(Source,Out,Error);}
    bool OriginalUnchanged()const{return Digest(Original.OwnerAsset)==OriginalOwnerDigest;}
};

bool SameSlot(const FMCPerkVFXSlot* A,const FMCPerkVFXSlot& B)
{return A&&FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(A,&B,0);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionPackStaleAssignment,
    "MessControl.VFX.ActionPacks.SessionSaveSurvivesOwnerOnlyReassignmentButApplyRefusesStale",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionPackStaleAssignment::RunTest(const FString&)
{
    using namespace MCSessionPackEdgeTests;if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FShieldFixture Fixture(*this);FString Error;
    if(!TestTrue(TEXT("Create isolated Shield owner before opening the real provider"),Fixture.Initialize({Hit,Break},Error))) {AddError(Error);return false;}
    FMCVFXActionSession Session;
    if(!TestTrue(TEXT("Open real Shield provider with isolated saved assignment"),Session.Initialize(Source,Error))) {AddError(Error);return false;}
    FMCVFXCanonicalProfileState Captured;if(!TestTrue(TEXT("Capture pre-edit canonical assignment"),Fixture.Read(Captured,Error)))return false;
    const FString InitialHash=Digest(FSoftObjectPath(Fixture.InitialProfile));
    const FGuid Group=Session.CreateEventGroup(Session.GetDraftToken(),Hit,TEXT("Private draft group"),Error);
    if(!TestTrue(TEXT("Author actual runtime draft data before the other author's assignment"),Group.IsValid())) {AddError(Error);return false;}
    auto* OtherProfile=NewFixtureAsset<UMCPerkVFXProfile>(Fixture.Root/TEXT("DA_OtherAuthorsProfile"));
    if(!TestTrue(TEXT("Save a separate author profile"),Save(OtherProfile,Error)))return false;
    Fixture.Owner->Profiles.Add(Source,OtherProfile);
    if(!TestTrue(TEXT("Reassign only the isolated owner after the draft was captured"),Save(Fixture.Owner,Error)))return false;
    const FString OwnerAfterReassignment=Digest(FSoftObjectPath(Fixture.Owner));
    FString StaleError;TestFalse(TEXT("The assignment guard really is stale"),MCVFXProfileBindings::ValidateUnchanged(Captured,StaleError));
    TestEqual(TEXT("Owner reassignment did not edit the draft's original profile"),Digest(FSoftObjectPath(Fixture.InitialProfile)),InitialHash);
    auto* Pack=Session.SavePackNew(FText::FromString(TEXT("Own work after external assignment")),Error,Fixture.Root/TEXT("Packs"));
    if(!TestNotNull(TEXT("Standalone SavePack remains available for one's own stale-assignment draft"),Pack)) {AddError(Error);return false;}
    const auto* SavedProfile=Pack->RuntimeProfile.LoadSynchronous();
    if(!TestNotNull(TEXT("Saved pack retains its complete runtime draft"),SavedProfile))return false;
    const auto* SavedGroups=SavedProfile->EventGroups.Find(Hit);
    TestTrue(TEXT("Private draft group is actually saved rather than discarded"),SavedGroups&&SavedGroups->Find(Group));
    TestFalse(TEXT("Successful standalone pack save clears only its own unsaved-pack guard"),Session.HasUnsavedPackChanges());
    FMCVFXCanonicalProfileState AfterSave;if(!TestTrue(TEXT("Read actual isolated assignment after SavePack"),Fixture.Read(AfterSave,Error)))return false;
    TestEqual(TEXT("SavePack preserves the other author's canonical assignment"),AfterSave.Profile.ToSoftObjectPath(),FSoftObjectPath(OtherProfile));
    TestEqual(TEXT("SavePack never rewrites the owner package"),Digest(FSoftObjectPath(Fixture.Owner)),OwnerAfterReassignment);
    TestFalse(TEXT("Applying that pack still rejects the stale canonical baseline"),Session.ApplySavedPack(Pack,Error));
    TestFalse(TEXT("Stale Apply provides a reason"),Error.IsEmpty());
    FMCVFXCanonicalProfileState AfterApply;if(!TestTrue(TEXT("Read isolated assignment after rejected Apply"),Fixture.Read(AfterApply,Error)))return false;
    TestEqual(TEXT("Rejected Apply retains the other author's saved profile"),AfterApply.Profile.ToSoftObjectPath(),FSoftObjectPath(OtherProfile));
    TestEqual(TEXT("Rejected Apply leaves owner bytes unchanged"),Digest(FSoftObjectPath(Fixture.Owner)),OwnerAfterReassignment);
    TestTrue(TEXT("Real mouth owner bytes remain unchanged"),Fixture.OriginalUnchanged());Session.Clear();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionPackLayoutReuse,
    "MessControl.VFX.ActionPacks.SessionLayoutOnlyVersionsReuseRuntimeAndPreserveAbsentDefaults",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionPackLayoutReuse::RunTest(const FString&)
{
    using namespace MCSessionPackEdgeTests;if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FShieldFixture Fixture(*this);FString Error;
    if(!TestTrue(TEXT("Create a clean isolated runtime profile with genuinely absent slots"),Fixture.Initialize({Hit,Break},Error))) {AddError(Error);return false;}
    const FString InitialHash=Digest(FSoftObjectPath(Fixture.InitialProfile)),OwnerHash=Digest(FSoftObjectPath(Fixture.Owner));
    FMCVFXActionSession Session;if(!TestTrue(TEXT("Open real native Shield context"),Session.Initialize(Source,Error))) {AddError(Error);return false;}
    if(!TestTrue(TEXT("Move only the event card"),Session.SetEventCardPosition(Session.GetDraftToken(),Hit,FVector2D(120,240),Error)))return false;
    TestFalse(TEXT("First card move is not a runtime edit"),Session.IsRuntimeDirty());
    auto* A=Session.SavePackNew(FText::FromString(TEXT("Layout A")),Error,Fixture.Root/TEXT("Packs"));
    if(!TestNotNull(TEXT("First layout version saves"),A)) {AddError(Error);return false;}
    TestEqual(TEXT("First layout save reuses the already-saved canonical runtime profile"),A->RuntimeProfile.ToSoftObjectPath(),FSoftObjectPath(Fixture.InitialProfile));
    const FString AHash=Digest(FSoftObjectPath(A));
    if(!TestTrue(TEXT("Move only the action hub for a second version"),Session.SetActionHubPosition(Session.GetDraftToken(),FVector2D(500,200),Error)))return false;
    TestTrue(TEXT("Layout B has an unsaved pack change"),Session.HasUnsavedPackChanges());
    TestFalse(TEXT("Second layout move still does not invent runtime data"),Session.IsRuntimeDirty());
    auto* B=Session.SavePackNew(FText::FromString(TEXT("Layout B")),Error,Fixture.Root/TEXT("Packs"));
    if(!TestNotNull(TEXT("Second layout-only version saves"),B)) {AddError(Error);return false;}
    TestTrue(TEXT("Saved A/B manifests have separate immutable identities"),A->PackID!=B->PackID&&A!=B);
    TestEqual(TEXT("Second saved version reuses the same clean runtime profile"),B->RuntimeProfile.ToSoftObjectPath(),A->RuntimeProfile.ToSoftObjectPath());
    TestEqual(TEXT("Saving B does not rewrite A"),Digest(FSoftObjectPath(A)),AHash);
    TestEqual(TEXT("Runtime source bytes are unchanged by both layout versions"),Digest(FSoftObjectPath(Fixture.InitialProfile)),InitialHash);
    TestEqual(TEXT("Absent native-default VFX slots are not inflated into explicit entries"),Fixture.InitialProfile->Slots.Num(),0);
    TestEqual(TEXT("Absent SFX slots are preserved"),Fixture.InitialProfile->SFXSlots.Num(),0);
    TestEqual(TEXT("Absent runtime groups are preserved"),Fixture.InitialProfile->EventGroups.Num(),0);
    TestTrue(TEXT("Native default draft does not create a native override asset"),Fixture.InitialProfile->NativePresentation.IsNull()&&A->NativeSchemaVersion==0&&B->NativeSchemaVersion==0);
    TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*Fixture.Root),Assets,true);
    int32 Profiles=0,RuntimeCompositions=0,NativeProfiles=0;
    for(const auto& Asset:Assets)
    {
        Profiles+=Asset.AssetClassPath==UMCPerkVFXProfile::StaticClass()->GetClassPathName();
        RuntimeCompositions+=Asset.AssetClassPath==UMCVFXComposition::StaticClass()->GetClassPathName();
        NativeProfiles+=Asset.AssetClassPath==UMCVFXNativePresentationSettings::StaticClass()->GetClassPathName();
    }
    TestEqual(TEXT("Only the one initial runtime profile exists under this isolated root"),Profiles,1);
    TestEqual(TEXT("Layout versions create no runtime Niagara compositions"),RuntimeCompositions,0);
    TestEqual(TEXT("Layout versions create no native-art runtime assets"),NativeProfiles,0);
    if(!TestTrue(TEXT("Load layout A without changing gameplay"),Session.LoadPack(A,Error,true))) {AddError(Error);return false;}
    FVector2D Card;TestTrue(TEXT("Loaded event position survives the saved snapshot"),Session.GetEventCardPosition(Hit,Card)&&Card==FVector2D(120,240));
    TestEqual(TEXT("Loaded native default policy remains UseDefault"),Session.GetPreviewSlotMode(Hit),EMCPerkVFXSlotMode::UseDefault);
    TestFalse(TEXT("Loaded A is clean relative to its own saved pack"),Session.HasUnsavedPackChanges());
    TestEqual(TEXT("Save/load never rewrites the isolated canonical owner"),Digest(FSoftObjectPath(Fixture.Owner)),OwnerHash);
    TestTrue(TEXT("Real mouth owner remains untouched"),Fixture.OriginalUnchanged());Session.Clear();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSessionPackExtraAllowedEvent,
    "MessControl.VFX.ActionPacks.SessionPreservesExtraAllowedWarningAndDormantDefaultControls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSessionPackExtraAllowedEvent::RunTest(const FString&)
{
    using namespace MCSessionPackEdgeTests;if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FMCPerkVFXSlot Dormant;Dormant.Mode=EMCPerkVFXSlotMode::UseDefault;
    Dormant.System=FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    Dormant.Transform=FTransform(FRotator(15,25,35),FVector(23,45,67),FVector(1.1,1.2,1.3));
    FMCVFXFloatOverride F;F.Name=TEXT("User.DormantAmount");F.bOverride=true;F.Value=3.25f;Dormant.Floats.Add(F);
    FMCVFXColorOverride C;C.Name=TEXT("User.DormantTint");C.bOverride=false;C.Value=FLinearColor(.2f,.3f,.4f,.5f);Dormant.Colors.Add(C);
    FMCVFXVectorOverride V;V.Name=TEXT("User.DormantVector");V.bOverride=true;V.Value=FVector(8,9,10);Dormant.Vectors.Add(V);
    FMCVFXBoolOverride B;B.Name=TEXT("User.DormantBool");B.bOverride=true;B.Value=true;Dormant.Bools.Add(B);
    FShieldFixture Fixture(*this);FString Error;
    if(!TestTrue(TEXT("Isolate canonical schema Hit/Break/Warning with dormant authored Warning controls"),Fixture.Initialize({Hit,Break,Warning},Error,&Dormant))) {AddError(Error);return false;}
    const FString InitialHash=Digest(FSoftObjectPath(Fixture.InitialProfile)),OwnerHash=Digest(FSoftObjectPath(Fixture.Owner));
    FMCVFXActionSession Session;if(!TestTrue(TEXT("Open real Shield provider whose visible events remain Hit/Break"),Session.Initialize(Source,Error))) {AddError(Error);return false;}
    TestTrue(TEXT("Provider has two visible stages while canonical contract keeps the third"),Session.GetActionStages().Num()==2&&!Session.GetActionStages().Contains(Warning));
    const FGuid Group=Session.CreateEventGroup(Session.GetDraftToken(),Hit,TEXT("Before load"),Error);
    if(!TestTrue(TEXT("Force an actual runtime save rather than merely reusing the original profile"),Group.IsValid()))return false;
    auto* A=Session.SavePackNew(FText::FromString(TEXT("Extra Warning A")),Error,Fixture.Root/TEXT("Packs"));
    if(!TestNotNull(TEXT("Save complete canonical schema despite a smaller preview provider"),A)) {AddError(Error);return false;}
    TestTrue(TEXT("Saved manifest retains all three actual allowed events"),A->EventIDs.Num()==3&&A->EventIDs.Contains(Warning)&&A->RecipeMasters.Contains(Warning));
    const auto* AProfile=A->RuntimeProfile.LoadSynchronous();if(!TestNotNull(TEXT("Saved full-schema profile exists"),AProfile))return false;
    TestTrue(TEXT("Runtime edit creates a new profile rather than hiding behind profile reuse"),AProfile!=Fixture.InitialProfile);
    TestTrue(TEXT("Dormant UseDefault system/transform/all override arrays are preserved exactly"),SameSlot(AProfile->Slots.Find(Warning),Dormant));
    TestTrue(TEXT("Absent Hit/Break slots are still absent"),!AProfile->Slots.Contains(Hit)&&!AProfile->Slots.Contains(Break));
    const auto* ExtraMaster=A->RecipeMasters.FindChecked(Warning).LoadSynchronous();
    TestTrue(TEXT("Dormant Warning source is not silently activated as a replacement composition"),ExtraMaster&&ExtraMaster->Layers.IsEmpty());
    if(!TestTrue(TEXT("Load pack with canonical allowed events, not only preview stages"),Session.LoadPack(A,Error,true))) {AddError(Error);return false;}
    TestTrue(TEXT("Load does not invent a third preview stage"),Session.GetActionStages().Num()==2&&!Session.GetActionStages().Contains(Warning));
    if(!TestTrue(TEXT("Edit after Load to force a second runtime profile from the loaded working snapshot"),Session.RenameEventGroup(Session.GetDraftToken(),Hit,Group,TEXT("After load"),Error)))return false;
    auto* AfterLoad=Session.SavePackNew(FText::FromString(TEXT("Extra Warning after load")),Error,Fixture.Root/TEXT("Packs"));
    if(!TestNotNull(TEXT("Save loaded full-schema working snapshot"),AfterLoad)) {AddError(Error);return false;}
    const auto* AfterProfile=AfterLoad->RuntimeProfile.LoadSynchronous();if(!TestNotNull(TEXT("Second complete runtime profile exists"),AfterProfile))return false;
    TestTrue(TEXT("Post-load runtime edit prevents reuse and proves the loaded Warning data survived"),AfterProfile!=AProfile);
    TestTrue(TEXT("Warning controls survive Save/Load/edit/Save without normalization"),SameSlot(AfterProfile->Slots.Find(Warning),Dormant));
    TestTrue(TEXT("Extra allowed event stays in the next manifest and its real masters"),AfterLoad->EventIDs.Contains(Warning)&&AfterLoad->RecipeMasters.Contains(Warning));
    TestEqual(TEXT("Initial authored profile is never rewritten"),Digest(FSoftObjectPath(Fixture.InitialProfile)),InitialHash);
    TestEqual(TEXT("Standalone saved versions never change canonical owner bytes"),Digest(FSoftObjectPath(Fixture.Owner)),OwnerHash);
    TestTrue(TEXT("The original mouth owner remains unchanged"),Fixture.OriginalUnchanged());Session.Clear();return true;
}
#endif
