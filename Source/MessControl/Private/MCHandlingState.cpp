#include "MCHandlingState.h"

bool FMCHandlingMoveSample::IsWellFormed() const
{
    const auto Mass=[](float V){return FMath::IsFinite(V) && V>=0 && V<=10000;};
    return (!Revision || LoadEpoch.IsValid()) && Mass(LoadMass) && Mass(StackMass) && Mass(IncomingMass) && StackMass<=LoadMass+.01f
        && FMath::IsFinite(DragSpeedLimit) && DragSpeedLimit>=0 && DragSpeedLimit<=2000
        && (Revision || (LoadMass==0 && StackMass==0 && IncomingMass==0 && DragSpeedLimit==0 && !bHeavyGrip && !bPlayerPull));
}
bool FMCHandlingMoveSample::Equals(const FMCHandlingMoveSample& O) const
{
    return LoadEpoch==O.LoadEpoch && Revision==O.Revision && LoadMass==O.LoadMass && StackMass==O.StackMass
        && IncomingMass==O.IncomingMass && DragSpeedLimit==O.DragSpeedLimit && bHeavyGrip==O.bHeavyGrip && bPlayerPull==O.bPlayerPull;
}
bool FMCHandlingMoveSample::Serialize(FArchive& Ar)
{
    Ar<<LoadEpoch;Ar.SerializeIntPacked(Revision);Ar<<LoadMass<<StackMass<<IncomingMass<<DragSpeedLimit;
    Ar.SerializeBits(&bHeavyGrip,1);Ar.SerializeBits(&bPlayerPull,1);
    return !Ar.IsError() && IsWellFormed();
}
