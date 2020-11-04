// MIT License
//
// Copyright (c) 2017 Eric Liu
// Copyright (c) 2019 Lucid Layers


using UnrealBuildTool;
using System;
using System.IO;
using System.Diagnostics;

namespace UnrealBuildTool.Rules
{
    public class RuntimeMeshImportExport : ModuleRules
    {
        private string ModulePath
        {
            get { return ModuleDirectory; }
        }

        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModulePath, "../ThirdParty/")); }
        }


        public RuntimeMeshImportExport(ReadOnlyTargetRules Target) : base(Target)
        {
            //Log.TraceInformation("ModulePath: " + ModulePath);
            //Log.TraceInformation("ThirdPartyPath: " + ThirdPartyPath);
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            bEnableExceptions = true;

            PublicDefinitions.AddRange(new string[] { "ASSIMP_DLL" });

            PublicIncludePaths.AddRange(
                new string[] {
                Path.Combine(ModuleDirectory, "Public"),
                Path.Combine(ThirdPartyPath, "assimp/include")
                    // ... add public include paths required here ...
                }
            );

            //foreach(string Pathad in PublicIncludePaths)
            //{
            //    Log.TraceInformation("PublicIncludePaths: " + Pathad);
            //}

            PrivateIncludePaths.AddRange(
                new string[] {
                "RuntimeMeshImportExport/Private",
                "RuntimeMeshImportExport/Public/Interface",
                "RuntimeMeshImportExport/Private/Interface",
                    // ... add other private include paths required here ...
                }
                );


            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                "Core",
                "CoreUObject",
                "Engine",
                "ProceduralMeshComponent"
               // "RHI",
               // "RenderCore",
                    // ... add other public dependencies that you statically link with here ...
                }
                );


            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    //	"Slate",
                    //	"SlateCore",
                    // ... add private dependencies that you statically link with here ...	
                    // 
                    "Projects"
                }
                );


            DynamicallyLoadedModuleNames.AddRange(
                new string[]
                {
                }
                );

            if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32))
            {
                string PlatformString = (Target.Platform == UnrealTargetPlatform.Win64) ? "x64" : "Win32";
                string ConfigurationString = (Target.Configuration == UnrealTargetConfiguration.Shipping) ? "Release" : "Debug";
                string FileNameNoExt = "assimp-vc141-mt" + (Target.Configuration == UnrealTargetConfiguration.Shipping ? "" : "d");

                string libFile = Path.Combine(ThirdPartyPath, "assimp/lib", PlatformString, ConfigurationString, FileNameNoExt + ".lib");
                if(!File.Exists(libFile))
                {
                    //Log.TraceInformation("Missing file: " + libFile);
                    Console.Error.WriteLine("Missing file: " + libFile);
                }
                PublicAdditionalLibraries.Add(libFile);

                string dllFileName = FileNameNoExt + ".dll";
                string dllFile = Path.Combine(ThirdPartyPath, "assimp/bin", PlatformString, ConfigurationString, dllFileName);
                if (!File.Exists(dllFile))
                {
                    //Log.TraceInformation("Missing file: " + dllFile);
                    Console.Error.WriteLine("Missing file: " + dllFile);
                }

                // Does only declare the file as dependency and seems important for packaging. Though it does not load the file.
                RuntimeDependencies.Add(dllFile);
                // The .dll is loaded manually with the start of the module. See the AnswerHub entry.
                // https://answers.unrealengine.com/questions/427772/adding-dll-path-for-plugin.html
                PublicDelayLoadDLLs.Add(dllFileName);
            }
            //else if (Target.Platform == UnrealTargetPlatform.Mac)
            //{
            //    string PlatformString = "Mac";
            //    PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "assimp/lib", PlatformString, "libassimp.4.1.0.dylib"));
            //}
        }
    }
}