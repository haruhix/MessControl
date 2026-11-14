#pragma once

#include "CoreMinimal.h"

/** Explicit, read-only Editor diagnostics for one current PIE world. Game-thread API only. */
namespace MCComposerAudioProbe
{
    /** Starts a new bounded observation history; requires exactly one live PIE world and an audio device. */
    bool Start(FString& OutError);
    /** Cancels future sampling and removes delegates/ticker. Keeps the last history for inspection. */
    void Stop();
    /** Nonblocking: returns the latest completed audio-thread sample, never waits on audio. */
    FString SnapshotJson();
}
