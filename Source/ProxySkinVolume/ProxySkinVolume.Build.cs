using UnrealBuildTool;

public class ProxySkinVolume : ModuleRules
{
	public ProxySkinVolume(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"ToolMenus",
				"LevelEditor",
				"PropertyEditor",
				"Projects",
				"EditorSubsystem",
				"MeshDescription",
				"StaticMeshDescription",
				"MeshMergeUtilities",
				"MeshUtilitiesCommon",
				"MeshReductionInterface",
				"GeometryCore",
				"DynamicMesh",
				"GeometryFramework",
				"ModelingComponents",
				"GeometryScriptingCore",
				"DeveloperSettings",
				"AssetRegistry",
				"AssetTools"
			});
	}
}
