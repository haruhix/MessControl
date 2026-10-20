#include "MCPreloadManifest.h"

#include "MCPreloadSettings.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCSingleDayDirector.h"
#include "Components/ActorComponent.h"
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
            Object(Class->GetDefaultObject(),Depth+1); return;
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
    for(const auto& Path:Settings->CommonAssets) AddPath(Path,OutPaths);
    for(const auto& Entry:Settings->Maps) if(Entry.Map==Map)
        for(const auto& Path:Entry.Assets) AddPath(Path,OutPaths);
    AddAsset(TEXT("/Engine/BasicShapes/Plane"),OutPaths);

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
        TEXT("/Game/Data/DA_PlayerAppearance"),TEXT("/Game/Data/DA_Equipment"),
        TEXT("/Game/Gameplay/UI/Theme/DA_GameplayUITheme"),
        TEXT("/Game/Data/DA_ToothAnimation"),TEXT("/Game/Data/DA_ArenaTooth"),
        TEXT("/Game/Data/DA_CoffeeWater"),TEXT("/Game/Data/DA_ColdCola"),
        TEXT("/Game/Gameplay/Liquid/River/MI_RiverCoffee"),TEXT("/Game/Gameplay/Liquid/River/MI_RiverCola"),
        TEXT("/Game/Gameplay/Liquid/River/MI_RiverWater"),TEXT("/Game/Cataclysm/Storm/MI_StormCoffee"),
        TEXT("/Game/Cataclysm/Storm/MI_StormCola"),TEXT("/Game/Cataclysm/Storm/M_StormSpray"),
        TEXT("/Game/Cataclysm/Jelly/M_CataclysmPaintTrail"),TEXT("/Game/Cataclysm/Jelly/M_CataclysmJellyGel")
    }) AddAsset(Root,OutPaths);
    // Native classes contain additional constructor-defined meshes/clips used by this day's events.
    for(const TCHAR* Class:{TEXT("MCNutBoss"),TEXT("MCIceEvent"),TEXT("MCCataclysmJelly"),TEXT("MCToothCharacter"),
        TEXT("MCSmallMintCandy"),TEXT("MCHazardWave")})
        AddPath(FSoftObjectPath(FString(TEXT("/Script/MessControl."))+Class),OutPaths);
}
