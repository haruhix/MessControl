#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWave.h"
#include "MCEventSFXTestTypes.generated.h"

/** Deterministic decoder-capability probe. It remains an ordinary nonprocedural SoundWave. */
UCLASS(Transient)
class UMCNonSeekableSoundWaveProbe final : public USoundWave
{
    GENERATED_BODY()
public:
    virtual bool IsSeekable() const override {return false;}
};
