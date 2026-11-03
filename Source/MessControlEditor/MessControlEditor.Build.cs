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
            "StaticMeshDescription", "Chaos", "ChaosCore",
            "UMG", "UMGEditor", "Slate", "SlateCore", "InputCore", "RenderCore", "RHI", "ImageCore", "PropertyEditor", "Kismet", "MovieScene",
            "GraphEditor", "BlueprintGraph", "KismetWidgets", "Merge", "SourceControl",
            "LevelEditor", "ToolMenus", "WorkspaceMenuStructure", "LevelSequence",
            "Niagara", "NiagaraEditor", "AdvancedPreviewScene", "ContentBrowser", "AssetTools", "AppFramework", "Json", "JsonUtilities", "ToolsetRegistry"
        });
        // Read-only inspection of UE5.8 native lightweight emitter objects.
        PrivateDependencyModuleNames.Add("NiagaraShader");
        PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Niagara"), "Internal"));
        PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("NiagaraShader"), "Internal"));
    }
}
