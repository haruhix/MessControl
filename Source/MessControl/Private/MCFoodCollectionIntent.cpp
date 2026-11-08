#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"

void UMCFoodCollectionComponent::RequestCollecting(bool Enabled)
{
    auto* H=Cast<AMCToothCharacter>(GetOwner());if(!H || (!H->HasAuthority() && !H->IsLocallyControlled())) return;
    const FGuid Epoch=H->HasAuthority()?H->CaptureNetworkCorrectionContext().LoadEpoch:H->GetSimulationFrame().LoadEpoch;
    if(PendingCollectionEpoch!=Epoch) {bHasPendingCollecting=false;NextCollectionRequest=0;PendingCollectionEpoch=Epoch;}
    if((bHasPendingCollecting?bPendingCollecting:bCollecting)==Enabled) return;
    if(!++NextCollectionRequest) ++NextCollectionRequest;
    PendingCollectionRequest=NextCollectionRequest;bPendingCollecting=Enabled;bHasPendingCollecting=true;
    ServerSetCollecting(Enabled,PendingCollectionRequest,Epoch);
}
void UMCFoodCollectionComponent::RequestToggle()
{RequestCollecting(!(bHasPendingCollecting?bPendingCollecting:bCollecting));}
void UMCFoodCollectionComponent::ServerSetCollecting_Implementation(bool Enabled,uint16 RequestId,FGuid Epoch)
{
    auto* H=Cast<AMCToothCharacter>(GetOwner());if(!H || !H->HasAuthority() || !RequestId) return;
    const FGuid CurrentEpoch=H->CaptureNetworkCorrectionContext().LoadEpoch;
    if(Epoch!=CurrentEpoch) return;
    if(ServerCollectionEpoch!=Epoch) {ServerCollectionEpoch=Epoch;LastServerCollectionRequest=0;}
    if(LastServerCollectionRequest && int16(RequestId-LastServerCollectionRequest)<=0) return;
    LastServerCollectionRequest=RequestId;
    const bool Accepted=H->ApplyFoodCollectionIntent(Enabled);
    CollectionIntent.LoadEpoch=Epoch;CollectionIntent.RequestId=RequestId;
    if(!++CollectionIntent.Revision) ++CollectionIntent.Revision;
    CollectionIntent.bCollecting=bCollecting;CollectionIntent.bAccepted=Accepted;
    H->ForceNetUpdate();OnRep_CollectionIntent();
}
void UMCFoodCollectionComponent::OnRep_CollectionIntent()
{
    if(bHasPendingCollecting && CollectionIntent.LoadEpoch==PendingCollectionEpoch
        && int16(CollectionIntent.RequestId-PendingCollectionRequest)>=0) bHasPendingCollecting=false;
}
