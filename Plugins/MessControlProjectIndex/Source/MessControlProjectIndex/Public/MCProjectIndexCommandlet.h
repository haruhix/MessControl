#pragma once

#include "Commandlets/Commandlet.h"
#include "MCProjectIndexCommandlet.generated.h"

/** Exports saved assets without modifying, compiling or saving their packages. */
UCLASS()
class MESSCONTROLPROJECTINDEX_API UMCProjectIndexCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UMCProjectIndexCommandlet();
    virtual int32 Main(const FString& Params) override;
};
