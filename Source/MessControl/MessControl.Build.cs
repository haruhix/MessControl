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
        PrivateDependencyModuleNames.AddRange(new[] { "AnimGraphRuntime", "KawaiiPhysics" });
        // StylizedDynamics is a local experimental install; upstream currently has no license file.
        bool HasStylizedDynamics = System.IO.Directory.Exists(System.IO.Path.Combine(ModuleDirectory, "../../Plugins/StylizedDynamics/Source/StylizedDynamics"));
        PublicDefinitions.Add("WITH_MC_STYLIZED_DYNAMICS=" + (HasStylizedDynamics ? "1" : "0"));
        if (HasStylizedDynamics) PrivateDependencyModuleNames.Add("StylizedDynamics");
        PrivateDependencyModuleNames.Add("Chaos");
        PrivateDependencyModuleNames.Add("ChaosCore");
        PrivateDependencyModuleNames.Add("RHI");
        PrivateDependencyModuleNames.Add("RenderCore");
        PrivateDependencyModuleNames.Add("ApplicationCore");
        PrivateDependencyModuleNames.Add("AudioExtensions");
        PrivateDependencyModuleNames.Add("MoviePlayer");
        PublicDependencyModuleNames.Add("DeveloperSettings");
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
