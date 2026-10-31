#include "MCVFXRecipePreset.h"

int32 UMCVFXRecipePreset::FindNode(const FGuid& ID) const
{return Nodes.IndexOfByPredicate([&ID](const FMCVFXRecipeNodeLayout& N){return N.ID==ID;});}

bool UMCVFXRecipePreset::ValidateLayout(FString& Error) const
{
    Error.Reset();
    if(Nodes.Num()!=Layers.Num()) {Error=TEXT("Recipe layer/layout counts differ. The original preset was not changed.");return false;}
    TSet<FGuid> IDs,GroupIDs;
    for(const auto& G:Groups)
    {
        if(!G.ID.IsValid() || GroupIDs.Contains(G.ID) || !FMath::IsFinite(G.Color.R) || !FMath::IsFinite(G.Color.G) || !FMath::IsFinite(G.Color.B) || !FMath::IsFinite(G.Color.A))
        {Error=TEXT("Invalid or duplicate graph group.");return false;}
        GroupIDs.Add(G.ID);
    }
    for(const auto& N:Nodes)
    {
        if(!N.ID.IsValid() || IDs.Contains(N.ID) || !FMath::IsFinite(N.Position.X) || !FMath::IsFinite(N.Position.Y)
            || (N.Group.IsValid() && !GroupIDs.Contains(N.Group)))
        {Error=TEXT("Invalid node identity, position or group. Repair a copy before authoring.");return false;}
        IDs.Add(N.ID);
    }
    return true;
}
