#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCSFXComposerEditorLibrary.generated.h"

/** Bounded controls for the public SFX window; uses its real shared draft and preview. */
UCLASS()
class MESSCONTROLEDITOR_API UMCSFXComposerEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="MessControl|SFX Composer") static void OpenSFXComposer();
    UFUNCTION(BlueprintPure,Category="MessControl|SFX Composer") static FString GetSFXComposerSnapshotJson();
    UFUNCTION(BlueprintPure,Category="MessControl|SFX Composer") static FString GetSharedComposerDocumentsSnapshotJson();
    UFUNCTION(BlueprintPure,Category="MessControl|Composer Review") static FString GetComposerReviewSnapshotJson();
    UFUNCTION(BlueprintCallable,Category="MessControl|Audio Validation") static bool StartPIEComposerAudioProbe(FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|Audio Validation") static void StopPIEComposerAudioProbe();
    UFUNCTION(BlueprintPure,Category="MessControl|Audio Validation") static FString GetPIEComposerAudioProbeSnapshotJson();
    UFUNCTION(BlueprintCallable,Category="MessControl|SFX Composer")
    static bool SelectSFXComposerTarget(FName Action,FName Event,FGuid Layer,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|SFX Composer")
    static bool PlaySFXComposerAction(const FString& Scenario,bool bLoop,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="MessControl|SFX Composer") static void StopSFXComposerAction();
    UFUNCTION(BlueprintCallable,Category="MessControl|SFX Composer") static bool ResizeSFXComposerForReview(FVector2D ClientSize);
};
