
#pragma once

#include <Assimp/ProgressHandler.hpp>
#include "RuntimeMeshImportExportTypes.h"
#include "Async/Async.h"
#include "RuntimeMeshImportExportLibrary.h"

class FAssimpProgressHandler : public Assimp::ProgressHandler
{
public:
	FAssimpProgressHandler(FRuntimeMeshImportExportProgressUpdate& inDelegateProgress) : delegateProgress(inDelegateProgress) {}
	FRuntimeMeshImportExportProgressUpdate delegateProgress;

	virtual bool Update(float percentage = -1.f) override
	{
		URuntimeMeshImportExportLibrary::SendProgress_AnyThread(delegateProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::Unknown, 100 * percentage, 100));
		return true;
	}

	virtual void UpdateFileRead(int currentStep, int numberOfSteps) override
	{
		URuntimeMeshImportExportLibrary::SendProgress_AnyThread(delegateProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::AssimpFileRead, currentStep, numberOfSteps));
	}

	virtual void UpdatePostProcess(int currentStep, int numberOfSteps) override
	{
		URuntimeMeshImportExportLibrary::SendProgress_AnyThread(delegateProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::AssimpPostProcess, currentStep, numberOfSteps));
	}

	virtual void UpdateFileWrite(int currentStep, int numberOfSteps) override
	{
		URuntimeMeshImportExportLibrary::SendProgress_AnyThread(delegateProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::AssimpFileWrite, currentStep, numberOfSteps));
	}

};