#include "MCVFXProfileBinding.h"

#include "MCPerkPreviewAdapter.h"
#include "MCPerkTypes.h"
#include "MCPerkPresentationEvents.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXNativePresentationSettings.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Curves/CurveFloat.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "MCPreloadManifest.h"
#include "MCEventSFXReadiness.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
struct FLibrarySource
{
    FSoftObjectPath Library;
    FName Key;
    TArray<FName> Events;
};

TMap<FName,FLibrarySource>& LibrarySources()
{
    static TMap<FName,FLibrarySource> Sources=[] {
        TMap<FName,FLibrarySource> Result;
        Result.Add(TEXT("EditorOnly.NutEnemyDeath"),{UMCVFXPresentationLibrary::MouthLibraryPath(),TEXT("EditorOnly.NutEnemyDeath"),{TEXT("Death"),TEXT("DeathDust")}});
        return Result;
    }();
    return Sources;
}

bool Fail(FString& Error,const FString& Message){Error=Message;return false;}

FString PackageDigest(const UObject* Object)
{
    if(!Object)return TEXT("None");
    const FString File=FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    return FPaths::FileExists(File)?LexToString(FMD5Hash::HashFile(*File)):TEXT("Unsaved");
}

FString ProfileValues(const UMCPerkVFXProfile* Profile)
{
    if(!Profile)return TEXT("None");
    TArray<FName> Names;Profile->Slots.GetKeys(Names);Names.Sort(FNameLexicalLess());
    FString Text;
    Text+=TEXT("Native=")+Profile->NativePresentation.ToString()+TEXT(";");
    if(const auto* Native=Profile->NativePresentation.LoadSynchronous())
    {
        // This is an explicit editor read, before a scenario starts. Recording
        // the digest before loading curve keys would create a false conflict
        // on the second preview even when the saved artwork did not change.
        auto ResolveCurves=[](const TArray<FMCVFXNativePartSettings>& Parts)
        {for(const auto& Part:Parts){Part.OpacityCurve.LoadSynchronous();Part.ExpansionCurve.LoadSynchronous();}};
        for(const auto& Pair:Native->Events)ResolveCurves(Pair.Value.Parts);ResolveCurves(Native->PersistentParts);
        Text+=LexToString(Native->ComputeSettingsHash());
    }
    for(FName Name:Names) {
        FString Value;FMCPerkVFXSlot::StaticStruct()->ExportText(Value,&Profile->Slots.FindChecked(Name),nullptr,nullptr,PPF_None,nullptr);
        Text+=Name.ToString()+TEXT("=")+Value+TEXT(";");
    }
    Names.Reset();Profile->SFXSlots.GetKeys(Names);Names.Sort(FNameLexicalLess());
    for(FName Name:Names){FString Value;FMCEventSFXSlot::StaticStruct()->ExportText(Value,&Profile->SFXSlots.FindChecked(Name),nullptr,nullptr,PPF_None,nullptr);Text+=TEXT("Audio/")+Name.ToString()+TEXT("=")+Value+TEXT(";");}
    Names.Reset();Profile->EventGroups.GetKeys(Names);Names.Sort(FNameLexicalLess());
    for(FName Name:Names){FString Value;FMCEventPresentationGroupSet::StaticStruct()->ExportText(Value,&Profile->EventGroups.FindChecked(Name),nullptr,nullptr,PPF_None,nullptr);Text+=TEXT("Groups/")+Name.ToString()+TEXT("=")+Value+TEXT(";");}
    FTCHARToUTF8 Bytes(*Text);return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());
}

bool SavedRuntime(const UObject* Object,const TCHAR* Label,FString& Error)
{
    if(!IsValid(Object) || Object->HasAnyFlags(RF_Transient) || Object->IsEditorOnly()
        || Object->GetClass()->GetOutermost()->GetName()==TEXT("/Script/MessControlEditor")
        || Object->GetOutermost()==GetTransientPackage()
        || Object->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly)
        || !FPackageName::IsValidLongPackageName(Object->GetOutermost()->GetName(),false)
        || !FPackageName::DoesPackageExist(Object->GetOutermost()->GetName()) || Object->GetOutermost()->IsDirty())
        return Fail(Error,FString::Printf(TEXT("%s должен быть сохранённым runtime-ассетом без несохранённых изменений."),Label));
    return true;
}

UDataTable* PerkTable()
{
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    return Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()?Table:nullptr;
}

bool ReadState(FName SourceID,FMCVFXCanonicalProfileState& Out,FString& Error)
{
    Out=FMCVFXCanonicalProfileState();Out.SourceID=SourceID;
    UObject* Owner=nullptr;
    if(const auto* Source=LibrarySources().Find(SourceID)) {
        auto* Library=Cast<UMCVFXPresentationLibrary>(Source->Library.TryLoad());
        if(!SavedRuntime(Library,TEXT("Библиотека представления"),Error))return false;
        Owner=Library;Out.OwnerAsset=FSoftObjectPath(Library);Out.OwnerKey=Source->Key;Out.AllowedEvents=Source->Events;
        if(const auto* Profile=Library->Profiles.Find(Source->Key)){Out.Profile=*Profile;Out.bOwnerKeyPresent=true;}
    }
    else {
        const auto* Adapter=MCPerkPreviewAdapters::Find(SourceID);
        if(Adapter && Adapter->Kind==EMCPerkPreviewAdapterKind::Context && Adapter->SourceRowID.IsNone())
            return Fail(Error,TEXT("Для контекста не зарегистрирован канонический источник профиля: ")+SourceID.ToString());
        const FName RowID=Adapter && !Adapter->SourceRowID.IsNone()?Adapter->SourceRowID:SourceID;
        auto* Table=PerkTable();
        if(!SavedRuntime(Table,TEXT("DT_Perks"),Error))return false;
        const auto* Row=Table->FindRow<FMCPerkDefinition>(RowID,TEXT("Read canonical VFX binding"),false);
        if(!Row || !Row->bEnabled)return Fail(Error,TEXT("Каноническая строка действия недоступна: ")+RowID.ToString());
        Owner=Table;Out.OwnerAsset=FSoftObjectPath(Table);Out.OwnerKey=RowID;Out.CanonicalRowID=RowID;Out.bOwnerKeyPresent=true;
        Out.Profile=Row->VFXProfile;
        // Existing active profiles may retain an unused Warning slot. Preserve
        // the complete five-event serialized contract instead of truncating it
        // to the subset currently emitted by one preview adapter.
        Out.AllowedEvents=Row->Kind==EMCPerkKind::Passive
            ?MCPassivePresentation::GetAllowedEvents(RowID):UMCPerkVFXProfile::GetSupportedStages();
    }
    if(Out.AllowedEvents.IsEmpty())return Fail(Error,TEXT("Источник не объявил ни одного допустимого визуального события."));
    auto* Profile=Out.Profile.LoadSynchronous();
    if(!Out.Profile.IsNull() && !Profile)return Fail(Error,TEXT("Назначенный профиль не загружается: ")+Out.Profile.ToString());
    if(Profile && (!SavedRuntime(Profile,TEXT("Назначенный VFX-профиль"),Error) || !Profile->ValidateForEvents(Out.AllowedEvents,Error)))return false;
    Out.OwnerDigest=PackageDigest(Owner);Out.ProfileDigest=PackageDigest(Profile);Out.ProfileValueDigest=ProfileValues(Profile);
    return true;
}
}

void MCVFXProfileBindings::RegisterLibrarySource(FName SourceID,const FSoftObjectPath& LibraryAsset,FName OwnerKey,const TArray<FName>& AllowedEvents)
{
    if(!SourceID.IsNone() && !LibraryAsset.IsNull() && !OwnerKey.IsNone() && !AllowedEvents.IsEmpty())
        LibrarySources().Add(SourceID,{LibraryAsset,OwnerKey,AllowedEvents});
}

bool MCVFXProfileBindings::ReadCanonical(FName SourceID,FMCVFXCanonicalProfileState& OutState,FString& Error)
{Error.Reset();return ReadState(SourceID,OutState,Error);}

bool MCVFXProfileBindings::ValidateUnchanged(const FMCVFXCanonicalProfileState& State,FString& Error)
{
    Error.Reset();FMCVFXCanonicalProfileState Current;
    if(!ReadState(State.SourceID,Current,Error))return false;
    if(Current.OwnerAsset!=State.OwnerAsset || Current.OwnerKey!=State.OwnerKey || Current.CanonicalRowID!=State.CanonicalRowID
        || Current.bOwnerKeyPresent!=State.bOwnerKeyPresent || Current.AllowedEvents!=State.AllowedEvents
        || Current.Profile.ToSoftObjectPath()!=State.Profile.ToSoftObjectPath() || Current.OwnerDigest!=State.OwnerDigest
        || Current.ProfileDigest!=State.ProfileDigest || Current.ProfileValueDigest!=State.ProfileValueDigest)
        return Fail(Error,TEXT("Каноническое назначение или сохранённый профиль изменены. Откройте действие заново; черновик не перезапишет чужую правку."));
    return true;
}

bool MCVFXProfileBindings::ValidateSavedProfileResources(const UMCPerkVFXProfile* Profile,const TArray<FName>& AllowedEvents,FString& Error)
{
    Error.Reset();
    if(!Profile || !Profile->ValidateForEvents(AllowedEvents,Error))return false;
    for(const auto& Pair:Profile->SFXSlots)
    {
        TArray<FSoftObjectPath> Paths;Pair.Value.GetSoftResources(Paths);
        for(const auto& Path:Paths)if(!SavedRuntime(Path.TryLoad(),TEXT("Ресурс звукового слоя"),Error))return false;
        for(const auto& Layer:Pair.Value.Layers)if(!Layer.Sound.IsNull())
        {
            FMCEventSFXSamplePlan Plan;
            bool bWaiting=false;
            if(!FMCPreloadManifest::PrepareEventAudioPlan(Layer.Sound.Get(),Plan,Error,bWaiting,true))return false;
            for(const USoundWave* Wave:Plan.SampleWaves)if(!SavedRuntime(Wave,TEXT("Сэмпл звукового источника"),Error))return false;
        }
    }
    if(!Profile->NativePresentation.IsNull())
    {
        const auto* Native=Profile->NativePresentation.LoadSynchronous();
        if(!SavedRuntime(Native,TEXT("Родная композиция"),Error) || !Native->ValidateForSource(Native->SourceID,AllowedEvents,Error))return false;
        auto CheckParts=[&](const TArray<FMCVFXNativePartSettings>& Parts)
        {
            for(const auto& P:Parts)
            {
                TArray<UObject*> Resources;
                if(!P.Material.IsNull())Resources.Add(P.Material.LoadSynchronous());
                if(!P.Mesh.IsNull())Resources.Add(P.Mesh.LoadSynchronous());
                if(!P.System.IsNull())Resources.Add(P.System.LoadSynchronous());
                if(!P.OpacityCurve.IsNull())Resources.Add(P.OpacityCurve.LoadSynchronous());
                if(!P.ExpansionCurve.IsNull())Resources.Add(P.ExpansionCurve.LoadSynchronous());
                for(const auto* Resource:Resources)if(!SavedRuntime(Resource,TEXT("Ресурс родной части"),Error))return false;
            }
            return true;
        };
        for(const auto& Pair:Native->Events)if(!CheckParts(Pair.Value.Parts))return false;
        if(!CheckParts(Native->PersistentParts))return false;
    }
    for(const auto& Pair:Profile->Slots) {
        const auto& Slot=Pair.Value;
        if(Slot.Mode==EMCPerkVFXSlotMode::Replace) {
            auto* System=Slot.System.LoadSynchronous();
            if(!SavedRuntime(System,TEXT("Niagara System"),Error)
                || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error))
            {Error=Pair.Key.ToString()+TEXT(": ")+Error;return false;}
        }
        else if(Slot.Mode==EMCPerkVFXSlotMode::Composition) {
            auto* Composition=Slot.Composition.LoadSynchronous();
            if(!SavedRuntime(Composition,TEXT("Runtime-композиция VFX"),Error) || !Composition->Validate(Error))
            {Error=Pair.Key.ToString()+TEXT(": ")+Error;return false;}
            for(const auto& Layer:Composition->Layers) {
                const auto* Groups=Profile->EventGroups.Find(Pair.Key);
                if(Layer.GroupID.IsValid() && (!Groups || !Groups->Contains(Layer.GroupID)))
                {Error=Pair.Key.ToString()+TEXT(" / ")+Layer.Name.ToString()+TEXT(": группа VFX-слоя отсутствует в событии профиля.");return false;}
                if(!Layer.bEnabled && Layer.System.IsNull())continue;
                auto* System=Layer.System.LoadSynchronous();
                if(!SavedRuntime(System,TEXT("Niagara слоя композиции"),Error)
                    || !UMCVFXComposition::ValidateParameters(Layer,System,Error)
                    || (Layer.bEnabled && !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Slot),System,Error)))
                {Error=Pair.Key.ToString()+TEXT(" / ")+Layer.Name.ToString()+TEXT(": ")+Error;return false;}
            }
        }
    }
    return true;
}

bool MCVFXProfileBindings::AssignSavedProfile(UMCPerkVFXProfile* Profile,const FMCVFXCanonicalProfileState& Expected,FMCVFXProfileCommitResult& Result,FString& Error)
{
    Result=FMCVFXProfileCommitResult();Error.Reset();Result.PreviousProfile=Expected.Profile;
    if(GEditor && GEditor->PlayWorld)return Fail(Error,TEXT("Завершите Play перед назначением профиля игре."));
    if(GIsTransacting || (GEditor && GEditor->IsTransactionActive()))return Fail(Error,TEXT("Завершите текущую операцию редактора перед применением профиля."));
    if(!SavedRuntime(Profile,TEXT("Новый VFX-профиль"),Error) || !Profile->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
        || !ValidateSavedProfileResources(Profile,Expected.AllowedEvents,Error)) {
        if(Error.IsEmpty())Error=TEXT("Новый профиль должен быть сохранён внутри /Game/.");return false;
    }
    if(!ValidateUnchanged(Expected,Error))return false;
    // Imported/manually authored profiles obey the exact same source/event
    // contract as runtime and UI. Unknown native audio remains protected.
    for(const auto& Pair:Profile->SFXSlots)
        if(!MCEventSFX::ValidateNativePolicy(Pair.Value,MCVFXSourceContracts::CanSuppressOriginalSFX(Expected.SourceID,Pair.Key),Error))return false;
    if(const auto* Native=Profile->NativePresentation.Get();Native && Native->SourceID!=Expected.SourceID)
        return Fail(Error,TEXT("Родная композиция принадлежит другому действию; назначение отменено."));
    UObject* Owner=Expected.OwnerAsset.ResolveObject();
    auto* Table=Cast<UDataTable>(Owner);auto* Library=Cast<UMCVFXPresentationLibrary>(Owner);
    FMCPerkDefinition* Row=Table?Table->FindRow<FMCPerkDefinition>(Expected.CanonicalRowID,TEXT("Assign canonical VFX binding"),false):nullptr;
    if((!Expected.CanonicalRowID.IsNone() && !Row) || (Expected.CanonicalRowID.IsNone() && !Library))
        return Fail(Error,TEXT("Канонический владелец профиля больше не доступен."));
    if(Expected.Profile.Get()==Profile){Result.bSaved=true;Result.Message=TEXT("Профиль уже назначен: ")+Expected.SourceID.ToString();return true;}
    FScopedTransaction Transaction(NSLOCTEXT("MCVFXBinding","Assign","Назначить профиль представления"));
    Owner->Modify();
    if(Row){Row->VFXProfile=Profile;Table->HandleDataTableChanged(Expected.CanonicalRowID);}
    else Library->Profiles.Add(Expected.OwnerKey,Profile);
    Owner->MarkPackageDirty();Result.bChangedInMemory=true;
    if(!UEditorLoadingAndSavingUtils::SavePackages({Owner->GetOutermost()},true)) {
        if(Row){Row->VFXProfile=Expected.Profile;Table->HandleDataTableChanged(Expected.CanonicalRowID);}
        else if(Expected.bOwnerKeyPresent)Library->Profiles.Add(Expected.OwnerKey,Expected.Profile);
        else Library->Profiles.Remove(Expected.OwnerKey);
        Owner->MarkPackageDirty();Result.bChangedInMemory=false;Result.bRestoredPreviousAssignment=true;
        const bool RestoredSaved=UEditorLoadingAndSavingUtils::SavePackages({Owner->GetOutermost()},true);
        Transaction.Cancel();
        Result.Message=RestoredSaved?TEXT("Новое назначение не сохранено; прежнее назначение восстановлено."):
            TEXT("Новое назначение не сохранено. Прежнее назначение восстановлено в памяти; пакет остаётся изменённым из-за ошибки сохранения.");
        return Fail(Error,Result.Message);
    }
    Result.bSaved=true;Result.Message=TEXT("Сохранено: ")+Expected.SourceID.ToString()+TEXT(" → ")+Profile->GetPathName();return true;
}
