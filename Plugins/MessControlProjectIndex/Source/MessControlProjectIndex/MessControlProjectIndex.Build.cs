using UnrealBuildTool;

public class MessControlProjectIndex : ModuleRules
{
    public MessControlProjectIndex(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "AssetRegistry", "Json", "JsonUtilities",
            "UnrealEd", "BlueprintGraph", "Niagara", "NiagaraEditor"
        });
    }
}
