// MIT License
//
// Copyright (c) 2019 Lucid Layers

#pragma once

#include "CoreMinimal.h"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "RuntimeMeshImportExportTypes.h"
#include "Tickable.h"
#include "Interface/MeshExportable.h"

struct FAssimpScene;
struct FAssimpMesh;
struct FRuntimeMeshExportParam;

/**
 *	This files provides wrappers for the Assimp types.
 *	These types hold all the data in TArrays and on request fill the
 *  pointers of the Assimp parent type that are needed for export.
 *  The destructors of the types makes sure that the pointers in the parent
 *  classes are removed before the parent destructor is called to prevent 
 *  heap corruption.
 */

struct FAssimpMesh : public aiMesh
{
public:
    ~FAssimpMesh();

    TArray<aiVector3D> vertices;
    TArray<aiVector3D> normals;
    TArray<aiVector3D> tangents;
    TArray<aiVector3D> bitangents;
    TArray<aiVector3D> textureCoordinates[AI_MAX_NUMBER_OF_TEXTURECOORDS];
	uint32 numUVComponents[AI_MAX_NUMBER_OF_TEXTURECOORDS];
    TArray<aiColor4D> vertexColors;

	// Note:: We had a FAssimpFace before that had an array
	// for the indices that are then copied to the parent
	// to be consistent with the other types.
	// That did not work out as the TArray makes the type 
	// larger. We can't do that, so work with aiFaces directly.
	// (If the parent would need a ptr to an array of aiFace*, it
	// would be no problem)
    TArray<aiFace> faces;

private:
    friend FAssimpScene;
    void SetDataAndPtrsToParentClass(const FRuntimeMeshExportParam& param);
};

struct FAssimpNode : public aiNode
{
public:
	FAssimpNode(const FName& inName, FAssimpNode* inParent) :  parent(inParent), name(inName)
	{}
    ~FAssimpNode();

    TArray<FAssimpNode*> children;
    TArray<uint32> meshRefIndices;

    const FAssimpNode* parent = nullptr;
    const FName name;
    FTransform worldTransform;
    TArray<TScriptInterface<IMeshExportable>> exportObjects;

    FString GetHierarchicalName() const;
	FAssimpNode* FindOrCreateNode(TArray<FString>& nodePathRelative);

private:
    friend struct FAssimpScene;

	// Helper index for async export
	int32 indexGatherNext = 0;
	TArray<TArray<FExportableMeshSection>> gatheredExportables;
	/**
	 *	Gathers the data from the exportables. This function must be run on the game thread.
	 *	Returns the number of exportables gathered.
	 */ 
	int32 GatherMeshData(FAssimpScene& scene, const FRuntimeMeshExportParam& param, const bool bGatherAll, const int32 numToGather = 0);

	void ProcessGatheredData_Recursive(FAssimpScene& scene, const FRuntimeMeshExportParam& param);
	void ProcessGatheredData_Internal(FAssimpScene& scene, const FRuntimeMeshExportParam& param);

    void CreateAssimpMeshesFromMeshData(FAssimpScene& scene, const FRuntimeMeshExportParam& param);
    bool ValidateMeshSection(FAssimpScene& scene, TScriptInterface<IMeshExportable>& exportable, FExportableMeshSection& section);

    void SetDataAndPtrsToParentClass(const FRuntimeMeshExportParam& param);
	void ClearParentDataAndPtrs();
	void ClearMeshData();
	void ClearExportData();

	void GetNodesRecursive(TArray<FAssimpNode*>& outNodes);
	UFUNCTION()
	void TextureExportCompleted(bool onSuccess);
};

struct FAssimpScene : public aiScene
{
public:
	FAssimpScene();
    ~FAssimpScene();

    FAssimpNode* rootNode = nullptr;
    TArray<FAssimpMesh*> meshes;
    TArray<aiMaterial*> materials;
	// Helper array to find unique materials
	TArray<UMaterialInterface*> uniqueMaterials;

	bool bLogToUnreal = false;
	int32 numObjectsSkipped = 0;

	// Writes to 'exportLog' if available and adds a new line at the end.
	void WriteToLogWithNewLine(const FString& logText);
	FString* exportLog = nullptr;
	
	void PrepareSceneForExport(const FRuntimeMeshExportParam& param);
	// Must be called on GameThread to gather mesh data.
	void PrepareSceneForExport_Async_Start(const FRuntimeMeshExportAsyncParam& param, FRuntimeMeshImportExportProgressUpdate callbackProgress
		, TFunction<void()> onPrepareFinished);

	// Call on NONE GameThread to finish processing the data
	void PrepareSceneForExport_Async_Finish(const FRuntimeMeshExportParam& param);
	void ClearSceneExportData();

private:
	/**
	 *	The whole data of the scene is stored in TArrays. The parent ai-Classes
	 *	only get ptrs to the data.
	 */
    void SetDataAndPtrsToParentClass_EntireScene(const FRuntimeMeshExportParam& param);
	void ClearParentDataAndPtrs();
	void ClearMeshData();

	TArray<FAssimpNode*> allNodesHelper;

	// Called from ticker
	void PrepareSceneForExport_Update(const FRuntimeMeshExportParam& param);

#pragma region Async
	int32 currentNodeIndex = 0;
	int32 numGatherPerTick = -1;
	int32 gatheredMeshNum = 0;
	double startTimeGatherMeshData = 0.f;
	FRuntimeMeshImportExportProgressUpdate delegateProgress;
	TFunction<void()> onGameThreadPrepareFinished;

	class FGatherMeshDataTicker : public FTickableGameObject
	{
	public:
		FGatherMeshDataTicker(FAssimpScene* inScene, const FRuntimeMeshExportAsyncParam& inParam)
			: scene(inScene), param(inParam) 
		{}
		virtual ~FGatherMeshDataTicker() {}

		virtual bool IsTickableWhenPaused() const override
		{
			return true;
		}

		virtual bool IsTickableInEditor() const override
		{
			return true;
		}

		virtual UWorld* GetTickableGameObjectWorld() const override
		{
			return nullptr;
		}

		virtual TStatId GetStatId() const override
		{
			return TStatId(); // Creation of a valid stat id is confusing, just pass an empty for now.
		}

		virtual void Tick(float DeltaTime) override
		{
			scene->PrepareSceneForExport_Update(param.param);
		}

	private:
		FAssimpScene* scene;
		const FRuntimeMeshExportAsyncParam param;
	};
	TUniquePtr<FGatherMeshDataTicker> gatherMeshDataTicker;
#pragma endregion Async

};

