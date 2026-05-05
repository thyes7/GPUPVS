// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GPUPVSEditor : ModuleRules
{
	public GPUPVSEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
		);		
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Landscape",
				"RenderCore",
				"Renderer",
				"RHI",
				"Projects",
				"UnrealEd",
				"SlateCore",
				"Slate",
                "GPUPVS",
                "WorkspaceMenuStructure",
				"EditorStyle",
				"ToolMenus",
				"ToolWidgets",
				"PropertyEditor",
				"LevelEditor",
				// ... add private dependencies that you statically link with here ...	
			}
		);
	}
}
