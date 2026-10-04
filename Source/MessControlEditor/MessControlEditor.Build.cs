using UnrealBuildTool;

public class MessControlEditor : ModuleRules
{
    public MessControlEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "MessControl", "UnrealEd", "NavigationSystem", "AIModule",
            "AssetRegistry", "PhysicsCore", "PhysicsUtilities", "MeshDescription",
            "StaticMeshDescription", "Chaos", "ChaosCore"
        });
    }
}
