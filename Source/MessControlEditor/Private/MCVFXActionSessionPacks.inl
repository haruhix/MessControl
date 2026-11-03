// Included by MCVFXActionSession.cpp: pack persistence shares the Session's only
// artifact writer and canonical guards. Never compile as a separate translation unit.
#include "MCVFXActionPacks.h"

UMCVFXActionPack* FMCVFXActionSession::SavePackNew(const FText& Label,FString& Error,const FString& AssetRoot)
{
    Error.Reset();
    if(!CanEditProfile()){Impl->Fail(Error,GetEditUnavailableReason());return nullptr;}
    if(GEditor && GEditor->PlayWorld){Impl->Fail(Error,TEXT("Завершите Play перед сохранением пака."));return nullptr;}
    if(Label.IsEmpty() || Label.ToString().Len()>192 || !AssetRoot.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(AssetRoot))
    {Impl->Fail(Error,TEXT("Укажите имя пака и новую папку внутри /Game/."));return nullptr;}
    if(!Impl->CanonicalProfileCurrent(Error,false) || !Impl->NativeResourcesSaved(Error))return nullptr;
    const auto* PreviousPack=Impl->LoadedPack.LoadSynchronous();
    TArray<FMCVFXActionPackDependencyFingerprint> ExpectedDependencies;
    if(PreviousPack)
    {
        TArray<FString> Warnings;
        if(!MCVFXActionPacks::CheckDependencies(PreviousPack,Warnings,Error))return nullptr;
        if(!Warnings.IsEmpty()){Impl->Fail(Error,TEXT("Общие ресурсы открытого пака изменились. Откройте актуальное действие и проверьте новые исходники перед сохранением версии. ")+FString::Join(Warnings,TEXT("\n")));return nullptr;}
        ExpectedDependencies=PreviousPack->Dependencies;
    }
    // This prepares/validates the real recipe without copying the preview Solo
    // mask into authored data. Build the saved profile from the unfiltered draft.
    if(!Impl->PrepareProfile(NAME_None,nullptr,Error,nullptr,false,false))return nullptr;
    Stop();
    const FString Stem=MCActionSession::AssetStem(Impl->ActionID);
    const FString RunPath=AssetRoot/(Stem+TEXT("_"))+FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    TArray<FString> Created;
    auto Fail=[&](const FString& Reason)->UMCVFXActionPack*
    {Impl->Fail(Error,Reason+(Created.IsEmpty()?FString():TEXT(" Новые промежуточные ассеты сохранены в ")+RunPath+TEXT(". Игровой профиль не назначался.")));return nullptr;};
    TStrongObjectPtr<UMCPerkVFXProfile> Profile(DuplicateObject<UMCPerkVFXProfile>(Impl->EventDraft.IsValid()?Impl->EventDraft.Get():Impl->BaseProfile.Get(),GetTransientPackage()));
    Profile->ClearFlags(RF_Public|RF_Standalone);Profile->SetFlags(RF_Transient);
    // Moving cards need not duplicate playable resources or create a new game
    // assignment. Reuse the saved runtime version when only its master/layout changed.
    bool bSamePackRuntime=PreviousPack && Impl->PackBaseline.IsValid() && MCActionSession::SameSlots(Impl->EventDraft.Get(),Impl->PackBaseline.Get());
    if(bSamePackRuntime)
    {
        bSamePackRuntime=Impl->NativeDraft.IsValid()==Impl->PackNativeBaseline.IsValid();
        if(bSamePackRuntime && Impl->NativeDraft.IsValid())bSamePackRuntime=Impl->NativeDraft->ComputeSettingsHash()==Impl->PackNativeBaseline->ComputeSettingsHash();
        for(const auto& Pair:Impl->Stages){const auto* Baseline=Impl->PackRecipeBaselines.Find(Pair.Key);bSamePackRuntime&=Baseline && MCActionSession::SameRuntimeRecipe(Pair.Value.Recipe.Get(),Baseline->Get());}
    }
    UMCPerkVFXProfile* ReuseProfile=bSamePackRuntime?PreviousPack->RuntimeProfile.LoadSynchronous():!IsRuntimeDirty()?Impl->TableBaseline.LoadSynchronous():nullptr;
    TMap<FName,UMCVFXRecipePreset*> Masters;TMap<FName,FString> MasterPaths;
    for(FName Event:Impl->StageOrder)
    {
        auto& Stage=Impl->Stages.FindChecked(Event);
        if(!Impl->ValidateRecipeGroups(Event,Stage.Recipe.Get(),Error) || !Stage.Recipe->ValidateLayout(Error))return Fail(Error);
        // Even an empty/native/audio-only event has a saved editable master.
        auto* Master=MCActionSession::SaveNewCopy(Stage.Recipe.Get(),RunPath/(TEXT("DA_")+MCActionSession::AssetStem(Event)+TEXT("_Source")),Error);
        if(!Master)return Fail(Error);Created.Add(Master->GetPathName());Masters.Add(Event,Master);MasterPaths.Add(Event,Master->GetPathName());
        if(ReuseProfile)continue;
        const auto* AuthoredSlot=Impl->EventDraft->Slots.Find(Event);
        FMCPerkVFXSlot Slot=AuthoredSlot?*AuthoredSlot:Stage.Outer; // retain dormant authored controls too
        if(Stage.IsRuntimeDirty() && !Impl->MakeSlot(Event,Stage,Stage.Recipe.Get(),Slot,Error))return Fail(Error);
        if(Slot.Mode==EMCPerkVFXSlotMode::Composition)
        {
            const UMCVFXComposition* RuntimeSource=Stage.IsRuntimeDirty()?Stage.Composition.Get():Slot.Composition.LoadSynchronous();
            if(!RuntimeSource)return Fail(TEXT("Сохранённая композиция события не загружается."));
            auto* Runtime=MCActionSession::SaveNewCopy(RuntimeSource,RunPath/(TEXT("DA_")+MCActionSession::AssetStem(Event)+TEXT("_Runtime")),Error);
            if(!Runtime)return Fail(Error);Created.Add(Runtime->GetPathName());
            Runtime->GetOutermost()->GetMetaData().SetValue(Runtime,TEXT("MCComposerSourceRecipe"),*Master->GetPathName());Runtime->MarkPackageDirty();
            if(!UEditorLoadingAndSavingUtils::SavePackages({Runtime->GetOutermost()},true))return Fail(TEXT("Не удалось сохранить связь runtime с авторским исходником."));
            Slot.Composition=Runtime;
        }
        // Original single systems keep their own export/source identity. Their
        // independent editable master is referenced by the pack, not forged as
        // the original Niagara's own source recipe.
        if(Slot.Mode==EMCPerkVFXSlotMode::Replace)MasterPaths.Remove(Event);
        if(Stage.IsRuntimeDirty() || Profile->Slots.Contains(Event))Profile->Slots.Add(Event,Slot);
    }
    // Active profiles may retain an unused Warning stage outside the current
    // preview provider. Preserve that serialized contract and its artwork; do
    // not erase it merely because the current sandbox has four visible events.
    for(FName Event:Impl->CanonicalBinding.AllowedEvents)if(!Masters.Contains(Event))
    {
        TStrongObjectPtr<UMCVFXRecipePreset> Extra(NewObject<UMCVFXRecipePreset>(GetTransientPackage()));Extra->PresetName=Event.ToString();
        auto* Slot=Profile->Slots.Find(Event);UMCVFXComposition* RuntimeSource=nullptr;
        if(Slot && Slot->Mode==EMCPerkVFXSlotMode::Composition)
        {
            RuntimeSource=Slot->Composition.LoadSynchronous();if(!RuntimeSource)return Fail(TEXT("Не загружается сохранённое дополнительное событие: ")+Event.ToString());
            Extra->Layers=RuntimeSource->Layers;Extra->DurationSeconds=RuntimeSource->DurationSeconds;Extra->TailSeconds=RuntimeSource->TailSeconds;
        }
        else if(Slot && Slot->Mode==EMCPerkVFXSlotMode::Replace)
        {FMCVFXCompositionLayer Layer;Layer.System=Slot->System;Layer.Name=Slot->System.GetAssetName().IsEmpty()?Event:FName(*Slot->System.GetAssetName());Extra->Layers.Add(Layer);}
        for(int32 I=0;I<Extra->Layers.Num();++I){FMCVFXRecipeNodeLayout Node;Node.ID=FGuid::NewGuid();Node.Position=FVector2D(I*270,0);Extra->Nodes.Add(Node);}
        auto* Master=MCActionSession::SaveNewCopy(Extra.Get(),RunPath/(TEXT("DA_")+MCActionSession::AssetStem(Event)+TEXT("_Source")),Error);
        if(!Master)return Fail(Error);Created.Add(Master->GetPathName());Masters.Add(Event,Master);
        if(!ReuseProfile && Slot && Slot->Mode==EMCPerkVFXSlotMode::Composition)
        {
            auto* Runtime=MCActionSession::SaveNewCopy(RuntimeSource,RunPath/(TEXT("DA_")+MCActionSession::AssetStem(Event)+TEXT("_Runtime")),Error);
            if(!Runtime)return Fail(Error);Created.Add(Runtime->GetPathName());
            Runtime->GetOutermost()->GetMetaData().SetValue(Runtime,TEXT("MCComposerSourceRecipe"),*Master->GetPathName());Runtime->MarkPackageDirty();
            if(!UEditorLoadingAndSavingUtils::SavePackages({Runtime->GetOutermost()},true))return Fail(TEXT("Не удалось сохранить исходник дополнительного события."));
            Slot->Composition=Runtime;MasterPaths.Add(Event,Master->GetPathName());
        }
    }
    if(!ReuseProfile && Impl->NativeOverride())
    {
        if(!Impl->NativeDraft.IsValid() || !Impl->NativeDraft->ValidateForSource(Impl->ActionID,Impl->StageOrder,Error))return Fail(Error);
        auto* Native=MCActionSession::SaveNewCopy(Impl->NativeDraft.Get(),RunPath/(TEXT("DA_")+Stem+TEXT("_NativeArt")),Error);
        if(!Native)return Fail(Error);Created.Add(Native->GetPathName());Profile->NativePresentation=Native;
    }
    else if(!ReuseProfile)Profile->NativePresentation.Reset();
    if(!Profile->ValidateForEvents(Impl->CanonicalBinding.AllowedEvents,Error))return Fail(Error);
    auto* SavedProfile=ReuseProfile?ReuseProfile:MCActionSession::SaveNewCopy(Profile.Get(),RunPath/(TEXT("DA_")+Stem+TEXT("_Presentation")),Error,&MasterPaths);
    if(!SavedProfile)return Fail(Error);if(!ReuseProfile)Created.Add(SavedProfile->GetPathName());
    if(!IsContextCurrent() || !Impl->CanonicalProfileCurrent(Error,false))return Fail(TEXT("Контекст изменился во время сохранения. ")+Error);
    FMCVFXActionPackSaveRequest Request;Request.ManifestPackagePath=RunPath/(TEXT("DA_")+Stem+TEXT("_Pack"));
    Request.SourceID=Impl->ActionID;Request.EventIDs=Impl->CanonicalBinding.AllowedEvents;Request.Label=Label;Request.VersionLabel=FDateTime::UtcNow().ToIso8601();
    Request.RuntimeProfile=SavedProfile;Request.RecipeMasters=Masters;Request.Layout=GetLayoutSnapshot();
    Request.ExpectedDependencies=MoveTemp(ExpectedDependencies);
    const auto* Native=SavedProfile->NativePresentation.LoadSynchronous();Request.NativeSchemaVersion=Native?Native->SchemaVersion:0;
    FMCVFXActionPackSaveResult Result;
    if(!MCVFXActionPacks::SaveNew(Request,Result,Error) || !Result.bSaved)return Fail(Error);
    Impl->LoadedPack=Result.Pack.Get();
    Impl->PackBaseline.Reset(DuplicateObject<UMCPerkVFXProfile>(Impl->EventDraft.Get(),GetTransientPackage()));
    Impl->PackRecipeBaselines.Reset();for(const auto& Pair:Impl->Stages)Impl->PackRecipeBaselines.Emplace(Pair.Key,TStrongObjectPtr<UMCVFXRecipePreset>(MCActionSession::CopyRecipe(Pair.Value.Recipe.Get(),TEXT("PackSavedRecipe"))));
    Impl->PackNativeBaseline.Reset(Impl->CopyNative(TEXT("PackSavedNative")));Impl->PackLayoutBaseline=GetLayoutSnapshot();
    MarkMasterLayoutSaved();Impl->Status=TEXT("Пак сохранён: ")+Label.ToString()+TEXT(". Игра не изменена; применение выполняется отдельно.");
    return Result.Pack.Get();
}

bool FMCVFXActionSession::LoadPack(const UMCVFXActionPack* Pack,FString& Error,bool bDiscardDraft)
{
    Error.Reset();if(!CanEditProfile())return Impl->Fail(Error,GetEditUnavailableReason());
    if(HasUnsavedPackChanges() && !bDiscardDraft)return Impl->Fail(Error,TEXT("В черновике есть несохранённые изменения. Сохраните пак или явно выберите сброс перед открытием."));
    if(!Impl->CanonicalProfileCurrent(Error,false))return false;
    if(!Pack || Pack->NativeSchemaVersion>1)return Impl->Fail(Error,TEXT("Неподдерживаемая версия родной основы пака."));
    FMCVFXActionPackLoaded Loaded;
    if(!MCVFXActionPacks::Load(Pack,Impl->ActionID,Impl->CanonicalBinding.AllowedEvents,1,Pack->NativeSchemaVersion,Loaded,Error))return Impl->Fail(Error,Error);
    TStrongObjectPtr<UMCPerkVFXProfile> Draft(DuplicateObject<UMCPerkVFXProfile>(Loaded.RuntimeProfile.Get(),GetTransientPackage()));
    Draft->ClearFlags(RF_Public|RF_Standalone);Draft->SetFlags(RF_Transient|RF_Transactional);
    TStrongObjectPtr<UMCVFXNativePresentationSettings> Native;
    const auto* SavedNative=Loaded.RuntimeProfile->NativePresentation.LoadSynchronous();
    if(SavedNative)
    {Native.Reset(DuplicateObject<UMCVFXNativePresentationSettings>(SavedNative,GetTransientPackage()));Native->ClearFlags(RF_Public|RF_Standalone);Native->SetFlags(RF_Transient|RF_Transactional);}
    else
    {
        bool bContract=!UMCVFXNativePresentationSettings::DescribeParts(Impl->ActionID,NAME_None).IsEmpty();
        for(FName Event:Impl->StageOrder)bContract|=!UMCVFXNativePresentationSettings::DescribeParts(Impl->ActionID,Event).IsEmpty();
        if(bContract)
        {
            Native.Reset(NewObject<UMCVFXNativePresentationSettings>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
            if(!Native->InitFromNativeDefaults(Impl->ActionID,Error))return Impl->Fail(Error,Error);
            for(auto& Pair:Native->Events){const auto* Slot=Draft->Slots.Find(Pair.Key);Pair.Value.bKeepNativeAccentsWithComposition=!Slot || Slot->Mode==EMCPerkVFXSlotMode::UseDefault;}
        }
    }
    // All saved inputs were checked before replacing any draft. Canonical state
    // and its stale-source guards stay independent of the version being viewed.
    Stop();Impl->PreviewSolo.Reset();Impl->EventDraft=MoveTemp(Draft);Impl->NativeDraft=MoveTemp(Native);
    Impl->NativeSource=Loaded.RuntimeProfile->NativePresentation;Impl->NativeSourceDigest=MCActionSession::PackageDigest(SavedNative);
    Impl->NativeSourceSettingsHash=SavedNative?SavedNative->ComputeSettingsHash():0;
    Impl->NativeBaseline.Reset(Impl->CopyNative(TEXT("PackNativeBaseline")));Impl->NativeDraftToken=FGuid::NewGuid();
    Impl->LayoutState.Reset(NewObject<UMCVFXActionLayoutState>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
    Impl->LayoutState->Layout=Loaded.Layout;Impl->LayoutBaseline=Loaded.Layout;
    for(FName Event:Impl->StageOrder)
    {
        auto& Stage=Impl->Stages.FindChecked(Event);const auto* Master=Loaded.RecipeMasters.FindChecked(Event).Get();
        Stage.Recipe.Reset(MCActionSession::CopyRecipe(Master,TEXT("LoadedPackDraft")));
        Stage.Baseline.Reset(MCActionSession::CopyRecipe(Master,TEXT("LoadedPackBaseline")));
        Stage.MasterBaseline.Reset(MCActionSession::CopyRecipe(Master,TEXT("LoadedPackMasterBaseline")));
        Stage.SourceMasterPath=Master->GetPathName();Stage.Outer=FMCPerkVFXSlot();
        if(const auto* Slot=Loaded.RuntimeProfile->Slots.Find(Event))Stage.Outer=MCActionSession::EffectiveOuter(*Slot);
        Stage.SourceRuntimeComposition=Stage.Outer.Mode==EMCPerkVFXSlotMode::Composition?Stage.Outer.Composition:TSoftObjectPtr<UMCVFXComposition>();
        Stage.RuntimeSourceDigest=MCActionSession::PackageDigest(Stage.SourceRuntimeComposition.LoadSynchronous());Stage.ClearCache();
    }
    Impl->LoadedPack=const_cast<UMCVFXActionPack*>(Pack);
    Impl->PackBaseline.Reset(DuplicateObject<UMCPerkVFXProfile>(Impl->EventDraft.Get(),GetTransientPackage()));Impl->PackRecipeBaselines.Reset();
    for(const auto& Pair:Impl->Stages)Impl->PackRecipeBaselines.Emplace(Pair.Key,TStrongObjectPtr<UMCVFXRecipePreset>(MCActionSession::CopyRecipe(Pair.Value.Recipe.Get(),TEXT("OpenedPackBaseline"))));
    Impl->PackNativeBaseline.Reset(Impl->CopyNative(TEXT("OpenedPackNative")));Impl->PackLayoutBaseline=Loaded.Layout;
    Impl->ReconcileObserved();++Impl->Revision;
    Impl->Status=TEXT("Открыт пак: ")+Pack->Label.ToString()+TEXT(". Это предпросмотр; назначение в игре не изменено.");return true;
}

bool FMCVFXActionSession::ApplySavedPack(const UMCVFXActionPack* Pack,FString& Error)
{
    Error.Reset();if(!CanEditProfile())return Impl->Fail(Error,GetEditUnavailableReason());
    if(GEditor && GEditor->PlayWorld)return Impl->Fail(Error,TEXT("Завершите Play перед применением пака."));
    if(HasUnsavedPackChanges())return Impl->Fail(Error,TEXT("Сначала сохраните правки как новую версию пака."));
    if(!Impl->CanonicalProfileCurrent(Error))return false;
    if(!Pack || Pack->NativeSchemaVersion>1)return Impl->Fail(Error,TEXT("Неподдерживаемая версия пака."));
    FMCVFXActionPackLoaded Loaded;
    if(!MCVFXActionPacks::Load(Pack,Impl->ActionID,Impl->CanonicalBinding.AllowedEvents,1,Pack->NativeSchemaVersion,Loaded,Error))return Impl->Fail(Error,Error);
    Stop();FMCVFXProfileCommitResult Commit;
    if(!MCVFXProfileBindings::AssignSavedProfile(Loaded.RuntimeProfile.Get(),Impl->CanonicalBinding,Commit,Error) || !Commit.bSaved)return Impl->Fail(Error,Error.IsEmpty()?Commit.Message:Error);
    const FName Action=Impl->ActionID;
    if(!Initialize(Action,Error))return Impl->Fail(Error,TEXT("Пак назначен игре; обновление окна не удалось. Откройте действие заново. ")+Error);
    if(!LoadPack(Pack,Error,true))return Impl->Fail(Error,TEXT("Пак назначен игре; повторное открытие не удалось. ")+Error);
    Impl->Status=Commit.Message;return true;
}

FSoftObjectPath FMCVFXActionSession::GetLoadedPackPath() const
{return Impl->LoadedPack.ToSoftObjectPath();}

bool FMCVFXActionSession::HasUnsavedPackChanges() const
{
    if(Impl->LoadedPack.IsNull())return IsDirty();
    if(!Impl->PackBaseline.IsValid() || !MCActionSession::SameSlots(Impl->EventDraft.Get(),Impl->PackBaseline.Get())
        || !MCActionSession::SameLayout(GetLayoutSnapshot(),Impl->PackLayoutBaseline))return true;
    if(Impl->NativeDraft.IsValid()!=Impl->PackNativeBaseline.IsValid())return true;
    if(Impl->NativeDraft.IsValid() && Impl->NativeDraft->ComputeSettingsHash()!=Impl->PackNativeBaseline->ComputeSettingsHash())return true;
    if(Impl->PackRecipeBaselines.Num()!=Impl->Stages.Num())return true;
    for(const auto& Pair:Impl->Stages)
    {const auto* Base=Impl->PackRecipeBaselines.Find(Pair.Key);if(!Base || !MCActionSession::SameRecipe(Pair.Value.Recipe.Get(),Base->Get()))return true;}
    return false;
}
