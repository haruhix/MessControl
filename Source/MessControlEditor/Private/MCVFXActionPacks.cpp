#include "MCVFXActionPacks.h"

#include "MCVFXRecipePreset.h"
#include "MCVFXProfileBinding.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXNativePresentationSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/Package.h"

namespace MCActionPacks
{
constexpr int32 MaxEvents=64,MaxDependencyPackages=4096;
bool Fail(FString& Error,const FString& Message){Error=Message;return false;}
bool Finite(FVector2D V){return FMath::IsFinite(V.X)&&FMath::IsFinite(V.Y);}
bool GamePath(const FSoftObjectPath& Path)
{
    return !Path.IsNull()&&Path.GetSubPathString().IsEmpty()&&Path.GetLongPackageName().StartsWith(TEXT("/Game/"))
        && FPackageName::IsValidLongPackageName(Path.GetLongPackageName(),false);
}
bool Identity(FName Value)
{
    const FString Text=Value.ToString();
    return !Value.IsNone()&&Text.Len()<=192&&!Text.Contains(TEXT("/"))&&!Text.Contains(TEXT("\\"))&&!Text.Contains(TEXT(":"));
}
bool SameEvents(const TArray<FName>& A,const TArray<FName>& B)
{
    if(A.Num()!=B.Num())return false;
    TSet<FName> Seen;for(FName Event:A){if(Seen.Contains(Event)||!B.Contains(Event))return false;Seen.Add(Event);}return true;
}
bool ValidateMaster(const UMCVFXRecipePreset* Master,FString& Error)
{
    if(!Master||!Master->ValidateLayout(Error))return false;
    // A UseDefault/Disabled/native/audio-only event still has a real empty
    // authoring master; it is not a playable Niagara composition by itself.
    if(!FMath::IsFinite(Master->DurationSeconds)||Master->DurationSeconds<.05f||Master->DurationSeconds>30.f
        ||!FMath::IsFinite(Master->TailSeconds)||Master->TailSeconds<0.f||Master->TailSeconds>10.f||Master->Layers.Num()>32)
        return Fail(Error,TEXT("Invalid event recipe duration, tail or layer count."));
    return !Master->Layers.ContainsByPredicate([](const auto& L){return L.bEnabled;})||Master->Validate(Error);
}
bool SavedObject(const UObject* Object,bool bGameOnly,bool bAllowEditor,FString& Error)
{
    if(!IsValid(Object)||!Object->IsAsset()||Object->HasAnyFlags(RF_Transient)||Object->GetOutermost()==GetTransientPackage()
        || Object->GetOutermost()->IsDirty()||!FPackageName::DoesPackageExist(Object->GetOutermost()->GetName()))
        return Fail(Error,TEXT("Pack references must be clean saved assets: ")+(Object?Object->GetPathName():TEXT("None")));
    if(bGameOnly&&!GamePath(FSoftObjectPath(Object)))return Fail(Error,TEXT("Pack/profile/master assets must live under /Game: ")+Object->GetPathName());
    if(!bAllowEditor&&(Object->IsEditorOnly()||Object->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly)
        || Object->GetClass()->GetOutermost()->GetName()==TEXT("/Script/MessControlEditor")))
        return Fail(Error,TEXT("A runtime pack dependency cannot be editor-only: ")+Object->GetPathName());
    return true;
}
bool FingerprintShape(const TArray<FMCVFXActionPackDependencyFingerprint>& Values,FString& Error)
{
    if(Values.IsEmpty()||Values.Num()>MaxDependencyPackages)return Fail(Error,TEXT("Pack dependency fingerprints are empty or exceed the 4096-package bound."));
    TSet<FName> Seen;
    for(const auto& Item:Values)
    {
        const FString Name=Item.PackageName.ToString();
        if(!FPackageName::IsValidLongPackageName(Name,false)||Name.StartsWith(TEXT("/Script/"))||Seen.Contains(Item.PackageName)
            || Item.ContentDigest.Len()!=32)
            return Fail(Error,TEXT("Invalid/duplicate portable package fingerprint: ")+Name);
        for(TCHAR Ch:Item.ContentDigest)if(!FChar::IsHexDigit(Ch))return Fail(Error,TEXT("Invalid package content digest."));
        Seen.Add(Item.PackageName);
    }
    return true;
}
bool PackageDigest(FName PackageName,FString& Digest,FString& Error)
{
    const FString Name=PackageName.ToString();FString Filename;
    if(!FPackageName::IsValidLongPackageName(Name,false)||!FPackageName::DoesPackageExist(Name,&Filename))
        return Fail(Error,TEXT("Referenced package is missing or unmounted: ")+Name);
    if(const UPackage* Loaded=FindPackage(nullptr,*Name);Loaded&&Loaded->IsDirty())
        return Fail(Error,TEXT("Referenced package has unsaved changes: ")+Name);
    FString Text;
    // File locations are local implementation details and are never serialized.
    const FString Stem=FPaths::ChangeExtension(Filename,TEXT(""));
    TArray<FString> Files={Filename};
    for(const TCHAR* Extension:{TEXT("uexp"),TEXT("ubulk"),TEXT("uptnl")})
    {const FString Sidecar=Stem+TEXT(".")+Extension;if(IFileManager::Get().FileExists(*Sidecar))Files.Add(Sidecar);}
    for(const FString& File:Files)
    {
        const FMD5Hash Hash=FMD5Hash::HashFile(*File);
        if(!Hash.IsValid())return Fail(Error,TEXT("Cannot read referenced package content: ")+Name);
        Text+=FPaths::GetExtension(File)+TEXT(":")+LexToString(Hash)+TEXT(";");
    }
    FTCHARToUTF8 Bytes(*Text);Digest=FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());return true;
}
bool CompareFingerprints(const TArray<FMCVFXActionPackDependencyFingerprint>& Values,TArray<FString>& Warnings,FString& Error)
{
    Warnings.Reset();if(!FingerprintShape(Values,Error))return false;
    for(const auto& Item:Values)
    {
        FString Current,Reason;
        if(!PackageDigest(Item.PackageName,Current,Reason))Warnings.Add(Reason);
        else if(Current!=Item.ContentDigest)Warnings.Add(TEXT("Shared dependency changed since this pack was saved: ")+Item.PackageName.ToString());
    }
    return true;
}
bool ValidateLayout(const FMCVFXActionLayout& Layout,const UMCPerkVFXProfile* Profile,const TArray<FName>& Events,FString& Error)
{
    if(!Finite(Layout.HubPosition)||Layout.Events.Num()>Events.Num())return Fail(Error,TEXT("Invalid action graph layout."));
    TSet<FName> SeenEvents;
    for(const auto& Event:Layout.Events)
    {
        if(!Events.Contains(Event.Event)||SeenEvents.Contains(Event.Event)||!Finite(Event.CardPosition)||Event.Groups.Num()>33||Event.SFXLayerPositions.Num()>32)
            return Fail(Error,TEXT("Invalid event graph layout: ")+Event.Event.ToString());
        SeenEvents.Add(Event.Event);TSet<FGuid> SeenGroups;int32 ExplicitGroups=0;const auto* Groups=Profile?Profile->EventGroups.Find(Event.Event):nullptr;
        for(const auto& Group:Event.Groups)
        {
            if((Group.GroupID.IsValid()&&++ExplicitGroups>32)||SeenGroups.Contains(Group.GroupID)||!Finite(Group.Position)||!Finite(Group.Size)
                ||Group.Size.X<=0||Group.Size.Y<=0||!FMath::IsFinite(Group.Color.R)||!FMath::IsFinite(Group.Color.G)
                ||!FMath::IsFinite(Group.Color.B)||!FMath::IsFinite(Group.Color.A)
                ||(Profile&&Group.GroupID.IsValid()&&(!Groups||!Groups->Contains(Group.GroupID))))
                return Fail(Error,TEXT("Layout references an invalid or absent event group: ")+Event.Event.ToString());
            SeenGroups.Add(Group.GroupID);
        }
        const auto* Slot=Profile?Profile->SFXSlots.Find(Event.Event):nullptr;
        for(const auto& Node:Event.SFXLayerPositions)
            if(!Node.Key.IsValid()||!Finite(Node.Value)||(Profile&&(!Slot||!Slot->Layers.ContainsByPredicate([&](const auto& L){return L.ID==Node.Key;}))))
                return Fail(Error,TEXT("Layout references an invalid or absent SFX layer: ")+Event.Event.ToString());
    }
    return true;
}
}

void UMCVFXActionPack::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
    Super::GetAssetRegistryTags(Context);TArray<FString> Events;for(FName Event:EventIDs)Events.Add(Event.ToString());Events.Sort();
    Context.AddTag(FAssetRegistryTag(TEXT("EventSchema"),FString::Join(Events,TEXT(",")),FAssetRegistryTag::TT_Alphabetical));
}

bool UMCVFXActionPack::ValidateStructure(FString& Error) const
{
    using namespace MCActionPacks;Error.Reset();
    if(SchemaVersion!=CurrentSchemaVersion||!PackID.IsValid()||!Identity(SourceID)||ProviderSchemaVersion<1||NativeSchemaVersion<0
        || EventIDs.IsEmpty()||EventIDs.Num()>MaxEvents||Label.IsEmpty()||Label.ToString().Len()>192||VersionLabel.Len()>96)
        return Fail(Error,TEXT("Unsupported/invalid action pack identity or schema."));
    TSet<FName> Seen;
    for(FName Event:EventIDs){if(!Identity(Event)||Seen.Contains(Event))return Fail(Error,TEXT("Pack event IDs must be exact, nonempty and unique."));Seen.Add(Event);}
    if(!GamePath(RuntimeProfile.ToSoftObjectPath())||RecipeMasters.Num()!=EventIDs.Num())
        return Fail(Error,TEXT("Pack requires a /Game runtime profile and one saved recipe master for every declared event."));
    for(const auto& Pair:RecipeMasters)if(!Seen.Contains(Pair.Key)||!GamePath(Pair.Value.ToSoftObjectPath()))
        return Fail(Error,TEXT("Pack master reference is absent, nonportable or outside its event schema."));
    return ValidateLayout(Layout,nullptr,EventIDs,Error)&&FingerprintShape(Dependencies,Error);
}

bool MCVFXActionPacks::Validate(const UMCVFXActionPack* Pack,FString& Error,bool bRequireSavedManifest)
{
    using namespace MCActionPacks;Error.Reset();if(!Pack||!Pack->ValidateStructure(Error))return false;
    if(bRequireSavedManifest&&!SavedObject(Pack,true,true,Error))return false;
    const auto* Profile=Pack->RuntimeProfile.LoadSynchronous();
    if(!SavedObject(Profile,true,false,Error)||!MCVFXProfileBindings::ValidateSavedProfileResources(Profile,Pack->EventIDs,Error))return false;
    if(!ValidateLayout(Pack->Layout,Profile,Pack->EventIDs,Error))return false;
    if(const auto* Native=Profile->NativePresentation.LoadSynchronous())
    {
        if(Native->SchemaVersion!=Pack->NativeSchemaVersion||!Native->ValidateForSource(Pack->SourceID,Pack->EventIDs,Error))
            return Fail(Error,TEXT("Pack native source/schema does not match its saved profile. ")+Error);
    }
    else if(Pack->NativeSchemaVersion!=0)return Fail(Error,TEXT("Pack declares a native override schema but its profile has no native override."));
    for(const auto& Pair:Pack->RecipeMasters)
    {
        const auto* Master=Pair.Value.LoadSynchronous();
        if(!SavedObject(Master,true,true,Error)||!ValidateMaster(Master,Error))return false;
        if(const auto* Slot=Profile->Slots.Find(Pair.Key);Slot&&Slot->Mode==EMCPerkVFXSlotMode::Composition)
        {
            const auto* Runtime=Slot->Composition.Get();
            if(!Runtime||Runtime->DurationSeconds!=Master->DurationSeconds||Runtime->TailSeconds!=Master->TailSeconds||Runtime->Layers.Num()!=Master->Layers.Num())
                return Fail(Error,TEXT("Event recipe master differs from its actual runtime composition: ")+Pair.Key.ToString());
            for(int32 Index=0;Index<Master->Layers.Num();++Index)
                if(!FMCVFXCompositionLayer::StaticStruct()->CompareScriptStruct(&Master->Layers[Index],&Runtime->Layers[Index],0))
                    return Fail(Error,TEXT("Event recipe master layer differs from its actual runtime composition, including disabled layers: ")+Pair.Key.ToString());
        }
        const auto* Groups=Profile->EventGroups.Find(Pair.Key);
        for(const auto& Layer:Master->Layers)
        {
            if(Layer.GroupID.IsValid()&&(!Groups||!Groups->Contains(Layer.GroupID)))return Fail(Error,TEXT("Recipe master refers to a group absent from its event profile: ")+Pair.Key.ToString());
            if(!Layer.bEnabled&&Layer.System.IsNull())continue;
            const auto* System=Layer.System.LoadSynchronous();
            if(!SavedObject(System,false,false,Error)||!UMCVFXComposition::ValidateParameters(Layer,System,Error))return false;
        }
    }
    TSet<FName> Fingerprinted;for(const auto& Item:Pack->Dependencies)Fingerprinted.Add(Item.PackageName);
    if(!Fingerprinted.Contains(FName(*Profile->GetOutermost()->GetName())))return Fail(Error,TEXT("Pack fingerprint omits its runtime profile."));
    for(const auto& Pair:Pack->RecipeMasters)if(!Fingerprinted.Contains(FName(*Pair.Value.GetLongPackageName())))
        return Fail(Error,TEXT("Pack fingerprint omits an event recipe master."));
    return true;
}

bool MCVFXActionPacks::CaptureDependencies(UMCPerkVFXProfile* Profile,const TMap<FName,UMCVFXRecipePreset*>& Masters,
    TArray<FMCVFXActionPackDependencyFingerprint>& Out,FString& Error,FName SourceID)
{
    using namespace MCActionPacks;Error.Reset();if(!SavedObject(Profile,true,false,Error))return false;
    IAssetRegistry& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FName> Pending={FName(*Profile->GetOutermost()->GetName())};TArray<FString> RootFiles;
    for(const auto& Pair:Masters){if(!SavedObject(Pair.Value,true,true,Error))return false;Pending.AddUnique(FName(*Pair.Value->GetOutermost()->GetName()));}
    // Native palette/direct-sound dependencies are not necessarily referenced
    // by custom SFXSlots. A/B packs still need to detect shared-source edits.
    if(const auto* Contract=MCVFXSourceContracts::Find(SourceID))for(const auto& Pair:Contract->OriginalAudio)
        if(!Pair.Value.Resource.IsNull())Pending.AddUnique(FName(*Pair.Value.Resource.GetLongPackageName()));
    for(FName Name:Pending){FString File;if(FPackageName::DoesPackageExist(Name.ToString(),&File))RootFiles.Add(File);}
    Registry.ScanFilesSynchronous(RootFiles,true);
    TSet<FName> Seen;TArray<FMCVFXActionPackDependencyFingerprint> Candidate;
    for(int32 Index=0;Index<Pending.Num();++Index)
    {
        const FName Name=Pending[Index];if(Seen.Contains(Name)||Name.ToString().StartsWith(TEXT("/Script/")))continue;
        if(Seen.Num()>=MaxDependencyPackages)return Fail(Error,TEXT("Selected pack dependency closure exceeds 4096 packages; no global content scan is performed."));
        Seen.Add(Name);FMCVFXActionPackDependencyFingerprint Item;Item.PackageName=Name;
        if(!PackageDigest(Name,Item.ContentDigest,Error))return false;Candidate.Add(MoveTemp(Item));
        TArray<FName> Children;
        if(!Registry.GetDependencies(Name,Children,UE::AssetRegistry::EDependencyCategory::Package))
            return Fail(Error,TEXT("Saved package dependency evidence is unavailable: ")+Name.ToString());
        for(FName Child:Children)if(!Seen.Contains(Child)&&!Child.ToString().StartsWith(TEXT("/Script/")))Pending.AddUnique(Child);
    }
    Candidate.Sort([](const auto& A,const auto& B){return A.PackageName.LexicalLess(B.PackageName);});Out=MoveTemp(Candidate);return true;
}

bool MCVFXActionPacks::CheckDependencies(const UMCVFXActionPack* Pack,TArray<FString>& OutWarnings,FString& Error)
{
    Error.Reset();if(!Pack)return MCActionPacks::Fail(Error,TEXT("No action pack selected."));
    return MCActionPacks::CompareFingerprints(Pack->Dependencies,OutWarnings,Error);
}

bool MCVFXActionPacks::SaveNew(const FMCVFXActionPackSaveRequest& Request,FMCVFXActionPackSaveResult& Out,FString& Error)
{
    using namespace MCActionPacks;Error.Reset();Out=FMCVFXActionPackSaveResult();
    if(GEditor&&GEditor->PlayWorld)return Fail(Error,TEXT("Stop PIE before saving an action pack."));
    const FString& Path=Request.ManifestPackagePath;
    if(!Path.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Path,false)
        ||FPackageName::DoesPackageExist(Path)||FindPackage(nullptr,*Path))return Fail(Error,TEXT("Save Pack requires a new /Game package name; existing versions are never overwritten."));
    if(!Request.ExpectedDependencies.IsEmpty())
    {
        TArray<FString> Changed;if(!CompareFingerprints(Request.ExpectedDependencies,Changed,Error))return false;
        if(!Changed.IsEmpty())return Fail(Error,TEXT("Saved source baseline changed before Save Pack: ")+FString::Join(Changed,TEXT("\n")));
    }
    TStrongObjectPtr<UMCVFXActionPack> Candidate(NewObject<UMCVFXActionPack>());
    Candidate->PackID=FGuid::NewGuid();Candidate->SourceID=Request.SourceID;Candidate->EventIDs=Request.EventIDs;
    Candidate->ProviderSchemaVersion=Request.ProviderSchemaVersion;Candidate->NativeSchemaVersion=Request.NativeSchemaVersion;
    Candidate->Label=Request.Label;Candidate->VersionLabel=Request.VersionLabel;Candidate->CreatedUTC=FDateTime::UtcNow();
    Candidate->RuntimeProfile=Request.RuntimeProfile;Candidate->Layout=Request.Layout;
    for(const auto& Pair:Request.RecipeMasters)Candidate->RecipeMasters.Add(Pair.Key,Pair.Value);
    if(!CaptureDependencies(Request.RuntimeProfile,Request.RecipeMasters,Candidate->Dependencies,Error,Request.SourceID)||!Validate(Candidate.Get(),Error,false))return false;
    TArray<FString> Changed;
    if(!CompareFingerprints(Candidate->Dependencies,Changed,Error))return false;
    if(!Changed.IsEmpty())return Fail(Error,TEXT("Resources changed while validating Save Pack; no manifest was created: ")+FString::Join(Changed,TEXT("\n")));
    UPackage* Package=CreatePackage(*Path);Package->SetPackageFlags(PKG_EditorOnly);
    auto* Saved=DuplicateObject<UMCVFXActionPack>(Candidate.Get(),Package,*FPackageName::GetLongPackageAssetName(Path));
    Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
    Out.Pack.Reset(Saved);Out.CreatedAssets.Add(FSoftObjectPath(Saved));
    FAssetRegistryModule::AssetCreated(Saved);Package->MarkPackageDirty();
    if(!UEditorLoadingAndSavingUtils::SavePackages({Package},true))return Fail(Error,TEXT("Manifest save failed; its new intermediate asset was retained: ")+Path);
    if(!CompareFingerprints(Saved->Dependencies,Changed,Error))return false;
    if(!Changed.IsEmpty())return Fail(Error,TEXT("Resources changed during manifest save; the intermediate manifest was retained for review: ")+FString::Join(Changed,TEXT("\n")));
    Out.bSaved=true;return true;
}

bool MCVFXActionPacks::Load(const UMCVFXActionPack* Pack,FName ExpectedSource,const TArray<FName>& ExpectedEvents,
    int32 ExpectedProviderSchema,int32 ExpectedNativeSchema,FMCVFXActionPackLoaded& Out,FString& Error,bool bAllowChangedDependencies)
{
    using namespace MCActionPacks;Error.Reset();
    if(!Pack||Pack->SourceID!=ExpectedSource||!SameEvents(Pack->EventIDs,ExpectedEvents)||Pack->ProviderSchemaVersion!=ExpectedProviderSchema
        ||(Pack->NativeSchemaVersion!=0&&Pack->NativeSchemaVersion!=ExpectedNativeSchema))
        return Fail(Error,TEXT("Pack SourceID or exact event/provider/native schema is incompatible with this action."));
    FMCVFXActionPackLoaded Candidate;
    if(!CheckDependencies(Pack,Candidate.DependencyWarnings,Error))return false;
    if(!bAllowChangedDependencies&&!Candidate.DependencyWarnings.IsEmpty())
        return Fail(Error,TEXT("This saved pack uses changed shared resources. Review before explicitly loading: ")+FString::Join(Candidate.DependencyWarnings,TEXT("\n")));
    if(!Validate(Pack,Error))return false;
    Candidate.Pack.Reset(const_cast<UMCVFXActionPack*>(Pack));Candidate.RuntimeProfile.Reset(Pack->RuntimeProfile.Get());Candidate.Layout=Pack->Layout;
    for(const auto& Pair:Pack->RecipeMasters)Candidate.RecipeMasters.Add(Pair.Key,TStrongObjectPtr<UMCVFXRecipePreset>(Pair.Value.Get()));
    Out=MoveTemp(Candidate);return true;
}
