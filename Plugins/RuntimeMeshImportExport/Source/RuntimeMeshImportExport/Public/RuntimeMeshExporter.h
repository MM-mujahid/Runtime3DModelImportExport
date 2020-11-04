// MIT License
//
// Copyright (c) 2019 Lucid Layers

#pragma once

#include "CoreMinimal.h"
#include "Interface/MeshExportable.h"
#include "UObject/NoExportTypes.h"
#include "Assimp/LogStream.hpp"
#include "Assimp/Logger.hpp"
#include "AssimpCustom.h"
#include "RuntimeMeshImportExportTypes.h"
#include "Engine/LatentActionManager.h"
#include "RuntimeMeshExporter.generated.h"

struct aiScene;
struct aiMesh;
struct aiNode;
struct aiMaterial;
class Assimp::Logger;

/**
 *	Exporter that uses Assimp library http://www.assimp.org/
 *
 *	Objects are placed in Nodes. Nodes are simple transform objects like a SceneComponent in Unreal.
 *	Nodes can contain Child Nodes to create a node tree (the scene).
 */
UCLASS(BlueprintType)
class RUNTIMEMESHIMPORTEXPORT_API URuntimeMeshExporter : public UObject
{
    GENERATED_BODY()
public:

    URuntimeMeshExporter();
    ~URuntimeMeshExporter();

    /**
     *	Adds a new node to the scene or overrides the existing node.
     *
     *	@param hierarchicalName			hierarchical name in the format: Outer1.Outer2.Outer3.MyNode
     *									Outers nodes that do not exist yet, will be created.
     *									Leave it empty to set the parameters for the root node.
     *  @param nodeTransformWS			The transform of the node in worldSpace
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    void AddNode(const FString& hierarchicalName, const FTransform& nodeTransformWS);

    /**
     *	@param exportable				The object you want to get exported
     *	@param bOverrideNode			false: the object is asked to return the node name.
     *									true: 'hierarchicalNodeName' is used to place the object in the scene
     *  @param hierarchicalNodeName		If bOverrideNode==true this is used to place the object in the scene. e.g.: Outer1.Outer2.Outer3.MyNode
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    void AddExportObject(const TScriptInterface<IMeshExportable>& exportable, const bool bOverrideNode, const FString& hierarchicalNodeName);

    /**
     *	@param exportables				The objects you want to get exported
     *	@param bOverrideNode			false: the objects are asked to return the node name.
     *									true: 'hierarchicalNodeName' is used to place the objects in the scene
     *  @param hierarchicalNodeName		If bOverrideNode==true this is used to place the objects in the scene. e.g.: Outer1.Outer2.Outer3.MyNode
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    void AddExportObjects(const TArray<TScriptInterface<IMeshExportable>>& exportables, const bool bOverrideNode, const FString& hierarchicalNodeName);

    /**
     *	Try to add a object to the export. Object must inherit from MeshExportable interface.
     *
     *	@param object					The object you want to get exported
     *	@param bOverrideNode			false: the objects are asked to return the node name.
     *									true: 'hierarchicalNodeName' is used to place the objects in the scene
     *  @param hierarchicalNodeName		If bOverrideNode==true this is used to place the objects in the scene. e.g.: Outer1.Outer2.Outer3.MyNode
     *  @returns						true: The object inherits from MeshExportable and was added.
     *									false: The object does not inherit from MeshExportable and was rejected.
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    bool AddObjectIfExportable(UObject* object, const bool bOverrideNode, const FString& hierarchicalNodeName);

    /**
     *	Tries to add an array of objects to the export. Only objects that inherit from MeshExportable interface are added.
     *
     *	@param objects					The objects you want to get exported
     *	@param bOverrideNode			false: the objects are asked to return the node name.
     *									true: 'hierarchicalNodeName' is used to place the objects in the scene
     *  @param hierarchicalNodeName		If bOverrideNode==true this is used to place the objects in the scene. e.g.: Outer1.Outer2.Outer3.MyNode
     *  @param notExportable			Objects that do not implement the MeshExportable interface
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    void AddObjectsIfExportable(UPARAM(ref) TArray<UObject*>& objects, const bool bOverrideNode, const FString& hierarchicalNodeName, TArray<UObject*>& notExportable);

    /**
     *	Exports the scene synchronous. This will block the game until export is finished.
     * 
     *	@param param		Export parameters
     *	@param result		Will contain the results of the export operation.
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter")
    void Export(const FRuntimeMeshExportParam& param, FRuntimeMeshExportResult& result);


    /**
     *	Export the scene asynchronous. Gathering of the mesh data is done in tick on the GameThread. During that time you should not modify the scene
     *	to ensure consistency of the scene. As soon as the data gathering on the GameThread is done 'callbackGatherDone' is fired and the export process
     *	is send to another thread where everything else is done.
     *
     *	@param param				The parameters for the export
     *	@param callbackProgress		Callback for a progress update of the exporter
     *	@param callbackGatherDone	This is fired as soon as the gathering of mesh data on the GameThread is done and it is save to modify the objects
     *	@param callbackFinished		This is fired when the export finished.
     */
    void Export_Async_Cpp(const FRuntimeMeshExportAsyncParam param, FRuntimeMeshImportExportProgressUpdate callbackProgress
                          , FRuntimeImportExportGameThreadDone callbackGatherDone
                          , FRuntimeExportFinished callbackFinished);

    /**
	 *	Export the scene asynchronous. Gathering of the mesh data is done in tick on the GameThread. During that time you should not modify the scene
	 *	to ensure consistency of the scene. As soon as the data gathering on the GameThread is done 'gatherDoneDelegate' is fired and the export process
	 *	is send to another thread where everything else is done.
	 *	
	 *	@param param				The parameters for the export
	 *	@param progressDelegate		Callback for a progress update of the exporter
	 *	@param gatherDoneDelegate	This is fired as soon as the gathering of mesh data on the GameThread is done and it is save to modify the objects
	 *	@param result				Is filled with the result of the export operation
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Exporter", meta = (WorldContext = "WorldContextObject", Latent, LatentInfo = "latentInfo"))
    void Export_Async(UObject* worldContextObject, FLatentActionInfo latentInfo, const FRuntimeMeshExportAsyncParam& param
                      , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate, FRuntimeImportExportGameThreadDoneDyn gatherDoneDelegate
                      , FRuntimeMeshExportResult& result);

    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport|Exporter")
    bool GetIsExporting();

private:

    // This function does most of the export work
    void Export_Async_AnyThread(const FRuntimeMeshExportParam param);
    void Export_Async_Finish();

    struct FExportLogger : public Assimp::LogStream
    {
        FExportLogger(FAssimpScene& inScene) : scene(inScene)
        { }

        virtual void write(const char* message) override
        {
            // log messages from Assimp should already contain a newline
            scene.WriteToLogWithNewLine(message);
        }

    private:
        FAssimpScene& scene;
    };

    bool bIsExporting = false;
    FAssimpScene* scene = nullptr;

    bool PreExportWork(const FRuntimeMeshExportParam& param, FRuntimeMeshExportResult& result);
    bool PostExportWork(FRuntimeMeshExportResult& result);

    Assimp::Logger* assimpLogger = nullptr;
    FExportLogger* exportLogger = nullptr;
    aiReturn aiExporterReturn;
    FString aiExporterError;

    #pragma region Async
    FRuntimeMeshExportResult asyncResult;
	FRuntimeMeshImportExportProgressUpdate delegateProgress;
	FRuntimeImportExportGameThreadDone delegateGatherDone;
	FRuntimeExportFinished delegateFinished;
    #pragma endregion Async
};
