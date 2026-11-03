#pragma once
#include "CoreMinimal.h"
#include "MCEventPresentationGroups.generated.h"

/** Independent simultaneous presentation groups, shared by an event's VFX and SFX. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCEventPresentationGroup
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Group",meta=(IgnoreForMemberInitializationTest)) FGuid ID=FGuid::NewGuid();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Group") FName Name=TEXT("Group");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Group") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Group") bool bMuteSFX=false;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCEventPresentationGroupSet
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Groups") TArray<FMCEventPresentationGroup> Groups;
    bool Validate(FString& Error)const
    {
        if(Groups.Num()>32){Error=TEXT("An event supports at most 32 presentation groups.");return false;}
        TSet<FGuid> Seen;
        for(const auto& G:Groups)
        {
            if(!G.ID.IsValid() || Seen.Contains(G.ID) || G.Name.IsNone() || G.Name.ToString().Len()>96)
            {Error=TEXT("Presentation group IDs must be valid/unique and names nonempty (up to 96 characters).");return false;}
            Seen.Add(G.ID);
        }
        return true;
    }
    const FMCEventPresentationGroup* Find(FGuid ID)const
    {return Groups.FindByPredicate([&](const auto& G){return G.ID==ID;});}
    bool Contains(FGuid ID)const{return !ID.IsValid() || Find(ID)!=nullptr;}
    bool IsEnabled(FGuid ID)const{const auto* G=Find(ID);return !G || G->bEnabled;}
    bool IsAudioMuted(FGuid ID)const{const auto* G=Find(ID);return G && G->bMuteSFX;}
};
