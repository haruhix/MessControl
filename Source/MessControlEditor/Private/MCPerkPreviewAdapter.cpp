#include "MCPerkPreviewAdapter.h"

#include "MCActiveSkillAction.h"
#include "MCRocketSkillAction.h"
#include "Engine/StaticMeshActor.h"

namespace
{
    TMap<FName,FMCPerkPreviewAdapterEntry>& Registry()
    {static TMap<FName,FMCPerkPreviewAdapterEntry> Entries;return Entries;}

    class FMCRocketPreviewAdapter final : public IMCPerkPreviewAdapter
    {
        EMCRocketSkillPhase LastPhase=EMCRocketSkillPhase::Prepared;
    public:
        bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
        {
            for(const auto& Weak:C.Targets)if(auto* Target=Cast<AStaticMeshActor>(Weak.Get())) {
                const bool Visible=C.Scenario!=EMCPerkPreviewScenario::Timeout;
                Target->SetActorHiddenInGame(!Visible);Target->SetActorEnableCollision(Visible);
                Target->SetActorLocation(FVector(C.Scenario==EMCPerkPreviewScenario::NearWall?250.f:900.f,0,140));
            }
            LastPhase=EMCRocketSkillPhase::Prepared;return true;
        }
        bool ValidateAction(const AMCActiveSkillAction& Action,FString& Error) const override
        {if(Cast<AMCRocketSkillAction>(&Action))return true;Error=TEXT("Фабрика строки не создала настоящую ракетницу.");return false;}
        void OnCommitted(FMCPerkPreviewContext& C) override
        {if(auto* A=Cast<AMCRocketSkillAction>(C.Action)){LastPhase=A->Phase;if(C.RecordEvent)C.RecordEvent(TEXT("Windup"));}}
        void Tick(FMCPerkPreviewContext& C,float) override
        {
            if(auto* A=Cast<AMCRocketSkillAction>(C.Action))if(A->Phase!=LastPhase) {
                LastPhase=A->Phase;
                if(C.RecordEvent)C.RecordEvent(FName(*StaticEnum<EMCRocketSkillPhase>()->GetNameStringByValue(int64(A->Phase))));
            }
        }
        FString GetPhaseName(const FMCPerkPreviewContext& C) const override
        {
            const auto* A=Cast<AMCRocketSkillAction>(C.Action);if(!A)return TEXT("Завершение / остаток эффекта");
            switch(A->Phase) {
            case EMCRocketSkillPhase::Prepared:return TEXT("Подготовка");
            case EMCRocketSkillPhase::Windup:return TEXT("Достаёт / прицеливается");
            case EMCRocketSkillPhase::Flying:return TEXT("Полёт / восстановление позы");
            case EMCRocketSkillPhase::Detonated:return TEXT("Попадание / остаток");
            default:return TEXT("Неизвестная фаза");
            }
        }
    };
    struct FRegisterRocketPreview
    {
        FRegisterRocketPreview()
        {
            FMCPerkPreviewAdapterEntry E;E.PerkID=TEXT("Consumable_Rocket");E.Label=NSLOCTEXT("MCPerkPreview","Rocket","Ракетница");
            E.Stages={TEXT("Start"),TEXT("Loop"),TEXT("Impact"),TEXT("End")};
            E.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::NearWall,EMCPerkPreviewScenario::Timeout};
            E.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,NSLOCTEXT("MCPerkPreview","RocketTarget","Попадание в цель"));
            E.ScenarioLabels.Add(EMCPerkPreviewScenario::NearWall,NSLOCTEXT("MCPerkPreview","RocketNearWall","Близкая стена"));
            E.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,NSLOCTEXT("MCPerkPreview","RocketTimeout","Промах / конец полёта"));
            E.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FMCRocketPreviewAdapter>();};
            MCPerkPreviewAdapters::Register(MoveTemp(E));
        }
    } RegisterRocketPreview;
}

void IMCPerkPreviewAdapter::Release(FMCPerkPreviewContext& C)
{if(IsValid(C.Action))C.Action->ReleaseInput();}
void IMCPerkPreviewAdapter::Cancel(FMCPerkPreviewContext& C)
{if(IsValid(C.Action))C.Action->Cancel(EMCActiveSkillEndReason::WorldEnded);}
void MCPerkPreviewAdapters::Register(FMCPerkPreviewAdapterEntry Entry)
{if(!Entry.PerkID.IsNone() && Entry.Create)Registry().Add(Entry.PerkID,MoveTemp(Entry));}
const FMCPerkPreviewAdapterEntry* MCPerkPreviewAdapters::Find(FName PerkID)
{return Registry().Find(PerkID);}
TArray<FMCPerkPreviewAdapterEntry> MCPerkPreviewAdapters::GetEntries()
{TArray<FMCPerkPreviewAdapterEntry> Values;Registry().GenerateValueArray(Values);Values.Sort([](const auto& A,const auto& B){return A.PerkID.LexicalLess(B.PerkID);});return Values;}
