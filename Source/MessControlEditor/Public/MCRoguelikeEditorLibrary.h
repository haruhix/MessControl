#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCRoguelikeEditorLibrary.generated.h"

class UWorld;
class UDataTable;
class USkeletalMesh;

/** Editor-only authoring and guarded transient play-world review; never packaged. */
UCLASS()
class MESSCONTROLEDITOR_API UMCRoguelikeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Read-only saved reference skeleton query, without constructing an unregistered pose component. */
    UFUNCTION(BlueprintPure,Category="Mess Control|Roguelike|Review")
    static bool GetReferenceBoneComponentTransform(USkeletalMesh* Mesh,FName Bone,FTransform& Transform);
    /** Owned editor authoring: patch one skill's numeric map without JSON-roundtripping other native rows. */
    UFUNCTION(BlueprintCallable,Category="Mess Control|Roguelike|Review")
    static bool PatchSkillParameters(UDataTable* Table,FName Row,const TMap<FName,float>& Changes,const TArray<FName>& Removed);
    /** Extent is the box half-size in centimetres. Reuses only the volume owned by this helper. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Roguelike|Navigation")
    static bool ConfigureBossNavigation(UWorld* World,FVector Center,FVector Extent);
    /** Solo PIE review only; changes temporary play-world state, never saved assets. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Roguelike|Review")
    static FString ReviewRogueGameplay(UWorld* World,FName Action);
    /** Owned standalone review only. Native duplicate preserves every row/type;
     * only the selected current consumable's InitialCharges becomes one. Never saved. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Roguelike|Review")
    static UDataTable* CreateLastChargeSkillReviewTable(UWorld* World,UDataTable* SourceTable,FName PerkID);
};
