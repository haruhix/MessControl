#include "MCVFXActionSession.h"
#include "MCVFXActionPack.h"
#include "MCVFXSourceContracts.h"
#include "MCDataAssets.h"
#include "MCArenaTooth.h"
#include "Widgets/Text/STextBlock.h"

#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "MCPerkTypes.h"
#include "MCVFXComposerBuilderLibrary.h"
#include "MCVFXRecipePreset.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXNativePresentationSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "FileHelpers.h"
#include "Misc/DateTime.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Curves/CurveFloat.h"
#include "Sound/SoundBase.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MCActionSession
{
    const FName RocketID(TEXT("Consumable_Rocket"));
    UDataTable* Table()
    {
        auto* T=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
        return T && T->GetRowStruct()==FMCPerkDefinition::StaticStruct()?T:nullptr;
    }
    const AMCActiveSkillAction* ActionDefaults(const FMCPerkDefinition& Row)
    {
        const auto* Effect=Row.EffectClass?Cast<UMCActiveSkillPerkEffect>(Row.EffectClass->GetDefaultObject()):nullptr;
        return Effect && Effect->ActionClass?Cast<AMCActiveSkillAction>(Effect->ActionClass->GetDefaultObject()):nullptr;
    }
    TSoftObjectPtr<UNiagaraSystem> DefaultSystem(const AMCActiveSkillAction* CDO,FName Stage)
    {
        if(!CDO)return nullptr;
        if(Stage==TEXT("Start"))return CDO->StartVFX;
        if(Stage==TEXT("Loop"))return CDO->LoopVFX;
        if(Stage==TEXT("Impact"))return CDO->ImpactVFX;
        if(Stage==TEXT("Warning"))return CDO->WarningVFX;
        if(Stage==TEXT("End"))return CDO->EndVFX;
        return nullptr;
    }
    template<class T> bool SameValues(const TArray<T>& A,const TArray<T>& B,UScriptStruct* Type)
    {
        if(A.Num()!=B.Num())return false;
        for(int32 I=0;I<A.Num();++I)if(!Type->CompareScriptStruct(&A[I],&B[I],0))return false;
        return true;
    }
    bool SameRecipe(const UMCVFXRecipePreset* A,const UMCVFXRecipePreset* B)
    {
        return A && B && A->DurationSeconds==B->DurationSeconds && A->TailSeconds==B->TailSeconds
            && A->PresetName==B->PresetName
            && SameValues(A->Layers,B->Layers,FMCVFXCompositionLayer::StaticStruct())
            && SameValues(A->Nodes,B->Nodes,FMCVFXRecipeNodeLayout::StaticStruct())
            && SameValues(A->Groups,B->Groups,FMCVFXRecipeGroup::StaticStruct());
    }
    bool SameRuntimeRecipe(const UMCVFXRecipePreset* A,const UMCVFXRecipePreset* B)
    {
        return A && B && A->DurationSeconds==B->DurationSeconds && A->TailSeconds==B->TailSeconds
            && SameValues(A->Layers,B->Layers,FMCVFXCompositionLayer::StaticStruct());
    }
    bool SameMasterLayout(const UMCVFXRecipePreset* A,const UMCVFXRecipePreset* B)
    {
        return A && B && A->PresetName==B->PresetName
            && SameValues(A->Nodes,B->Nodes,FMCVFXRecipeNodeLayout::StaticStruct())
            && SameValues(A->Groups,B->Groups,FMCVFXRecipeGroup::StaticStruct());
    }
    bool SameLayout(const FMCVFXActionLayout& A,const FMCVFXActionLayout& B)
    {return FMCVFXActionLayout::StaticStruct()->CompareScriptStruct(&A,&B,0);}
    bool ValidPosition(FVector2D P)
    {return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::Abs(P.X)<=1000000 && FMath::Abs(P.Y)<=1000000;}
    bool SamePlaybackGroups(const TMap<FName,FMCEventPresentationGroupSet>& A,const TMap<FName,FMCEventPresentationGroupSet>& B)
    {
        if(A.Num()!=B.Num())return false;
        for(const auto& Pair:A)
        {
            const auto* Other=B.Find(Pair.Key);if(!Other || Pair.Value.Groups.Num()!=Other->Groups.Num())return false;
            for(const auto& Group:Pair.Value.Groups)
            {
                const auto* G=Other->Find(Group.ID);
                if(!G || Group.bEnabled!=G->bEnabled || Group.bMuteSFX!=G->bMuteSFX)return false;
            }
        }
        return true;
    }
    UMCVFXRecipePreset* CopyRecipe(const UMCVFXRecipePreset* Source,const TCHAR* Name)
    {
        auto* Copy=DuplicateObject<UMCVFXRecipePreset>(Source,GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UMCVFXRecipePreset::StaticClass(),Name));
        Copy->ClearFlags(RF_Public|RF_Standalone);Copy->SetFlags(RF_Transient|RF_Transactional);return Copy;
    }
    void CopyRecipeValues(const UMCVFXRecipePreset* Source,UMCVFXRecipePreset* Target)
    {
        Target->Layers=Source->Layers;Target->Nodes=Source->Nodes;Target->Groups=Source->Groups;
        Target->PresetName=Source->PresetName;Target->DurationSeconds=Source->DurationSeconds;Target->TailSeconds=Source->TailSeconds;
    }
    bool HasEnabled(const UMCVFXRecipePreset* Recipe)
    {return Recipe && Recipe->Layers.ContainsByPredicate([](const auto& L){return L.bEnabled;});}
    FString PackageDigest(const UObject* Object)
    {
        if(!Object)return TEXT("None");
        const FString File=FPackageName::LongPackageNameToFilename(Object->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        return FPaths::FileExists(File)?LexToString(FMD5Hash::HashFile(*File)):TEXT("Unsaved");
    }
    bool SameSlots(const UMCPerkVFXProfile* A,const UMCPerkVFXProfile* B)
    {
        if(!A || !B || A->Slots.Num()!=B->Slots.Num() || A->NativePresentation!=B->NativePresentation)return false;
        for(const auto& Pair:A->Slots){const auto* Other=B->Slots.Find(Pair.Key);if(!Other || !FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0))return false;}
        if(A->SFXSlots.Num()!=B->SFXSlots.Num() || A->EventGroups.Num()!=B->EventGroups.Num())return false;
        for(const auto& Pair:A->SFXSlots){const auto* Other=B->SFXSlots.Find(Pair.Key);if(!Other || !FMCEventSFXSlot::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0))return false;}
        for(const auto& Pair:A->EventGroups){const auto* Other=B->EventGroups.Find(Pair.Key);if(!Other || !FMCEventPresentationGroupSet::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0))return false;}
        return true;
    }
    FString StageMasterKey(FName Stage){return TEXT("MCActionSourceRecipe_")+Stage.ToString();}
    FString AssetStem(FName Identity)
    {
        const FString Original=Identity.ToString();FString Safe;
        for(TCHAR Ch:Original)Safe.AppendChar(FChar::IsAlnum(Ch)||Ch==TEXT('_')?Ch:TEXT('_'));
        if(Safe.IsEmpty())Safe=TEXT("Source");
        // Source/event identities remain exact in the registry and metadata.
        // Their package-safe representation is not a new gameplay identity.
        if(Safe!=Original || Safe.Len()>80)Safe=Safe.Left(64)+FString::Printf(TEXT("_%08X"),FCrc::StrCrc32(*Original));
        return Safe;
    }
    FMCPerkVFXSlot EffectiveOuter(const FMCPerkVFXSlot& Slot)
    {if(Slot.Mode==EMCPerkVFXSlotMode::Replace || Slot.Mode==EMCPerkVFXSlotMode::Composition)return Slot;FMCPerkVFXSlot Result;Result.Mode=Slot.Mode;return Result;}
    template<class T> T* SaveNewCopy(const T* Source,const FString& PackagePath,FString& Error,const TMap<FName,FString>* StageMasters=nullptr)
    {
        if(!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath)
            || FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr,*PackagePath))
        {Error=TEXT("Сохранение требует нового имени /Game; существующие ассеты не перезаписываются.");return nullptr;}
        UPackage* P=CreatePackage(*PackagePath);
        auto* Saved=DuplicateObject<T>(Source,P,*FPackageName::GetLongPackageAssetName(PackagePath));
        Saved->ClearFlags(RF_Transient);Saved->SetFlags(RF_Public|RF_Standalone|RF_Transactional);
        if(StageMasters)for(const auto& Pair:*StageMasters)if(!Pair.Value.IsEmpty())P->GetMetaData().SetValue(Saved,*StageMasterKey(Pair.Key),*Pair.Value);
        FAssetRegistryModule::AssetCreated(Saved);P->MarkPackageDirty();
        if(!UEditorLoadingAndSavingUtils::SavePackages({P},true))
        {Error=TEXT("Не удалось сохранить новый ассет: ")+PackagePath;return nullptr;}
        return Saved;
    }
}

struct FMCVFXActionSession::FImpl
{
    struct FStage
    {
        TStrongObjectPtr<UMCVFXRecipePreset> Recipe;
        TStrongObjectPtr<UMCVFXRecipePreset> Baseline;
        TStrongObjectPtr<UMCVFXRecipePreset> MasterBaseline;
        TStrongObjectPtr<UMCVFXRecipePreset> Observed;
        TStrongObjectPtr<UMCVFXRecipePreset> CompiledSnapshot;
        TStrongObjectPtr<UNiagaraSystem> Compiled;
        TStrongObjectPtr<UMCVFXComposition> Composition;
        TSoftObjectPtr<UMCVFXComposition> SourceRuntimeComposition;
        FString RuntimeSourceDigest;
        FMCPerkVFXSlot Outer;
        FString SourceMasterPath;
        FString CompiledSourceDigest;
        TMap<FSoftObjectPath,FGuid> TrialIDs;
        TArray<FMCVFXNativeSourcePart> NativeSources;
        bool IsRuntimeDirty() const{return !MCActionSession::SameRuntimeRecipe(Recipe.Get(),Baseline.Get());}
        bool IsLayoutDirty() const{return !MCActionSession::SameMasterLayout(Recipe.Get(),MasterBaseline.Get());}
        void ClearCache(){Compiled.Reset();Composition.Reset();CompiledSnapshot.Reset();CompiledSourceDigest.Reset();}
    };
    FName ActionID;
    FName CanonicalRowID;
    FMCVFXCanonicalProfileState CanonicalBinding;
    bool bHasCanonicalBinding=false;
    TArray<FName> StageOrder;
    TMap<FName,FText> StageLabels;
    FText ActionLabel;
    bool bSupportsProfileOverride=false;
    bool bHasContextPreview=false;
    TStrongObjectPtr<UMCPerkVFXProfile> BaseProfile;
    TStrongObjectPtr<UMCPerkVFXProfile> EventDraft;
    TSoftObjectPtr<UMCVFXActionPack> LoadedPack;
    TStrongObjectPtr<UMCPerkVFXProfile> PackBaseline;
    TMap<FName,TStrongObjectPtr<UMCVFXRecipePreset>> PackRecipeBaselines;
    FMCVFXActionLayout PackLayoutBaseline;
    TStrongObjectPtr<UMCVFXNativePresentationSettings> PackNativeBaseline;
    TStrongObjectPtr<UMCVFXActionLayoutState> LayoutState;
    FMCVFXActionLayout LayoutBaseline,ObservedLayout;
    TMap<FName,FMCEventSFXSlot> ObservedAudio;
    TMap<FName,FMCPerkVFXSlot> ObservedSlots;
    TMap<FName,FMCEventPresentationGroupSet> ObservedGroups;
    TMap<FName,TArray<FGuid>> PreviewSolo;
    uint64 ObservedNativeHash=0;
    bool bObserved=false;
    TStrongObjectPtr<UMCVFXNativePresentationSettings> NativeDraft;
    TStrongObjectPtr<UMCVFXNativePresentationSettings> NativeBaseline;
    TSoftObjectPtr<UMCVFXNativePresentationSettings> NativeSource;
    FString NativeSourceDigest;
    uint64 NativeSourceSettingsHash=0;
    FGuid NativeDraftToken=FGuid::NewGuid();
    TStrongObjectPtr<UMCPerkVFXProfile> AuditionProfile;
    TArray<TStrongObjectPtr<UMCVFXComposition>> PreviewCompositions;
    TMap<FName,FStage> Stages;
    TSoftObjectPtr<UMCPerkVFXProfile> TableBaseline;
    FString ProfileSourceDigest;
    FString Status=TEXT("Выберите действие.");
    uint64 Revision=0;
    uint64 PlaybackRevision=0,LayoutRevision=0;
    TFunction<void(int32)> SetPreviewCamera;
    bool bOwnsPlayback=false;
    bool ContextCurrent() const
    {
        if(ActionID.IsNone()||!BaseProfile.IsValid()||Stages.Num()!=StageOrder.Num())return false;
        const auto* Entry=MCPerkPreviewAdapters::Find(ActionID);
        if(!Entry||Entry->Stages!=StageOrder)return false;
        return !bHasContextPreview || (MCPerkActionPreviewPanel::GetContextPerkID()==ActionID
            && MCPerkActionPreviewPanel::GetActiveRunner().IsValid());
    }
    bool Fail(FString& Error,const FString& Message){Error=Message;Status=Message;return false;}
    bool NativeDirty()const
    {return NativeDraft.IsValid() && NativeBaseline.IsValid() && NativeDraft->ComputeSettingsHash()!=NativeBaseline->ComputeSettingsHash();}
    bool NativeOverride()const
    {
        if(!NativeSource.IsNull() || NativeDirty())return true;
        if(NativeDraft.IsValid())for(const auto& P:Stages)
            if(P.Value.Outer.Mode==EMCPerkVFXSlotMode::UseDefault && P.Value.IsRuntimeDirty()
                && !P.Value.Recipe->Layers.IsEmpty() && NativeDraft->Events.Contains(P.Key))return true;
        return false;
    }
    bool KeepNative(FName Event)const
    {const auto* E=NativeDraft.IsValid()?NativeDraft->Events.Find(Event):nullptr;return E && E->bKeepNativeAccentsWithComposition;}
    bool EventDraftDirty()const{return EventDraft.IsValid() && BaseProfile.IsValid() && !MCActionSession::SameSlots(EventDraft.Get(),BaseProfile.Get());}
    bool AudioEditScope(FGuid Token,FName Event,FString& Error)
    {
        Error.Reset();
        if(Token!=NativeDraftToken || !ContextCurrent() || !bSupportsProfileOverride || !EventDraft.IsValid() || !StageOrder.Contains(Event))
            return Fail(Error,TEXT("Звуковой слой относится к прежнему черновику или неизвестному событию."));
        return true;
    }
    bool LayoutEditScope(FGuid Token,FName Event,FString& Error)
    {
        Error.Reset();
        if(Token!=NativeDraftToken || !ContextCurrent() || !LayoutState.IsValid() || (!Event.IsNone() && !StageOrder.Contains(Event)))
            return Fail(Error,TEXT("Граф относится к прежнему черновику или неизвестному событию."));
        return true;
    }
    bool ValidateLayout(const FMCVFXActionLayout& Layout,FString& Error)
    {
        if((Layout.bHasHubPosition && !MCActionSession::ValidPosition(Layout.HubPosition)) || Layout.Events.Num()>StageOrder.Num())
            return Fail(Error,TEXT("Некорректное положение графа."));
        TSet<FName> Events;
        for(const auto& E:Layout.Events)
        {
            const auto* S=Stages.Find(E.Event);
            if(!S || Events.Contains(E.Event) || (E.bHasCardPosition && !MCActionSession::ValidPosition(E.CardPosition)) || E.Groups.Num()>33 || E.SFXLayerPositions.Num()>32)
                return Fail(Error,TEXT("Расположение содержит неизвестное событие или некорректные координаты."));
            Events.Add(E.Event);TSet<FGuid> GroupIDs;
            const auto* Groups=EventDraft.IsValid()?EventDraft->EventGroups.Find(E.Event):nullptr;
            for(const auto& G:E.Groups)
            {
                if(GroupIDs.Contains(G.GroupID) || (G.GroupID.IsValid() && (!Groups || !Groups->Contains(G.GroupID)))
                    || !MCActionSession::ValidPosition(G.Position) || !MCActionSession::ValidPosition(G.Size)
                    || G.Size.X<80 || G.Size.Y<60 || G.Size.X>10000 || G.Size.Y>10000
                    || !FMath::IsFinite(G.Color.R) || !FMath::IsFinite(G.Color.G) || !FMath::IsFinite(G.Color.B) || !FMath::IsFinite(G.Color.A))
                    return Fail(Error,TEXT("Расположение группы не соответствует этому событию."));
                GroupIDs.Add(G.GroupID);
            }
            const auto* Audio=EventDraft.IsValid()?EventDraft->SFXSlots.Find(E.Event):nullptr;
            for(const auto& P:E.SFXLayerPositions)
                if(!P.Key.IsValid() || !MCActionSession::ValidPosition(P.Value) || !Audio || !Audio->Layers.ContainsByPredicate([&](const auto& L){return L.ID==P.Key;}))
                    return Fail(Error,TEXT("Расположение ссылается на отсутствующий звуковой слой."));
        }
        return true;
    }
    FMCVFXEventGraphLayout& LayoutEvent(FName Event)
    {
        auto* E=LayoutState->Layout.Events.FindByPredicate([&](const auto& Entry){return Entry.Event==Event;});
        if(E)return *E;
        FMCVFXEventGraphLayout New;New.Event=Event;return LayoutState->Layout.Events.Add_GetRef(MoveTemp(New));
    }
    UMCVFXNativePresentationSettings* CopyNative(const TCHAR* Name)const
    {
        if(!NativeDraft.IsValid())return nullptr;
        auto* Copy=DuplicateObject<UMCVFXNativePresentationSettings>(NativeDraft.Get(),GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UMCVFXNativePresentationSettings::StaticClass(),Name));
        Copy->ClearFlags(RF_Public|RF_Standalone);Copy->SetFlags(RF_Transient|RF_Transactional);return Copy;
    }
    bool NativeResourcesSaved(FString& Error)
    {
        if(!NativeDraft.IsValid())return true;
        auto Check=[&](const TArray<FMCVFXNativePartSettings>& Parts)
        {
            for(const auto& Part:Parts)
            {
                TArray<UObject*> Objects;
                if(!Part.System.IsNull())Objects.Add(Part.System.LoadSynchronous());
                if(!Part.Material.IsNull())Objects.Add(Part.Material.LoadSynchronous());
                if(!Part.Mesh.IsNull())Objects.Add(Part.Mesh.LoadSynchronous());
                if(!Part.OpacityCurve.IsNull())Objects.Add(Part.OpacityCurve.LoadSynchronous());
                if(!Part.ExpansionCurve.IsNull())Objects.Add(Part.ExpansionCurve.LoadSynchronous());
                for(const auto* Object:Objects)if(!Object || Object->HasAnyFlags(RF_Transient) || Object->IsEditorOnly()
                    || Object->GetOutermost()->IsDirty() || !FPackageName::DoesPackageExist(Object->GetOutermost()->GetName()))
                    return Fail(Error,TEXT("Сначала сохраните исходный ресурс родной части отдельно: ")+Part.PartID.ToString());
            }
            return true;
        };
        for(const auto& Pair:NativeDraft->Events)if(!Check(Pair.Value.Parts))return false;
        return Check(NativeDraft->PersistentParts);
    }
    bool CanonicalProfileCurrent(FString& Error,bool bRequireAssignment=true)
    {
        if(!bHasCanonicalBinding)return !bSupportsProfileOverride || Fail(Error,TEXT("Для записи VFX этого контекста ещё не подключён источник данных."));
        if(bRequireAssignment && !MCVFXProfileBindings::ValidateUnchanged(CanonicalBinding,Error))return Fail(Error,Error);
        const auto* Current=CanonicalBinding.Profile.LoadSynchronous();
        if((Current && Current->GetOutermost()->IsDirty()) || MCActionSession::PackageDigest(Current)!=ProfileSourceDigest
            || (Current && !MCActionSession::SameSlots(Current,BaseProfile.Get())))
            return Fail(Error,TEXT("Назначенный VFX-профиль был изменён по прежнему адресу. Черновик не перезапишет его; откройте действие заново."));
        if(!NativeSource.IsNull())
        {
            const auto* Native=NativeSource.LoadSynchronous();
            if(!Native || Native->GetOutermost()->IsDirty() || Native->ComputeSettingsHash()!=NativeSourceSettingsHash
                || MCActionSession::PackageDigest(Native)!=NativeSourceDigest)
                return Fail(Error,TEXT("Исходная родная композиция изменена другим редактором. Откройте действие заново; черновик сохранён."));
        }
        for(const auto& Pair:Stages)if(!Pair.Value.SourceRuntimeComposition.IsNull())
        {
            auto* Runtime=Pair.Value.SourceRuntimeComposition.LoadSynchronous();
            if(!Runtime || Runtime->GetOutermost()->IsDirty() || MCActionSession::PackageDigest(Runtime)!=Pair.Value.RuntimeSourceDigest)
                return Fail(Error,TEXT("Композиция события изменена другим редактором. Откройте действие заново; старый черновик не перезапишет её."));
        }
        return true;
    }
    FStage* Stage(FName Name,FString& Error)
    {
        Error.Reset();
        if(!ContextCurrent()){Fail(Error,TEXT("Контекст действия изменился. Откройте действие заново; черновики сохранены в окне."));return nullptr;}
        if(!bSupportsProfileOverride){Fail(Error,TEXT("Этот сценарий проверяет настоящую механику, но её runtime-эффект пока не читает VFX-профиль. Назначение слоя отключено до подключения привязки."));return nullptr;}
        if(auto* S=Stages.Find(Name))return S;
        Fail(Error,TEXT("Неизвестное событие действия: ")+Name.ToString());return nullptr;
    }
    /** Reconcile actual authored values, so a global Undo of unrelated data or a
     * layout drag does not cancel a running mechanic or discard prepared systems. */
    bool ReconcileObserved()
    {
        bool bPlayback=!bObserved,bLayout=!bObserved;
        const uint64 NativeHash=NativeDraft.IsValid()?NativeDraft->ComputeSettingsHash():0;
        bPlayback|=NativeHash!=ObservedNativeHash;
        if(EventDraft.IsValid())
        {
            if(EventDraft->Slots.Num()!=ObservedSlots.Num())bPlayback=true;
            else for(const auto& Pair:EventDraft->Slots)
            {
                const auto* Other=ObservedSlots.Find(Pair.Key);
                if(!Other || !FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0)){bPlayback=true;break;}
            }
            if(EventDraft->SFXSlots.Num()!=ObservedAudio.Num())bPlayback=true;
            else for(const auto& Pair:EventDraft->SFXSlots)
            {
                const auto* Other=ObservedAudio.Find(Pair.Key);
                if(!Other || !FMCEventSFXSlot::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0)){bPlayback=true;break;}
            }
            bPlayback|=!MCActionSession::SamePlaybackGroups(EventDraft->EventGroups,ObservedGroups);
            ObservedSlots=EventDraft->Slots;ObservedAudio=EventDraft->SFXSlots;ObservedGroups=EventDraft->EventGroups;
        }
        for(auto& Pair:Stages)
        {
            auto& S=Pair.Value;
            if(!MCActionSession::SameRuntimeRecipe(S.Recipe.Get(),S.Observed.Get())){S.ClearCache();bPlayback=true;}
            bLayout|=!MCActionSession::SameMasterLayout(S.Recipe.Get(),S.Observed.Get());
            if(!S.Observed.IsValid())S.Observed.Reset(MCActionSession::CopyRecipe(S.Recipe.Get(),TEXT("ObservedActionRecipe")));
            else MCActionSession::CopyRecipeValues(S.Recipe.Get(),S.Observed.Get());
        }
        const FMCVFXActionLayout Current=LayoutState.IsValid()?LayoutState->Layout:FMCVFXActionLayout();
        bLayout|=!MCActionSession::SameLayout(Current,ObservedLayout);ObservedLayout=Current;
        ObservedNativeHash=NativeHash;bObserved=true;
        if(bPlayback)++PlaybackRevision;if(bLayout)++LayoutRevision;
        return bPlayback;
    }
    void Changed(FName StageName)
    {
        ReconcileObserved();++Revision;
        Status=TEXT("Черновик изменён. «Просмотреть действие» показывает правку; «Применить» сохраняет её в игру.");
    }
    bool MakeSlot(FName StageName,FStage& S,const UMCVFXRecipePreset* Recipe,FMCPerkVFXSlot& Slot,FString& Error)
    {
        if(!ValidateRecipeGroups(StageName,Recipe,Error))return false;
        Slot=S.Outer;
        if(!MCActionSession::HasEnabled(Recipe)){Slot.Mode=KeepNative(StageName)?EMCPerkVFXSlotMode::UseDefault:EMCPerkVFXSlotMode::Disabled;Slot.System.Reset();Slot.Composition.Reset();return true;}
        if(!Recipe->ValidateLayout(Error) || !Recipe->Validate(Error))return false;
        FString SourceDigest;
        for(const auto& Layer:Recipe->Layers)if(Layer.bEnabled)
        {
            auto* NS=Layer.System.LoadSynchronous();
            if(!NS || NS->GetOutermost()->IsDirty()){S.ClearCache();Error=TEXT("Сначала сохраните изменённую исходную Niagara отдельно, затем повторите просмотр.");return false;}
            SourceDigest+=NS->GetPathName()+TEXT(":")+MCActionSession::PackageDigest(NS)+TEXT(";");
        }
        if(!S.Composition.IsValid() || S.CompiledSourceDigest!=SourceDigest || !MCActionSession::SameRuntimeRecipe(Recipe,S.CompiledSnapshot.Get()))
        {
            S.ClearCache();S.CompiledSnapshot.Reset(MCActionSession::CopyRecipe(Recipe,TEXT("ActionCompositionSnapshot")));
            auto* Runtime=NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient);
            Runtime->Layers=Recipe->Layers;Runtime->DurationSeconds=Recipe->DurationSeconds;Runtime->TailSeconds=Recipe->TailSeconds;
            if(!Runtime->Validate(Error))return false;
            for(const auto& Layer:Runtime->Layers)if(Layer.bEnabled)
            {
                auto* NS=Layer.System.LoadSynchronous();
                if(!NS || !UMCVFXComposition::ValidateParameters(Layer,NS,Error))return false;
                NS->WaitForCompilationComplete(true,false);
                if(!NS->IsReadyToRun()){Error=TEXT("Исходная Niagara ещё не готова к воспроизведению.");return false;}
            }
            S.Composition.Reset(Runtime);S.CompiledSourceDigest=SourceDigest;
        }
        Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.System.Reset();Slot.Composition=S.Composition.Get();
        return UMCPerkVFXProfile::ValidateSlot(Slot,Error);
    }
    bool ValidateLayerGroup(FName Event,const FMCVFXCompositionLayer& Layer,FString& Error)
    {
        const auto* Groups=EventDraft.IsValid()?EventDraft->EventGroups.Find(Event):nullptr;
        if(Layer.GroupID.IsValid() && (!Groups || !Groups->Contains(Layer.GroupID)))
            return Fail(Error,TEXT("Группа VFX-слоя отсутствует в выбранном событии. Импортируйте весь пак или выберите существующую группу."));
        return true;
    }
    bool ValidateRecipeGroups(FName Event,const UMCVFXRecipePreset* Recipe,FString& Error)
    {
        if(Recipe)for(const auto& Layer:Recipe->Layers)if(!ValidateLayerGroup(Event,Layer,Error))return false;
        return true;
    }
    bool ApplyPreviewSolo(UMCPerkVFXProfile* Profile,FString& Error)
    {
        for(const auto& Pair:PreviewSolo)
        {
            if(Pair.Value.IsEmpty())continue;
            const auto* S=Stages.Find(Pair.Key);if(!S)return Fail(Error,TEXT("Событие соло больше не существует."));
            auto* Groups=Profile->EventGroups.Find(Pair.Key);
            for(FGuid ID:Pair.Value)if(ID.IsValid() && (!Groups || !Groups->Contains(ID)))
                return Fail(Error,TEXT("Группа соло больше не существует. Сбросьте соло и выберите группу заново."));
            if(Groups)for(auto& Group:Groups->Groups)Group.bEnabled&=Pair.Value.Contains(Group.ID);
            if(auto* Audio=Profile->SFXSlots.Find(Pair.Key))for(auto& Layer:Audio->Layers)
                Layer.bEnabled&=Pair.Value.Contains(Layer.GroupID);
            auto Slot=Profile->Slots.Contains(Pair.Key)?Profile->Slots.FindChecked(Pair.Key):S->Outer;
            // The raw composition is copied. Persistent group enable flags remain
            // orthogonal; their normal runtime preparation still runs afterwards.
            const UMCVFXComposition* Source=nullptr;
            if(Slot.Mode==EMCPerkVFXSlotMode::Composition)Source=Slot.Composition.LoadSynchronous();
            else if(Slot.Mode!=EMCPerkVFXSlotMode::Disabled)Source=S->Recipe.Get();
            if(!Source || Source->Layers.IsEmpty())continue; // Procedural/native base is not a cloneable user layer.
            auto* Filtered=NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient);
            Filtered->DurationSeconds=Source->DurationSeconds;Filtered->TailSeconds=Source->TailSeconds;Filtered->Layers=Source->Layers;
            for(auto& Layer:Filtered->Layers)Layer.bEnabled&=Pair.Value.Contains(Layer.GroupID);
            if(Filtered->Layers.ContainsByPredicate([](const auto& L){return L.bEnabled;}))
            {
                Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.System.Reset();Slot.Composition=Filtered;
                PreviewCompositions.Add(TStrongObjectPtr<UMCVFXComposition>(Filtered));
            }
            else {Slot.Mode=EMCPerkVFXSlotMode::Disabled;Slot.System.Reset();Slot.Composition.Reset();}
            if(!UMCPerkVFXProfile::ValidateSlot(Slot,Error))return false;
            Profile->Slots.Add(Pair.Key,MoveTemp(Slot));
        }
        return true;
    }
    bool PrepareProfile(FName TrialStage,UNiagaraSystem* Trial,FString& Error,USoundBase* TrialSound=nullptr,bool bApplyPreviewFilter=true,bool bRequireCanonicalAssignment=true)
    {
        if(!ContextCurrent())return Fail(Error,TEXT("Предпросмотр закрыт или профиль был заменён. Откройте действие заново."));
        // Mechanic-only adapters have no editable presentation contract yet.
        // Their real scenario must still run without inventing a VFX profile.
        if(!bSupportsProfileOverride){AuditionProfile.Reset();return true;}
        if(!CanonicalProfileCurrent(Error,bRequireCanonicalAssignment))return false;
        if(!NativeResourcesSaved(Error))return false;
        PreviewCompositions.Reset();
        auto* Profile=DuplicateObject<UMCPerkVFXProfile>(EventDraft.IsValid()?EventDraft.Get():BaseProfile.Get(),GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("ActionHubAudition")));
        Profile->ClearFlags(RF_Public|RF_Standalone);Profile->SetFlags(RF_Transient);
        AuditionProfile.Reset(Profile);
        if(EventDraft.IsValid()){Profile->SFXSlots=EventDraft->SFXSlots;Profile->EventGroups=EventDraft->EventGroups;}
        if(TrialSound)
        {
            auto& Audio=Profile->SFXSlots.FindOrAdd(TrialStage);
            if(Audio.Layers.Num()>=32)return Fail(Error,TEXT("В событии уже 32 звуковых слоя."));
            FMCEventSFXLayer Layer;Layer.Name=TrialSound->GetFName();Layer.Sound=TrialSound;Audio.Layers.Add(Layer);
            if(Audio.Mode==EMCEventSFXMode::UseDefault)Audio.Mode=EMCEventSFXMode::Add;
        }
        const auto* TrialState=Stages.Find(TrialStage);
        const bool bAddToNative=Trial && TrialState && TrialState->Outer.Mode==EMCPerkVFXSlotMode::UseDefault
            && NativeDraft.IsValid() && NativeDraft->Events.Contains(TrialStage);
        if(NativeOverride() || bAddToNative)
        {
            auto* Native=CopyNative(TEXT("ActionNativeAudition"));
            if(!Native)return Fail(Error,TEXT("Родная композиция не подготовлена."));
            Profile->NativePresentation=Native;
        }
        for(FName StageName:StageOrder)
        {
            auto& S=Stages.FindChecked(StageName);
            const bool bTrial=Trial && StageName==TrialStage;
            if(!S.IsRuntimeDirty() && !bTrial)continue;
            const UMCVFXRecipePreset* Recipe=S.Recipe.Get();
            TStrongObjectPtr<UMCVFXRecipePreset> TrialRecipe;
            if(bTrial)
            {
                TrialRecipe.Reset(MCActionSession::CopyRecipe(Recipe,TEXT("ActionGalleryTrial")));
                if(TrialRecipe->Layers.Num()>=32)return Fail(Error,TEXT("В событии уже 32 элемента; удалите один перед примеркой."));
                FMCVFXCompositionLayer L;L.Name=Trial->GetFName();L.System=Trial;
                TrialRecipe->Layers.Add(L);TrialRecipe->DiscoverParameters(TrialRecipe->Layers.Num()-1,Error);
                FMCVFXRecipeNodeLayout N;
                auto& TrialID=S.TrialIDs.FindOrAdd(FSoftObjectPath(Trial));if(!TrialID.IsValid())TrialID=FGuid::NewGuid();N.ID=TrialID;
                N.Position=FVector2D(TrialRecipe->Nodes.Num()*270,0);TrialRecipe->Nodes.Add(N);Recipe=TrialRecipe.Get();
            }
            FMCPerkVFXSlot Slot;
            if(!MakeSlot(StageName,S,Recipe,Slot,Error))return Fail(Error,StageName.ToString()+TEXT(": ")+Error);
            Profile->Slots.Add(StageName,Slot);
        }
        if(bApplyPreviewFilter && !ApplyPreviewSolo(Profile,Error))return Fail(Error,Error);
        if(!Profile->ValidateForEvents(CanonicalBinding.AllowedEvents,Error))return Fail(Error,Error);
        for(const auto& Pair:Profile->SFXSlots)if(!MCEventSFX::ValidateNativePolicy(Pair.Value,false,Error))return Fail(Error,Error);
        return true;
    }
};

FMCVFXActionSession::FMCVFXActionSession():Impl(MakeUnique<FImpl>())
{if(GEditor)GEditor->RegisterForUndo(this);}
FMCVFXActionSession::~FMCVFXActionSession(){Stop();if(GEditor)GEditor->UnregisterForUndo(this);}
void FMCVFXActionSession::Clear()
{Stop();const uint64 Next=Impl->Revision+1,Play=Impl->PlaybackRevision+1,Layout=Impl->LayoutRevision+1;Impl=MakeUnique<FImpl>();Impl->Revision=Next;Impl->PlaybackRevision=Play;Impl->LayoutRevision=Layout;}

TArray<FMCVFXActionCatalogueEntry> FMCVFXActionSession::GetCatalogue()
{return MCVFXActionCatalog::GetCatalogue();}
TArray<FName> FMCVFXActionSession::GetStages(){return {TEXT("Start"),TEXT("Loop"),TEXT("Impact"),TEXT("End")};}
FText FMCVFXActionSession::GetStageLabel(FName Stage)
{
    if(Stage==TEXT("Start"))return FText::FromString(TEXT("Выстрел"));
    if(Stage==TEXT("Loop"))return FText::FromString(TEXT("Полёт ракеты"));
    if(Stage==TEXT("Impact"))return FText::FromString(TEXT("Попадание"));
    return FText::FromString(TEXT("Завершение"));
}

bool FMCVFXActionSession::Initialize(FName ActionID,FString& Error)
{
    Error.Reset();
    const auto* Adapter=MCPerkPreviewAdapters::Find(ActionID);
    if(!Adapter || Adapter->Stages.IsEmpty())return Impl->Fail(Error,TEXT("Для этого действия ещё не зарегистрированы редактируемые события."));
    const bool HasPreview=Adapter->Create && !Adapter->Scenarios.IsEmpty();
    const FName RowID=Adapter->Kind==EMCPerkPreviewAdapterKind::Context?Adapter->SourceRowID:(Adapter->SourceRowID.IsNone()?ActionID:Adapter->SourceRowID);
    auto* T=MCActionSession::Table();const auto* Row=T&&!RowID.IsNone()?T->FindRow<FMCPerkDefinition>(RowID,TEXT("ActionHub"),false):nullptr;
    const auto* CDO=Row?MCActionSession::ActionDefaults(*Row):nullptr;
    if(!RowID.IsNone() && (!Row || !Row->bEnabled))return Impl->Fail(Error,TEXT("Игровое действие недоступно в DT_Perks."));
    Stop();
    if(HasPreview&&!MCPerkActionPreviewPanel::EnsureContext(ActionID,Error))return Impl->Fail(Error,Error);
    Impl->Stages.Reset();Impl->ActionID=ActionID;Impl->CanonicalRowID=RowID;
    Impl->StageOrder=Adapter->Stages;Impl->StageLabels=Adapter->StageLabels;Impl->ActionLabel=Adapter->Label;
    Impl->bSupportsProfileOverride=Adapter->bSupportsProfileOverride;
    Impl->bHasContextPreview=HasPreview;
    Impl->bHasCanonicalBinding=false;Impl->CanonicalBinding=FMCVFXCanonicalProfileState();
    if(Impl->bSupportsProfileOverride)
    {
        if(!MCVFXProfileBindings::ReadCanonical(ActionID,Impl->CanonicalBinding,Error))return Impl->Fail(Error,Error);
        Impl->bHasCanonicalBinding=true;Impl->CanonicalRowID=Impl->CanonicalBinding.CanonicalRowID;
        TSet<FName> Seen;
        for(FName Stage:Impl->StageOrder)
        {
            if(Stage.IsNone() || Seen.Contains(Stage) || !Impl->CanonicalBinding.AllowedEvents.Contains(Stage))
                return Impl->Fail(Error,TEXT("Сценарий и источник профиля объявляют разные VFX-события."));
            Seen.Add(Stage);
        }
    }
    Impl->TableBaseline=Impl->bHasCanonicalBinding?Impl->CanonicalBinding.Profile:Row?Row->VFXProfile:TSoftObjectPtr<UMCPerkVFXProfile>();
    auto* Source=Impl->TableBaseline.LoadSynchronous();
    if(!Impl->TableBaseline.IsNull() && !Source)return Impl->Fail(Error,TEXT("Не удалось загрузить назначенный игре VFX-профиль."));
    if(Source && Source->GetOutermost()->IsDirty())return Impl->Fail(Error,TEXT("Назначенный VFX-профиль имеет несохранённые изменения."));
    auto* Base=Source?DuplicateObject<UMCPerkVFXProfile>(Source,GetTransientPackage()):NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    Base->ClearFlags(RF_Public|RF_Standalone);Base->SetFlags(RF_Transient);Impl->BaseProfile.Reset(Base);Impl->ProfileSourceDigest=MCActionSession::PackageDigest(Source);
    Impl->EventDraft.Reset(DuplicateObject<UMCPerkVFXProfile>(Base,GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("ActionEventDraft"))));
    Impl->EventDraft->ClearFlags(RF_Public|RF_Standalone);Impl->EventDraft->SetFlags(RF_Transient|RF_Transactional);
    Impl->LoadedPack.Reset();Impl->PackBaseline.Reset();Impl->PackRecipeBaselines.Reset();Impl->PackLayoutBaseline=FMCVFXActionLayout();Impl->PackNativeBaseline.Reset();
    Impl->LayoutState.Reset(NewObject<UMCVFXActionLayoutState>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional));
    Impl->LayoutBaseline=FMCVFXActionLayout();Impl->ObservedLayout=FMCVFXActionLayout();Impl->PreviewSolo.Reset();Impl->bObserved=false;
    Impl->NativeDraft.Reset();Impl->NativeBaseline.Reset();Impl->NativeSource=Base->NativePresentation;
    Impl->NativeSourceDigest.Reset();Impl->NativeSourceSettingsHash=0;Impl->NativeDraftToken=FGuid::NewGuid();
    if(!Impl->NativeSource.IsNull())
    {
        const auto* Native=Impl->NativeSource.LoadSynchronous();
        if(!Native || Native->GetOutermost()->IsDirty() || !Native->ValidateForSource(ActionID,Impl->StageOrder,Error))
            return Impl->Fail(Error,TEXT("Родная композиция недоступна или изменена: ")+Error);
        auto* Draft=DuplicateObject<UMCVFXNativePresentationSettings>(Native,GetTransientPackage());
        Draft->ClearFlags(RF_Public|RF_Standalone);Draft->SetFlags(RF_Transient|RF_Transactional);Impl->NativeDraft.Reset(Draft);
        Impl->NativeSourceDigest=MCActionSession::PackageDigest(Native);Impl->NativeSourceSettingsHash=Native->ComputeSettingsHash();
    }
    else
    {
        bool bNativeContract=!UMCVFXNativePresentationSettings::DescribeParts(ActionID,NAME_None).IsEmpty();
        for(FName Stage:Impl->StageOrder)bNativeContract|=!UMCVFXNativePresentationSettings::DescribeParts(ActionID,Stage).IsEmpty();
        if(bNativeContract)
        {
            auto* Native=NewObject<UMCVFXNativePresentationSettings>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional);
            if(!Native->InitFromNativeDefaults(ActionID,Error))return Impl->Fail(Error,Error);
            // This is a working default, not an assigned override. A first
            // added layer retains the visible native base; an explicit checkbox
            // choice remains authoritative instead of being overwritten by Add.
            for(auto& Pair:Native->Events)
            {
                const auto* Slot=Base->Slots.Find(Pair.Key);
                Pair.Value.bKeepNativeAccentsWithComposition=!Slot || Slot->Mode==EMCPerkVFXSlotMode::UseDefault;
            }
            Impl->NativeDraft.Reset(Native);
        }
    }
    if(Impl->NativeDraft.IsValid())
    {
        // Curve values participate in the prepared-art fingerprint. Resolve
        // saved dependencies before recording either authoring baseline.
        if(!Impl->NativeResourcesSaved(Error))return false;
        if(const auto* Native=Impl->NativeSource.Get())Impl->NativeSourceSettingsHash=Native->ComputeSettingsHash();
        Impl->NativeBaseline.Reset(Impl->CopyNative(TEXT("ActionNativeBaseline")));
    }
    for(FName StageName:Impl->StageOrder)
    {
        FImpl::FStage S;if(Adapter->DescribeNativeSources)S.NativeSources=Adapter->DescribeNativeSources(StageName);
        if(const auto* Outer=Base->Slots.Find(StageName))
        {
            // Native UseDefault/Disabled ignore dormant transforms and overrides.
            // Do not accidentally reactivate old Replace settings when adding a layer.
            S.Outer=MCActionSession::EffectiveOuter(*Outer);
        }
        auto* Recipe=NewObject<UMCVFXRecipePreset>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional);
        Recipe->PresetName=GetEventLabel(StageName).ToString();
        UNiagaraSystem* Effective=nullptr;
        if(S.Outer.Mode==EMCPerkVFXSlotMode::Replace)Effective=S.Outer.System.LoadSynchronous();
        else if(S.Outer.Mode==EMCPerkVFXSlotMode::UseDefault)
        {
            const auto* NativeDefault=Adapter->DefaultSystems.Find(StageName);
            Effective=NativeDefault?NativeDefault->LoadSynchronous():MCActionSession::DefaultSystem(CDO,StageName).LoadSynchronous();
        }
        if(S.Outer.Mode==EMCPerkVFXSlotMode::Replace && !Effective)return Impl->Fail(Error,StageName.ToString()+TEXT(": назначенная Niagara не загружается."));
        FString MasterPath;
        if(Source && S.Outer.Mode!=EMCPerkVFXSlotMode::UseDefault)MasterPath=Source->GetOutermost()->GetMetaData().GetValue(Source,*MCActionSession::StageMasterKey(StageName));
        UMCVFXComposition* RuntimeSource=nullptr;
        if(S.Outer.Mode==EMCPerkVFXSlotMode::Composition)
        {
            RuntimeSource=S.Outer.Composition.LoadSynchronous();
            if(!RuntimeSource || RuntimeSource->GetOutermost()->IsDirty() || !RuntimeSource->Validate(Error))
                return Impl->Fail(Error,StageName.ToString()+TEXT(": сохранённая runtime-композиция недоступна или изменена. ")+Error);
            S.SourceRuntimeComposition=RuntimeSource;S.RuntimeSourceDigest=MCActionSession::PackageDigest(RuntimeSource);
            const FString RuntimeMaster=RuntimeSource->GetOutermost()->GetMetaData().GetValue(RuntimeSource,TEXT("MCComposerSourceRecipe"));
            if(!RuntimeMaster.IsEmpty())
            {
                if(!MasterPath.IsEmpty() && MasterPath!=RuntimeMaster)return Impl->Fail(Error,TEXT("Композиция и профиль ссылаются на разные авторские исходники."));
                MasterPath=RuntimeMaster;
            }
            if(MasterPath.IsEmpty())
            {
                Recipe->Layers=RuntimeSource->Layers;Recipe->DurationSeconds=RuntimeSource->DurationSeconds;Recipe->TailSeconds=RuntimeSource->TailSeconds;
                for(int32 I=0;I<Recipe->Layers.Num();++I){FMCVFXRecipeNodeLayout N;N.ID=FGuid::NewGuid();N.Position=FVector2D(I*270,0);Recipe->Nodes.Add(N);}
            }
        }
        if(Effective)
        {
            const FString SourcePath=Effective->GetOutermost()->GetMetaData().GetValue(Effective,TEXT("MCComposerSourceRecipe"));
            const FString Version=Effective->GetOutermost()->GetMetaData().GetValue(Effective,TEXT("MCComposerBuilderVersion"));
            if(!MasterPath.IsEmpty() && MasterPath!=SourcePath)return Impl->Fail(Error,StageName.ToString()+TEXT(": ссылка на исходник профиля не совпадает с назначенной Niagara."));
            if(!SourcePath.IsEmpty() || !Version.IsEmpty())
            {
                MasterPath=SourcePath;
                if(MasterPath.IsEmpty())return Impl->Fail(Error,StageName.ToString()+TEXT(": у экспортированной Niagara отсутствует исходник; плоская подмена отклонена."));
            }
            if(MasterPath.IsEmpty())
            {
                FMCVFXCompositionLayer Layer;Layer.Name=Effective->GetFName();Layer.System=Effective;Recipe->Layers.Add(Layer);
                FMCVFXRecipeNodeLayout Node;Node.ID=FGuid::NewGuid();Recipe->Nodes.Add(Node);
                Recipe->DiscoverParameters(0,Error);
            }
        }
        if(!MasterPath.IsEmpty())
        {
            auto* Master=MasterPath.StartsWith(TEXT("/Game/"))?LoadObject<UMCVFXRecipePreset>(nullptr,*MasterPath):nullptr;
            if(!Master || Master->HasAnyFlags(RF_Transient) || Master->GetOutermost()->IsDirty() || !Master->ValidateLayout(Error))
                return Impl->Fail(Error,StageName.ToString()+TEXT(": исходник композиции не восстановлен: ")+MasterPath+TEXT(". ")+Error);
            if(RuntimeSource && (!MCActionSession::SameValues(Master->Layers,RuntimeSource->Layers,FMCVFXCompositionLayer::StaticStruct())
                || Master->DurationSeconds!=RuntimeSource->DurationSeconds || Master->TailSeconds!=RuntimeSource->TailSeconds))
                return Impl->Fail(Error,TEXT("Авторский исходник не совпадает с сохранённой runtime-композицией. Пересохраните их явно."));
            MCActionSession::CopyRecipeValues(Master,Recipe);S.SourceMasterPath=MasterPath;
        }
        S.Recipe.Reset(Recipe);S.Baseline.Reset(MCActionSession::CopyRecipe(Recipe,TEXT("ActionStageBaseline")));
        S.MasterBaseline.Reset(MCActionSession::CopyRecipe(Recipe,TEXT("ActionMasterBaseline")));
        Impl->Stages.Add(StageName,MoveTemp(S));
    }
    Impl->ReconcileObserved();++Impl->Revision;Impl->Status=HasPreview?TEXT("Выберите событие. Примерка временная; «Добавить» меняет черновик, «Применить» — игру."):
        TEXT("Доступны сохранённые VFX и исходники. Контекстный предпросмотр ещё не подключён.");Error.Reset();return true;
}

FName FMCVFXActionSession::GetActionID()const{return Impl->ActionID;}
bool FMCVFXActionSession::IsContextCurrent()const{return Impl->ContextCurrent();}
bool FMCVFXActionSession::CanPreview()const
{
    const auto* A=MCPerkPreviewAdapters::Find(Impl->ActionID);
    return IsContextCurrent()&&Impl->bHasContextPreview&&A&&A->Create&&!A->Scenarios.IsEmpty();
}
FString FMCVFXActionSession::GetPreviewUnavailableReason()const
{
    if(CanPreview())return FString();
    return IsContextCurrent()?TEXT("Контекстный игровой сценарий для этого действия ещё не подключён. Доступны редактирование сохранённых VFX и открытие исходников.")
        :TEXT("Контекст действия изменился. Выберите действие заново.");
}
bool FMCVFXActionSession::CanEditProfile()const{return IsContextCurrent() && Impl->bSupportsProfileOverride && Impl->bHasCanonicalBinding;}
FString FMCVFXActionSession::GetEditUnavailableReason()const
{
    if(!IsContextCurrent())return TEXT("Контекст действия изменился. Выберите действие заново, чтобы восстановить предпросмотр.");
    if(!Impl->bSupportsProfileOverride)return TEXT("Runtime этого эффекта пока не читает VFX-профиль: добавление и применение слоёв ещё не подключены.");
    if(!Impl->bHasCanonicalBinding)return TEXT("Источник VFX-профиля недоступен. Откройте действие заново после проверки его сохранённых данных.");
    return FString();
}
TArray<FName> FMCVFXActionSession::GetActionStages()const{return Impl->StageOrder;}
FText FMCVFXActionSession::GetEventLabel(FName Stage)const
{
    if(const auto* Label=Impl->StageLabels.Find(Stage))return *Label;
    if(Impl->ActionID==MCActionSession::RocketID)return GetStageLabel(Stage);
    if(Stage==TEXT("Start"))return FText::FromString(TEXT("Начало"));
    if(Stage==TEXT("Loop"))return FText::FromString(TEXT("Действие"));
    if(Stage==TEXT("Warning"))return FText::FromString(TEXT("Предупреждение"));
    if(Stage==TEXT("Impact"))return FText::FromString(TEXT("Попадание"));
    if(Stage==TEXT("End"))return FText::FromString(TEXT("Завершение"));
    return FText::FromName(Stage);
}
FText FMCVFXActionSession::GetActionLabel()const{return Impl->ActionLabel;}
FSoftObjectPath FMCVFXActionSession::GetCanonicalOwner()const{return Impl->bHasCanonicalBinding?Impl->CanonicalBinding.OwnerAsset:FSoftObjectPath();}
TArray<EMCPerkPreviewScenario> FMCVFXActionSession::GetScenarios()const
{const auto* Adapter=MCPerkPreviewAdapters::Find(Impl->ActionID);return Adapter?Adapter->Scenarios:TArray<EMCPerkPreviewScenario>();}
FText FMCVFXActionSession::GetScenarioLabel(EMCPerkPreviewScenario Scenario)const
{
    if(const auto* Adapter=MCPerkPreviewAdapters::Find(Impl->ActionID))if(const auto* Label=Adapter->ScenarioLabels.Find(Scenario))return *Label;
    return FText::FromString(Scenario==EMCPerkPreviewScenario::Target?TEXT("Основной сценарий"):Scenario==EMCPerkPreviewScenario::NearWall?TEXT("Препятствие"):TEXT("Удержание / завершение"));
}
FString FMCVFXActionSession::GetStatus()const{return Impl->Status;}
uint64 FMCVFXActionSession::GetRevision()const{return Impl->Revision;}
uint64 FMCVFXActionSession::GetPlaybackRevision()const{return Impl->PlaybackRevision;}
uint64 FMCVFXActionSession::GetLayoutRevision()const{return Impl->LayoutRevision;}
FGuid FMCVFXActionSession::GetDraftToken()const{return Impl->NativeDraftToken;}
bool FMCVFXActionSession::IsRuntimeDirty()const
{if(Impl->NativeDirty() || Impl->EventDraftDirty())return true;for(const auto& P:Impl->Stages)if(P.Value.IsRuntimeDirty())return true;return false;}
bool FMCVFXActionSession::IsMasterLayoutDirty()const
{if(Impl->LayoutState.IsValid() && !MCActionSession::SameLayout(Impl->LayoutState->Layout,Impl->LayoutBaseline))return true;for(const auto& P:Impl->Stages)if(P.Value.IsLayoutDirty())return true;return false;}
bool FMCVFXActionSession::IsDirty()const{return IsRuntimeDirty() || IsMasterLayoutDirty();}
const FMCEventPresentationGroupSet* FMCVFXActionSession::GetEventGroups(FName Event)const
{return Impl->EventDraft.IsValid()?Impl->EventDraft->EventGroups.Find(Event):nullptr;}
FGuid FMCVFXActionSession::CreateEventGroup(FGuid Token,FName Event,FName Name,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return FGuid();
    auto Groups=GetEventGroups(Event)?*GetEventGroups(Event):FMCEventPresentationGroupSet();
    FMCEventPresentationGroup Group;Group.Name=Name;Groups.Groups.Add(Group);
    if(!Groups.Validate(Error))return FGuid();
    const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","CreateGroup","Создать общую группу VFX и SFX"));
    Impl->EventDraft->Modify();Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));Impl->Changed(Event);return Group.ID;
}
bool FMCVFXActionSession::RenameEventGroup(FGuid Token,FName Event,FGuid Group,FName Name,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetEventGroups(Event);if(!Existing || !Existing->Find(Group))return Impl->Fail(Error,TEXT("Группа события больше не существует."));
    auto Groups=*Existing;auto* G=Groups.Groups.FindByPredicate([&](const auto& Entry){return Entry.ID==Group;});
    if(G->Name==Name)return true;G->Name=Name;if(!Groups.Validate(Error))return false;
    const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","RenameGroup","Переименовать группу события"));
    Impl->EventDraft->Modify();Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));Impl->Changed(Event);return true;
}
bool FMCVFXActionSession::SetEventGroupEnabled(FGuid Token,FName Event,FGuid Group,bool bEnabled,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetEventGroups(Event);if(!Existing || !Existing->Find(Group))return Impl->Fail(Error,TEXT("Группа события больше не существует."));
    auto Groups=*Existing;auto* G=Groups.Groups.FindByPredicate([&](const auto& Entry){return Entry.ID==Group;});
    if(G->bEnabled==bEnabled)return true;G->bEnabled=bEnabled;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","EnableGroup","Включить или выключить группу события"));
    Impl->EventDraft->Modify();Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));Impl->Changed(Event);return true;
}
bool FMCVFXActionSession::SetEventGroupAudioMuted(FGuid Token,FName Event,FGuid Group,bool bMuted,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetEventGroups(Event);if(!Existing || !Existing->Find(Group))return Impl->Fail(Error,TEXT("Группа события больше не существует."));
    auto Groups=*Existing;auto* G=Groups.Groups.FindByPredicate([&](const auto& Entry){return Entry.ID==Group;});
    if(G->bMuteSFX==bMuted)return true;G->bMuteSFX=bMuted;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","MuteGroup","Отключить звук группы события"));
    Impl->EventDraft->Modify();Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));Impl->Changed(Event);return true;
}
bool FMCVFXActionSession::AssignLayerToGroup(FGuid Token,FName Event,EMCComposerLayerLane Lane,FGuid Layer,FGuid Group,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Groups=GetEventGroups(Event);
    if(Group.IsValid() && (!Groups || !Groups->Contains(Group)))return Impl->Fail(Error,TEXT("Целевая группа не принадлежит этому событию."));
    if(Lane==EMCComposerLayerLane::VFX)
    {
        auto* S=Impl->Stages.Find(Event);const int32 Index=S?S->Recipe->FindNode(Layer):INDEX_NONE;
        if(Index==INDEX_NONE)return Impl->Fail(Error,TEXT("Перемещаемый VFX-слой больше не существует."));
        auto Copy=S->Recipe->Layers[Index];Copy.GroupID=Group;return SetLayer(Event,Layer,Copy,Error);
    }
    if(Lane!=EMCComposerLayerLane::SFX)return Impl->Fail(Error,TEXT("Неизвестная полоса слоя."));
    const auto* Slot=GetSFXSlot(Event);const auto* Found=Slot?Slot->Layers.FindByPredicate([&](const auto& L){return L.ID==Layer;}):nullptr;
    if(!Found)return Impl->Fail(Error,TEXT("Перемещаемый SFX-слой больше не существует."));
    auto Copy=*Found;Copy.GroupID=Group;return SetSFXLayer(Token,Event,Layer,Copy,Error);
}
FGuid FMCVFXActionSession::DuplicateEventGroup(FGuid Token,FName Event,FGuid Group,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return FGuid();
    const auto* Existing=GetEventGroups(Event);const auto* Original=Existing?Existing->Find(Group):nullptr;
    auto* S=Impl->Stages.Find(Event);
    if(!Original || !S || !S->Recipe->ValidateLayout(Error)){Impl->Fail(Error,TEXT("Группа для копирования больше не существует."));return FGuid();}
    auto Groups=*Existing;auto NewGroup=*Original;NewGroup.ID=FGuid::NewGuid();NewGroup.Name=FName(*(NewGroup.Name.ToString().Left(90)+TEXT("_Copy")));
    Groups.Groups.Add(NewGroup);if(!Groups.Validate(Error))return FGuid();
    auto Audio=GetSFXSlot(Event)?*GetSFXSlot(Event):FMCEventSFXSlot();
    auto Layers=S->Recipe->Layers;auto Nodes=S->Recipe->Nodes;
    TMap<FGuid,FGuid> AudioRemap;
    const int32 VCount=Layers.Num(),ACount=Audio.Layers.Num();
    for(int32 I=0;I<VCount;++I)if(Layers[I].GroupID==Group)
    {
        auto L=Layers[I];auto N=Nodes[I];L.GroupID=NewGroup.ID;N.ID=FGuid::NewGuid();N.Position+=FVector2D(40,40);
        if(!MCActionSession::ValidPosition(N.Position)){Impl->Fail(Error,TEXT("Копия выйдет за пределы графа. Переместите исходную группу ближе к центру."));return FGuid();}
        // Recipe comment-frame membership is a separate editor feature, not the runtime group.
        Layers.Add(MoveTemp(L));Nodes.Add(MoveTemp(N));
    }
    for(int32 I=0;I<ACount;++I)if(Audio.Layers[I].GroupID==Group)
    {
        auto L=Audio.Layers[I];const FGuid Old=L.ID;L.ID=FGuid::NewGuid();L.GroupID=NewGroup.ID;
        AudioRemap.Add(Old,L.ID);Audio.Layers.Add(MoveTemp(L));
    }
    if(Layers.Num()>32 || Audio.Layers.Num()>32 || !Audio.Validate(Error))
    {if(Error.IsEmpty())Error=TEXT("Копия превысит 32 VFX- или SFX-слоя события.");return FGuid();}
    if(const auto* Layout=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& E){return E.Event==Event;}))
    {
        if(const auto* Source=Layout->Groups.FindByPredicate([&](const auto& G){return G.GroupID==Group;}))
            if(!MCActionSession::ValidPosition(Source->Position+FVector2D(40,40))){Impl->Fail(Error,TEXT("Расположение копии выйдет за пределы графа."));return FGuid();}
        for(const auto& Pair:AudioRemap)if(const auto* P=Layout->SFXLayerPositions.Find(Pair.Key))
            if(!MCActionSession::ValidPosition(*P+FVector2D(40,40))){Impl->Fail(Error,TEXT("Расположение звуковой копии выйдет за пределы графа."));return FGuid();}
    }
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","DuplicateGroup","Дублировать группу с VFX и SFX"));
    Impl->EventDraft->Modify();S->Recipe->Modify();Impl->LayoutState->Modify();
    Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));
    if(GetSFXSlot(Event))Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Audio));
    S->Recipe->Layers=MoveTemp(Layers);S->Recipe->Nodes=MoveTemp(Nodes);
    auto& Layout=Impl->LayoutEvent(Event);
    if(const auto* Source=Layout.Groups.FindByPredicate([&](const auto& G){return G.GroupID==Group;}))
    {auto Copy=*Source;Copy.GroupID=NewGroup.ID;Copy.Position+=FVector2D(40,40);Copy.Order=Copy.Order<MAX_int32?Copy.Order+1:Copy.Order;Layout.Groups.Add(MoveTemp(Copy));}
    for(const auto& Pair:AudioRemap)if(const auto* P=Layout.SFXLayerPositions.Find(Pair.Key))
    {const FVector2D Copy=*P+FVector2D(40,40);Layout.SFXLayerPositions.Add(Pair.Value,Copy);}
    Impl->Changed(Event);return NewGroup.ID;
}
bool FMCVFXActionSession::RemoveEventGroup(FGuid Token,FName Event,FGuid Group,EMCComposerGroupRemoval Policy,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetEventGroups(Event);
    if(!Group.IsValid() || !Existing || !Existing->Find(Group))return Impl->Fail(Error,TEXT("Удаляемая группа больше не существует. «Без группы» — закреплённая полоса."));
    if(Policy!=EMCComposerGroupRemoval::UngroupChildren && Policy!=EMCComposerGroupRemoval::DeleteChildren)return Impl->Fail(Error,TEXT("Неизвестный способ удаления группы."));
    auto* S=Impl->Stages.Find(Event);if(!S || !S->Recipe->ValidateLayout(Error))return false;
    auto Groups=*Existing;Groups.Groups.RemoveAll([&](const auto& G){return G.ID==Group;});
    auto Layers=S->Recipe->Layers;auto Nodes=S->Recipe->Nodes;
    auto Audio=GetSFXSlot(Event)?*GetSFXSlot(Event):FMCEventSFXSlot();TSet<FGuid> RemovedAudio;
    for(int32 I=Layers.Num()-1;I>=0;--I)if(Layers[I].GroupID==Group)
    {if(Policy==EMCComposerGroupRemoval::DeleteChildren){Layers.RemoveAt(I);Nodes.RemoveAt(I);}else Layers[I].GroupID.Invalidate();}
    for(int32 I=Audio.Layers.Num()-1;I>=0;--I)if(Audio.Layers[I].GroupID==Group)
    {if(Policy==EMCComposerGroupRemoval::DeleteChildren){RemovedAudio.Add(Audio.Layers[I].ID);Audio.Layers.RemoveAt(I);}else Audio.Layers[I].GroupID.Invalidate();}
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","RemoveGroup","Удалить общую группу события"));
    Impl->EventDraft->Modify();S->Recipe->Modify();Impl->LayoutState->Modify();
    if(Groups.Groups.IsEmpty() && !Impl->BaseProfile->EventGroups.Contains(Event))Impl->EventDraft->EventGroups.Remove(Event);
    else Impl->EventDraft->EventGroups.Add(Event,MoveTemp(Groups));
    if(GetSFXSlot(Event))Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Audio));
    S->Recipe->Layers=MoveTemp(Layers);S->Recipe->Nodes=MoveTemp(Nodes);
    if(auto* Layout=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& E){return E.Event==Event;}))
    {Layout->Groups.RemoveAll([&](const auto& G){return G.GroupID==Group;});for(const FGuid ID:RemovedAudio)Layout->SFXLayerPositions.Remove(ID);}
    if(auto* Solo=Impl->PreviewSolo.Find(Event)){Solo->Remove(Group);if(Solo->IsEmpty())Impl->PreviewSolo.Remove(Event);}
    Impl->Changed(Event);return true;
}
FMCVFXActionLayout FMCVFXActionSession::GetLayoutSnapshot()const
{return Impl->LayoutState.IsValid()?Impl->LayoutState->Layout:FMCVFXActionLayout();}
bool FMCVFXActionSession::SetLayoutSnapshot(FGuid Token,const FMCVFXActionLayout& Layout,FString& Error)
{
    if(!Impl->LayoutEditScope(Token,NAME_None,Error) || !Impl->ValidateLayout(Layout,Error))return false;
    if(MCActionSession::SameLayout(Impl->LayoutState->Layout,Layout))return true;
    const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","GraphLayout","Переместить элементы графа"));
    Impl->LayoutState->Modify();Impl->LayoutState->Layout=Layout;Impl->Changed(NAME_None);
    Impl->Status=TEXT("Изменено только расположение графа. Игровые эффекты и время запуска не изменились.");return true;
}
bool FMCVFXActionSession::GetActionHubPosition(FVector2D& Out)const
{if(!Impl->LayoutState.IsValid() || !Impl->LayoutState->Layout.bHasHubPosition)return false;Out=Impl->LayoutState->Layout.HubPosition;return true;}
bool FMCVFXActionSession::SetActionHubPosition(FGuid Token,FVector2D P,FString& Error)
{auto Layout=GetLayoutSnapshot();Layout.bHasHubPosition=true;Layout.HubPosition=P;return SetLayoutSnapshot(Token,Layout,Error);}
bool FMCVFXActionSession::GetEventCardPosition(FName Event,FVector2D& Out)const
{
    if(!Impl->LayoutState.IsValid())return false;
    const auto* E=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    if(!E || !E->bHasCardPosition)return false;Out=E->CardPosition;return true;
}
bool FMCVFXActionSession::SetEventCardPosition(FGuid Token,FName Event,FVector2D P,FString& Error)
{
    if(!Impl->LayoutEditScope(Token,Event,Error))return false;
    auto Layout=GetLayoutSnapshot();auto* E=Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    if(!E){FMCVFXEventGraphLayout New;New.Event=Event;E=&Layout.Events.Add_GetRef(MoveTemp(New));}
    E->bHasCardPosition=true;E->CardPosition=P;return SetLayoutSnapshot(Token,Layout,Error);
}
bool FMCVFXActionSession::GetEventGroupLayout(FName Event,FGuid Group,FMCComposerGroupLayout& Out)const
{
    if(!Impl->LayoutState.IsValid())return false;
    const auto* E=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    const auto* G=E?E->Groups.FindByPredicate([&](const auto& L){return L.GroupID==Group;}):nullptr;
    if(!G)return false;Out=*G;return true;
}
bool FMCVFXActionSession::SetEventGroupLayout(FGuid Token,FName Event,FGuid Group,const FMCComposerGroupLayout& GroupLayout,FString& Error)
{
    if(!Impl->LayoutEditScope(Token,Event,Error))return false;
    if(GroupLayout.GroupID!=Group)return Impl->Fail(Error,TEXT("Расположение принадлежит другой группе."));
    auto Layout=GetLayoutSnapshot();auto* E=Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    if(!E){FMCVFXEventGraphLayout New;New.Event=Event;E=&Layout.Events.Add_GetRef(MoveTemp(New));}
    auto* G=E->Groups.FindByPredicate([&](const auto& L){return L.GroupID==Group;});if(G)*G=GroupLayout;else E->Groups.Add(GroupLayout);
    return SetLayoutSnapshot(Token,Layout,Error);
}
bool FMCVFXActionSession::GetLayerGraphPosition(FName Event,EMCComposerLayerLane Lane,FGuid Layer,FVector2D& Out)const
{
    if(Lane==EMCComposerLayerLane::VFX)
    {const auto* S=Impl->Stages.Find(Event);const int32 I=S?S->Recipe->FindNode(Layer):INDEX_NONE;if(I==INDEX_NONE)return false;Out=S->Recipe->Nodes[I].Position;return true;}
    if(Lane!=EMCComposerLayerLane::SFX || !Impl->LayoutState.IsValid())return false;
    const auto* E=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    const auto* P=E?E->SFXLayerPositions.Find(Layer):nullptr;if(!P)return false;Out=*P;return true;
}
bool FMCVFXActionSession::SetLayerGraphPosition(FGuid Token,FName Event,EMCComposerLayerLane Lane,FGuid Layer,FVector2D P,FString& Error)
{
    if(!Impl->LayoutEditScope(Token,Event,Error))return false;
    if(!MCActionSession::ValidPosition(P))return Impl->Fail(Error,TEXT("Некорректное положение слоя."));
    if(Lane==EMCComposerLayerLane::VFX)
    {
        auto* S=Impl->Stages.Find(Event);const int32 I=S?S->Recipe->FindNode(Layer):INDEX_NONE;
        if(I==INDEX_NONE)return Impl->Fail(Error,TEXT("VFX-слой больше не существует."));
        if(S->Recipe->Nodes[I].Position==P)return true;
        const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","VFXNodePosition","Переместить VFX-слой"));
        S->Recipe->Modify();S->Recipe->Nodes[I].Position=P;Impl->Changed(Event);return true;
    }
    if(Lane!=EMCComposerLayerLane::SFX)return Impl->Fail(Error,TEXT("Неизвестная полоса слоя."));
    auto Layout=GetLayoutSnapshot();auto* E=Layout.Events.FindByPredicate([&](const auto& L){return L.Event==Event;});
    if(!E){FMCVFXEventGraphLayout New;New.Event=Event;E=&Layout.Events.Add_GetRef(MoveTemp(New));}
    E->SFXLayerPositions.Add(Layer,P);return SetLayoutSnapshot(Token,Layout,Error);
}
void FMCVFXActionSession::MarkMasterLayoutSaved()
{
    Impl->LayoutBaseline=GetLayoutSnapshot();
    for(auto& Pair:Impl->Stages)Pair.Value.MasterBaseline.Reset(MCActionSession::CopyRecipe(Pair.Value.Recipe.Get(),TEXT("SavedMasterLayoutBaseline")));
    ++Impl->Revision;
}
bool FMCVFXActionSession::SetPreviewSoloGroups(FGuid Token,FName Event,const TArray<FGuid>& Groups,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Declared=GetEventGroups(Event);TArray<FGuid> Unique;
    for(FGuid ID:Groups)
    {if(ID.IsValid() && (!Declared || !Declared->Contains(ID)))return Impl->Fail(Error,TEXT("Соло относится к отсутствующей группе события."));Unique.AddUnique(ID);}
    const auto Current=GetPreviewSoloGroups(Event);
    if(Current.Num()==Unique.Num() && !Current.ContainsByPredicate([&](FGuid ID){return !Unique.Contains(ID);}))return true;
    Stop();if(Unique.IsEmpty())Impl->PreviewSolo.Remove(Event);else Impl->PreviewSolo.Add(Event,MoveTemp(Unique));
    ++Impl->Revision;++Impl->PlaybackRevision;Impl->Status=TEXT("Соло изменяет только предпросмотр. Сохранённые группы не изменены.");return true;
}
TArray<FGuid> FMCVFXActionSession::GetPreviewSoloGroups(FName Event)const
{const auto* Groups=Impl->PreviewSolo.Find(Event);return Groups?*Groups:TArray<FGuid>();}
void FMCVFXActionSession::ClearPreviewSolo()
{if(Impl->PreviewSolo.IsEmpty())return;Stop();Impl->PreviewSolo.Reset();++Impl->Revision;++Impl->PlaybackRevision;}
int32 FMCVFXActionSession::GetLayerCount(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S && S->Recipe.IsValid()?S->Recipe->Layers.Num():0;}
UMCVFXRecipePreset* FMCVFXActionSession::GetRecipe(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S?S->Recipe.Get():nullptr;}
FMCPerkVFXSlot FMCVFXActionSession::GetOriginalSlot(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S?S->Outer:FMCPerkVFXSlot();}
TArray<FMCVFXNativeSourcePart> FMCVFXActionSession::GetNativeSources(FName Stage)const
{
    const auto* S=Impl->Stages.Find(Stage);if(!S)return {};
    if(!Impl->NativeDraft.IsValid() || !Impl->NativeDraft->Events.Contains(Stage))return S->NativeSources;
    TArray<FMCVFXNativeSourcePart> Result;
    for(const auto& Descriptor:UMCVFXNativePresentationSettings::DescribeParts(Impl->ActionID,Stage))
    {
        const auto* P=Impl->NativeDraft->FindPart(Stage,Descriptor.PartID);if(!P)continue;
        auto Add=[&](const TCHAR* Role,EMCVFXNativeSourceKind Kind,FSoftObjectPath Asset)
        {
            if(Asset.IsNull())return;
            FMCVFXNativeSourcePart Part;Part.ID=FName(*(Descriptor.PartID.ToString()+TEXT(".")+Role));
            Part.Label=Descriptor.Label;Part.Kind=Kind;Part.Asset=Asset;Result.Add(MoveTemp(Part));
        };
        Add(TEXT("System"),EMCVFXNativeSourceKind::Niagara,P->System.ToSoftObjectPath());
        Add(TEXT("Material"),EMCVFXNativeSourceKind::Material,P->Material.ToSoftObjectPath());
        Add(TEXT("Mesh"),EMCVFXNativeSourceKind::Geometry,P->Mesh.ToSoftObjectPath());
    }
    // The original generator remains inspectable alongside the actual working
    // resource references. Changing a material must not keep opening the CDO's
    // previous material from a stale graph row.
    for(const auto& Part:S->NativeSources)if(!Part.SourceFile.IsEmpty())Result.Add(Part);
    return Result;
}
EMCPerkVFXSlotMode FMCVFXActionSession::GetPreviewSlotMode(FName Stage)const
{
    const auto* S=Impl->Stages.Find(Stage);if(!S)return EMCPerkVFXSlotMode::UseDefault;
    return S->IsRuntimeDirty()?(!MCActionSession::HasEnabled(S->Recipe.Get())?(Impl->KeepNative(Stage)?EMCPerkVFXSlotMode::UseDefault:EMCPerkVFXSlotMode::Disabled):EMCPerkVFXSlotMode::Composition):S->Outer.Mode;
}

const UMCVFXNativePresentationSettings* FMCVFXActionSession::GetNativeDraft()const{return Impl->NativeDraft.Get();}
FGuid FMCVFXActionSession::GetNativeDraftToken()const{return Impl->NativeDraftToken;}
bool FMCVFXActionSession::HasNativeOverride()const{return Impl->NativeOverride();}
bool FMCVFXActionSession::SetNativePart(FGuid Token,bool bPersistent,FName Event,FName PartID,const FMCVFXNativePartSettings& Part,FString& Error)
{
    Error.Reset();
    if(Token!=Impl->NativeDraftToken || !CanEditProfile() || !Impl->NativeDraft.IsValid())
        return Impl->Fail(Error,TEXT("Эта настройка относится к прежнему черновику. Выберите часть заново."));
    if(!bPersistent && (!Impl->StageOrder.Contains(Event) || !Impl->NativeDraft->Events.Contains(Event)))
        return Impl->Fail(Error,TEXT("У действия нет такого события родной основы."));
    const FName Scope=bPersistent?NAME_None:Event;
    const auto* Existing=Impl->NativeDraft->FindPart(Scope,PartID);
    if(!Existing || Part.PartID!=PartID || Existing->Kind!=Part.Kind)
        return Impl->Fail(Error,TEXT("Часть не принадлежит настоящему событию этого действия."));
    if(FMCVFXNativePartSettings::StaticStruct()->CompareScriptStruct(Existing,&Part,0))return true;
    TStrongObjectPtr<UMCVFXNativePresentationSettings> Candidate(Impl->CopyNative(TEXT("ActionNativeCandidate")));
    auto& Parts=bPersistent?Candidate->PersistentParts:Candidate->Events.FindChecked(Event).Parts;
    auto* Target=Parts.FindByPredicate([&](const auto& P){return P.PartID==PartID;});
    *Target=Part;
    if(!Candidate->ValidateForSource(Impl->ActionID,Impl->StageOrder,Error))return Impl->Fail(Error,Error);
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","NativePart","Изменить родную часть эффекта"));
    Impl->NativeDraft->Modify();
    auto& Actual=bPersistent?Impl->NativeDraft->PersistentParts:Impl->NativeDraft->Events.FindChecked(Event).Parts;
    *Actual.FindByPredicate([&](const auto& P){return P.PartID==PartID;})=Part;
    Impl->Changed(Scope);return true;
}
bool FMCVFXActionSession::ResetNativePart(FGuid Token,bool bPersistent,FName Event,FName PartID,FString& Error)
{
    TStrongObjectPtr<UMCVFXNativePresentationSettings> Defaults(NewObject<UMCVFXNativePresentationSettings>());
    if(!Defaults->InitFromNativeDefaults(Impl->ActionID,Error))return false;
    const auto* Part=Defaults->FindPart(bPersistent?NAME_None:Event,PartID);
    if(!Part)return Impl->Fail(Error,TEXT("Исходная часть не найдена."));
    return SetNativePart(Token,bPersistent,Event,PartID,*Part,Error);
}
bool FMCVFXActionSession::SetNativeKeepWithComposition(FGuid Token,FName Event,bool bKeep,FString& Error)
{
    Error.Reset();
    if(Token!=Impl->NativeDraftToken || !CanEditProfile() || !Impl->NativeDraft.IsValid() || !Impl->StageOrder.Contains(Event))
        return Impl->Fail(Error,TEXT("Событие относится к прежнему черновику."));
    auto* E=Impl->NativeDraft->Events.Find(Event);if(!E)return Impl->Fail(Error,TEXT("Нет родной основы этого события."));
    if(E->bKeepNativeAccentsWithComposition==bKeep)return true;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","NativeKeep","Режим родной основы"));
    Impl->NativeDraft->Modify();E->bKeepNativeAccentsWithComposition=bKeep;Impl->Changed(Event);return true;
}

const FMCEventSFXSlot* FMCVFXActionSession::GetSFXSlot(FName Event)const
{return Impl->EventDraft.IsValid()?Impl->EventDraft->SFXSlots.Find(Event):nullptr;}
FGuid FMCVFXActionSession::GetSFXDraftToken()const{return Impl->NativeDraftToken;}
FMCComposerSFXCapabilities FMCVFXActionSession::GetSFXCapability(FName Event)const
{
    FMCComposerSFXCapabilities Result;
    if(!CanEditProfile() || !Impl->StageOrder.Contains(Event))
    {Result.UnavailableReason=GetEditUnavailableReason();return Result;}
    Result.bCanAuditionInContext=IsPreviewAudioEnabled();
    const auto Status=MCVFXSourceContracts::GetOriginalAudioStatus(GetActionID(),Event);
    Result.OriginalAudioKnowledge=Status.Knowledge;
    Result.bOriginalAudioConfirmedSilent=Status.Knowledge==EMCVFXOriginalAudioKnowledge::ConfirmedAbsent;
    if(Result.bOriginalAudioConfirmedSilent)Result.OriginalAudioSilenceEvidence=Status.Evidence;
    if(Result.bOriginalAudioConfirmedSilent) {
        // Absence is not an invented sound source or a native suppression hook.
        Result.bCanReplaceNativeAudio=true;return Result;
    }
    if(Status.Knowledge==EMCVFXOriginalAudioKnowledge::DeclaredSource)
    if(const auto* Contract=MCVFXSourceContracts::Find(GetActionID()))if(const auto* Original=Contract->OriginalAudio.Find(Event)) {
        Result.bOriginalSourceKnown=!Original->Resource.IsNull();
        Result.bOriginalAudioHookMapped=MCVFXSourceContracts::CanSuppressOriginalSFX(GetActionID(),Event);
        Result.bCanReplaceNativeAudio=Result.bOriginalAudioHookMapped;
        FMCComposerOriginalSFXSource Source;Source.ID=Original->ID;Source.NativeEvent=Original->PaletteEvent;
        Source.Label=Original->Label;Source.Sound=Original->Resource;Source.SourceOwner=Contract->SourceOwner;
        Source.SourceProperty=Original->PaletteEvent.IsNone()?TEXT("SoundBase"):Original->PaletteEvent.ToString();
        Source.bRuntimeHookMapped=Result.bOriginalAudioHookMapped;Result.Originals.Add(MoveTemp(Source));
        // Show the actual authored SoundBase choices without performing selection,
        // consuming gameplay RNG, or claiming that an instance override is a CDO.
        TSet<FSoftObjectPath> Shown;Shown.Add(Original->Resource);
        auto AddChoice=[&](USoundBase* Sound,const FString& Property,const FString& Label)
        {
            if(!IsValid(Sound))return;const FSoftObjectPath Path(Sound);if(Shown.Contains(Path))return;Shown.Add(Path);
            FMCComposerOriginalSFXSource Choice;
            Choice.ID=FName(*(Original->ID.ToString()+TEXT(".")+Property));Choice.NativeEvent=Original->PaletteEvent;
            Choice.Label=FText::FromString(Label);Choice.Sound=Path;Choice.SourceOwner=Contract->SourceOwner;
            Choice.SourceProperty=Property;Choice.bRuntimeHookMapped=Result.bOriginalAudioHookMapped;
            Result.Originals.Add(MoveTemp(Choice));
        };
        if(!Original->PaletteEvent.IsNone())
            if(const auto* Palette=Cast<UMCSoundPalette>(Original->Resource.TryLoad())) {
                FName Resolved;const auto* Entry=Palette->ResolveEvent(Original->PaletteEvent,&Resolved);
                if(Entry)for(int32 I=0;I<Entry->Sounds.Num();++I)
                    AddChoice(Entry->Sounds[I].Get(),FString::Printf(TEXT("Events.%s.Sounds[%d]"),*Resolved.ToString(),I),
                        TEXT("Родной вариант · ")+GetNameSafe(Entry->Sounds[I].Get())+TEXT(" · ")+Resolved.ToString());
            }
        if(GetActionID()==TEXT("World.Throat.Suction")&&Event==TEXT("Loop")) {
            // AMCThroatVortex selects this only when AirflowSound failed to load.
            // Expose the real fallback source; inspecting it does not select or play it.
            AddChoice(LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Pull.S_Pull")),
                TEXT("NativeFallback.AirflowSound"),TEXT("Родной резервный звук · S_Pull (если основной недоступен)"));
        }
        if(GetActionID()==TEXT("World.ArenaTooth.Piano")&&Event==TEXT("Hit")) {
            const auto* Defaults=GetDefault<AMCArenaTooth>();
            AddChoice(Defaults->PianoSound.Get(),TEXT("NativeDefault.PianoSound"),TEXT("Родной дефолт C4 · ")+GetNameSafe(Defaults->PianoSound.Get()));
            for(int32 I=0;I<Defaults->PianoUpperOctaves.Num();++I)
                AddChoice(Defaults->PianoUpperOctaves[I].Get(),FString::Printf(TEXT("NativeDefault.PianoUpperOctaves[%d]"),I),
                    FString::Printf(TEXT("Родной дефолт C%d · "),I+5)+GetNameSafe(Defaults->PianoUpperOctaves[I].Get()));
        }
        if(Result.bCanReplaceNativeAudio)return Result;
    }
    Result.UnavailableReason=Status.Evidence;
    return Result;
}
FGuid FMCVFXActionSession::AddSFXLayer(FGuid Token,FName Event,USoundBase* Sound,FGuid GroupID,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error) || !Sound)return FGuid();
    if(Sound->HasAnyFlags(RF_Transient) || Sound->IsEditorOnly() || Sound->GetOutermost()->IsDirty())
    {Impl->Fail(Error,TEXT("Сначала сохраните исходный звуковой ассет отдельно."));return FGuid();}
    FMCEventSFXSlot Slot=GetSFXSlot(Event)?*GetSFXSlot(Event):FMCEventSFXSlot();
    if(Slot.Layers.Num()>=32){Impl->Fail(Error,TEXT("В событии уже 32 звуковых слоя."));return FGuid();}
    const auto* Groups=Impl->EventDraft->EventGroups.Find(Event);
    if(GroupID.IsValid() && (!Groups || !Groups->Contains(GroupID)))
    {Impl->Fail(Error,TEXT("Группа этого события больше не существует."));return FGuid();}
    FMCEventSFXLayer Layer;Layer.Name=Sound->GetFName();Layer.Sound=Sound;Layer.GroupID=GroupID;
    Slot.Layers.Add(Layer);if(Slot.Mode==EMCEventSFXMode::UseDefault)Slot.Mode=EMCEventSFXMode::Add;
    if(!Slot.Validate(Error))return FGuid();
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","AddSound","Добавить звуковой слой события"));
    Impl->EventDraft->Modify();Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Slot));Impl->Changed(Event);return Layer.ID;
}
bool FMCVFXActionSession::SetSFXLayer(FGuid Token,FName Event,FGuid LayerID,const FMCEventSFXLayer& Layer,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetSFXSlot(Event);if(!Existing || Layer.ID!=LayerID)
        return Impl->Fail(Error,TEXT("Звуковой слой больше не существует."));
    FMCEventSFXSlot Slot=*Existing;auto* Target=Slot.Layers.FindByPredicate([&](const auto& L){return L.ID==LayerID;});
    if(!Target)return Impl->Fail(Error,TEXT("Звуковой слой больше не существует."));
    if(FMCEventSFXLayer::StaticStruct()->CompareScriptStruct(Target,&Layer,0))return true;
    const auto* Groups=Impl->EventDraft->EventGroups.Find(Event);
    if(Layer.GroupID.IsValid() && (!Groups || !Groups->Contains(Layer.GroupID)))
        return Impl->Fail(Error,TEXT("Слой ссылается на неизвестную группу этого события."));
    *Target=Layer;if(!Slot.Validate(Error))return false;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","EditSound","Изменить звуковой слой события"));
    Impl->EventDraft->Modify();Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Slot));Impl->Changed(Event);return true;
}
bool FMCVFXActionSession::RemoveSFXLayer(FGuid Token,FName Event,FGuid LayerID,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    const auto* Existing=GetSFXSlot(Event);if(!Existing)return Impl->Fail(Error,TEXT("Событие не содержит звуковых слоёв."));
    FMCEventSFXSlot Slot=*Existing;
    if(Slot.Layers.RemoveAll([&](const auto& L){return L.ID==LayerID;})!=1)return Impl->Fail(Error,TEXT("Звуковой слой больше не существует."));
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","RemoveSound","Удалить звуковой слой события"));
    Impl->EventDraft->Modify();Impl->LayoutState->Modify();Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Slot));
    if(auto* Layout=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& E){return E.Event==Event;}))Layout->SFXLayerPositions.Remove(LayerID);
    Impl->Changed(Event);return true;
}
FGuid FMCVFXActionSession::DuplicateSFXLayer(FGuid Token,FName Event,FGuid LayerID,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return FGuid();
    const auto* Existing=GetSFXSlot(Event);if(!Existing || Existing->Layers.Num()>=32)
    {Impl->Fail(Error,TEXT("Невозможно дублировать звуковой слой."));return FGuid();}
    const auto* Found=Existing->Layers.FindByPredicate([&](const auto& L){return L.ID==LayerID;});
    if(!Found){Impl->Fail(Error,TEXT("Звуковой слой больше не существует."));return FGuid();}
    auto Slot=*Existing;auto Layer=*Found;Layer.ID=FGuid::NewGuid();Layer.Name=FName(*(Layer.Name.ToString()+TEXT("_Copy")));Slot.Layers.Add(Layer);
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","DuplicateSound","Дублировать звуковой слой события"));
    Impl->EventDraft->Modify();Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Slot));
    if(auto* Layout=Impl->LayoutState->Layout.Events.FindByPredicate([&](const auto& E){return E.Event==Event;}))
        if(const auto* OriginalPosition=Layout->SFXLayerPositions.Find(LayerID))
        {const FVector2D Copy=*OriginalPosition+FVector2D(40,40);Impl->LayoutState->Modify();Layout->SFXLayerPositions.Add(Layer.ID,Copy);}
    Impl->Changed(Event);return Layer.ID;
}
bool FMCVFXActionSession::SetSFXMode(FGuid Token,FName Event,EMCEventSFXMode Mode,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Event,Error))return false;
    auto Slot=GetSFXSlot(Event)?*GetSFXSlot(Event):FMCEventSFXSlot();Slot.Mode=Mode;
    if(!Slot.Validate(Error) || !MCEventSFX::ValidateNativePolicy(Slot,GetSFXCapability(Event).bCanReplaceNativeAudio,Error))return false;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","SoundMode","Режим звука события"));
    Impl->EventDraft->Modify();Impl->EventDraft->SFXSlots.Add(Event,MoveTemp(Slot));Impl->Changed(Event);return true;
}
bool FMCVFXActionSession::AuditionSFX(FName Event,USoundBase* Sound,FString& Error,EMCPerkPreviewScenario Scenario)
{
    if(!CanPreview())return Impl->Fail(Error,GetPreviewUnavailableReason());
    if(!Sound || !Impl->Stage(Event,Error))return false;
    if(!IsPreviewAudioEnabled())return Impl->Fail(Error,TEXT("Включите звук предпросмотра перед примеркой."));
    Stop();if(!Impl->PrepareProfile(Event,nullptr,Error,Sound))return false;
    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!Runner || !Runner->StartAction(Impl->ActionID,Scenario,Impl->AuditionProfile.Get()))return Impl->Fail(Error,TEXT("Не удалось запустить звуковую примерку настоящего действия."));
    Impl->bOwnsPlayback=true;Impl->Status=TEXT("Звуковая примерка в настоящем событии. Черновик и игра не изменены.");return true;
}
bool FMCVFXActionSession::SetPreviewAudioEnabled(bool bEnabled,FString& Error)
{
    if(!CanPreview())return Impl->Fail(Error,GetPreviewUnavailableReason());
    const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();if(!Runner)return Impl->Fail(Error,TEXT("Предпросмотр не открыт."));
    Stop();return Runner->SetAudioEnabled(bEnabled,Error);
}
bool FMCVFXActionSession::IsPreviewAudioEnabled()const
{if(!CanPreview())return false;const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();return Runner && Runner->IsAudioEnabled();}
FString FMCVFXActionSession::GetPreviewAudioStatus()const
{if(!CanPreview())return GetPreviewUnavailableReason();const auto Runner=MCPerkActionPreviewPanel::GetActiveRunner();return Runner?Runner->GetAudioStatus():TEXT("Предпросмотр не открыт.");}

FGuid FMCVFXActionSession::AddSystem(FName Stage,UNiagaraSystem* System,FString& Error)
{return AddSystemToGroup(GetDraftToken(),Stage,System,FGuid(),Error);}
FGuid FMCVFXActionSession::AddSystemToGroup(FGuid Token,FName Stage,UNiagaraSystem* System,FGuid Group,FString& Error)
{
    if(!Impl->AudioEditScope(Token,Stage,Error))return FGuid();
    auto* S=Impl->Stage(Stage,Error);if(!S || !System)return FGuid();
    const auto* Groups=GetEventGroups(Stage);
    if(Group.IsValid() && (!Groups || !Groups->Contains(Group))){Impl->Fail(Error,TEXT("Целевая группа этого события больше не существует."));return FGuid();}
    if(S->Recipe->Layers.Num()>=32){Impl->Fail(Error,TEXT("В событии может быть не более 32 элементов."));return FGuid();}
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Add","Добавить VFX в событие"));S->Recipe->Modify();
    FMCVFXCompositionLayer L;L.Name=System->GetFName();L.System=System;L.GroupID=Group;S->Recipe->Layers.Add(L);
    FMCVFXRecipeNodeLayout N;N.ID=FGuid::NewGuid();N.Position=FVector2D(S->Recipe->Nodes.Num()*270,0);S->Recipe->Nodes.Add(N);
    S->Recipe->DiscoverParameters(S->Recipe->Layers.Num()-1,Error);Impl->Changed(Stage);return N.ID;
}
bool FMCVFXActionSession::SetLayer(FName Stage,const FGuid& ID,const FMCVFXCompositionLayer& Layer,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return false;const int32 I=S->Recipe->FindNode(ID);
    if(I==INDEX_NONE)return Impl->Fail(Error,TEXT("Элемент больше не существует. Выберите его заново."));
    if(!Impl->ValidateLayerGroup(Stage,Layer,Error))return false;
    if(FMCVFXCompositionLayer::StaticStruct()->CompareScriptStruct(&S->Recipe->Layers[I],&Layer,0))return true;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Edit","Изменить VFX события"));S->Recipe->Modify();S->Recipe->Layers[I]=Layer;Impl->Changed(Stage);return true;
}
bool FMCVFXActionSession::RemoveLayer(FName Stage,const FGuid& ID,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return false;const int32 I=S->Recipe->FindNode(ID);
    if(I==INDEX_NONE)return Impl->Fail(Error,TEXT("Удаляемый элемент не найден."));
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Remove","Удалить VFX события"));S->Recipe->Modify();S->Recipe->Layers.RemoveAt(I);S->Recipe->Nodes.RemoveAt(I);Impl->Changed(Stage);return true;
}
FGuid FMCVFXActionSession::DuplicateLayer(FName Stage,const FGuid& ID,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return FGuid();const int32 I=S->Recipe->FindNode(ID);
    if(I==INDEX_NONE || S->Recipe->Layers.Num()>=32){Impl->Fail(Error,TEXT("Невозможно дублировать элемент."));return FGuid();}
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Duplicate","Дублировать VFX события"));S->Recipe->Modify();
    auto L=S->Recipe->Layers[I];auto N=S->Recipe->Nodes[I];N.ID=FGuid::NewGuid();N.Position+=FVector2D(45,45);L.Name=FName(*(L.Name.ToString()+TEXT("_Copy")));
    S->Recipe->Layers.Add(L);S->Recipe->Nodes.Add(N);Impl->Changed(Stage);return N.ID;
}
bool FMCVFXActionSession::SetLayerEnabled(FName Stage,const FGuid& ID,bool bEnabled,FString& Error)
{auto* S=Impl->Stage(Stage,Error);if(!S)return false;const int32 I=S->Recipe->FindNode(ID);if(I==INDEX_NONE)return Impl->Fail(Error,TEXT("Элемент не найден."));auto L=S->Recipe->Layers[I];L.bEnabled=bEnabled;return SetLayer(Stage,ID,L,Error);}
bool FMCVFXActionSession::NewCompound(FName Stage,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return false;Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","NewCompound","Новая композиция события"));S->Recipe->Modify();
    S->Recipe->Layers.Reset();S->Recipe->Nodes.Reset();S->Recipe->Groups.Reset();S->Recipe->PresetName=GetEventLabel(Stage).ToString();Impl->Changed(Stage);return true;
}
bool FMCVFXActionSession::ImportRecipe(FName Stage,const UMCVFXRecipePreset* Source,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return false;
    if(!Source || !Source->ValidateLayout(Error) || !Source->Validate(Error))return Impl->Fail(Error,Error.IsEmpty()?TEXT("Исходник композиции недоступен."):Error);
    if(!Impl->ValidateRecipeGroups(Stage,Source,Error))return false;
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Import","Открыть композицию события"));S->Recipe->Modify();MCActionSession::CopyRecipeValues(Source,S->Recipe.Get());Impl->Changed(Stage);return true;
}

bool FMCVFXActionSession::AuditionNS(FName Stage,UNiagaraSystem* System,FString& Error,EMCPerkPreviewScenario Scenario)
{
    if(!CanPreview())return Impl->Fail(Error,GetPreviewUnavailableReason());
    if(!System)return Impl->Fail(Error,TEXT("Выберите эффект из библиотеки."));
    if(!Impl->Stage(Stage,Error))return false;Stop();
    if(!Impl->PrepareProfile(Stage,System,Error))return false;
    auto R=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!R || !R->StartAction(Impl->ActionID,Scenario,Impl->AuditionProfile.Get()))return Impl->Fail(Error,TEXT("Не удалось запустить примерку настоящего действия."));
    Impl->bOwnsPlayback=true;Impl->Status=TEXT("Временная примерка: ")+System->GetName()+TEXT(". Состав черновика и игра не изменены.");return true;
}
bool FMCVFXActionSession::PlayFullAction(FString& Error,EMCPerkPreviewScenario Scenario)
{
    if(!CanPreview())return Impl->Fail(Error,GetPreviewUnavailableReason());
    Error.Reset();Stop();if(!Impl->PrepareProfile(NAME_None,nullptr,Error))return false;
    auto R=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!R || !R->StartAction(Impl->ActionID,Scenario,Impl->bSupportsProfileOverride?Impl->AuditionProfile.Get():nullptr))return Impl->Fail(Error,TEXT("Не удалось запустить настоящее действие."));
    Impl->bOwnsPlayback=true;Impl->Status=TEXT("Настоящий сценарий действия; игра не изменена.");return true;
}
void FMCVFXActionSession::Pause(bool bPaused){if(Impl->bOwnsPlayback && Impl->ContextCurrent())if(auto R=MCPerkActionPreviewPanel::GetActiveRunner())R->SetPaused(bPaused);}
void FMCVFXActionSession::Stop(){if(Impl->bOwnsPlayback && Impl->ContextCurrent())MCPerkActionPreviewPanel::ResetPreview();Impl->bOwnsPlayback=false;}
TSharedRef<SWidget> FMCVFXActionSession::MakePreviewWidget()
{
    if(!CanPreview())return SNew(STextBlock).Text(FText::FromString(GetPreviewUnavailableReason())).AutoWrapText(true);
    return MCPerkActionPreviewPanel::MakeSharedActionViewport(&Impl->SetPreviewCamera);
}
void FMCVFXActionSession::SetPreviewView(int32 Preset){if(Impl->SetPreviewCamera)Impl->SetPreviewCamera(Preset);}

bool FMCVFXActionSession::ApplyAll(FString& Error,const FString& AssetRoot)
{
    Error.Reset();if(!CanEditProfile())return Impl->Fail(Error,GetEditUnavailableReason());
    if(GEditor && GEditor->PlayWorld)return Impl->Fail(Error,TEXT("Завершите Play перед применением VFX к игре."));
    if(!Impl->CanonicalProfileCurrent(Error))return false;
    if(!IsRuntimeDirty()){Impl->Status=TEXT("Игровые настройки не менялись. Расположение графа сохраняется отдельно с паком.");return true;}
    if(!AssetRoot.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(AssetRoot))return Impl->Fail(Error,TEXT("Выберите папку ассетов внутри /Game/."));
    // Validate every changed stage before writing any asset. Canonical owner assignment is last.
    if(!Impl->PrepareProfile(NAME_None,nullptr,Error,nullptr,false))return false;
    for(const auto& P:Impl->Stages)if(P.Value.IsRuntimeDirty())for(const auto& L:P.Value.Recipe->Layers)if(L.bEnabled)
    {auto* NS=L.System.LoadSynchronous();if(!NS || NS->GetOutermost()->IsDirty())return Impl->Fail(Error,P.Key.ToString()+TEXT(": сначала сохраните исходную Niagara отдельно."));}
    Stop();
    const FString EffectiveRoot=Impl->CanonicalRowID.IsNone() && AssetRoot==TEXT("/Game/Gameplay/ActiveSkills/VFX/ActionCompositions")
        ?TEXT("/Game/Gameplay/VFX/ActionCompositions"):AssetRoot;
    const FString SourceStem=MCActionSession::AssetStem(Impl->ActionID);
    const FString RunPath=EffectiveRoot/(SourceStem+TEXT("_"))+FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    TArray<FString> SavedPaths;TMap<FName,FString> StageMasters;for(const auto& P:Impl->Stages)StageMasters.Add(P.Key,P.Value.SourceMasterPath);
    TStrongObjectPtr<UMCPerkVFXProfile> NewProfile(DuplicateObject<UMCPerkVFXProfile>(Impl->EventDraft.IsValid()?Impl->EventDraft.Get():Impl->BaseProfile.Get(),GetTransientPackage()));
    NewProfile->ClearFlags(RF_Public|RF_Standalone);NewProfile->SetFlags(RF_Transient);
    if(Impl->EventDraft.IsValid()){NewProfile->SFXSlots=Impl->EventDraft->SFXSlots;NewProfile->EventGroups=Impl->EventDraft->EventGroups;}
    auto Failed=[&](const FString& Why,bool bCanonicalUnchanged=true){return Impl->Fail(Error,Why+(SavedPaths.IsEmpty()?FString():TEXT(" Промежуточные новые ассеты оставлены в ")+RunPath
        +(bCanonicalUnchanged?TEXT("; игровой профиль не заменён."):TEXT("."))));};
    for(FName StageName:Impl->StageOrder)
    {
        auto& S=Impl->Stages.FindChecked(StageName);if(!S.IsRuntimeDirty())continue;
        FMCPerkVFXSlot Slot=S.Outer;
        const FString StageStem=MCActionSession::AssetStem(StageName);
        const FString MasterPath=RunPath/(TEXT("DA_")+SourceStem+TEXT("_")+StageStem+TEXT("_Source"));
        auto* Master=MCActionSession::SaveNewCopy(S.Recipe.Get(),MasterPath,Error);if(!Master)return Failed(Error);SavedPaths.Add(Master->GetPathName());
        StageMasters.Add(StageName,Master->GetPathName());
        if(!MCActionSession::HasEnabled(S.Recipe.Get())){Slot.Mode=Impl->KeepNative(StageName)?EMCPerkVFXSlotMode::UseDefault:EMCPerkVFXSlotMode::Disabled;Slot.System.Reset();Slot.Composition.Reset();NewProfile->Slots.Add(StageName,Slot);continue;}
        if(!S.Composition.IsValid())return Failed(TEXT("Подготовленная композиция отсутствует. Повторите просмотр."));
        auto* Runtime=MCActionSession::SaveNewCopy(S.Composition.Get(),RunPath/(TEXT("DA_")+SourceStem+TEXT("_")+StageStem),Error);
        if(!Runtime)return Failed(Error);SavedPaths.Add(Runtime->GetPathName());
        Runtime->GetOutermost()->GetMetaData().SetValue(Runtime,TEXT("MCComposerSourceRecipe"),*Master->GetPathName());
        Runtime->MarkPackageDirty();
        if(!UEditorLoadingAndSavingUtils::SavePackages({Runtime->GetOutermost()},true))return Failed(TEXT("Не удалось сохранить ссылку на авторский исходник."));
        Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.System.Reset();Slot.Composition=Runtime;
        if(!UMCPerkVFXProfile::ValidateSlot(Slot,Error))return Failed(StageName.ToString()+TEXT(": ")+Error);
        NewProfile->Slots.Add(StageName,Slot);
    }
    if(Impl->NativeDirty() || (Impl->NativeOverride() && Impl->NativeSource.IsNull()))
    {
        if(!Impl->NativeDraft->ValidateForSource(Impl->ActionID,Impl->StageOrder,Error))return Failed(Error);
        auto* Native=MCActionSession::SaveNewCopy(Impl->NativeDraft.Get(),RunPath/(TEXT("DA_")+SourceStem+TEXT("_NativeArt")),Error);
        if(!Native)return Failed(Error);SavedPaths.Add(Native->GetPathName());NewProfile->NativePresentation=Native;
    }
    if(!NewProfile->ValidateForEvents(Impl->CanonicalBinding.AllowedEvents,Error))return Failed(Error);
    auto* SavedProfile=MCActionSession::SaveNewCopy(NewProfile.Get(),RunPath/(TEXT("DA_")+SourceStem+TEXT("_ActionVFX")),Error,&StageMasters);
    if(!SavedProfile)return Failed(Error);SavedPaths.Add(SavedProfile->GetPathName());
    // Repeat the guard after export/waits, before the only canonical write.
    if(!IsContextCurrent() || !Impl->CanonicalProfileCurrent(Error))
        return Failed(TEXT("Контекст или источник профиля изменились во время сохранения. Применение отменено. ")+Error);
    FMCVFXProfileCommitResult Commit;
    if(!MCVFXProfileBindings::AssignSavedProfile(SavedProfile,Impl->CanonicalBinding,Commit,Error) || !Commit.bSaved)
        return Failed(Error.IsEmpty()?Commit.Message:Error,false);
    Impl->TableBaseline=SavedProfile;Impl->BaseProfile.Reset(DuplicateObject<UMCPerkVFXProfile>(SavedProfile,GetTransientPackage()));
    Impl->BaseProfile->ClearFlags(RF_Public|RF_Standalone);Impl->BaseProfile->SetFlags(RF_Transient);Impl->ProfileSourceDigest=MCActionSession::PackageDigest(SavedProfile);
    Impl->EventDraft.Reset(DuplicateObject<UMCPerkVFXProfile>(Impl->BaseProfile.Get(),GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("AppliedEventDraft"))));
    Impl->EventDraft->ClearFlags(RF_Public|RF_Standalone);Impl->EventDraft->SetFlags(RF_Transient|RF_Transactional);
    Impl->NativeSource=SavedProfile->NativePresentation;
    if(const auto* Native=Impl->NativeSource.LoadSynchronous())
    {Impl->NativeSourceDigest=MCActionSession::PackageDigest(Native);Impl->NativeSourceSettingsHash=Native->ComputeSettingsHash();}
    if(Impl->NativeDraft.IsValid())Impl->NativeBaseline.Reset(Impl->CopyNative(TEXT("AppliedNativeBaseline")));
    Impl->NativeDraftToken=FGuid::NewGuid();
    for(auto& P:Impl->Stages)
    {
        if(const auto* Slot=SavedProfile->Slots.Find(P.Key))
        {
            P.Value.Outer=MCActionSession::EffectiveOuter(*Slot);P.Value.SourceRuntimeComposition=Slot->Mode==EMCPerkVFXSlotMode::Composition?Slot->Composition:TSoftObjectPtr<UMCVFXComposition>();
            P.Value.RuntimeSourceDigest=MCActionSession::PackageDigest(P.Value.SourceRuntimeComposition.LoadSynchronous());
        }
        P.Value.SourceMasterPath=StageMasters.FindRef(P.Key);
        if(P.Value.IsRuntimeDirty())
        {
            P.Value.Baseline.Reset(MCActionSession::CopyRecipe(P.Value.Recipe.Get(),TEXT("AppliedStageBaseline")));
            P.Value.MasterBaseline.Reset(MCActionSession::CopyRecipe(P.Value.Recipe.Get(),TEXT("AppliedMasterBaseline")));
        }
        P.Value.ClearCache();
    }
    FMCVFXCanonicalProfileState UpdatedBinding;
    if(!MCVFXProfileBindings::ReadCanonical(Impl->ActionID,UpdatedBinding,Error))
    {
        Impl->bHasCanonicalBinding=false;++Impl->Revision;
        Impl->Status=TEXT("Профиль сохранён и назначен, но повторное чтение источника не удалось. Откройте действие заново. ")+Error;
        Error.Reset();return true;
    }
    Impl->CanonicalBinding=MoveTemp(UpdatedBinding);
    Impl->ReconcileObserved();++Impl->Revision;Impl->Status=Commit.Message;return true;
}

void FMCVFXActionSession::PostUndo(bool bSuccess)
{if(bSuccess){if(Impl->ReconcileObserved())Stop();++Impl->Revision;Impl->Status=TEXT("Изменён черновик после Undo. Проверьте действие перед применением.");}}
void FMCVFXActionSession::PostRedo(bool bSuccess){PostUndo(bSuccess);}
FString FMCVFXActionSession::GetSnapshotJson()const
{
    auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("action"),Impl->ActionID.ToString());Data->SetBoolField(TEXT("context_current"),IsContextCurrent());
    Data->SetBoolField(TEXT("can_edit_profile"),CanEditProfile());Data->SetStringField(TEXT("edit_unavailable_reason"),GetEditUnavailableReason());
    Data->SetBoolField(TEXT("can_preview"),CanPreview());Data->SetStringField(TEXT("preview_unavailable_reason"),GetPreviewUnavailableReason());
    Data->SetBoolField(TEXT("dirty"),IsDirty());Data->SetNumberField(TEXT("revision"),double(Impl->Revision));Data->SetStringField(TEXT("status"),Impl->Status);
    Data->SetBoolField(TEXT("runtime_dirty"),IsRuntimeDirty());Data->SetBoolField(TEXT("layout_dirty"),IsMasterLayoutDirty());
    Data->SetNumberField(TEXT("playback_revision"),double(Impl->PlaybackRevision));Data->SetNumberField(TEXT("layout_revision"),double(Impl->LayoutRevision));
    Data->SetStringField(TEXT("draft_token"),GetDraftToken().ToString());
    Data->SetStringField(TEXT("table_baseline"),Impl->TableBaseline.ToString());
    Data->SetStringField(TEXT("canonical_owner"),Impl->CanonicalBinding.OwnerAsset.ToString());
    Data->SetStringField(TEXT("canonical_key"),Impl->CanonicalBinding.OwnerKey.ToString());
    Data->SetBoolField(TEXT("canonical_binding"),Impl->bHasCanonicalBinding);
    Data->SetBoolField(TEXT("event_audio_groups_dirty"),Impl->EventDraftDirty());Data->SetBoolField(TEXT("preview_audio_enabled"),IsPreviewAudioEnabled());
    Data->SetStringField(TEXT("preview_audio_status"),GetPreviewAudioStatus());
    Data->SetBoolField(TEXT("native_editable"),Impl->NativeDraft.IsValid());Data->SetBoolField(TEXT("native_dirty"),Impl->NativeDirty());
    Data->SetBoolField(TEXT("native_override"),HasNativeOverride());Data->SetStringField(TEXT("native_draft_token"),Impl->NativeDraftToken.ToString());
    Data->SetStringField(TEXT("native_source"),Impl->NativeSource.ToString());
    if(Impl->NativeDraft.IsValid())Data->SetStringField(TEXT("native_settings_hash"),LexToString(Impl->NativeDraft->ComputeSettingsHash()));
    TArray<TSharedPtr<FJsonValue>> Stages;
    for(FName StageName:Impl->StageOrder)if(const auto* S=Impl->Stages.Find(StageName))
    {
        auto D=MakeShared<FJsonObject>();D->SetStringField(TEXT("stage"),StageName.ToString());D->SetBoolField(TEXT("dirty"),S->IsRuntimeDirty() || S->IsLayoutDirty());D->SetBoolField(TEXT("runtime_dirty"),S->IsRuntimeDirty());D->SetBoolField(TEXT("layout_dirty"),S->IsLayoutDirty());D->SetNumberField(TEXT("layers"),S->Recipe->Layers.Num());
        TArray<TSharedPtr<FJsonValue>> Audio;
        if(const auto* Slot=GetSFXSlot(StageName))
        {
            D->SetStringField(TEXT("audio_mode"),UEnum::GetValueAsString(Slot->Mode));
            for(const auto& Layer:Slot->Layers)
            {
                auto L=MakeShared<FJsonObject>();L->SetStringField(TEXT("id"),Layer.ID.ToString());L->SetStringField(TEXT("group"),Layer.GroupID.ToString());
                L->SetStringField(TEXT("sound"),Layer.Sound.ToString());L->SetBoolField(TEXT("enabled"),Layer.bEnabled);
                L->SetNumberField(TEXT("volume"),Layer.Volume);L->SetNumberField(TEXT("pitch"),Layer.Pitch);L->SetNumberField(TEXT("delay"),Layer.DelaySeconds);
                Audio.Add(MakeShared<FJsonValueObject>(L));
            }
        }
        D->SetArrayField(TEXT("audio_layers"),Audio);
        TArray<TSharedPtr<FJsonValue>> Groups,Solo;
        if(const auto* Set=GetEventGroups(StageName))for(const auto& Group:Set->Groups)
        {
            auto G=MakeShared<FJsonObject>();G->SetStringField(TEXT("id"),Group.ID.ToString());G->SetStringField(TEXT("name"),Group.Name.ToString());
            G->SetBoolField(TEXT("enabled"),Group.bEnabled);G->SetBoolField(TEXT("mute_sfx"),Group.bMuteSFX);Groups.Add(MakeShared<FJsonValueObject>(G));
        }
        for(FGuid ID:GetPreviewSoloGroups(StageName))Solo.Add(MakeShared<FJsonValueString>(ID.ToString()));
        D->SetArrayField(TEXT("groups"),Groups);D->SetArrayField(TEXT("preview_solo_groups"),Solo);
        FVector2D Card;if(GetEventCardPosition(StageName,Card)){D->SetNumberField(TEXT("card_x"),Card.X);D->SetNumberField(TEXT("card_y"),Card.Y);}
        const auto NativeSources=GetNativeSources(StageName);
        D->SetNumberField(TEXT("native_sources"),NativeSources.Num());D->SetStringField(TEXT("preview_slot_mode"),UEnum::GetValueAsString(GetPreviewSlotMode(StageName)));
        TArray<TSharedPtr<FJsonValue>> Native;
        for(const auto& Part:NativeSources){auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("id"),Part.ID.ToString());N->SetStringField(TEXT("kind"),MCVFXNativeSources::KindLabel(Part.Kind).ToString());N->SetStringField(TEXT("asset"),Part.Asset.ToString());N->SetStringField(TEXT("source_file"),Part.SourceFile);Native.Add(MakeShared<FJsonValueObject>(N));}
        D->SetArrayField(TEXT("native_source_rows"),Native);
        D->SetStringField(TEXT("cached_system"),GetPathNameSafe(S->Compiled.Get()));D->SetStringField(TEXT("cached_composition"),GetPathNameSafe(S->Composition.Get()));TArray<TSharedPtr<FJsonValue>> Nodes;
        for(int32 I=0;I<S->Recipe->Layers.Num();++I){auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("id"),S->Recipe->Nodes[I].ID.ToString());N->SetStringField(TEXT("group"),S->Recipe->Layers[I].GroupID.ToString());N->SetStringField(TEXT("system"),S->Recipe->Layers[I].System.ToString());N->SetBoolField(TEXT("enabled"),S->Recipe->Layers[I].bEnabled);N->SetNumberField(TEXT("x"),S->Recipe->Nodes[I].Position.X);N->SetNumberField(TEXT("y"),S->Recipe->Nodes[I].Position.Y);Nodes.Add(MakeShared<FJsonValueObject>(N));}
        D->SetArrayField(TEXT("nodes"),Nodes);Stages.Add(MakeShared<FJsonValueObject>(D));
    }
    Data->SetStringField(TEXT("loaded_pack"),GetLoadedPackPath().ToString());Data->SetBoolField(TEXT("pack_dirty"),HasUnsavedPackChanges());
    Data->SetArrayField(TEXT("stages"),Stages);FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}

#include "MCVFXActionSessionPacks.inl"
