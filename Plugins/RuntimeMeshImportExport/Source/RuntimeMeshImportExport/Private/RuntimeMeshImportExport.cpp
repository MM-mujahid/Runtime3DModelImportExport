// MIT License
//
// Copyright (c) 2019 Lucid Layers

#include "RuntimeMeshImportExport.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FRuntimeMeshImportExportModule"

void FRuntimeMeshImportExportModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FString PluginBaseDir = IPluginManager::Get().FindPlugin("RuntimeMeshImportExport")->GetBaseDir();
	FString configString;
#if UE_BUILD_SHIPPING
	configString = "Release";
#else 
	configString = "Debug";
#endif

#if PLATFORM_WINDOWS
#if PLATFORM_32BITS
	FString platformString = "Win32";
#elif PLATFORM_64BITS
	FString platformString = "x64";
#endif
#elif PLATFORM_MAC
	FString platformString = "Mac";
#endif

	FString dllFileName = FString(TEXT("assimp-vc141-mt")) + (UE_BUILD_SHIPPING ? TEXT("") : TEXT("d")) + TEXT(".dll");
	FString dllFile = FPaths::Combine(PluginBaseDir, FString("Source/ThirdParty/assimp/bin"), platformString, configString, dllFileName);
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*dllFile))
	{
		RMIE_LOG(Fatal, "Missing file: %s", *dllFile);
	}
		
	dllHandle_assimp = FPlatformProcess::GetDllHandle(*dllFile);
}

void FRuntimeMeshImportExportModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FPlatformProcess::FreeDllHandle(dllHandle_assimp);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRuntimeMeshImportExportModule, RuntimeMeshImportExport)