// MIT License
//
// Copyright (c) 2019 Lucid Layers

#include "RuntimeMeshExporter.h"
#include "RuntimeMeshImportExport.h"
#include "assimp/scene.h"
#include "assimp/Exporter.hpp"
#include "assimp/DefaultLogger.hpp"
#include "assimp/postprocess.h"
#include "Engine/Engine.h"
#include "assimp/LogStream.hpp"
#include "RuntimeMeshImportExportLibrary.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformFile.h"
#include "Vector"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "string.h"
#include "AssimpProgressHandler.h"

const unsigned int exportFlags = aiPostProcessSteps::aiProcess_MakeLeftHanded;

URuntimeMeshExporter::URuntimeMeshExporter()
{
    // Make sure Assimp does not use double precision
    check(sizeof(ai_real) == sizeof(float));
    check(sizeof(ai_int) == sizeof(int32));

    if (!(GetFlags() & EObjectFlags::RF_ClassDefaultObject))
    {
        scene = new FAssimpScene();
    }
}

URuntimeMeshExporter::~URuntimeMeshExporter()
{
    delete scene;
}

void URuntimeMeshExporter::AddNode(const FString& hierarchicalName, const FTransform& nodeTransformWS)
{
    if (bIsExporting)
    {
        RMIE_LOG(Warning, "Currently exporting, you should not call functions on the exporter!");
        return;
    }

    check(scene && scene->rootNode);
    TArray<FString> nodeList;
    hierarchicalName.ParseIntoArray(nodeList, TEXT("."), true);

    FAssimpNode* foundNode = scene->rootNode->FindOrCreateNode(nodeList);

    foundNode->worldTransform = nodeTransformWS;
}

void URuntimeMeshExporter::AddExportObject(const TScriptInterface<IMeshExportable>& exportable, const bool bOverrideNode, const FString& hierarchicalNodeName)
{
    if (bIsExporting)
    {
        RMIE_LOG(Warning, "Currently exporting, you should not call functions on the exporter!");
        return;
    }

    check(scene && scene->rootNode);
    TArray<FString> nodeList;
    (bOverrideNode ? hierarchicalNodeName : exportable->Execute_GetHierarchicalNodeName(exportable.GetObject())).ParseIntoArray(nodeList, TEXT("."), true);
    scene->rootNode->FindOrCreateNode(nodeList)->exportObjects.Add(exportable);
}

void URuntimeMeshExporter::AddExportObjects(const TArray<TScriptInterface<IMeshExportable>>& exportables, const bool bOverrideNode, const FString& hierarchicalNodeName)
{
    if (bIsExporting)
    {
        RMIE_LOG(Warning, "Currently exporting, you should not call functions on the exporter!");
        return;
    }

    for (const TScriptInterface<IMeshExportable>& object : exportables)
    {
        AddExportObject(object, bOverrideNode, hierarchicalNodeName);
    }
}

bool URuntimeMeshExporter::AddObjectIfExportable(UObject* object, const bool bOverrideNode, const FString& hierarchicalNodeName)
{
    if (bIsExporting)
    {
        RMIE_LOG(Warning, "Currently exporting, you should not call functions on the exporter!");
        return false;
    }

    if (!object->GetClass()->ImplementsInterface(UMeshExportable::StaticClass()))
    {
        return false;
    }

    TScriptInterface<IMeshExportable> interfaceObject(object);
    AddExportObject(interfaceObject, bOverrideNode, hierarchicalNodeName);
    return true;
}

void URuntimeMeshExporter::AddObjectsIfExportable(UPARAM(ref) TArray<UObject*>& objects, const bool bOverrideNode, const FString& hierarchicalNodeName, TArray<UObject*>& notExportable)
{
    if (bIsExporting)
    {
        RMIE_LOG(Warning, "Currently exporting, you should not call functions on the exporter!");
        return;
    }

    for (UObject* object : objects)
    {
        if (!AddObjectIfExportable(object, bOverrideNode, hierarchicalNodeName))
        {
            notExportable.Add(object);
        }
    }
}

void URuntimeMeshExporter::Export(const FRuntimeMeshExportParam& param, FRuntimeMeshExportResult& result)
{
    if (!PreExportWork(param, result))
    {
        result.bSuccess = false;
        return;
    }

    FAssimpScene& sceneRef = *scene;

    // Prepare the scene
    sceneRef.PrepareSceneForExport(param);
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("Scene does contain %d meshes."), sceneRef.mNumMeshes));
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("Scene does contain %d materials."), sceneRef.mNumMaterials));

    // Do the export
    Assimp::Exporter exporter;
    try
    {
    sceneRef.WriteToLogWithNewLine(FString(TEXT("Begin export scene.")));
    double duration = 0.f;
    {
        FScopedDurationTimer timer(duration);
        aiExporterReturn = exporter.Export(&sceneRef, TCHAR_TO_ANSI(*param.formatId), TCHAR_TO_ANSI(*param.file), exportFlags);
    }
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("End export scene. Duration: %.3fs"), duration));
    }
    catch (const std::exception& e)
    {
    	FString exceptionString = FString::Printf(TEXT("Exception thrown during export: %s"), ANSI_TO_TCHAR(e.what()));
    	sceneRef.WriteToLogWithNewLine(exceptionString);
    	URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, exceptionString);
    	RMIE_LOG(Error, "%s", *exceptionString);
    }
    aiExporterError = FString(exporter.GetErrorString());

    result.bSuccess = PostExportWork(result);
    return;
}

void URuntimeMeshExporter::Export_Async_Cpp(const FRuntimeMeshExportAsyncParam param, FRuntimeMeshImportExportProgressUpdate callbackProgress
        , FRuntimeImportExportGameThreadDone callbackGatherDone
        , FRuntimeExportFinished callbackFinished)
{
    if (!PreExportWork(param.param, asyncResult))
    {
        asyncResult.bSuccess = false;
        callbackFinished.ExecuteIfBound(asyncResult);
        return;
    }

    delegateProgress = callbackProgress;
    delegateGatherDone = callbackGatherDone;
    delegateFinished = callbackFinished;

    scene->PrepareSceneForExport_Async_Start(param, callbackProgress, [this, param]() {
        delegateGatherDone.ExecuteIfBound();
        AsyncTask(ENamedThreads::AnyThread, [this, param]() {
            this->Export_Async_AnyThread(param.param);
        });
    });
}


class FExportAsyncAction : public FPendingLatentAction
{
public:
    const FLatentActionInfo latentInfo;

    FExportAsyncAction(const FLatentActionInfo& inLatentInfo, URuntimeMeshExporter* inExporter
                       , const FRuntimeMeshExportAsyncParam& param, FRuntimeMeshImportExportProgressUpdateDyn inProgressDelegate
                       , FRuntimeImportExportGameThreadDoneDyn gatherDoneDelegate
                       , FRuntimeMeshExportResult& inResult)
        : latentInfo(inLatentInfo), exporter(inExporter), result(inResult)
    {
        FRuntimeMeshImportExportProgressUpdate progressDelegateRaw;
        progressDelegateRaw.BindLambda([inProgressDelegate](const FRuntimeMeshImportExportProgress& progress) {
            check(IsInGameThread());
			inProgressDelegate.ExecuteIfBound(progress);
        });

        FRuntimeImportExportGameThreadDone gatherDoneDelegateRaw;
        gatherDoneDelegateRaw.BindLambda([gatherDoneDelegate]() {
            check(IsInGameThread());
            gatherDoneDelegate.ExecuteIfBound();
        });

        FRuntimeExportFinished finishedDelegateRaw;
        finishedDelegateRaw.BindLambda([this](FRuntimeMeshExportResult delegateResult) {
            check(IsInGameThread());
            result = MoveTemp(delegateResult);
            bExportFinished = true;
        });

        exporter->Export_Async_Cpp(param, progressDelegateRaw, gatherDoneDelegateRaw, finishedDelegateRaw);
    }

    virtual void UpdateOperation(FLatentResponse& response) override
    {
        if (bExportFinished)
        {
            if (!exporter->GetIsExporting())
            {
                response.FinishAndTriggerIf(true, latentInfo.ExecutionFunction, latentInfo.Linkage, latentInfo.CallbackTarget);
            }
        }
    }

#if WITH_EDITOR
    // Returns a human readable description of the latent operation's current state
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("LatentAction for URuntimeMeshExporter export task."));
    }
#endif

private:
    bool bExportFinished = false;
    URuntimeMeshExporter* exporter = nullptr;
    FRuntimeMeshExportResult& result;
    //FRuntimeMeshExportDelegateStatusDyn statusDelegate;
};

void URuntimeMeshExporter::Export_Async(UObject* worldContextObject, FLatentActionInfo latentInfo, const FRuntimeMeshExportAsyncParam& param
                                        , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate, FRuntimeImportExportGameThreadDoneDyn gatherDoneDelegate
                                        , FRuntimeMeshExportResult& result)
{
    if (UWorld* world = GEngine->GetWorldFromContextObject(worldContextObject, EGetWorldErrorMode::LogAndReturnNull))
    {
        FLatentActionManager& latentActionManager = world->GetLatentActionManager();
        if (latentActionManager.FindExistingAction<FExportAsyncAction>(latentInfo.CallbackTarget, latentInfo.UUID) == NULL)
        {
            latentActionManager.AddNewAction(latentInfo.CallbackTarget, latentInfo.UUID, new FExportAsyncAction(latentInfo, this, param, progressDelegate, gatherDoneDelegate, result));
        }
    }
}

bool URuntimeMeshExporter::GetIsExporting()
{
    return bIsExporting;
}

void URuntimeMeshExporter::Export_Async_AnyThread(const FRuntimeMeshExportParam param)
{
    check(!IsInGameThread());

    FAssimpScene& sceneRef = *scene;
    sceneRef.PrepareSceneForExport_Async_Finish(param);
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("Scene does contain %d meshes."), sceneRef.mNumMeshes));
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("Scene does contain %d materials."), sceneRef.mNumMaterials));
    
    //for debuging
    for (unsigned int i = 0; i < sceneRef.mNumMaterials; i++)
    {
        
        sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT(" oooo material name = %s"), *sceneRef.mMaterials[i]->GetName().C_Str()));
    }

    Assimp::Exporter exporter;
	FAssimpProgressHandler progressHandler(delegateProgress);
    exporter.SetProgressHandler(&progressHandler);

    //AsyncTask(ENamedThreads::GameThread, [this]() {
    //    delegateStatus.ExecuteIfBound(FString(TEXT("Exporting scene with Assimp.")));
    //});

    sceneRef.WriteToLogWithNewLine(FString(TEXT("Begin export scene.")));
    double duration = 0.f;
    {
        FScopedDurationTimer timer(duration);
        aiExporterReturn = exporter.Export(scene, TCHAR_TO_ANSI(*param.formatId), TCHAR_TO_ANSI(*param.file), exportFlags);
    }
    exporter.SetProgressHandler(nullptr);
    sceneRef.WriteToLogWithNewLine(FString::Printf(TEXT("End export scene. Duration: %.3fs"), duration));

    aiExporterError = FString(exporter.GetErrorString());

    /*AsyncTask(ENamedThreads::GameThread, [this]() {
        delegateProgress.ExecuteIfBound(FString(TEXT("Clearing export data.")));
    });*/
    sceneRef.ClearSceneExportData();

    // Done, continue in GameThread
    AsyncTask(ENamedThreads::GameThread, [this]() {
        Export_Async_Finish();
    });
}

void URuntimeMeshExporter::Export_Async_Finish()
{
    check(IsInGameThread());
    asyncResult.bSuccess = PostExportWork(asyncResult);

    delegateFinished.ExecuteIfBound(asyncResult);
    delegateProgress.Unbind();
    delegateGatherDone.Unbind();
    delegateFinished.Unbind();
    asyncResult.error.Empty();
    asyncResult.exportLog.Empty();
    asyncResult.numObjectsSkipped = 0;
}

bool URuntimeMeshExporter::PreExportWork(const FRuntimeMeshExportParam& param, FRuntimeMeshExportResult& result)
{
    check(IsInGameThread());

    if (bIsExporting)
    {
        URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, FString::Printf(TEXT("Already exporting!")));
        return false;
    }

    result.error.Empty();
    result.exportLog.Empty();
    aiExporterReturn = aiReturn_FAILURE;
    aiExporterError.Empty();

    IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!param.bOverrideExisting)
    {
        if (platformFile.FileExists(*param.file))
        {
            URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, FString::Printf(TEXT("File %s does already exist!"), *param.file));
            return false;
        }
    }

    // Assimp can only write to a directory that exists.
    // Make sure it does.
    if (!platformFile.CreateDirectoryTree(*FPaths::GetPath(param.file)))
    {
        URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, FString::Printf(TEXT("Could not create directory: %s"), *FPaths::GetPath(param.file)));
        return false;
    }

    // Create log stuff
    scene->bLogToUnreal = param.bLogToUnreal;
    scene->exportLog = &result.exportLog;
    assimpLogger = Assimp::DefaultLogger::create("", Assimp::Logger::NORMAL);
    exportLogger = new FExportLogger(*scene);
    assimpLogger->attachStream(exportLogger);

    bIsExporting = true;

    return true;
}

bool URuntimeMeshExporter::PostExportWork(FRuntimeMeshExportResult& result)
{
    check(IsInGameThread());
    bool bExportSuccessful = false;

    // Check for errors
    if (!aiExporterError.IsEmpty())
    {
        URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, aiExporterError);
        RMIE_LOG(Error, "Error during export: %s", *aiExporterError);
    }

    // Evaluate result
    if (aiExporterReturn == aiReturn::aiReturn_OUTOFMEMORY)
    {
        URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, FString::Printf(TEXT("Export failed: out of memory!")));
        RMIE_LOG(Error, "Export failed: out of memory");
        bExportSuccessful = false;
    }
    else if (aiExporterReturn == aiReturn::aiReturn_FAILURE)
    {
        URuntimeMeshImportExportLibrary::NewLineAndAppend(result.error, FString::Printf(TEXT("Export failed!")));
        RMIE_LOG(Error, "Export failed!");
        bExportSuccessful = false;
    }
    else if (!result.error.IsEmpty())
    {
        bExportSuccessful = false;
    }
    else if (aiExporterReturn == aiReturn::aiReturn_SUCCESS)
    {
        bExportSuccessful = true;
    }

    // Get rid of log stuff
    scene->exportLog = nullptr;
    assimpLogger->detatchStream(exportLogger);
    delete exportLogger;
    exportLogger = nullptr;
    assimpLogger = nullptr;
    Assimp::DefaultLogger::kill(); // kill assimpLogger instance.

    // Cleanup
    result.numObjectsSkipped = scene->numObjectsSkipped;
    scene->ClearSceneExportData();
    aiExporterError.Empty();
    aiExporterReturn = aiReturn_FAILURE;

    bIsExporting = false;

    return bExportSuccessful;
}
