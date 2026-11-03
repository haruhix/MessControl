#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "UObject/Package.h"

namespace MCVFXTestWorld
{
inline UWorld* Create(EWorldType::Type Type,bool bInformEngine,FName Name=NAME_None,
    UPackage* Package=nullptr,bool bAddToRoot=true,ERHIFeatureLevel::Type FeatureLevel=ERHIFeatureLevel::Num,
    const UWorld::InitializationValues* Init=nullptr,bool bSkipInit=false)
{
    checkf(Package==nullptr,TEXT("VFX fixtures may create only their own fresh world package."));
    UPackage* Owned=CreatePackage(nullptr);
    Owned->SetFlags(RF_Transient);
    return UWorld::CreateWorld(Type,bInformEngine,Name,Owned,bAddToRoot,FeatureLevel,Init,bSkipInit);
}
}
#endif
