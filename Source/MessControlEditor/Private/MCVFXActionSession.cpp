#include "MCVFXActionSession.h"

#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCPerkActionPreviewPanel.h"
#include "MCPerkPreviewEditorLibrary.h"
#include "MCPerkTypes.h"
#include "MCVFXComposerBuilderLibrary.h"
#include "MCVFXRecipePreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "FileHelpers.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraSystem.h"
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
        if(!A || !B || A->Slots.Num()!=B->Slots.Num())return false;
        for(const auto& Pair:A->Slots){const auto* Other=B->Slots.Find(Pair.Key);if(!Other || !FMCPerkVFXSlot::StaticStruct()->CompareScriptStruct(&Pair.Value,Other,0))return false;}
        return true;
    }
    FString StageMasterKey(FName Stage){return TEXT("MCActionSourceRecipe_")+Stage.ToString();}
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
        TStrongObjectPtr<UMCVFXRecipePreset> CompiledSnapshot;
        TStrongObjectPtr<UNiagaraSystem> Compiled;
        TStrongObjectPtr<UMCVFXComposition> Composition;
        TSoftObjectPtr<UMCVFXComposition> SourceRuntimeComposition;
        FString RuntimeSourceDigest;
        FMCPerkVFXSlot Outer;
        FString SourceMasterPath;
        FString CompiledSourceDigest;
        TMap<FSoftObjectPath,FGuid> TrialIDs;
        bool IsDirty() const{return !MCActionSession::SameRecipe(Recipe.Get(),Baseline.Get());}
        void ClearCache(){Compiled.Reset();Composition.Reset();CompiledSnapshot.Reset();CompiledSourceDigest.Reset();}
    };
    FName ActionID;
    FName CanonicalRowID;
    TArray<FName> StageOrder;
    TMap<FName,FText> StageLabels;
    FText ActionLabel;
    bool bSupportsProfileOverride=false;
    TStrongObjectPtr<UMCPerkVFXProfile> BaseProfile;
    TStrongObjectPtr<UMCPerkVFXProfile> AuditionProfile;
    TMap<FName,FStage> Stages;
    TSoftObjectPtr<UMCPerkVFXProfile> TableBaseline;
    FString ProfileSourceDigest;
    FString Status=TEXT("Выберите действие.");
    uint64 Revision=0;
    TFunction<void(int32)> SetPreviewCamera;
    bool bOwnsPlayback=false;
    bool ContextCurrent() const
    {return MCPerkActionPreviewPanel::GetContextPerkID()==ActionID && MCPerkActionPreviewPanel::GetActiveRunner().IsValid() && BaseProfile.IsValid() && Stages.Num()==StageOrder.Num();}
    bool Fail(FString& Error,const FString& Message){Error=Message;Status=Message;return false;}
    bool CanonicalProfileCurrent(FString& Error)
    {
        if(CanonicalRowID.IsNone())return !bSupportsProfileOverride || Fail(Error,TEXT("Для записи VFX этого контекста ещё не подключён источник данных."));
        const auto* T=MCActionSession::Table();const auto* Row=T?T->FindRow<FMCPerkDefinition>(CanonicalRowID,TEXT("ActionProfileGuard"),false):nullptr;
        if(!Row || Row->VFXProfile.ToSoftObjectPath()!=TableBaseline.ToSoftObjectPath())return Fail(Error,TEXT("Назначение VFX-профиля в игре изменилось. Откройте действие заново."));
        const auto* Current=Row->VFXProfile.LoadSynchronous();
        if((Current && Current->GetOutermost()->IsDirty()) || MCActionSession::PackageDigest(Current)!=ProfileSourceDigest
            || (Current && !MCActionSession::SameSlots(Current,BaseProfile.Get())))
            return Fail(Error,TEXT("Назначенный VFX-профиль был изменён по прежнему адресу. Черновик не перезапишет его; откройте действие заново."));
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
    void Changed(FName StageName)
    {
        if(auto* S=Stages.Find(StageName))S->ClearCache();
        ++Revision;Status=TEXT("Черновик изменён. «Просмотреть действие» показывает правку; «Применить» сохраняет её в игру.");
    }
    bool MakeSlot(FStage& S,const UMCVFXRecipePreset* Recipe,FMCPerkVFXSlot& Slot,FString& Error)
    {
        Slot=S.Outer;
        if(!MCActionSession::HasEnabled(Recipe)){Slot.Mode=EMCPerkVFXSlotMode::Disabled;Slot.System.Reset();Slot.Composition.Reset();return true;}
        if(!Recipe->ValidateLayout(Error) || !Recipe->Validate(Error))return false;
        FString SourceDigest;
        for(const auto& Layer:Recipe->Layers)if(Layer.bEnabled)
        {
            auto* NS=Layer.System.LoadSynchronous();
            if(!NS || NS->GetOutermost()->IsDirty()){S.ClearCache();Error=TEXT("Сначала сохраните изменённую исходную Niagara отдельно, затем повторите просмотр.");return false;}
            SourceDigest+=NS->GetPathName()+TEXT(":")+MCActionSession::PackageDigest(NS)+TEXT(";");
        }
        if(!S.Composition.IsValid() || S.CompiledSourceDigest!=SourceDigest || !MCActionSession::SameRecipe(Recipe,S.CompiledSnapshot.Get()))
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
    bool PrepareProfile(FName TrialStage,UNiagaraSystem* Trial,FString& Error)
    {
        if(!ContextCurrent())return Fail(Error,TEXT("Предпросмотр закрыт или профиль был заменён. Откройте действие заново."));
        if(!CanonicalProfileCurrent(Error))return false;
        auto* Profile=DuplicateObject<UMCPerkVFXProfile>(BaseProfile.Get(),GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UMCPerkVFXProfile::StaticClass(),TEXT("ActionHubAudition")));
        Profile->ClearFlags(RF_Public|RF_Standalone);Profile->SetFlags(RF_Transient);
        AuditionProfile.Reset(Profile);
        for(FName StageName:StageOrder)
        {
            auto& S=Stages.FindChecked(StageName);
            const bool bTrial=Trial && StageName==TrialStage;
            if(!S.IsDirty() && !bTrial)continue;
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
            if(!MakeSlot(S,Recipe,Slot,Error))return Fail(Error,StageName.ToString()+TEXT(": ")+Error);
            Profile->Slots.Add(StageName,Slot);
        }
        if(!Profile->Validate(Error))return Fail(Error,Error);
        return true;
    }
};

FMCVFXActionSession::FMCVFXActionSession():Impl(MakeUnique<FImpl>())
{if(GEditor)GEditor->RegisterForUndo(this);}
FMCVFXActionSession::~FMCVFXActionSession(){Stop();if(GEditor)GEditor->UnregisterForUndo(this);}
void FMCVFXActionSession::Clear(){Stop();const uint64 Next=Impl->Revision+1;Impl=MakeUnique<FImpl>();Impl->Revision=Next;}

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
    if(!Adapter || !Adapter->Create)return Impl->Fail(Error,TEXT("Для этого действия ещё не зарегистрирован сценарий предпросмотра."));
    const FName RowID=Adapter->Kind==EMCPerkPreviewAdapterKind::Context?Adapter->SourceRowID:(Adapter->SourceRowID.IsNone()?ActionID:Adapter->SourceRowID);
    auto* T=MCActionSession::Table();const auto* Row=T&&!RowID.IsNone()?T->FindRow<FMCPerkDefinition>(RowID,TEXT("ActionHub"),false):nullptr;
    const auto* CDO=Row?MCActionSession::ActionDefaults(*Row):nullptr;
    if(!RowID.IsNone() && (!Row || !Row->bEnabled))return Impl->Fail(Error,TEXT("Игровое действие недоступно в DT_Perks."));
    Stop();
    if(!MCPerkActionPreviewPanel::EnsureContext(ActionID,Error))return Impl->Fail(Error,Error);
    Impl->Stages.Reset();Impl->ActionID=ActionID;Impl->CanonicalRowID=RowID;Impl->TableBaseline=Row?Row->VFXProfile:TSoftObjectPtr<UMCPerkVFXProfile>();
    Impl->StageOrder=Adapter->Stages;Impl->StageLabels=Adapter->StageLabels;Impl->ActionLabel=Adapter->Label;
    Impl->bSupportsProfileOverride=Adapter->bSupportsProfileOverride && Row;
    auto* Source=Row?Row->VFXProfile.LoadSynchronous():nullptr;
    if(Row && !Row->VFXProfile.IsNull() && !Source)return Impl->Fail(Error,TEXT("Не удалось загрузить назначенный игре VFX-профиль."));
    if(Source && Source->GetOutermost()->IsDirty())return Impl->Fail(Error,TEXT("Назначенный VFX-профиль имеет несохранённые изменения."));
    auto* Base=Source?DuplicateObject<UMCPerkVFXProfile>(Source,GetTransientPackage()):NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    Base->ClearFlags(RF_Public|RF_Standalone);Base->SetFlags(RF_Transient);Impl->BaseProfile.Reset(Base);Impl->ProfileSourceDigest=MCActionSession::PackageDigest(Source);
    for(FName StageName:Impl->StageOrder)
    {
        FImpl::FStage S;if(const auto* Outer=Base->Slots.Find(StageName))
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
        Impl->Stages.Add(StageName,MoveTemp(S));
    }
    ++Impl->Revision;Impl->Status=TEXT("Выберите событие. Примерка временная; «Добавить» меняет черновик, «Применить» — игру.");Error.Reset();return true;
}

FName FMCVFXActionSession::GetActionID()const{return Impl->ActionID;}
bool FMCVFXActionSession::IsContextCurrent()const{return Impl->ContextCurrent();}
bool FMCVFXActionSession::CanEditProfile()const{return IsContextCurrent() && Impl->bSupportsProfileOverride;}
FString FMCVFXActionSession::GetEditUnavailableReason()const
{
    if(!IsContextCurrent())return TEXT("Контекст действия изменился. Выберите действие заново, чтобы восстановить предпросмотр.");
    if(!Impl->bSupportsProfileOverride)return TEXT("Реальный сценарий доступен. Runtime этого эффекта пока не читает VFX-профиль: добавление и применение слоёв ещё не подключены.");
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
TArray<EMCPerkPreviewScenario> FMCVFXActionSession::GetScenarios()const
{const auto* Adapter=MCPerkPreviewAdapters::Find(Impl->ActionID);return Adapter?Adapter->Scenarios:TArray<EMCPerkPreviewScenario>();}
FText FMCVFXActionSession::GetScenarioLabel(EMCPerkPreviewScenario Scenario)const
{
    if(const auto* Adapter=MCPerkPreviewAdapters::Find(Impl->ActionID))if(const auto* Label=Adapter->ScenarioLabels.Find(Scenario))return *Label;
    return FText::FromString(Scenario==EMCPerkPreviewScenario::Target?TEXT("Основной сценарий"):Scenario==EMCPerkPreviewScenario::NearWall?TEXT("Препятствие"):TEXT("Удержание / завершение"));
}
FString FMCVFXActionSession::GetStatus()const{return Impl->Status;}
uint64 FMCVFXActionSession::GetRevision()const{return Impl->Revision;}
bool FMCVFXActionSession::IsDirty()const{for(const auto& P:Impl->Stages)if(P.Value.IsDirty())return true;return false;}
int32 FMCVFXActionSession::GetLayerCount(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S && S->Recipe.IsValid()?S->Recipe->Layers.Num():0;}
UMCVFXRecipePreset* FMCVFXActionSession::GetRecipe(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S?S->Recipe.Get():nullptr;}
FMCPerkVFXSlot FMCVFXActionSession::GetOriginalSlot(FName Stage)const{const auto* S=Impl->Stages.Find(Stage);return S?S->Outer:FMCPerkVFXSlot();}

FGuid FMCVFXActionSession::AddSystem(FName Stage,UNiagaraSystem* System,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S || !System)return FGuid();
    if(S->Recipe->Layers.Num()>=32){Impl->Fail(Error,TEXT("В событии может быть не более 32 элементов."));return FGuid();}
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Add","Добавить VFX в событие"));S->Recipe->Modify();
    FMCVFXCompositionLayer L;L.Name=System->GetFName();L.System=System;S->Recipe->Layers.Add(L);
    FMCVFXRecipeNodeLayout N;N.ID=FGuid::NewGuid();N.Position=FVector2D(S->Recipe->Nodes.Num()*270,0);S->Recipe->Nodes.Add(N);
    S->Recipe->DiscoverParameters(S->Recipe->Layers.Num()-1,Error);Impl->Changed(Stage);return N.ID;
}
bool FMCVFXActionSession::SetLayer(FName Stage,const FGuid& ID,const FMCVFXCompositionLayer& Layer,FString& Error)
{
    auto* S=Impl->Stage(Stage,Error);if(!S)return false;const int32 I=S->Recipe->FindNode(ID);
    if(I==INDEX_NONE)return Impl->Fail(Error,TEXT("Элемент больше не существует. Выберите его заново."));
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
    Stop();const FScopedTransaction Tx(NSLOCTEXT("MCVFXAction","Import","Открыть композицию события"));S->Recipe->Modify();MCActionSession::CopyRecipeValues(Source,S->Recipe.Get());Impl->Changed(Stage);return true;
}

bool FMCVFXActionSession::AuditionNS(FName Stage,UNiagaraSystem* System,FString& Error,EMCPerkPreviewScenario Scenario)
{
    if(!System)return Impl->Fail(Error,TEXT("Выберите эффект из библиотеки."));
    if(!Impl->Stage(Stage,Error))return false;Stop();
    if(!Impl->PrepareProfile(Stage,System,Error))return false;
    auto R=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!R || !R->StartAction(Impl->ActionID,Scenario,Impl->AuditionProfile.Get()))return Impl->Fail(Error,TEXT("Не удалось запустить примерку настоящего действия."));
    Impl->bOwnsPlayback=true;Impl->Status=TEXT("Временная примерка: ")+System->GetName()+TEXT(". Состав черновика и игра не изменены.");return true;
}
bool FMCVFXActionSession::PlayFullAction(FString& Error,EMCPerkPreviewScenario Scenario)
{
    Error.Reset();Stop();if(!Impl->PrepareProfile(NAME_None,nullptr,Error))return false;
    auto R=MCPerkActionPreviewPanel::GetActiveRunner();
    if(!R || !R->StartAction(Impl->ActionID,Scenario,Impl->bSupportsProfileOverride?Impl->AuditionProfile.Get():nullptr))return Impl->Fail(Error,TEXT("Не удалось запустить настоящее действие."));
    Impl->bOwnsPlayback=true;Impl->Status=TEXT("Настоящий сценарий действия; игра не изменена.");return true;
}
void FMCVFXActionSession::Pause(bool bPaused){if(Impl->bOwnsPlayback && Impl->ContextCurrent())if(auto R=MCPerkActionPreviewPanel::GetActiveRunner())R->SetPaused(bPaused);}
void FMCVFXActionSession::Stop(){if(Impl->bOwnsPlayback && Impl->ContextCurrent())MCPerkActionPreviewPanel::ResetPreview();Impl->bOwnsPlayback=false;}
TSharedRef<SWidget> FMCVFXActionSession::MakePreviewWidget(){return MCPerkActionPreviewPanel::MakeSharedActionViewport(&Impl->SetPreviewCamera);}
void FMCVFXActionSession::SetPreviewView(int32 Preset){if(Impl->SetPreviewCamera)Impl->SetPreviewCamera(Preset);}

bool FMCVFXActionSession::ApplyAll(FString& Error,const FString& AssetRoot)
{
    Error.Reset();if(!CanEditProfile())return Impl->Fail(Error,GetEditUnavailableReason());
    if(GEditor && GEditor->PlayWorld)return Impl->Fail(Error,TEXT("Завершите Play перед применением VFX к игре."));
    auto* T=MCActionSession::Table();auto* Row=T?T->FindRow<FMCPerkDefinition>(Impl->CanonicalRowID,TEXT("ApplyActionVFX"),false):nullptr;
    if(!T || !Row)return Impl->Fail(Error,TEXT("Канонический игровой каталог недоступен."));
    if(T->GetOutermost()->IsDirty())return Impl->Fail(Error,TEXT("DT_Perks имеет несохранённые изменения; сначала сохраните или отмените их отдельно."));
    if(Row->VFXProfile.ToSoftObjectPath()!=Impl->TableBaseline.ToSoftObjectPath())return Impl->Fail(Error,TEXT("Профиль действия в игре изменён другим редактором. Откройте действие заново."));
    if(!IsDirty()){Impl->Status=TEXT("Изменений для применения нет.");return true;}
    if(!AssetRoot.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(AssetRoot))return Impl->Fail(Error,TEXT("Выберите папку ассетов внутри /Game/."));
    // Validate every changed stage before writing any asset. Table assignment is last.
    if(!Impl->PrepareProfile(NAME_None,nullptr,Error))return false;
    for(const auto& P:Impl->Stages)if(P.Value.IsDirty())for(const auto& L:P.Value.Recipe->Layers)if(L.bEnabled)
    {auto* NS=L.System.LoadSynchronous();if(!NS || NS->GetOutermost()->IsDirty())return Impl->Fail(Error,P.Key.ToString()+TEXT(": сначала сохраните исходную Niagara отдельно."));}
    Stop();
    const FString RunPath=AssetRoot/(Impl->ActionID.ToString()+TEXT("_"))+FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    TArray<FString> SavedPaths;TMap<FName,FString> StageMasters;for(const auto& P:Impl->Stages)StageMasters.Add(P.Key,P.Value.SourceMasterPath);
    TStrongObjectPtr<UMCPerkVFXProfile> NewProfile(DuplicateObject<UMCPerkVFXProfile>(Impl->BaseProfile.Get(),GetTransientPackage()));
    NewProfile->ClearFlags(RF_Public|RF_Standalone);NewProfile->SetFlags(RF_Transient);
    auto Failed=[&](const FString& Why){return Impl->Fail(Error,Why+(SavedPaths.IsEmpty()?FString():TEXT(" Промежуточные новые ассеты оставлены в ")+RunPath+TEXT("; игровой профиль не заменён.")));};
    for(FName StageName:Impl->StageOrder)
    {
        auto& S=Impl->Stages.FindChecked(StageName);if(!S.IsDirty())continue;
        FMCPerkVFXSlot Slot=S.Outer;
        const FString MasterPath=RunPath/(TEXT("DA_")+Impl->ActionID.ToString()+TEXT("_")+StageName.ToString()+TEXT("_Source"));
        auto* Master=MCActionSession::SaveNewCopy(S.Recipe.Get(),MasterPath,Error);if(!Master)return Failed(Error);SavedPaths.Add(Master->GetPathName());
        StageMasters.Add(StageName,Master->GetPathName());
        if(!MCActionSession::HasEnabled(S.Recipe.Get())){Slot.Mode=EMCPerkVFXSlotMode::Disabled;Slot.System.Reset();Slot.Composition.Reset();NewProfile->Slots.Add(StageName,Slot);continue;}
        if(!S.Composition.IsValid())return Failed(TEXT("Подготовленная композиция отсутствует. Повторите просмотр."));
        auto* Runtime=MCActionSession::SaveNewCopy(S.Composition.Get(),RunPath/(TEXT("DA_")+Impl->ActionID.ToString()+TEXT("_")+StageName.ToString()),Error);
        if(!Runtime)return Failed(Error);SavedPaths.Add(Runtime->GetPathName());
        Runtime->GetOutermost()->GetMetaData().SetValue(Runtime,TEXT("MCComposerSourceRecipe"),*Master->GetPathName());
        Runtime->MarkPackageDirty();
        if(!UEditorLoadingAndSavingUtils::SavePackages({Runtime->GetOutermost()},true))return Failed(TEXT("Не удалось сохранить ссылку на авторский исходник."));
        Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.System.Reset();Slot.Composition=Runtime;
        if(!UMCPerkVFXProfile::ValidateSlot(Slot,Error))return Failed(StageName.ToString()+TEXT(": ")+Error);
        NewProfile->Slots.Add(StageName,Slot);
    }
    if(!NewProfile->Validate(Error))return Failed(Error);
    auto* SavedProfile=MCActionSession::SaveNewCopy(NewProfile.Get(),RunPath/(TEXT("DA_")+Impl->ActionID.ToString()+TEXT("_ActionVFX")),Error,&StageMasters);
    if(!SavedProfile)return Failed(Error);SavedPaths.Add(SavedProfile->GetPathName());
    // Repeat the guard after export/waits, before the only canonical write.
    Row=T->FindRow<FMCPerkDefinition>(Impl->CanonicalRowID,TEXT("ApplyActionVFXFinalGuard"),false);
    if(!IsContextCurrent() || !Row || T->GetOutermost()->IsDirty() || !Impl->CanonicalProfileCurrent(Error))
        return Failed(TEXT("Контекст или DT_Perks изменились во время сохранения. Применение отменено."));
    const auto Previous=Row->VFXProfile;
    const FString Result=UMCPerkPreviewEditorLibrary::AssignSavedProfileToPerk(Impl->CanonicalRowID,SavedProfile);
    if(Row->VFXProfile.Get()!=SavedProfile)return Failed(Result);
    if(T->GetOutermost()->IsDirty())
    {
        // The shared writer leaves an in-memory assignment on save failure. Restore
        // the previous row immediately; do not discard the user's stage drafts.
        Row->VFXProfile=Previous;T->HandleDataTableChanged(Impl->CanonicalRowID);T->MarkPackageDirty();
        const bool Restored=UEditorLoadingAndSavingUtils::SavePackages({T->GetOutermost()},true);
        return Impl->Fail(Error,Result+(Restored?TEXT(" Предыдущее назначение восстановлено."):TEXT(" Предыдущее назначение восстановлено в памяти; сохранение DT также не удалось, пакет оставлен изменённым.")));
    }
    Impl->TableBaseline=SavedProfile;Impl->BaseProfile.Reset(DuplicateObject<UMCPerkVFXProfile>(SavedProfile,GetTransientPackage()));
    Impl->BaseProfile->ClearFlags(RF_Public|RF_Standalone);Impl->BaseProfile->SetFlags(RF_Transient);Impl->ProfileSourceDigest=MCActionSession::PackageDigest(SavedProfile);
    for(auto& P:Impl->Stages)
    {
        if(const auto* Slot=SavedProfile->Slots.Find(P.Key))
        {
            P.Value.Outer=MCActionSession::EffectiveOuter(*Slot);P.Value.SourceRuntimeComposition=Slot->Mode==EMCPerkVFXSlotMode::Composition?Slot->Composition:TSoftObjectPtr<UMCVFXComposition>();
            P.Value.RuntimeSourceDigest=MCActionSession::PackageDigest(P.Value.SourceRuntimeComposition.LoadSynchronous());
        }
        P.Value.SourceMasterPath=StageMasters.FindRef(P.Key);P.Value.Baseline.Reset(MCActionSession::CopyRecipe(P.Value.Recipe.Get(),TEXT("AppliedStageBaseline")));P.Value.ClearCache();
    }
    ++Impl->Revision;Impl->Status=Result;return true;
}

void FMCVFXActionSession::PostUndo(bool bSuccess)
{if(bSuccess){Stop();for(auto& P:Impl->Stages)P.Value.ClearCache();++Impl->Revision;Impl->Status=TEXT("Изменён черновик после Undo. Проверьте действие перед применением.");}}
void FMCVFXActionSession::PostRedo(bool bSuccess){PostUndo(bSuccess);}
FString FMCVFXActionSession::GetSnapshotJson()const
{
    auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("action"),Impl->ActionID.ToString());Data->SetBoolField(TEXT("context_current"),IsContextCurrent());
    Data->SetBoolField(TEXT("can_edit_profile"),CanEditProfile());Data->SetStringField(TEXT("edit_unavailable_reason"),GetEditUnavailableReason());
    Data->SetBoolField(TEXT("dirty"),IsDirty());Data->SetNumberField(TEXT("revision"),double(Impl->Revision));Data->SetStringField(TEXT("status"),Impl->Status);
    Data->SetStringField(TEXT("table_baseline"),Impl->TableBaseline.ToString());TArray<TSharedPtr<FJsonValue>> Stages;
    for(FName StageName:Impl->StageOrder)if(const auto* S=Impl->Stages.Find(StageName))
    {
        auto D=MakeShared<FJsonObject>();D->SetStringField(TEXT("stage"),StageName.ToString());D->SetBoolField(TEXT("dirty"),S->IsDirty());D->SetNumberField(TEXT("layers"),S->Recipe->Layers.Num());
        D->SetStringField(TEXT("cached_system"),GetPathNameSafe(S->Compiled.Get()));D->SetStringField(TEXT("cached_composition"),GetPathNameSafe(S->Composition.Get()));TArray<TSharedPtr<FJsonValue>> Nodes;
        for(int32 I=0;I<S->Recipe->Layers.Num();++I){auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("id"),S->Recipe->Nodes[I].ID.ToString());N->SetStringField(TEXT("system"),S->Recipe->Layers[I].System.ToString());N->SetBoolField(TEXT("enabled"),S->Recipe->Layers[I].bEnabled);Nodes.Add(MakeShared<FJsonValueObject>(N));}
        D->SetArrayField(TEXT("nodes"),Nodes);Stages.Add(MakeShared<FJsonValueObject>(D));
    }
    Data->SetArrayField(TEXT("stages"),Stages);FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Data,Writer);return Result;
}
