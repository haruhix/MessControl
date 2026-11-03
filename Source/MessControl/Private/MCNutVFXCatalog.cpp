#include "MCNutVFXCatalog.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXPresentationLibrary.h"
#include "MCPerkVFXProfile.h"

namespace MCNutVFX
{
TConstArrayView<FMCNutVFXAction> Actions()
{
    static const TArray<FMCNutVFXAction> Rows=[]
    {
        TArray<FMCNutVFXAction> R;
        auto Add=[&](const TCHAR* ID,const TCHAR* Label,std::initializer_list<const TCHAR*> Events,const TCHAR* Lab)
        {
            FMCNutVFXAction A;A.ID=FName(ID);A.Label=FText::FromString(Label);A.LabScenario=FName(Lab);
            for(const TCHAR* Event:Events)A.Events.Add(FName(Event));R.Add(MoveTemp(A));
        };
        Add(TEXT("Boss.NutKnight.Shield"),TEXT("Ореховый рыцарь · щит"),{TEXT("Hit"),TEXT("Break"),TEXT("Durability")},TEXT("TankShieldHit"));
        Add(TEXT("Boss.NutKnight.Melee"),TEXT("Ореховый рыцарь · ближний бой"),{TEXT("Tell"),TEXT("Slash"),TEXT("Slam")},TEXT("TankMelee"));
        Add(TEXT("Boss.NutKnight.Charge"),TEXT("Ореховый рыцарь · разбег"),{TEXT("Tell"),TEXT("Move")},TEXT("TankCharge"));
        Add(TEXT("Boss.NutKnight.Roll"),TEXT("Ореховый рыцарь · перекат"),{TEXT("Transform"),TEXT("TransformBurst"),TEXT("Tell"),TEXT("Move")},TEXT("TankRoll"));
        Add(TEXT("Boss.NutKnight.Jump"),TEXT("Ореховый рыцарь · прыжок"),{TEXT("Tell"),TEXT("Impact")},TEXT("TankJump"));
        Add(TEXT("Boss.NutKnight.CounterShove"),TEXT("Ореховый рыцарь · контрудар"),{TEXT("Tell"),TEXT("Impact")},TEXT("TankCounterShove"));
        Add(TEXT("Boss.NutKnight.Entrance"),TEXT("Ореховый рыцарь · появление"),{TEXT("Tell"),TEXT("Impact")},TEXT("TankEntrance"));
        Add(TEXT("Boss.NutKnight.Death"),TEXT("Ореховый рыцарь · смерть"),{TEXT("Burst"),TEXT("BodyBurst")},TEXT("TankDeath"));
        Add(TEXT("Boss.NutKnight.Transform"),TEXT("Ореховый рыцарь → маг"),{TEXT("HeadSplit"),TEXT("HeadDust")},TEXT("TankHeadSplit"));
        Add(TEXT("Boss.NutMage.Fireball"),TEXT("Ореховый маг · огненный шар"),{TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")},TEXT("MageFireball"));
        Add(TEXT("Boss.NutMage.Melee"),TEXT("Ореховый маг · удар"),{TEXT("Tell"),TEXT("Slash")},TEXT("MageMelee"));
        Add(TEXT("Boss.NutMage.Summon"),TEXT("Ореховый маг · призыв"),{TEXT("Focus"),TEXT("ReleaseBurst"),TEXT("GroundTell"),TEXT("Release")},TEXT("MageSummon"));
        Add(TEXT("Boss.NutMage.NutRain"),TEXT("Ореховый маг · ореховый дождь"),{TEXT("Focus"),TEXT("ReleaseBurst"),TEXT("Storm"),TEXT("DropTrail"),TEXT("Approach"),TEXT("Impact")},TEXT("MageNutRain"));
        Add(TEXT("Boss.NutMage.Teleport"),TEXT("Ореховый маг · телепорт"),{TEXT("Tell"),TEXT("Transfer"),TEXT("ArrivalBurst")},TEXT("MageTeleport"));
        Add(TEXT("Boss.NutMage.Entrance"),TEXT("Ореховый маг · появление"),{TEXT("Tell"),TEXT("Impact")},TEXT("MageEntrance"));
        Add(TEXT("Boss.NutMage.Death"),TEXT("Ореховый маг · смерть"),{TEXT("Burst"),TEXT("BodyBurst")},TEXT("MageDeath"));
        Add(TEXT("Enemy.NutKamikaze.Fuse"),TEXT("Орех-камикадзе · фитиль"),{TEXT("Fuse"),TEXT("Danger")},TEXT("KamikazeFuse"));
        Add(TEXT("Enemy.NutKamikaze.Blast"),TEXT("Орех-камикадзе · взрыв"),{TEXT("Impact")},TEXT("KamikazeBlast"));
        Add(TEXT("Event.NutRain.Entry"),TEXT("Ореховый дождь · приземление"),{TEXT("Approach"),TEXT("Impact")},TEXT("RainEntry"));
        return R;
    }();
    return Rows;
}

const FMCNutVFXAction* Find(FName Source)
{
    for(const auto& Row:Actions())if(Row.ID==Source)return &Row;
    return nullptr;
}

bool Prepare(UMCVFXPresentationComponent* Component,FName Source,FString& Error,bool bAllowLoad)
{
    const auto* Contract=Find(Source);
    if(!IsValid(Component)||!Contract){Error=TEXT("Unknown native nut presentation source.");return false;}
    if(Component->IsSourcePrepared(Source))return true;
    const auto Path=UMCVFXPresentationLibrary::MouthLibraryPath();
    auto* Library=Cast<UMCVFXPresentationLibrary>(bAllowLoad?Path.TryLoad():Path.ResolveObject());
    if(!Library){Error=TEXT("The arena presentation library was not retained before the nut action.");return false;}
    const auto* Reference=Library->Profiles.Find(Source);
    UMCPerkVFXProfile* Profile=Reference?(bAllowLoad?Reference->LoadSynchronous():Reference->Get()):nullptr;
    if(Reference&&!Reference->IsNull()&&!Profile){Error=TEXT("The nut presentation profile was not retained before readiness.");return false;}
    return Component->PrepareSourceProfile(Source,Contract->Events,Profile,Error,bAllowLoad);
}
}
