#include "MCMouthSurface.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXComposition.h"
#include "MCNiagaraProceduralSurfaceResources.h"
#include "MCWorldPreloadSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

void AMCMouthSurface::UpdateComposerLiquid()
{
    PresentAcceptedStainCleaning();
    static const FName SourceID(TEXT("EditorOnly.CleanStain")),Event(TEXT("Surface"));
    if(!StainComposer)return;
    auto Stop=[&]() {
        FString Error;StainComposer->UpdateState(SourceID,Event,false,FTransform::Identity,FMCPerkVFXGameplayValues(),nullptr,Error);
        StainSurfaceEpisode.Invalidate();
    };
    // UpdateLiquid ran first: this is the actual original visibility, geometry,
    // native MID and current gameplay WipeMask, never a reconstructed stain.
    if(GetNetMode()==NM_DedicatedServer||bUlcer||!Liquid||!Liquid->IsVisible()||!LiquidMID
        ||Liquid->GetNumSections()!=1||!GetWorld()) {Stop();return;}
    const auto* Section=Liquid->GetProcMeshSection(0);
    if(!Section||Section->ProcIndexBuffer.IsEmpty()){Stop();return;}
    auto* Channel=StainComposer->GetSource(SourceID);if(!Channel)return;
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(!Channel->GetPreparedEventMode(SourceID,Event,Mode)){Stop();return;}
    bool IntentionalEmpty=false,HasSurfaceLayer=false;
    if(const auto* Prepared=Channel->GetPreparedSourceEvent(SourceID,Event);Prepared&&Prepared->Snapshot) {
        IntentionalEmpty=!Prepared->Snapshot->Layers.ContainsByPredicate([](const auto& L){return L.bEnabled;});
        for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)
            if(Prepared->Snapshot->Layers[I].bEnabled&&Prepared->Systems.IsValidIndex(I)
                &&UMCNiagaraProceduralSurfaceResources::Uses(Prepared->Systems[I]))HasSurfaceLayer=true;
    }
    FMCPerkVFXGameplayValues Values;Values.VisualSubject=Liquid;Values.Duration=86400.f;
    const auto* State=GetWorld()->GetGameState();
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    Values.LocalStateAgeSeconds=FMath::Max(0.f,float(Now-LiquidBornAt));
    if(!StainSurfaceEpisode.IsValid())StainSurfaceEpisode=FGuid::NewGuid();Values.EpisodeID=StainSurfaceEpisode;
    FString Error;
    const bool Accepted=StainComposer->UpdateState(SourceID,Event,true,Liquid->GetComponentTransform(),Values,Liquid,Error);
    const auto* Loading=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    const auto* Resources=Loading?Loading->GetProceduralSurfaceResources():nullptr;
    const bool DrawAccepted=HasSurfaceLayer&&Resources&&Resources->HasAcceptedDraw(Liquid,SourceID,StainSurfaceEpisode);
    // Empty/disabled is intentional artist silence. A delayed, unavailable or
    // exhausted prepared surface keeps the original, even if NI particles exist.
    // Ordinary artist replacements remain independently authorable in this slot.
    const bool ReplacementVisible=HasSurfaceLayer?DrawAccepted:Channel->IsSourceEpisodeVisualActive(SourceID,StainSurfaceEpisode);
    if(Accepted&&!StainComposer->UsesNativeVisuals(SourceID,Event)
        &&(Mode==EMCPerkVFXSlotMode::Disabled||IntentionalEmpty||ReplacementVisible))Liquid->SetVisibility(false);
}

bool AMCMouthSurface::PrepareLiquidPresentation(FString& Error)
{
    Error.Reset();
    if(!HasActorBegunPlay()||!GetWorld()||GetNetMode()==NM_DedicatedServer||bUlcer||IsClean())
    {Error=TEXT("A live renderable native stain is required for presentation preparation.");return false;}
    // Existing cosmetic setup only: no actor/world tick, no work/contact call.
    UpdateLiquid(0.f);
    const auto* Section=Liquid?Liquid->GetProcMeshSection(0):nullptr;
    if(!LiquidMID||!WipeTexture||!Section||Section->ProcIndexBuffer.IsEmpty())
    {Error=TEXT("The original stain MID, wipe texture or geometry could not be prepared.");return false;}
    return true;
}

void AMCMouthSurface::OnRep_StainCleaningPresentation()
{
    PresentAcceptedStainCleaning();
}

void AMCMouthSurface::PresentAcceptedStainCleaning()
{
    if(!StainComposer||!GetWorld()||GetNetMode()==NM_DedicatedServer||bUlcer)return;
    static const FName SourceID(TEXT("EditorOnly.CleanStain"));
    auto* Channel=StainComposer->GetSource(SourceID);if(!Channel)return;
    const auto* State=GetWorld()->GetGameState();
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const auto& Accepted=StainCleaningState;
    const bool Active=Accepted.ContactSerial!=0&&Now>=Accepted.ContactAt&&Now-Accepted.ContactAt<=.25;
    if(PresentedStainContactSerial!=Accepted.ContactSerial) {
        PresentedStainContactSerial=Accepted.ContactSerial;StainContactEpisode=FGuid::NewGuid();
    }
    FMCPerkVFXGameplayValues Values;Values.VisualSubject=Liquid;Values.Duration=86400.f;
    Values.EpisodeID=StainContactEpisode;
    Values.LocalStateAgeSeconds=FMath::Max(0.f,float(Now-Accepted.ContactStartedAt));
    const FTransform Frame(FRotationMatrix::MakeFromZ(FVector(Accepted.Normal)).ToQuat(),FVector(Accepted.Point));
    FString Error;StainComposer->UpdateState(SourceID,TEXT("Contact"),Active,Frame,Values,Liquid,Error);
    if(PresentedStainCompletionSerial!=Accepted.CompletionSerial) {
        PresentedStainCompletionSerial=Accepted.CompletionSerial;
        const float Age=FMath::Max(0.f,float(Now-Accepted.CompletedAt));
        // Initial late replication does not replay an old reward/finish sound.
        if(Accepted.CompletionSerial&&Now>=Accepted.CompletedAt&&Age<=1.f) {
            Values.EpisodeID=FGuid::NewGuid();Values.Duration=1.f;Values.LocalStateAgeSeconds=Age;
            Channel->PlayLocalSourceEvent(SourceID,TEXT("Complete"),Frame,Values);
        }
    }
}
