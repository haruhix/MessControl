using UnrealBuildTool;
public class MessControl : ModuleRules
{
    public MessControl(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "PhysicsControl", "PhysicsCore", "NetCore", "ProceduralMeshComponent" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("AssetRegistry");
    }
}
