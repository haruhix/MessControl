#include "MCPreloadManifest.h"

#include "MCPreloadSettings.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCSingleDayDirector.h"
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
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

namespace
{
// DataTable map references describe destinations, not dependencies to load into this world.
bool CanPrepareClass(const UClass* Class)
{
    return !Class || (!Class->IsChildOf(UWorld::StaticClass()) && !Class->IsChildOf(ULevel::StaticClass())
        && !Class->IsChildOf(USoundBase::StaticClass()));
}

void AddPath(const FSoftObjectPath& Path,TArray<FSoftObjectPath>& Out)
{
    const FString Name=Path.GetAssetPathString();
    if(!Path.IsNull() && (Name.StartsWith(TEXT("/Game/")) || Name.StartsWith(TEXT("/Engine/"))
        || Name.StartsWith(TEXT("/Script/MessControl.")))) Out.AddUnique(Path);
}

class FMCVisualReferenceCollector
{
public:
    explicit FMCVisualReferenceCollector(TArray<FSoftObjectPath>& InOut) : Out(InOut) {}
    void Object(UObject* Value,int32 Depth=0)
    {
        if(!IsValid(Value) || Depth>24 || Visited.Contains(Value)) return;
        Visited.Add(Value);
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
                for(const auto& Row:Table->GetRowMap()) Struct(RowStruct,Row.Value,Value,Depth+1);
        }
        Struct(Value->GetClass(),Value,Value,Depth+1);
    }
private:
    void Struct(const UStruct* Type,const void* Data,UObject* Owner,int32 Depth)
    {
        if(!Type || !Data || Depth>24) return;
        for(TFieldIterator<FProperty> It(Type);It;++It) {
            if(It->HasAnyPropertyFlags(CPF_Deprecated|CPF_EditorOnly|CPF_Transient)) continue;
            for(int32 Index=0;Index<It->ArrayDim;++Index)
                Property(*It,It->ContainerPtrToValuePtr<void>(Data,Index),Owner,Depth+1);
        }
    }
    void Property(const FProperty* Prop,const void* Data,UObject* Owner,int32 Depth)
    {
        if(!Data || Depth>24) return;
        if(const auto* Soft=CastField<FSoftObjectProperty>(Prop)) {
            if(CanPrepareClass(Soft->PropertyClass)) AddPath(Soft->GetPropertyValue(Data).ToSoftObjectPath(),Out);
        }
        else if(const auto* Hard=CastField<FObjectPropertyBase>(Prop)) {
            UObject* Target=Hard->GetObjectPropertyValue(Data);
            if(!IsValid(Target) || !CanPrepareClass(Target->GetClass())) return;
            if(Target->IsAsset() || Target->IsA<UClass>()) AddPath(FSoftObjectPath(Target),Out);
            if(Target->IsA<UDataAsset>() || Target->IsA<UDataTable>() || Target->IsA<UClass>()
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
    TArray<FSoftObjectPath>& Out;
};

void AddAsset(const TCHAR* Package,TArray<FSoftObjectPath>& Out)
{
    const FString Name(Package);
    AddPath(FSoftObjectPath(Name+TEXT(".")+FPackageName::GetShortName(Name)),Out);
}
}

void FMCPreloadManifest::CollectSoftReferences(UObject* Object,TArray<FSoftObjectPath>& OutPaths)
{
    FMCVisualReferenceCollector(OutPaths).Object(Object);
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
    FMCVisualReferenceCollector Collector(OutPaths);
    for(TActorIterator<AActor> It(World);It;++It) {
        Collector.Object(*It);
        TInlineComponentArray<UActorComponent*> Components(*It);
        for(auto* Component:Components) Collector.Object(Component);
    }
    const bool Arena=Map==TEXT("L_Mouth") || Map==TEXT("L_Tutorial");
    if(!Arena) return;

    // Match the run's authored profile, including a host-selected day, on clients as well.
    const auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    const auto* State=World->GetGameState<AMCGameState>();
    if(Mode) Collector.Object(const_cast<AMCGameMode*>(Mode));
    if(State && State->DayPlan) Collector.Object(State->DayPlan);
    else if(!Mode || Mode->FirstDayPlan.IsNull()) AddAsset(TEXT("/Game/Data/DA_Day01"),OutPaths);
    FMCHubDestinationRow Destination;
    const int32 SelectedDay=FCString::Atoi(World->URL.GetOption(TEXT("MCHubDay="),TEXT("1")));
    if(AMCHubGameState::ResolveAuthoredDestination(SelectedDay,Destination) && Destination.bEnabled)
        AddPath(Destination.SingleDayProfile.ToSoftObjectPath(),OutPaths);
    else AddAsset(TEXT("/Game/Gameplay/CoreLoop/DA_SingleDay"),OutPaths);
    for(const TCHAR* Root:{
        TEXT("/Game/Gameplay/CoreLoop/DA_SingleDayDirector"),
        TEXT("/Game/Gameplay/CoreLoop/DA_NutRain"),
        TEXT("/Game/Data/DA_PlayerAppearance"),TEXT("/Game/Data/DA_Equipment"),
        TEXT("/Game/Data/DA_ToothAnimation"),TEXT("/Game/Data/DA_ArenaTooth"),
        TEXT("/Game/Data/DA_CoffeeWater"),TEXT("/Game/Data/DA_ColdCola"),
        TEXT("/Game/Data/DA_ColdColaLiquid"),
        // These are assigned in event BeginPlay rather than reflected defaults.
        TEXT("/Game/Gameplay/CoreLoop/M_NutEnemyEyes"),
        TEXT("/Game/Gameplay/VFX/M_Reaction"),TEXT("/Game/Gameplay/VFX/M_ReactionSoft"),
        TEXT("/Game/Gameplay/VFX/M_ReactionFire"),TEXT("/Game/Gameplay/VFX/M_ReactionSmoke"),
        TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost"),
        TEXT("/Game/Gameplay/Cold/VFX/M_IcePhasePowder"),TEXT("/Game/Gameplay/Cold/VFX/NS_IcePhasePowder"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderLow"),TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderBody"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderHigh"),
        // Loaded on the character but activated only on later cleaning contact.
        TEXT("/Game/Gameplay/VFX/NS_BrushFoam"),
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
    // Native classes contain additional constructor-defined meshes/clips used by this day's events.
    for(const TCHAR* Class:{TEXT("MCNutBoss"),TEXT("MCIceEvent"),TEXT("MCCataclysmJelly"),TEXT("MCToothCharacter"),
        TEXT("MCSmallMintCandy"),TEXT("MCHazardWave"),TEXT("MCNutKamikaze"),TEXT("MCNutLandingShadow"),
        TEXT("MCFogBrawlEvent"),TEXT("MCCataclysmDebris"),TEXT("MCRewardChest"),TEXT("MCIceEventVFX")})
        AddPath(FSoftObjectPath(FString(TEXT("/Script/MessControl."))+Class),OutPaths);
}
