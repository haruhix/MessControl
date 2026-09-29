using UnrealBuildTool;
public class MessControl : ModuleRules
{
    public MessControl(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "PhysicsControl", "PhysicsCore", "NetCore", "ProceduralMeshComponent" });
        PublicDependencyModuleNames.Add("Niagara");
        PrivateDependencyModuleNames.Add("AnimationCore");
        PrivateDependencyModuleNames.Add("RHI");
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("AssetRegistry");
            PrivateDependencyModuleNames.Add("NiagaraShader");
            // Editor-only authoring for UE's lightweight Niagara emitter modules.
            PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Niagara"), "Internal"));
            PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("NiagaraShader"), "Internal"));
        }
    }
}
