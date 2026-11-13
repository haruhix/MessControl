#include "MCPreloadManifest.h"
#include "MCReactionVFX.h"
#include "MCArenaTooth.h"

#include "MCPreloadSettings.h"
#include "MCGameMode.h"
#include "MCGameDirector.h"
#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCSingleDayDirector.h"
#include "MCPassiveCombatCue.h"
#include "MCPassiveFeedbackCue.h"
#include "MCDefenseSprayPassives.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXSourceContracts.h"
#include "MCNutCombatEffect.h"
#include "MCNutVFXCatalog.h"
#include "MCNutSpellProjectile.h"
#include "MCDataAssets.h"
#include "MCEventSFXReadiness.h"
#include "MCExperimentalAudioComponent.h"
#include "MCPerkTypes.h"
#include "MCSceneAudioComponent.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundAssetManager.h"
#include "MetasoundFrontendNodeClassRegistry.h"
#include "MetasoundGlobals.h"
#include "MetasoundSettings.h"
#include "MetasoundSource.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraParameterStore.h"
#include "MCFreezeCapturedGeometry.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
// DataTable map references describe destinations, not dependencies to load into this world.
bool CanPrepareClass(const UClass* Class)
{
    return !Class || (!Class->IsChildOf(UWorld::StaticClass()) && !Class->IsChildOf(ULevel::StaticClass()));
}

bool RequiresFreezePresentation(UWorld* World)
{
    const auto* GS=World?World->GetGameState<AMCGameState>():nullptr;
    const FString Map=World?UGameplayStatics::GetCurrentLevelName(World,true):FString();
    return (Map==TEXT("L_Mouth")||Map==TEXT("L_Tutorial"))&&GS&&GS->GameplayLoadId.IsValid()
        &&GS->IcePresentationScope.GameplayLoadId==GS->GameplayLoadId
        &&GS->IcePresentationScope.Scope==EMCIcePresentationScope::Required;
}

bool IsArenaLocation(UWorld* World)
{
    const FString Map=World?UGameplayStatics::GetCurrentLevelName(World,true):FString();
    return Map==TEXT("L_Mouth") || Map==TEXT("L_Tutorial");
}

void AddPath(const FSoftObjectPath& Path,TArray<FSoftObjectPath>& Out)
{
    const FString Name=Path.GetAssetPathString();
    // Explicit reflected dependencies may live under any mounted runtime
    // content root, including plugins. This never enumerates a plugin bank.
    if(!Path.IsNull() && (FPackageName::IsValidLongPackageName(Path.GetLongPackageName(),false)
        || Name.StartsWith(TEXT("/Script/MessControl.")))) Out.AddUnique(Path);
}

bool ValidateRuntimeAudioObject(const UObject* Object,FString& Error)
{
    if(!IsValid(Object) || Object->IsEditorOnly() || Object->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly))
    {Error=TEXT("Audio dependency is absent or editor-only.");return false;}
    if(Object->IsAsset() && Object->GetOutermost()!=GetTransientPackage()
        && !FPackageName::IsValidLongPackageName(Object->GetOutermost()->GetName(),false))
    {Error=TEXT("Audio asset requires a mounted runtime content root: ")+Object->GetPathName();return false;}
    return true;
}

bool IsAudioResourceClass(const UClass* Class)
{
    return Class && (Class->IsChildOf(USoundBase::StaticClass()) || Class->IsChildOf(USoundClass::StaticClass())
        || Class->IsChildOf(USoundSubmixBase::StaticClass()) || Class->IsChildOf(USoundAttenuation::StaticClass())
        || Class->IsChildOf(USoundConcurrency::StaticClass()));
}

class FMCSoundCueResourceClosure
{
public:
    FMCSoundCueResourceClosure(FMCEventSFXSamplePlan& InPlan,FString& InError,bool& InWaiting,bool bInPrepare=false,bool bInAllowLoad=false)
        : Plan(InPlan),Error(InError),bWaiting(InWaiting),bPrepare(bInPrepare),bAllowLoad(bInAllowLoad) {}
    bool Visit(USoundNode* Node,int32 Depth=0)
    {
        if(!Node)return true; // An authored empty branch is intentional silence.
        if(Active.Contains(Node)){Error=TEXT("SoundCue resource graph contains a cycle.");return false;}
        if(Visited.Contains(Node))return true;
        if(Depth>64 || Visited.Num()+Active.Num()>=512)
        {Error=TEXT("SoundCue resource graph exceeds the 512-node/64-depth preparation bound.");return false;}
        Active.Add(Node);
        if(Node->GetClass()==USoundNodeWavePlayer::StaticClass()) {
            auto* Player=CastChecked<USoundNodeWavePlayer>(Node);
            auto* Wave=Player->GetSoundWave();
            // Read the authored reference. Only explicit prebarrier preparation
            // may populate the engine's transient cache; readonly validation
            // never substitutes the resident soft target for a missing cache.
            if(const auto* Property=FindFProperty<FSoftObjectProperty>(Player->GetClass(),TEXT("SoundWaveAssetPtr"))) {
                const FSoftObjectPtr Reference=Property->GetPropertyValue(Property->ContainerPtrToValuePtr<void>(Player));
                auto* Resident=Cast<USoundWave>(Reference.Get());
                if(!Reference.IsNull() && !FPackageName::IsValidLongPackageName(Reference.ToSoftObjectPath().GetLongPackageName(),false)
                    && !Resident)
                {Error=TEXT("SoundCue wave has no mounted runtime content root: ")+Reference.ToString();return false;}
                if(bPrepare && !Resident && bAllowLoad)Resident=Cast<USoundWave>(Reference.ToSoftObjectPath().TryLoad());
                if(bPrepare && (!Wave || Wave!=Resident) && !Reference.IsNull() && !Player->IsCurrentlyAsyncLoadingAsset()) {
                    Player->LoadAsset(false); // Runtime cache only; never Modify/SetSoundWave/save.
                    Wave=Player->GetSoundWave();
                }
                if(Wave && Resident && Wave!=Resident)
                {Error=TEXT("SoundCue's cached WavePlayer differs from its authored source; reload/recompile the source.");return false;}
            }
            if(!IsValid(Wave) || Player->IsCurrentlyAsyncLoadingAsset()) {
                bWaiting=true;Error=TEXT("SoundCue WavePlayer cache preparation is pending; use explicit prebarrier preparation: ")+Player->GetPathName();return false;
            }
            if(!ValidateRuntimeAudioObject(Wave,Error))return false;
            if(Wave->IsProcedurallyGenerated() || Wave->bIsSourceBus)
            {Error=TEXT("Unsupported dynamic/procedural SoundCue wave; its source provider needs an audited resource closure: ")+Wave->GetPathName();return false;}
            Plan.SampleWaves.AddUnique(Wave);
        } else {
            // Static routing/modulation may choose among authored children; all
            // branches are retained without choosing RNG or gameplay parameters.
            static const TSet<FName> StaticNodeClasses={
                TEXT("SoundNodeAttenuation"),TEXT("SoundNodeBranch"),TEXT("SoundNodeConcatenator"),
                TEXT("SoundNodeDelay"),TEXT("SoundNodeDistanceCrossFade"),TEXT("SoundNodeDoppler"),
                TEXT("SoundNodeEnveloper"),TEXT("SoundNodeGroupControl"),TEXT("SoundNodeLooping"),
                TEXT("SoundNodeMature"),TEXT("SoundNodeMixer"),TEXT("SoundNodeModulator"),
                TEXT("SoundNodeModulatorContinuous"),TEXT("SoundNodeOscillator"),TEXT("SoundNodeParamCrossFade"),
                TEXT("SoundNodeQualityLevel"),TEXT("SoundNodeRandom"),TEXT("SoundNodeSoundClass"),TEXT("SoundNodeSwitch")};
            if(Node->GetClass()->GetOutermost()->GetFName()!=TEXT("/Script/Engine") || !StaticNodeClasses.Contains(Node->GetClass()->GetFName()))
            {Error=TEXT("Unsupported SoundCue node requires a source-specific resource audit: ")+Node->GetClass()->GetPathName();return false;}
        }
        for(USoundNode* Child:Node->ChildNodes)if(!Visit(Child,Depth+1))return false;
        Active.Remove(Node);Visited.Add(Node);return true;
    }
private:
    FMCEventSFXSamplePlan& Plan;
    FString& Error;
    bool& bWaiting;
    bool bPrepare=false,bAllowLoad=false;
    TSet<USoundNode*> Visited,Active;
};

class FMCMetaSoundResourceClosure
{
public:
    FMCMetaSoundResourceClosure(FMCEventSFXSamplePlan& InPlan, FString& InError)
        : Plan(InPlan), Error(InError) {}

    bool Visit(FMetasoundAssetBase& Graph, int32 Depth=0)
    {
        const UObject* Asset=Graph.GetOwningAsset();
        if(!ValidateRuntimeAudioObject(Asset,Error))return false;
        if(Active.Contains(Asset) || Depth>32 || Visited.Num()+Active.Num()>=128)
        {Error=TEXT("MetaSound graph dependencies contain a cycle or exceed the preparation bound.");return false;}
        if(Visited.Contains(Asset))return true;
        Active.Add(Asset);

        const auto References=Graph.GetReferencedAssets();
        if(References.Num()!=Graph.GetReferencedAssetClassKeys().Num())
        {Error=TEXT("MetaSound graph dependencies are unresolved. Open and save the source and its referenced graphs.");return false;}
        for(FMetasoundAssetBase* Reference:References) {
            if(!Visit(*Reference,Depth+1))return false;
            Plan.ReferencedGraphs.AddUnique(Reference->GetOwningAsset());
        }

        // Follow the same authored page selection and UObject literals used by
        // UE's graph proxy cache, including defaults, arrays and graph variables.
        Metasound::Frontend::ForEachLiteral(Graph.GetConstDocumentChecked(),
            [this](const FName&, const FMetasoundFrontendLiteral& Literal)
            {
                if(!Error.IsEmpty())return;
                UObject* Object=nullptr;
                if(Literal.TryGet(Object))AddSample(Object);
                else if(TArray<UObject*> Objects;Literal.TryGet(Objects))
                    for(UObject* Item:Objects)AddSample(Item);
            }, UMetaSoundSettings::GetPageOrder());
        if(!Error.IsEmpty())return false;
        Active.Remove(Asset);
        Visited.Add(Asset);
        return true;
    }

private:
    void AddSample(UObject* Object)
    {
        if(!Object || !Error.IsEmpty())return;
        if(!ValidateRuntimeAudioObject(Object,Error))return;
        auto* Wave=Cast<USoundWave>(Object);
        if(!Wave || Wave->IsProcedurallyGenerated() || Wave->bIsSourceBus)
        {Error=TEXT("MetaSound object input requires a provider audit: ")+Object->GetPathName();return;}
        Plan.SampleWaves.AddUnique(Wave);
    }

    FMCEventSFXSamplePlan& Plan;
    FString& Error;
    TSet<const UObject*> Visited,Active;
};

class FMCVisualReferenceCollector
{
public:
    explicit FMCVisualReferenceCollector(TArray<FSoftObjectPath>& InOut,UWorld* InLocation=nullptr,FString* InAudioError=nullptr)
        : Out(InOut),Location(InLocation),AudioError(InAudioError) {}
    void Object(UObject* Value,int32 Depth=0)
    {
        if(!IsValid(Value) || Depth>24 || Visited.Contains(Value)) return;
        if(AudioError && (!AudioError->IsEmpty() || (IsAudioResourceClass(Value->GetClass()) && !ValidateRuntimeAudioObject(Value,*AudioError))))return;
        Visited.Add(Value);
        // The retired manual comparison bank is not a gameplay dependency.
        if(Value->IsA<UMCExperimentalAudioComponent>())return;
        if(const auto* SceneAudio=Cast<UMCSceneAudioComponent>(Value)) {
            FMCSoundScene Scene;
            if(SceneAudio->SoundPalette && SceneAudio->SoundPalette->GetScene(SceneAudio->SceneContext,Scene)) {
                AddPath(Scene.Music.Sound.ToSoftObjectPath(),Out);
                AddPath(Scene.Ambience.Sound.ToSoftObjectPath(),Out);
            }
        }
        if(const auto* Palette=Cast<UMCSoundPalette>(Value);Palette && IsArenaLocation(Location)) {
            // These contexts are chosen by the arena's actual scene controller.
            // Hub/menu prepare their selected context through SceneAudio above.
            for(const TCHAR* Name:{TEXT("Gameplay"),TEXT("Relax"),TEXT("NutEncounter"),TEXT("IceEvent"),
                TEXT("FogBrawl"),TEXT("CataclysmRafts"),TEXT("CataclysmJellies"),TEXT("ColdCola"),TEXT("FinalBoss")}) {
                FMCSoundScene Scene;if(Palette->GetScene(FName(Name),Scene)) {
                    AddPath(Scene.Music.Sound.ToSoftObjectPath(),Out);
                    AddPath(Scene.Ambience.Sound.ToSoftObjectPath(),Out);
                }
            }
        }
        if(auto* Class=Cast<UClass>(Value)) {
            // Inspect authored defaults without constructing any gameplay actors.
            Object(Class->GetDefaultObject(),Depth+1);
            // A generated UMG class stores its Designer brushes/fonts in the
            // archetype tree, not in its user-widget CDO. Walk only that authored
            // tree; do not traverse transient live-widget/Slate object graphs.
            if(auto* WidgetClass=Cast<UWidgetBlueprintGeneratedClass>(Class))
                if(auto* TreeClass=WidgetClass->FindWidgetTreeOwningClass())
                    if(const auto* Tree=TreeClass->GetWidgetTreeArchetype())
                        Tree->ForEachWidget([this,Depth](UWidget* Widget) {Object(Widget,Depth+1);});
            return;
        }
        if(auto* Table=Cast<UDataTable>(Value)) {
            if(const auto* RowStruct=Table->GetRowStruct())
                for(const auto& Row:Table->GetRowMap()) {
                    // Runtime prepares available boosters, not retired catalog
                    // experiments. Editor/index discovery (no Location) stays complete.
                    if(Location && RowStruct==FMCPerkDefinition::StaticStruct()
                        && !reinterpret_cast<const FMCPerkDefinition*>(Row.Value)->bEnabled)continue;
                    Struct(RowStruct,Row.Value,Value,Depth+1);
                }
        }
        // Emitter/renderer subobjects are not DataAssets. Follow their actual
        // authored render resources explicitly, including material overrides,
        // rather than relying on the first spawned NiagaraComponent to find them.
        if(const auto* System=Cast<UNiagaraSystem>(Value)) {
            if(FMCPreparedFreezeGeometryLayer::UsesCapture(System)) {
                TArray<FNiagaraVariable> Inputs;System->GetExposedParameters().GetParameters(Inputs);
                for(const auto& Input:Inputs)if(FMCPreparedFreezeGeometryLayer::IsCaptureName(Input.GetName())
                    &&Input.GetType()==FNiagaraTypeDefinition(UMaterialInterface::StaticClass()))
                    if(auto* Material=Cast<UMaterialInterface>(System->GetExposedParameters().GetUObject(Input).Get())) {
                        AddPath(FSoftObjectPath(Material),Out);Object(Material,Depth+1);
                    }
            }
            for(const auto& Handle:System->GetEmitterHandles())
                if(const auto* Data=Handle.GetEmitterData())
                    for(auto* Renderer:Data->GetRenderers()) if(IsValid(Renderer)) {
                        Object(Renderer,Depth+1);
                        TArray<UMaterialInterface*> Materials;
                        Renderer->GetUsedMaterials(nullptr,Materials);
                        for(auto* Material:Materials) if(IsValid(Material)) {
                            AddPath(FSoftObjectPath(Material),Out);Object(Material,Depth+1);
                        }
                        TArray<UObject*> Meshes;Renderer->GetUsedMeshes(nullptr,Meshes);
                        for(auto* Mesh:Meshes) if(IsValid(Mesh)) {
                            AddPath(FSoftObjectPath(Mesh),Out);Object(Mesh,Depth+1);
                        }
                    }
        }
        Struct(Value->GetClass(),Value,Value,Depth+1);
    }
private:
    bool PropertyMayContainReference(const FProperty* Prop)
    {
        if(!Prop || Prop->HasAnyPropertyFlags(CPF_Deprecated|CPF_EditorOnly|CPF_Transient))return false;
        if(const bool* Known=ReferenceProperties.Find(Prop))return *Known;
        // A recursive reflected type is conservatively reference-bearing until
        // resolved. Never prune a possible object/soft-object dependency cycle.
        ReferenceProperties.Add(Prop,true);
        bool Result=Prop->IsA<FSoftObjectProperty>() || Prop->IsA<FObjectPropertyBase>();
        if(const auto* Structure=CastField<FStructProperty>(Prop)) {
            for(TFieldIterator<FProperty> It(Structure->Struct);It;++It)
                if(PropertyMayContainReference(*It)){Result=true;break;}
        }
        else if(const auto* Array=CastField<FArrayProperty>(Prop))Result=PropertyMayContainReference(Array->Inner);
        else if(const auto* Map=CastField<FMapProperty>(Prop))
            Result=PropertyMayContainReference(Map->KeyProp)||PropertyMayContainReference(Map->ValueProp);
        else if(const auto* Set=CastField<FSetProperty>(Prop))Result=PropertyMayContainReference(Set->ElementProp);
        ReferenceProperties[Prop]=Result;return Result;
    }
    void Struct(const UStruct* Type,const void* Data,UObject* Owner,int32 Depth)
    {
        if(!Type || !Data || Depth>24) return;
        for(TFieldIterator<FProperty> It(Type);It;++It) {
            if(!PropertyMayContainReference(*It))continue;
            if(const auto* Mode=Cast<AMCGameMode>(Owner);Location && Mode) {
                const FName Field=It->GetFName();
                if(Mode->bUseSingleDayLoop && Field==TEXT("EventPool"))continue;
                if(!Mode->bUseSingleDayLoop && Field==TEXT("SingleDayProfile"))continue;
                const auto* State=Location->GetGameState<AMCGameState>();
                if(Mode->bUseSingleDayLoop && State && State->SingleDayDirector
                    && State->SingleDayDirector->GetRunProfile() && State->SingleDayDirector->GetSupportProfile()
                    && (Field==TEXT("SingleDayProfile") || Field==TEXT("DirectorProfile")))continue;
                // FirstDayPlan also supplies the real single-day run. Keep it.
            }
            if(const auto* Library=Cast<UMCVFXPresentationLibrary>(Owner);Location&&Library&&It->GetFName()==TEXT("Profiles")
                &&Library->GetPathName()==MCVFXSourceContracts::WorldLibraryPath().ToString()) {
                for(const auto& Pair:Library->Profiles)
                    if(Pair.Key!=TEXT("Character.FreezeDeath")||RequiresFreezePresentation(Location))AddPath(Pair.Value.ToSoftObjectPath(),Out);
                continue;
            }
            // A palette contains scenes for several locations, not one global preload bank.
            if(Owner->IsA<UMCSoundPalette>() && It->GetFName()==TEXT("Scenes"))continue;
            if(Location && !IsArenaLocation(Location) && It->GetFName()==TEXT("VFXProfile"))
                if(const auto* Table=Cast<UDataTable>(Owner);Table && Table->GetRowStruct()==FMCPerkDefinition::StaticStruct())continue;
            for(int32 Index=0;Index<It->ArrayDim;++Index)
                Property(*It,It->ContainerPtrToValuePtr<void>(Data,Index),Owner,Depth+1);
        }
    }
    void Property(const FProperty* Prop,const void* Data,UObject* Owner,int32 Depth)
    {
        if(!Data || Depth>24) return;
        if(const auto* Soft=CastField<FSoftObjectProperty>(Prop)) {
            if(AudioError && IsAudioResourceClass(Soft->PropertyClass)) {
                const FSoftObjectPtr Reference=Soft->GetPropertyValue(Data);
                if(!Reference.IsNull() && !FPackageName::IsValidLongPackageName(Reference.ToSoftObjectPath().GetLongPackageName(),false))
                {*AudioError=TEXT("Audio dependency requires a mounted runtime content root: ")+Reference.ToSoftObjectPath().ToString();return;}
                if(UObject* Resource=Reference.Get();Resource && !ValidateRuntimeAudioObject(Resource,*AudioError))return;
            }
            if(CanPrepareClass(Soft->PropertyClass)) AddPath(Soft->GetPropertyValue(Data).ToSoftObjectPath(),Out);
        }
        else if(const auto* Hard=CastField<FObjectPropertyBase>(Prop)) {
            UObject* Target=Hard->GetObjectPropertyValue(Data);
            if(!IsValid(Target) || !CanPrepareClass(Target->GetClass())) return;
            if(Target->IsAsset() || Target->IsA<UClass>()) AddPath(FSoftObjectPath(Target),Out);
            if(Target->IsA<UDataAsset>() || Target->IsA<UDataTable>() || Target->IsA<UClass>()
                || Target->IsA<USoundBase>() || Target->IsA<USoundNode>() || Target->IsA<USoundClass>()
                || Target->IsA<USoundSubmixBase>() || Target->IsA<USoundAttenuation>() || Target->IsA<USoundConcurrency>()
                || (Target->IsA<UActorComponent>() && Target->IsIn(Owner))) Object(Target,Depth+1);
        }
        else if(const auto* Structure=CastField<FStructProperty>(Prop)) Struct(Structure->Struct,Data,Owner,Depth+1);
        else if(const auto* Array=CastField<FArrayProperty>(Prop)) {
            FScriptArrayHelper Helper(Array,Data);
            for(int32 I=0;I<Helper.Num();++I) Property(Array->Inner,Helper.GetRawPtr(I),Owner,Depth+1);
        }
        else if(const auto* Map=CastField<FMapProperty>(Prop)) {
            FScriptMapHelper Helper(Map,Data);
            for(int32 I=0;I<Helper.GetMaxIndex();++I) if(Helper.IsValidIndex(I)) {
                Property(Map->KeyProp,Helper.GetKeyPtr(I),Owner,Depth+1);
                Property(Map->ValueProp,Helper.GetValuePtr(I),Owner,Depth+1);
            }
        }
        else if(const auto* Set=CastField<FSetProperty>(Prop)) {
            FScriptSetHelper Helper(Set,Data);
            for(int32 I=0;I<Helper.GetMaxIndex();++I) if(Helper.IsValidIndex(I))
                Property(Set->ElementProp,Helper.GetElementPtr(I),Owner,Depth+1);
        }
    }
    TSet<const UObject*> Visited;
    TMap<const FProperty*,bool> ReferenceProperties;
    TArray<FSoftObjectPath>& Out;
    UWorld* Location=nullptr;
    FString* AudioError=nullptr;
};

bool BuildMetaSoundPlan(UMetaSoundSource* Source, FMCEventSFXSamplePlan& OutPlan, FString& Error, bool bPrepare)
{
    FMCEventSFXSamplePlan Candidate;
    Candidate.Source=Source;
    if(!FMCMetaSoundResourceClosure(Candidate,Error).Visit(*Source)) {
        Error=Source->GetPathName()+TEXT(": ")+Error;
        return false;
    }
    TArray<FSoftObjectPath> Dependencies;
    FMCVisualReferenceCollector(Dependencies,nullptr,&Error).Object(Source);
    if(!Error.IsEmpty())return false;

    if(bPrepare && Metasound::CanEverExecuteGraph()) {
        Metasound::Frontend::FMetaSoundAssetRegistrationOptions Options;
        Options.PageOrder=UMetaSoundSettings::GetPageOrder();
        Options.bForceReregister=false;
        Options.bAutoUpdate=false;
#if WITH_EDITOR
        Options.bForceViewSynchronization=false;
        Options.bRebuildReferencedAssetClasses=false;
#endif
        Source->UpdateAndRegisterForExecution(Options);
    }

    const auto* Registry=Metasound::Frontend::INodeClassRegistry::Get();
    if(!Source->IsRegistered() || !Registry || !Registry->IsGraphRegistered(Source->GetGraphRegistryKey())) {
        Error=TEXT("MetaSound graph execution resources could not be prepared: ")+Source->GetPathName();
        return false;
    }
    Candidate.bDependencyAuditComplete=true;
    Candidate.bGraphPrepared=true;
    OutPlan=MoveTemp(Candidate);
    return true;
}

void AddAsset(const TCHAR* Package,TArray<FSoftObjectPath>& Out)
{
    const FString Name(Package);
    AddPath(FSoftObjectPath(Name+TEXT(".")+FPackageName::GetShortName(Name)),Out);
}
}

void FMCPreloadManifest::CollectSoftReferences(UObject* Object,TArray<FSoftObjectPath>& OutPaths,UWorld* Location)
{
    FMCVisualReferenceCollector(OutPaths,Location).Object(Object);
}

bool FMCPreloadManifest::BuildEventAudioPlan(USoundBase* Source,FMCEventSFXSamplePlan& OutPlan,FString& Error)
{
    bool Waiting=false;
    Error.Reset();FMCEventSFXSamplePlan Candidate;Candidate.Source=Source;
    if(!ValidateRuntimeAudioObject(Source,Error))return false;
    if(auto* Wave=Cast<USoundWave>(Source);Wave && !Wave->IsProcedurallyGenerated() && !Wave->bIsSourceBus) {
        TArray<FSoftObjectPath> Dependencies;FMCVisualReferenceCollector(Dependencies,nullptr,&Error).Object(Source);
        if(!Error.IsEmpty())return false;
        Candidate.SampleWaves.Add(Wave);OutPlan=MoveTemp(Candidate);return true;
    }
    if(auto* Cue=Cast<USoundCue>(Source)) {
        if(!FMCSoundCueResourceClosure(Candidate,Error,Waiting).Visit(Cue->FirstNode)) {
            Error=Source->GetPathName()+TEXT(": ")+Error;return false;
        }
        Candidate.bDependencyAuditComplete=true;Candidate.bGraphPrepared=true;
        TArray<FSoftObjectPath> Dependencies;FMCVisualReferenceCollector(Dependencies,nullptr,&Error).Object(Source);
        if(!Error.IsEmpty())return false;
        OutPlan=MoveTemp(Candidate);return true;
    }
    if(auto* MetaSound=Cast<UMetaSoundSource>(Source))return BuildMetaSoundPlan(MetaSound,OutPlan,Error,false);
    Error=TEXT("Unsupported procedural/dynamic audio source: resource dependency closure needs its actual source-provider audit: ")+Source->GetPathName();
    return false;
}

bool FMCPreloadManifest::PrepareEventAudioPlan(USoundBase* Source,FMCEventSFXSamplePlan& OutPlan,FString& Error,
    bool& bWaiting,bool bAllowDependencyLoad)
{
    bWaiting=false;Error.Reset();
    if(!ValidateRuntimeAudioObject(Source,Error))return false;
    if(auto* Cue=Cast<USoundCue>(Source)) {
        FMCEventSFXSamplePlan Candidate;Candidate.Source=Source;
        if(!FMCSoundCueResourceClosure(Candidate,Error,bWaiting,true,bAllowDependencyLoad).Visit(Cue->FirstNode))return false;
        return BuildEventAudioPlan(Source,OutPlan,Error);
    }
    if(auto* MetaSound=Cast<UMetaSoundSource>(Source))return BuildMetaSoundPlan(MetaSound,OutPlan,Error,true);
    return BuildEventAudioPlan(Source,OutPlan,Error);
}

void FMCPreloadManifest::Collect(UWorld* World,TArray<FSoftObjectPath>& OutPaths)
{
    if(!World) return;
    const FName Map(*UGameplayStatics::GetCurrentLevelName(World,true));
    const auto* Settings=GetDefault<UMCPreloadSettings>();
    // The same resident artwork covers blocking travel and the local readiness barrier.
    AddPath(Settings->LoadingScreenArt.ToSoftObjectPath(),OutPaths);
    for(const auto& Path:Settings->CommonAssets) AddPath(Path,OutPaths);
    for(const auto& Entry:Settings->Maps) if(Entry.Map==Map)
        for(const auto& Path:Entry.Assets) AddPath(Path,OutPaths);
    AddAsset(TEXT("/Engine/BasicShapes/Plane"),OutPaths);
    // The factory soft-loads this shared theme in Hub/menu as well as the arena.
    // Its role map is the finite root for future settings/reward/pause widgets.
    AddAsset(TEXT("/Game/Gameplay/UI/Theme/DA_GameplayUITheme"),OutPaths);
    // The default interface needs its finite UI/font and glyph roots in packaged
    // games too; F3 presentation comparison is only an optional development tool.
    if(FPackageName::DoesPackageExist(TEXT("/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2")))
        AddAsset(TEXT("/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2"),OutPaths);
    AddAsset(TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Glyph"),OutPaths);
    AddPath(MCVFXSourceContracts::InterfaceLibraryPath(),OutPaths);
    // Late-spawned Hub players use the same authored locomotion and face clips.
    AddAsset(TEXT("/Game/Data/DA_ToothAnimation"),OutPaths);
    // MCUIVisualStyle chooses these shared surfaces by native path rather than
    // a saved Designer brush. Include them before shop/wardrobe cards are built.
    for(const TCHAR* Root:{TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Panel"),
        TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Fill"),
        TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Tool")}) AddAsset(Root,OutPaths);
    // Hub purchases and equipped accessories continue into the following arena.
    // Their tables are finite roots, including items not yet equipped at load time.
    for(const TCHAR* Root:{TEXT("/Game/Gameplay/Hub/Economy/DT_HubCatalog"),
        TEXT("/Game/Gameplay/Hub/Economy/DT_GrillRewards"),TEXT("/Game/Gameplay/Hub/Economy/DT_HubEconomyRules"),
        TEXT("/Game/Data/DT_CharacterCosmetics"),TEXT("/Game/Gameplay/Roguelike/DT_Perks")}) AddAsset(Root,OutPaths);

    // The actual saved actors/components also capture artist overrides in the current map.
    FMCVisualReferenceCollector Collector(OutPaths,World);
    for(TActorIterator<AActor> It(World);It;++It) {
        Collector.Object(*It);
        TInlineComponentArray<UActorComponent*> Components(*It);
        for(auto* Component:Components) Collector.Object(Component);
    }
    const bool Hub=Map==TEXT("Hub") || Map==TEXT("L_HubSelection");
    if(Hub) {
        AddPath(MCVFXSourceContracts::HubLibraryPath(),OutPaths);
        AddAsset(TEXT("/Game/Gameplay/Hub/Economy/M_CaseHologramProjection"),OutPaths);
        AddAsset(TEXT("/Game/Gameplay/Hub/Economy/T_CaseRareToothReaction"),OutPaths);
        AddAsset(TEXT("/Game/Gameplay/Hub/Economy/SFX/SW_CaseRareFanfare"),OutPaths);
        AddAsset(TEXT("/Engine/EngineMaterials/Widget3DPassThrough"),OutPaths);
        AddAsset(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided"),OutPaths);
        // Hub characters share their actual movement/hit tool source library.
        AddPath(MCVFXSourceContracts::ToolLibraryPath(),OutPaths);
    }
    if(RequiresFreezePresentation(World)) {
        AddPath(MCVFXSourceContracts::WorldLibraryPath(),OutPaths);
        AddPath(FSoftObjectPath(TEXT("/Script/MessControl.MCFreezeDeathVFX")),OutPaths); // Existing original native material roots only.
    }
    const bool Arena=Map==TEXT("L_Mouth") || Map==TEXT("L_Tutorial");
    // The procedural weapon ribbon uses this material instance in Hub and arena.
    if(Hub || Arena) AddAsset(TEXT("/Game/Gameplay/VFX/WeaponTrail/MI_WeaponSwingTrail"),OutPaths);
    if(Hub || Arena) {
        AddAsset(TEXT("/Game/Gameplay/CharacterCurrent/Tools/SM_Katana"),OutPaths);
        AddAsset(TEXT("/Game/Gameplay/UI/V2/Art/Tools/T_UI_PSD_Katana"),OutPaths);
    }
    if(!Arena) return;
    if(Map==TEXT("L_Tutorial"))AddAsset(TEXT("/Game/Audio/S_Win"),OutPaths); // Actual native FoamParty producer.
    // This location's finite event/profile root; reflected load waves expand
    // Library -> Profile -> runtime Composition -> original NS/render resources.
    AddPath(UMCVFXPresentationLibrary::MouthLibraryPath(),OutPaths);
    AddPath(MCVFXSourceContracts::WorldLibraryPath(),OutPaths);
    AddPath(MCVFXSourceContracts::ToolLibraryPath(),OutPaths);
    // Late passive cues retain their reflected CDO resources before readiness.
    Collector.Object(AMCPassiveCombatCue::StaticClass());
    Collector.Object(AMCPassiveFeedbackCue::StaticClass());
    Collector.Object(AMCSprayCoating::StaticClass());
    // Active explosions can break food before any ordinary food hit occurs.
    // Retain the reaction actor's reflected dust system before that first use.
    Collector.Object(AMCReactionVFX::StaticClass());
    // All original piano octave samples/attenuation plus actual key art before
    // the arena barrier; custom Hit layers come through the scoped World library.
    Collector.Object(AMCArenaTooth::StaticClass());
    // Native fallback flames, cast particles and projectile trail live on
    // these CDOs, rather than on the boss's reflected class defaults. Retain
    // them in this location before the first ordinary or custom cast.
    Collector.Object(AMCNutCombatEffect::StaticClass());
    Collector.Object(AMCNutSpellProjectile::StaticClass());
    if(Map==TEXT("L_Mouth")) {
        const auto* Chain=MCNutVFX::Find(MCNutVFX::ChainSource());
        for(FName Event:Chain->Events) {
            AddPath(MCNutVFX::ChainDefaultSystem(Event).ToSoftObjectPath(),OutPaths);
            AddPath(MCNutVFX::ChainDefaultSound(Event).ToSoftObjectPath(),OutPaths);
        }
        if(const auto* GravityWaves=MCNutVFX::Find(MCNutVFX::GravityWavesSource()))
            for(FName Event:GravityWaves->Events)
                AddPath(MCNutVFX::GravityWavesDefaultSystem(Event).ToSoftObjectPath(),OutPaths);
        for(const FName Source:{FName(TEXT("Boss.NutMage.Flash")),FName(TEXT("Boss.NutMage.NutSphere"))})
            if(const auto* Spell=MCNutVFX::Find(Source))
                for(const FName Event:Spell->Events)
                    if(const TCHAR* Path=MCNutVFX::WizardSpellDefaultPath(Source,Event))
                        AddPath(FSoftObjectPath(Path),OutPaths);
    }

    // Match the run's authored profile, including a host-selected day, on clients as well.
    const auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    const auto* State=World->GetGameState<AMCGameState>();
    if(Mode) Collector.Object(const_cast<AMCGameMode*>(Mode));
    if(State && State->DayPlan) Collector.Object(State->DayPlan);
    else if(!Mode || Mode->FirstDayPlan.IsNull()) AddAsset(TEXT("/Game/Data/DA_Day01"),OutPaths);
    const auto* SelectedDirector=State?State->SingleDayDirector.Get():nullptr;
    if(SelectedDirector && SelectedDirector->GetRunProfile() && SelectedDirector->GetSupportProfile()) {
        Collector.Object(const_cast<UMCSingleDayProfile*>(SelectedDirector->GetRunProfile()));
        Collector.Object(const_cast<UMCGameDirectorProfile*>(SelectedDirector->GetSupportProfile()));
    }
    else if(!Mode || Mode->bUseSingleDayLoop) {
        FMCHubDestinationRow Destination;
        const int32 SelectedDay=FCString::Atoi(World->URL.GetOption(TEXT("MCHubDay="),TEXT("1")));
        if(AMCHubGameState::ResolveAuthoredDestination(SelectedDay,Destination) && Destination.bEnabled)
            AddPath(Destination.SingleDayProfile.ToSoftObjectPath(),OutPaths);
        else AddAsset(TEXT("/Game/Gameplay/CoreLoop/DA_SingleDay"),OutPaths);
        AddAsset(TEXT("/Game/Gameplay/CoreLoop/DA_SingleDayDirector"),OutPaths);
    }
    for(const TCHAR* Root:{
        TEXT("/Game/Gameplay/CoreLoop/DA_NutRain"),
        TEXT("/Game/Data/DA_PlayerAppearance"),TEXT("/Game/Data/DA_Equipment"),
        TEXT("/Game/Data/DA_ToothAnimation"),TEXT("/Game/Data/DA_ArenaTooth"),
        TEXT("/Game/Data/DA_CoffeeWater"),TEXT("/Game/Data/DA_ColdCola"),
        TEXT("/Game/Data/DA_ColdColaLiquid"),
        // These are assigned in event BeginPlay rather than reflected defaults.
        TEXT("/Game/Gameplay/CoreLoop/M_NutEnemyEyes"),
        // Native selection of opaque cut-surface variants must be warm before the first knife hit.
        TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Cream"),TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Orange"),
        TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Tomato"),TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Strawberry"),
        TEXT("/Game/Gameplay/Food/CutMaterials/MI_FoodCut_Green"),
        TEXT("/Game/Gameplay/VFX/M_Reaction"),TEXT("/Game/Gameplay/VFX/M_ReactionSoft"),
        TEXT("/Game/Gameplay/VFX/MI_PassiveGhost"),TEXT("/Game/Gameplay/VFX/MI_PassiveLightning"),
        TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackSoft"),TEXT("/Game/Gameplay/VFX/MI_PassiveFeedbackGlow"),
        TEXT("/Game/Gameplay/VFX/M_CartoonReceiptSolid"),
        TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque"),
        TEXT("/Game/Gameplay/VFX/M_ReactionFire"),TEXT("/Game/Gameplay/VFX/M_ReactionSmoke"),
        TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost"),
        TEXT("/Game/Gameplay/Cold/VFX/M_IcePhasePowder"),TEXT("/Game/Gameplay/Cold/VFX/NS_IcePhasePowder"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderLow"),TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderBody"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderHigh"),
        // Loaded on the character but activated only on later cleaning contact.
        TEXT("/Game/Gameplay/VFX/NS_BrushFoam"),
        TEXT("/Game/Gameplay/VFX/NS_SprayMist"),TEXT("/Game/Gameplay/VFX/NS_WatergunMist"),
        TEXT("/Game/Gameplay/VFX/NS_IceShatter"),
        // Tooth care retains its vertex-driven pearl/bubble/glint material under loading.
        TEXT("/Game/Gameplay/Care/M_ToothCarePearl"),
        // Objective coin pool, opaque material and inline short SFX are retained
        // before arena presentation readiness, never loaded by the reward RPC.
        TEXT("/Game/Gameplay/Rewards/M_TaskCoin"),TEXT("/Game/Audio/Rewards/S_TaskCoins"),
        TEXT("/Game/Gameplay/Liquid/River/MI_RiverCoffee"),TEXT("/Game/Gameplay/Liquid/River/MI_RiverCola"),
        TEXT("/Game/Gameplay/Liquid/River/MI_RiverWater"),TEXT("/Game/Cataclysm/Storm/MI_StormCoffee"),
        TEXT("/Game/Cataclysm/Storm/MI_StormCola"),TEXT("/Game/Cataclysm/Storm/M_StormSpray"),
        TEXT("/Game/Cataclysm/Jelly/M_CataclysmPaintTrail"),TEXT("/Game/Cataclysm/Jelly/M_CataclysmJellyGel")
    }) AddAsset(Root,OutPaths);
    // The native suction loop uses this only when its primary AirflowSound is unavailable.
    AddAsset(TEXT("/Game/Audio/S_Pull"),OutPaths);
    // Native classes contain additional constructor-defined meshes/clips used by this day's events.
    for(const TCHAR* Class:{TEXT("MCNutBoss"),TEXT("MCIceEvent"),TEXT("MCCataclysmJelly"),TEXT("MCToothCharacter"),
        TEXT("MCSmallMintCandy"),TEXT("MCHazardWave"),TEXT("MCNutKamikaze"),TEXT("MCNutLandingShadow"),
        TEXT("MCFogBrawlEvent"),TEXT("MCCataclysmDebris"),TEXT("MCRewardChest"),TEXT("MCIceEventVFX"),TEXT("MCThroatVortex"),TEXT("MCVomitBurst")})
        AddPath(FSoftObjectPath(FString(TEXT("/Script/MessControl."))+Class),OutPaths);
}
