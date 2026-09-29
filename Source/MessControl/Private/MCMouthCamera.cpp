#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"

void AMCToothCharacter::UpdateMouthCamera(float Dt)
{
    if(!IsLocallyControlled()) return;
    float CenterY=0;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {CenterY=It->GetActorLocation().Y;break;}
    // At the molars, keep the camera over the tongue and look across at the player.
    // The spring-arm sweep then keeps the near plane inside the actual palate shell.
    const FVector P=GetActorLocation(),Focus=P+FVector(80,0,75);
    const FVector Eye=P+FVector(-1100,(CenterY-P.Y)*.70,FMath::Min(440.,650.-P.Z));
    const FRotator Rotation=(Focus-Eye).Rotation();
    CameraBoom->TargetOffset=Focus-P;
    CameraBoom->SetWorldRotation(FMath::RInterpTo(CameraBoom->GetComponentRotation(),Rotation,Dt,7));
    CameraBoom->TargetArmLength=FMath::FInterpTo(CameraBoom->TargetArmLength,FVector::Dist(Eye,Focus),Dt,7);
}
