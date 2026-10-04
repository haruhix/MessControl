using UnrealBuildTool;
public class MessControl : ModuleRules
{
    public MessControl(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "PhysicsControl", "PhysicsCore", "NetCore", "ProceduralMeshComponent" });
        PublicDependencyModuleNames.Add("Niagara");
        PublicDependencyModuleNames.AddRange(new[] { "AIModule", "GameplayTasks" });
        PrivateDependencyModuleNames.Add("NavigationSystem");
        PrivateDependencyModuleNames.Add("AnimationCore");
        PrivateDependencyModuleNames.Add("Chaos");
        PrivateDependencyModuleNames.Add("ChaosCore");
        PrivateDependencyModuleNames.Add("RHI");
        PrivateDependencyModuleNames.Add("RenderCore");
        PrivateDependencyModuleNames.Add("ApplicationCore");
        PrivateDependencyModuleNames.AddRange(new[] { "LevelSequence", "MovieScene", "MovieSceneTracks", "CinematicCamera" });
        PublicDependencyModuleNames.AddRange(new[] { "OnlineBase", "OnlineSubsystem", "OnlineSubsystemUtils" });
        DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
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
