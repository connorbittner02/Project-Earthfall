// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

public class Earthfall : ModuleRules
{
    public Earthfall(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Slate", "SlateCore", "Niagara" });
        PublicDependencyModuleNames.Add("ProceduralMeshComponent");

        // Cesium for Unreal is optional and never committed. The streamed-terrain
        // code compiles in only when the plugin is present in Plugins/.
        bool bCesium = Target.ProjectFile != null
            && Directory.Exists(Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins", "CesiumForUnreal"));
        PublicDefinitions.Add("EARTHFALL_WITH_CESIUM=" + (bCesium ? "1" : "0"));
        if (bCesium) PrivateDependencyModuleNames.Add("CesiumRuntime");
    }
}
