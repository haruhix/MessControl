#include "MCDayPlan.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
void FMCFoodRow::Sanitize()
{
    auto Safe=[](float V,float D,float Lo,float Hi){return FMath::IsFinite(V)?FMath::Clamp(V,Lo,Hi):D;};
    SelectionWeight=Safe(SelectionWeight,1,0,100); Health=Safe(Health,75,1,1000);
    Mass=Safe(Mass,9,1,50); SpoilSeconds=Safe(SpoilSeconds,180,3,600); Fragments=FMath::Clamp(Fragments,2,5);
    FuseSeconds=Safe(FuseSeconds,10,1,60); FirstPulseRadius=Safe(FirstPulseRadius,180,50,600);
    RadiusPerRound=Safe(RadiusPerRound,90,0,200); PulseDamage=Safe(PulseDamage,18,0,100);
    AbsorbSeconds=Safe(AbsorbSeconds,2,.5f,10);
    for (int32 Axis=0;Axis<3;++Axis) {
        Scale[Axis]=FMath::IsFinite(Scale[Axis])?FMath::Max(.01,Scale[Axis]):1.;
        FragmentScale[Axis]=FMath::IsFinite(FragmentScale[Axis])?FMath::Max(.01,FragmentScale[Axis]):.5;
    }
    if (HalfExtent.ContainsNaN()) HalfExtent=FVector(45,35,35);
    HalfExtent=HalfExtent.GetAbs().BoundToBox(FVector(10),FVector(100));
    Stack.Sanitize();
    Collision.Sanitize();
}
UMCFoodCollisionData* FMCFoodRow::FindCollisionData(const UStaticMesh* Mesh) const
{
    if(Collision.bAutoOptimize)
        for(UMCFoodCollisionData* Data:CollisionData)
            if(Data && Data->MatchesSource(Mesh) && Data->HasValidCollision()) return Data;
    return nullptr;
}
#if WITH_EDITOR
EDataValidationResult FMCFoodRow::IsDataValid(FDataValidationContext& Context) const
{
    bool Valid=Stack.Validate(Context);
    FMCFoodCollisionSettings Limits=Collision; Limits.Sanitize();
    if(Limits.WholeHullLimit!=Collision.WholeHullLimit || Limits.FragmentHullLimit!=Collision.FragmentHullLimit
        || Limits.HullVertexLimit!=Collision.HullVertexLimit || Limits.VoxelResolution!=Collision.VoxelResolution)
    {
        Context.AddError(FText::FromString(TEXT("Food collision limits are outside their supported ranges.")));
        Valid=false;
    }
    if(Collision.bAutoOptimize)
    {
        auto ValidateMeshes=[&](const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes,int32 HullLimit)
        {
            for(const auto& Mesh:Meshes)
            {
                if(Mesh.IsNull()) continue;
                bool Found=false;
                for(const UMCFoodCollisionData* Data:CollisionData)
                    if(Data && Data->SourceMesh.ToSoftObjectPath()==Mesh.ToSoftObjectPath()
                        && Data->HasValidCollision() && !Data->SourceGeometryKey.IsEmpty()
                        && Data->HullLimit<=HullLimit && Data->HullVertexLimit<=Collision.HullVertexLimit)
                    { Found=true; break; }
                if(!Found)
                {
                    Context.AddError(FText::FromString(FString::Printf(TEXT("Food collision profile for %s is missing or exceeds its menu budget. Save the menu to bake it."),*Mesh.ToString())));
                    Valid=false;
                }
            }
        };
        ValidateMeshes(WholeMeshes,Collision.WholeHullLimit);
        ValidateMeshes(FragmentMeshes,Collision.FragmentHullLimit);
    }
    return Valid?EDataValidationResult::Valid:EDataValidationResult::Invalid;
}
#endif
UMCDayPlan::UMCDayPlan()
{
    CoffeeProfile=TSoftObjectPtr<UMCCoffeeProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_CoffeeWater.DA_CoffeeWater")));
    ColdColaProfile=TSoftObjectPtr<UMCColdColaProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_ColdCola.DA_ColdCola")));
    Menu=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu")));
    auto Add=[&](EMCDayStep Step,float Seconds,const TCHAR* Title,const TCHAR* Hint)
    { FMCDayStepSettings S; S.Step=Step; S.Seconds=Seconds; S.Title=FText::FromString(Title); S.Instruction=FText::FromString(Hint); Steps.Add(S); };
    Add(EMCDayStep::BrushLesson,0,TEXT("01 / LEARN TO BRUSH"),TEXT("Slot 1: your permanent brush. Hold LMB: clean teeth AND floor stains. No event timer."));
    // Retain the saved step layout; the director skips this legacy objective.
    Add(EMCDayStep::DiscardBrushes,0,TEXT("PERMANENT TOOLS"),TEXT("All four tools stay in inventory. Slot 1 always contains your brush."));
    Add(EMCDayStep::BreakfastRain,2,TEXT("02 / BREAKFAST IS FALLING"),TEXT("Dodge the food! Broccoli, egg, bacon and carrot are chosen from the menu."));
    Add(EMCDayStep::BreakfastCleanup,20,TEXT("BREAKFAST / CLEAN UP"),TEXT("Slot 3 + LMB / F: cut food. LMB: collect a flat stack. Enter the THROAT zone to deliver automatically. Others can add food for 3 seconds, then a 2-second swallow begins."));
    Add(EMCDayStep::CoffeeWaves,6,TEXT("КОФЕ / ЦУНАМИ"),TEXT("Волна сносит к глотке и смывает мелкие кусочки еды. WASD + Shift — бежать. ПКМ у стены, еды или игрока — держаться."));
    Add(EMCDayStep::CoffeeCleanup,20,TEXT("COFFEE / BRUSH EVERYTHING"),TEXT("Slot 1: brush. Hold LMB: clean teeth and floor stains. C: clean yourself."));
    Add(EMCDayStep::ColdCola,45,TEXT("ХОЛОДНАЯ КОЛА"),TEXT("Скользко! Слот 2 + ЛКМ: разбей лёд. E у зуба: зацепиться, W/S: лазать, Space: отпрыгнуть."));
    Add(EMCDayStep::StuckFood,35,TEXT("03 / BETWEEN THE TEETH"),TEXT("Slot 3 + LMB / F: cut stuck food free. LMB: collect flat pieces. Enter the THROAT zone to deliver automatically. Add more food within 3 seconds before the 2-second swallow."));
}
void UMCDayPlan::Sanitize()
{
    auto Safe=[](float V,float D,float Lo,float Hi){return FMath::IsFinite(V)?FMath::Clamp(V,Lo,Hi):D;};
    TargetDaySeconds=Safe(TargetDaySeconds,240,30,600);
    for (auto& S:Steps) { S.Seconds=Safe(S.Seconds,30,0,120); S.FailureDamage=Safe(S.FailureDamage,8,0,50); }
    BreakfastCount=FMath::Clamp(BreakfastCount,1,12); StuckCount=FMath::Clamp(StuckCount,1,8);
    FoodEntry.Sanitize();
    SurfacePatches=FMath::Clamp(SurfacePatches,1,24); WaveCount=FMath::Clamp(WaveCount,1,8);
    FloodHeight=Safe(FloodHeight,155,60,240); FlowAcceleration=Safe(FlowAcceleration,320,0,800);
    PaddleAcceleration=Safe(PaddleAcceleration,400,0,800); AnchorReach=Safe(AnchorReach,160,60,250);
    UlcerHealSeconds=Safe(UlcerHealSeconds,7,6,8); UlcerDamagePerSecond=Safe(UlcerDamagePerSecond,.35f,0,5);
    UlcerDisturbDamage=Safe(UlcerDisturbDamage,1,0,10);
    UlcerPulseInterval=Safe(UlcerPulseInterval,3,1,15);
    if (ArenaHalfSize.ContainsNaN()) ArenaHalfSize=FVector(1050,740,220);
    ArenaHalfSize=ArenaHalfSize.GetAbs().BoundToBox(FVector(500,300,100),FVector(3000,2000,600));
    if (ArenaCenter.ContainsNaN()) ArenaCenter=FVector::ZeroVector;
    ArenaCenter=ArenaCenter.BoundToBox(FVector(-5000),FVector(5000));
}
