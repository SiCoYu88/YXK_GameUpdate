// Copyright czm. All Rights Reserved.

using UnrealBuildTool;

public class HotUpdateEditor : ModuleRules
{
	public HotUpdateEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = false;
		ShadowVariableWarningLevel = WarningLevel.Warning;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HotUpdate",
			"InputCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"UnrealEd",
			"AssetRegistry",
			"ContentBrowser",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"MainFrame",
			"DesktopPlatform",
			
			"Json",
			"PakFile",
			"AssetTools",
			"DeveloperToolSettings"
		});
	}
}