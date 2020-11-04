// MIT License
//
// Copyright (c) 2019 Lucid Layers

#pragma once
#include "Modules/ModuleManager.h"

// RuntimeMeshImportExport Module Log
DECLARE_LOG_CATEGORY_CLASS(LogRuntimeMeshImportExport, Log, All);
#define RMIE_LOG(Verbosity, Format, ...) UE_LOG(LogRuntimeMeshImportExport, Verbosity, TEXT("%s(%s): %s"), *FString(__FUNCTION__), *FString::FromInt(__LINE__), *FString::Printf(TEXT(Format), ##__VA_ARGS__ ))

class FRuntimeMeshImportExportModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void* dllHandle_assimp = nullptr;
};
