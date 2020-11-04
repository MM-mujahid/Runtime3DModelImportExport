// MIT License
//
// Copyright (c) 2017 Eric Liu
// Copyright (c) 2019 Lucid Layers

#include "RuntimeMeshImportExportLibrary.h"
#include "RuntimeMeshImportExport.h"
#include "Engine/Engine.h"
#include "Engine/LatentActionManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include <assimp/Importer.hpp>  // C++ importer interface
#include <assimp/Exporter.hpp>  // C++ exporter interface
#include <assimp/IOSystem.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>       // Output data structure
#include <assimp/postprocess.h> // Post processing flags
#include "ImageUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AssimpProgressHandler.h"

class FLoadMeshAsyncAction : public FPendingLatentAction
{
public:
    FName executionFunction;
    int32 outputLink;
    FWeakObjectPtr callbackTarget;
    FString file;

    FLoadMeshAsyncAction(const FLatentActionInfo& latentInfo, const FString inFile, const FTransform& transform
                         , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate
                         , FRuntimeMeshImportResult& result
                         , const EPathType pathType, const EImportMethodMesh importMethodMesh
                         , const EImportMethodSection importMethodSection, const bool bNormalizeMesh);

    virtual void UpdateOperation(FLatentResponse& response) override
    {
        if (bTaskDone)
        {
            response.FinishAndTriggerIf(bTaskDone, executionFunction, outputLink, callbackTarget);
        }
    }

#if WITH_EDITOR
    // Returns a human readable description of the latent operation's current state
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("FLoadMeshAsyncAction, loading file: %s"), *file);
    }
#endif

private:
    // Reference to the outer result of the user
    FRuntimeMeshImportResult& resultRef;
    bool bTaskDone = false;
};

void ImportMeshesOfNode(const aiScene* scene, aiNode* node, FRuntimeMeshImportResult& result, const FTransform& nodeTransform)
{
    if (node->mNumMeshes == 0)
    {
        RMIE_LOG(Log, "Mesh has no sections, not adding it as mesh to the result. Node: %s", *FString(node->mName.C_Str()));
        return;
    }

    RMIE_LOG(Log, "Importing %d sections for mesh: %s", node->mNumMeshes, *FString(node->mName.C_Str()));

    FRuntimeMeshImportMeshInfo& meshInfoRef = result.meshInfos.Add_GetRef(FRuntimeMeshImportMeshInfo());
    meshInfoRef.meshName = FName(node->mName.C_Str());
    meshInfoRef.sections.SetNum(node->mNumMeshes);

    for (uint32 nodeMeshIndex = 0; nodeMeshIndex < node->mNumMeshes; nodeMeshIndex++)
    {
        int sceneMeshIndex = node->mMeshes[nodeMeshIndex];
        aiMesh *mesh = scene->mMeshes[sceneMeshIndex];

        FRuntimeMeshImportSectionInfo &sectionInfoRef = meshInfoRef.sections[nodeMeshIndex];

        // Transform
        //FTransform transform = URuntimeMeshImportExportLibrary::AiTransformToFTransform(node->mTransformation);
        FTransform transform = nodeTransform;
 /*       transform = transform * composedParentTransform;*/

        sectionInfoRef.materialName = FName(scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str());
        sectionInfoRef.materialIndex = mesh->mMaterialIndex;

        // Vertices
        sectionInfoRef.vertices.Reserve(mesh->mNumVertices);
        sectionInfoRef.normals.Reserve(mesh->mNumVertices);
        sectionInfoRef.uv0.Reserve(mesh->mNumVertices);
        sectionInfoRef.tangents.Reserve(mesh->mNumVertices);
        sectionInfoRef.vertexColors.Reserve(mesh->mNumVertices);
        for (uint32 vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
        {
            sectionInfoRef.vertices.Push(transform.TransformPosition(FVector(
                                             mesh->mVertices[vertexIndex].x,
                                             mesh->mVertices[vertexIndex].y,
                                             mesh->mVertices[vertexIndex].z)));
        }

        //https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/geometry/transforming-normals
        FTransform transformForNormal = FTransform(transform.ToMatrixWithScale().Inverse().GetTransposed());

        // Normal
        if (mesh->HasNormals())
        {
			FVector normal;
            for (uint32 normalIndex = 0; normalIndex < mesh->mNumVertices; ++normalIndex)
            {
				sectionInfoRef.normals.Push(transformForNormal.TransformVector(FVector(
					mesh->mNormals[normalIndex].x,
					mesh->mNormals[normalIndex].y,
					mesh->mNormals[normalIndex].z)).GetSafeNormal());
            }
        }
        else
        {
            sectionInfoRef.normals.SetNumZeroed(mesh->mNumVertices);
        }

        // UV Coordinates
        if (mesh->HasTextureCoords(0))
        {
            for (uint32 textureCoordinateIndex = 0; textureCoordinateIndex < mesh->mNumVertices; ++textureCoordinateIndex)
            {
                sectionInfoRef.uv0.Add(FVector2D(mesh->mTextureCoords[0][textureCoordinateIndex].x
                                                 , -mesh->mTextureCoords[0][textureCoordinateIndex].y));
            }
        }

        // Tangent
        if (mesh->HasTangentsAndBitangents())
        {
            for (uint32 tangentIndex = 0; tangentIndex < mesh->mNumVertices; ++tangentIndex)
            {
                sectionInfoRef.tangents.Push(transform.TransformVectorNoScale(FVector(
                                                 mesh->mTangents[tangentIndex].x,
                                                 mesh->mTangents[tangentIndex].y,
                                                 mesh->mTangents[tangentIndex].z
                                             )).GetSafeNormal());
            }
        }

        // Vertex color
        if (mesh->HasVertexColors(0))
        {
            for (uint32 vertexColorIndex = 0; vertexColorIndex < mesh->mNumVertices; ++vertexColorIndex)
            {
                sectionInfoRef.vertexColors.Push(FLinearColor(
                                                     mesh->mColors[0][vertexColorIndex].r,
                                                     mesh->mColors[0][vertexColorIndex].g,
                                                     mesh->mColors[0][vertexColorIndex].b,
                                                     mesh->mColors[0][vertexColorIndex].a
                                                 ));
            }
        }


        // Triangles
        // When the mesh is inside out cause of the scale, flip the winding order of the triangles
        sectionInfoRef.triangles.Reserve(mesh->mNumFaces * 3);
        const bool bFlipTriangleWindingOrder = (transform.GetScale3D().X * transform.GetScale3D().Y * transform.GetScale3D().Z) < 0;
        const int32 numFaces = mesh->mNumFaces;
		if (bFlipTriangleWindingOrder)
		{
			for (int32 faceIndex = 0; faceIndex < numFaces; ++faceIndex)
			{
				aiFace& face = mesh->mFaces[faceIndex];
				const int32 numIndices = face.mNumIndices;
				check(numIndices == 3);
				sectionInfoRef.triangles.Push(face.mIndices[0]);
				sectionInfoRef.triangles.Push(face.mIndices[2]);
				sectionInfoRef.triangles.Push(face.mIndices[1]);
			}
		}
		else
		{
			for (int32 faceIndex = 0; faceIndex < numFaces; ++faceIndex)
			{
				aiFace& face = mesh->mFaces[faceIndex];
				const int32 numIndices = face.mNumIndices;
				check(numIndices == 3);
				sectionInfoRef.triangles.Push(face.mIndices[0]);
				sectionInfoRef.triangles.Push(face.mIndices[1]);
				sectionInfoRef.triangles.Push(face.mIndices[2]);
			}
		}
    }
}

template<typename Predicate>
void IterateSceneNodes(aiNode* node, Predicate predicate)
{
    predicate(node);

    for (uint32 m = 0; m < node->mNumChildren; ++m)
    {
        IterateSceneNodes(node->mChildren[m], predicate);
    }
}

void BuildComposedNodeTransform(aiNode* node, FTransform& transform)
{
	if (node->mParent)
	{
		BuildComposedNodeTransform(node->mParent, transform);
	}

	transform = FTransform(URuntimeMeshImportExportLibrary::AiTransformToFTransform(node->mTransformation)) * transform;
}

/**
 * Assumes that path starts with '*'
 */
bool ReadTextureFromSceneByMaterialParamPath(const aiScene* scene, FString path, FRuntimeMeshImportExportMaterialParamTexture& texture)
{
    if (!path.StartsWith(TEXT("*")))
    {
        RMIE_LOG(Error, "Variable path does not start with *!");
        return false;
    }

    path.RemoveFromStart(TEXT("*"), ESearchCase::CaseSensitive);
    uint32 sceneTextureIndex = FCString::Atoi(*path);

    if (sceneTextureIndex >= 0 && sceneTextureIndex < scene->mNumTextures)
    {
        aiTexture* sceneTexture = scene->mTextures[sceneTextureIndex];

        texture.width = sceneTexture->mWidth;
        texture.height = sceneTexture->mHeight;
        texture.byteDescription = FString(ANSI_TO_TCHAR(sceneTexture->achFormatHint));

        int32 numBytes = sceneTexture->mHeight == 0 ? sceneTexture->mWidth : sceneTexture->mWidth * sceneTexture->mHeight * sizeof(aiTexel);
        texture.byteData.SetNumUninitialized(numBytes);
        FMemory::Memcpy(texture.byteData.GetData(), (uint8*)sceneTexture->pcData, numBytes);
    }
    else
    {
        RMIE_LOG(Error, "Texture index %d is not part of the Assimp scene", sceneTextureIndex);
        return false;
    }

    return true;
}

/**
 * @param importFile					Scene description file that is being imported
 * @param texturefileRelativePath		The path to the texture file relative to the imported file.
 */
bool ImportTextureFromFile(const FString& importFile, const FString& relativeTexturePath, FRuntimeMeshImportExportMaterialParamTexture& texture)
{
    if (importFile.IsEmpty())
    {
        RMIE_LOG(Error, "Parameter importFile is empty!");
        return false;
    }

    if (relativeTexturePath.IsEmpty())
    {
        RMIE_LOG(Error, "Parameter relativeTexturePath is empty!");
        return false;
    }

    // SANITIZE
    // OBJ files for example can contain a parameter "bump mybump.jpg -bm 1". -bm is a bump multiplier and is added to the path read from Assimp. That breaks outer code!
    FString relativeTexturePathSanitized = relativeTexturePath;
    int32 dotIndex = relativeTexturePath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (dotIndex >= 0)
    {
        int32 spaceIndex = relativeTexturePath.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, dotIndex);
        if (spaceIndex >= 0)
        {
            relativeTexturePathSanitized = relativeTexturePath.Left(spaceIndex);
        }
    }
    if (!relativeTexturePathSanitized.Equals(relativeTexturePath, ESearchCase::CaseSensitive))
    {
        RMIE_LOG(Warning, "While importing %s Sanitized relative texture path from \"%s\" to \"%s\"", *importFile, *relativeTexturePath, *relativeTexturePathSanitized)
    }

    FString absolutTextureFilePath = FPaths::Combine(FPaths::GetPath(importFile), relativeTexturePathSanitized);

    if (absolutTextureFilePath.IsEmpty())
    {
        RMIE_LOG(Error, "Combined file path is empty!");
        return false;
    }
    else
    {
        texture.byteDescription = FPaths::GetExtension(absolutTextureFilePath).ToLower();
        // To stay in sync with Assimp, byteDescription should only be 3 characters long when it contains a file format!
        if (texture.byteDescription.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
        {
            texture.byteDescription = FString(TEXT("jpg"));
        }
        check(texture.byteDescription.Len() <= 3 && "Only file formats with 3 characters are allowed");
        FFileHelper::LoadFileToArray(texture.byteData, *absolutTextureFilePath);
        texture.width = texture.byteData.Num();
        return true;
    }

    return false;
}

bool ImportTextureStackFromMaterial(const FString& importFile, const aiScene *const scene, const aiMaterial *const material, aiTextureType textureType, const FName stackName, FRuntimeMeshImportMaterialInfo& materialInfo)
{
    uint32 textureStackSize = material->GetTextureCount(textureType);
    if (textureStackSize == 0)
    {
        return true; // No texture of that type, so we declare it as success
    }

    bool bKillTexture = false;

    int32 materialInfoTextureIndex = materialInfo.textures.Add(FRuntimeMeshImportExportMaterialParamTexture(stackName));
    for (uint32 textureIndex = 0; textureIndex < textureStackSize; ++textureIndex)
    {
        if (textureIndex > 0)
        {
            RMIE_LOG(Warning, "During Import we noticed that Texture %s is a stack of more than one Texture. But we only import the first Texture of the stack! Stacksize: %d", *stackName.ToString(), textureStackSize);
            break;
        }

        aiString texturePath;
        if (material->GetTexture(textureType, textureIndex, &texturePath) == AI_SUCCESS)
        {
            FString path(texturePath.C_Str());
            // If the path starts with '*', the texture is part of the file. After the '*' the texture index is specified.
            // If not, path should point to a file on disk
            if (path.StartsWith(TEXT("*")))
            {
                bKillTexture |= !ReadTextureFromSceneByMaterialParamPath(scene, path, materialInfo.textures[materialInfoTextureIndex]);
            }
            else
            {
                bKillTexture |= !ImportTextureFromFile(importFile, path, materialInfo.textures[materialInfoTextureIndex]);
            }

        }
        else
        {
            bKillTexture = true;
            RMIE_LOG(Error, "That was an issue that we can't further define when importing Texture %s!", *stackName.ToString());
        }
    }

    if (bKillTexture)
    {
        materialInfo.textures.RemoveAt(materialInfoTextureIndex);
        RMIE_LOG(Error, "Failed to import Texture %s for Material %s", *stackName.ToString(), *materialInfo.name.ToString());
        return false;
    }

    return true;
}

void ImportSceneMaterials(const FString& importFile, const aiScene* scene, FRuntimeMeshImportResult& result, FRuntimeMeshImportExportProgressUpdate callbackProgress)
{
    if (!scene || !scene->HasMaterials())
    {
        return;
    }

    for (uint32 sceneMaterialIndex = 0; sceneMaterialIndex < scene->mNumMaterials; ++sceneMaterialIndex)
    {
        aiMaterial* aiMaterial = scene->mMaterials[sceneMaterialIndex];
        FRuntimeMeshImportMaterialInfo& materialInfo = result.materialInfos.Add_GetRef(FRuntimeMeshImportMaterialInfo());

        materialInfo.name = FName(aiMaterial->GetName().C_Str());

        // Params that are reused
        int intParam;
        float floatParam;
        aiColor3D vectorParam;
        auto IntParamToBool = [&intParam]() -> bool { return intParam != 0 ? true : false; };
        auto VectorParamToLinearColor = [&vectorParam]() -> FLinearColor { return FLinearColor(vectorParam.r, vectorParam.g, vectorParam.b); };

        if (aiMaterial->Get(AI_MATKEY_TWOSIDED, intParam) == AI_SUCCESS)
        {
            materialInfo.bTwoSided = IntParamToBool();
        }

        if (aiMaterial->Get(AI_MATKEY_ENABLE_WIREFRAME, intParam) == AI_SUCCESS)
        {
            materialInfo.bWireFrame = IntParamToBool();
        }

        if (aiMaterial->Get(AI_MATKEY_SHADING_MODEL, intParam) == AI_SUCCESS)
        {
            materialInfo.shadingMode = MaterialShadingModeFromInt(intParam);
            materialInfo.shadingModeInt = intParam;
        }
        else
        {
            materialInfo.shadingMode = ERuntimeMeshImportExportMaterialShadingMode::Unknown;
            materialInfo.shadingModeInt = -1;
        }

        if (aiMaterial->Get(AI_MATKEY_BLEND_FUNC, intParam) == AI_SUCCESS)
        {
            materialInfo.blendMode = MaterialBlendModeFromInt(intParam);
            materialInfo.blendModeInt = intParam;
        }
        else
        {
            materialInfo.blendMode = ERuntimeMeshImportExportMaterialBlendMode::Unknown;
            materialInfo.blendModeInt = -1;
        }

        if (aiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, vectorParam) == AI_SUCCESS)
        {
            materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Diffuse"), VectorParamToLinearColor()));
        }

        //if (aiMaterial->Get(AI_MATKEY_COLOR_AMBIENT, vectorParam) == AI_SUCCESS) // Removed as it seems to be same as diffuse
        //{
        //    materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Ambient"), VectorParamToLinearColor()));
        //}

        if (aiMaterial->Get(AI_MATKEY_COLOR_SPECULAR, vectorParam) == AI_SUCCESS)
        {
            materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Specular"), VectorParamToLinearColor()));
        }

        if (aiMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, vectorParam) == AI_SUCCESS)
        {
            materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Emissive"), VectorParamToLinearColor()));
        }

        if (aiMaterial->Get(AI_MATKEY_COLOR_TRANSPARENT, vectorParam) == AI_SUCCESS)
        {
            materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Transparent"), VectorParamToLinearColor()));
        }

        if (aiMaterial->Get(AI_MATKEY_COLOR_REFLECTIVE, vectorParam) == AI_SUCCESS)
        {
            materialInfo.vectors.Add(FRuntimeMeshImportExportMaterialParamVector(TEXT("Reflective"), VectorParamToLinearColor()));
        }

        if (aiMaterial->Get(AI_MATKEY_OPACITY, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("Opacity"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_TRANSPARENCYFACTOR, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("Transparency"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_BUMPSCALING, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("BumpScaling"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_SHININESS, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("Shininess"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_SHININESS_STRENGTH, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("ShininessStrength"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_REFLECTIVITY, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("Reflectivity"), floatParam));
        }

        if (aiMaterial->Get(AI_MATKEY_REFRACTI, floatParam) == AI_SUCCESS)
        {
            materialInfo.scalars.Add(FRuntimeMeshImportExportMaterialParamScalar(TEXT("Refraction"), floatParam));
        }

        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_DIFFUSE, TEXT("TexDiffuse"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_SPECULAR, TEXT("TexSpecular"), materialInfo);
        //ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_AMBIENT, TEXT("TexAmbient"), materialInfo); // Removed as it seems to be same as diffuse
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_EMISSIVE, TEXT("TexEmissive"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_HEIGHT, TEXT("TexHeight"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_NORMALS, TEXT("TexNormal"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_SHININESS, TEXT("TexShininess"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_OPACITY, TEXT("TexOpacity"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_DISPLACEMENT, TEXT("TexDisplacement"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_LIGHTMAP, TEXT("TexLightmap"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_REFLECTION, TEXT("TexReflection"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_BASE_COLOR, TEXT("TexBaseColor"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_NORMAL_CAMERA, TEXT("TexNormalCamera"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_EMISSION_COLOR, TEXT("TexEmissive"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_METALNESS, TEXT("TexMetallic"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_DIFFUSE_ROUGHNESS, TEXT("TexRoughness"), materialInfo);
        ImportTextureStackFromMaterial(importFile, scene, aiMaterial, aiTextureType_AMBIENT_OCCLUSION, TEXT("TexAmbientOcclusion"), materialInfo);

        URuntimeMeshImportExportLibrary::SendProgress_AnyThread(callbackProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::ImportingMaterials, sceneMaterialIndex+1, scene->mNumMaterials));
    }
}

void MergeMeshes(TArray<FRuntimeMeshImportMeshInfo>& meshInfos)
{
    if (meshInfos.Num() < 2) return;

    // Merging different named meshes and keeping a name is useless.
    // Set it to None.
    meshInfos[0].meshName = FName();

    // Merge
    for (int32 meshIndex = 1; meshIndex < meshInfos.Num(); ++meshIndex)
    {
        meshInfos[0].sections.Append(MoveTemp(meshInfos[meshIndex].sections));
    }

    // Get rid of merged meshes
    meshInfos.SetNum(1);
}

void MergeAllSections(TArray<FRuntimeMeshImportSectionInfo>& sectionInfos)
{
    if (sectionInfos.Num() < 2) return;

    // Merge
    for (int32 sectionIndex = 1; sectionIndex < sectionInfos.Num(); ++sectionIndex)
    {
        sectionInfos[0].Append_Move(MoveTemp(sectionInfos[sectionIndex]));
    }

    // Get rid of merged sections
    sectionInfos.SetNum(1);
}

void MergeSectionsSameMaterial(TArray<FRuntimeMeshImportSectionInfo>& sectionInfos)
{
    TMap<FName, FRuntimeMeshImportSectionInfo> map_MatName_Mesh;
    for (int32 i = sectionInfos.Num() - 1; i>=0; --i)
    {
        FRuntimeMeshImportSectionInfo& meshInfoRef = map_MatName_Mesh.FindOrAdd(sectionInfos[i].materialName);
        if (meshInfoRef.vertices.Num() == 0)
        {
            // Key was just added
            meshInfoRef = MoveTemp(sectionInfos[i]);
        }
        else
        {
            // Already existing data on that key
            meshInfoRef.Append_Move(MoveTemp(sectionInfos[i]));
        }
    }

    // Copy over the combined meshes from the map into the result
    sectionInfos.Empty();
    map_MatName_Mesh.GenerateValueArray(sectionInfos);
}

void URuntimeMeshImportExportLibrary::ImportScene(const FString file, const FTransform& transform, FRuntimeMeshImportResult& result
        , const EPathType pathType, const EImportMethodMesh importMethodMesh, const EImportMethodSection importMethodSection, const bool bNormalizeScene)
{
    FRuntimeMeshImportExportProgressUpdate progDelegate;
    ImportScene_AnyThread(file, transform, progDelegate, result, pathType, importMethodMesh, importMethodSection, bNormalizeScene);
}

FLoadMeshAsyncAction::FLoadMeshAsyncAction(const FLatentActionInfo& latentInfo, const FString inFile, const FTransform& transform
        , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate
        , FRuntimeMeshImportResult& result, const EPathType pathType, const EImportMethodMesh importMethodMesh
        , const EImportMethodSection importMethodSection, const bool bNormalizeMesh)
    : executionFunction(latentInfo.ExecutionFunction)
    , outputLink(latentInfo.Linkage)
    , callbackTarget(latentInfo.CallbackTarget)
    , file(inFile)
    , resultRef(result)
{
    FRuntimeImportFinished callbackFinishedRaw;
    callbackFinishedRaw.BindLambda([this](FRuntimeMeshImportResult result) ->void {
        resultRef = MoveTemp(result);
        bTaskDone = true;
    });

    FRuntimeMeshImportExportProgressUpdate callbackProgressRaw;
    callbackProgressRaw.BindLambda([progressDelegate](FRuntimeMeshImportExportProgress progress) {
        progressDelegate.ExecuteIfBound(progress);
    });

    URuntimeMeshImportExportLibrary::ImportScene_Async_Cpp(inFile, transform, callbackFinishedRaw, callbackProgressRaw, pathType, importMethodMesh
            , importMethodSection, bNormalizeMesh);
}

void URuntimeMeshImportExportLibrary::ImportScene_Async(UObject* worldContextObject, FLatentActionInfo latentInfo
        , const FString file
        , const FTransform& transform
        , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate
        , FRuntimeMeshImportResult& result
        , const EPathType pathType
        , const EImportMethodMesh importMethodMesh
        , const EImportMethodSection importMethodSection
        , const bool bNormalizeScene)
{
    if (UWorld* world = GEngine->GetWorldFromContextObject(worldContextObject, EGetWorldErrorMode::LogAndReturnNull))
    {
        FLatentActionManager& latentActionManager = world->GetLatentActionManager();
        if (latentActionManager.FindExistingAction<FLoadMeshAsyncAction>(latentInfo.CallbackTarget, latentInfo.UUID) == NULL)
        {
            latentActionManager.AddNewAction(latentInfo.CallbackTarget, latentInfo.UUID
                                             , new FLoadMeshAsyncAction(latentInfo, file, transform, progressDelegate, result, pathType, importMethodMesh, importMethodSection, bNormalizeScene));
        }
    }
}

void URuntimeMeshImportExportLibrary::ImportScene_Async_Cpp(const FString file, const FTransform& transform, FRuntimeImportFinished callbackFinished
        , FRuntimeMeshImportExportProgressUpdate callbackProgress, const EPathType pathType, const EImportMethodMesh importMethodMesh
        , const EImportMethodSection importMethodSection, bool bNormalizeMesh)
{
    AsyncTask(ENamedThreads::AnyThread, [=]()-> void
    {
        FRuntimeMeshImportResult* result = new FRuntimeMeshImportResult();
        URuntimeMeshImportExportLibrary::ImportScene_AnyThread(file, transform, callbackProgress, *result, pathType, importMethodMesh, importMethodSection, bNormalizeMesh);
        AsyncTask(ENamedThreads::GameThread, [=]() -> void
        {
            callbackFinished.ExecuteIfBound(MoveTemp(*result));
            check(result->meshInfos.Num() == 0);
            delete result;
        });
    });
}

bool URuntimeMeshImportExportLibrary::GetIsExtensionSupportedImport(FString extension)
{
    if(!extension.StartsWith(TEXT(".")))
    {
        extension = FString(TEXT(".")).Append(extension);
    }
    Assimp::Importer importer;
    return importer.IsExtensionSupported(TCHAR_TO_ANSI(*extension));
}

void URuntimeMeshImportExportLibrary::GetSupportedExtensionsImport(TArray<FString>& extensions)
{
    std::string string;
    Assimp::Importer importer;
    importer.GetExtensionList(string);
    FString extensionsString(string.c_str());
    extensionsString = extensionsString.Replace(TEXT("*"), TEXT(""));
    extensionsString.ParseIntoArray(extensions, TEXT(";"), true);
}

bool URuntimeMeshImportExportLibrary::GetIsExtensionSupportedExport(FString extension)
{
    extension.Replace(TEXT("."), TEXT("")); // Don't want a dot
    TArray<FAssimpExportFormat> formats;
    GetSupportedExtensionsExport(formats);
    FAssimpExportFormat* foundFormat = formats.FindByPredicate([extension](const FAssimpExportFormat& format) -> bool {
        return format.fileExtension == extension;
    });
    return foundFormat ? true : false;
}

void URuntimeMeshImportExportLibrary::GetSupportedExtensionsExport(TArray<FAssimpExportFormat>& formats)
{
    formats.Reset();
    Assimp::Exporter exporter;

    int32 formatCount = exporter.GetExportFormatCount();
    for (int32 i = 0; i < formatCount; ++i)
    {
        formats.Add(FAssimpExportFormat(exporter.GetExportFormatDescription(i)));
    }
}

void URuntimeMeshImportExportLibrary::GetTransformCorrectionPresetsExport(TMap<FString, FTransformCorrection>& corrections)
{
    corrections.Empty(1);

    // Blender
    FTransformCorrection blender;
    blender.RollCorrection_X = ERotationCorrection::Plus_90;
    blender.scaleFactor = 0.01f;
    corrections.Add(TEXT("Blender"), blender);
}

FTransform URuntimeMeshImportExportLibrary::TransformCorrectionToTransform(const FTransformCorrection& correction)
{
    FRotator rotation;
    rotation.Roll = RotationCorrectionToValue(correction.RollCorrection_X);
    rotation.Pitch = RotationCorrectionToValue(correction.PitchCorrection_Y);
    rotation.Yaw = RotationCorrectionToValue(correction.YawCorrection_Z);

    FVector translation(0.f);

    FVector scale(correction.scaleFactor);
    scale.X *= correction.bFlipX ? -1.f : 1.f;
    scale.Y *= correction.bFlipY ? -1.f : 1.f;
    scale.Z *= correction.bFlipZ ? -1.f : 1.f;

    return FTransform(rotation, translation, scale);
}

void URuntimeMeshImportExportLibrary::ConvertVectorToProceduralMeshTangent(const TArray<FVector>& tangents, const bool bFlipTangentY, TArray<FProcMeshTangent>& procTangents)
{
    procTangents.Reset(tangents.Num());
    for (const FVector& tan : tangents)
    {
        procTangents.Add(FProcMeshTangent(tan, bFlipTangentY));
    }
}

void URuntimeMeshImportExportLibrary::ConvertProceduralMeshTangentToVector(const TArray<FProcMeshTangent>& procTangents, TArray<FVector>& tangents)
{
    tangents.Reset(procTangents.Num());
    for (const FProcMeshTangent& tan : procTangents)
    {
        tangents.Add(tan.TangentX);
    }
}

FString URuntimeMeshImportExportLibrary::MaterialInfoToLogString(const FRuntimeMeshImportMaterialInfo& materialInfo)
{
    FString out;
    UEnum* myEnum = nullptr;
    FString enumValueAsString;

    out.Append(FString::Printf(TEXT("-------------------- MaterialInfo -------------------"), *materialInfo.name.ToString()));
    NewLineAndAppend(out, FString::Printf(TEXT("Material Name: %s"), *materialInfo.name.ToString()));
    NewLineAndAppend(out, FString::Printf(TEXT("\tbWireframe: %s"), materialInfo.bWireFrame ? TEXT("true") : TEXT("false")));
    NewLineAndAppend(out, FString::Printf(TEXT("\tbTwoSided: %s"), materialInfo.bTwoSided ? TEXT("true") : TEXT("false")));

    myEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ERuntimeMeshImportExportMaterialShadingMode"), true);
    if (myEnum)
    {
        enumValueAsString = myEnum->GetNameStringByIndex((int32)materialInfo.shadingMode);
    }
    else
    {
        enumValueAsString = FString(TEXT("Failed to read ShadingMode Enum"));
    }
    NewLineAndAppend(out, FString::Printf(TEXT("\tShadingModel: %s, as int: %d"), *enumValueAsString, materialInfo.shadingModeInt));

    myEnum = nullptr;
    myEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ERuntimeMeshImportExportMaterialBlendMode"), true);
    if (myEnum)
    {
        enumValueAsString = myEnum->GetNameStringByIndex((int32)materialInfo.blendMode);
    }
    else
    {
        enumValueAsString = FString(TEXT("Failed to read BlendMode Enum"));
    }
    NewLineAndAppend(out, FString::Printf(TEXT("\tBlendMode: %s, as int: %d"), *enumValueAsString, materialInfo.blendModeInt));

    // Scalar parameter
    NewLineAndAppend(out, FString::Printf(TEXT("\tScalarParameter")));
    for (FRuntimeMeshImportExportMaterialParamScalar scalar : materialInfo.scalars)
    {
        NewLineAndAppend(out, FString::Printf(TEXT("\t\t%s: %f"), *scalar.name.ToString(), scalar.value));
    }

    // Vector parameter
    NewLineAndAppend(out, FString::Printf(TEXT("\tVectorParameter")));
    for (FRuntimeMeshImportExportMaterialParamVector vector : materialInfo.vectors)
    {
        NewLineAndAppend(out, FString::Printf(TEXT("\t\t%s: %s"), *vector.name.ToString(), *vector.value.ToString()));
    }

    // Texture parameter
    NewLineAndAppend(out, FString::Printf(TEXT("\tTextureParameter")));
    for (FRuntimeMeshImportExportMaterialParamTexture texture : materialInfo.textures)
    {
        NewLineAndAppend(out, FString::Printf(TEXT("\t\t%s: width: %d, height: %d, byteDescription: %s, byteCount: %d")
                                              , *texture.name.ToString(), texture.width, texture.height, *texture.byteDescription, texture.byteData.Num()));
    }

    NewLineAndAppend(out, FString::Printf(TEXT("-----------------------------------------------------"), *materialInfo.name.ToString()));

    return out;
}

UTexture2D* URuntimeMeshImportExportLibrary::MaterialParamTextureToTexture2D(const FRuntimeMeshImportExportMaterialParamTexture& textureParam)
{
    if (textureParam.height == 0)
    {
        // This is the easy case. The byte data is a image file. Just pass the data to the ImageWrapper.
        return FImageUtils::ImportBufferAsTexture2D(textureParam.byteData);
    }
    else
    {
        ensureAlwaysMsgf(0, TEXT("raw pixel data of Assimp Texture is not yet supported"));
    }
    return nullptr;
}

UMaterialInstanceDynamic* URuntimeMeshImportExportLibrary::MaterialInfoToDynamicMaterial(UObject* worldContextObject, const FRuntimeMeshImportMaterialInfo& materialInfo, UMaterialInterface* sourceMaterial)
{
    if (!sourceMaterial)
    {
        RMIE_LOG(Error, "A source material must be specified!");
        return nullptr;
    }

    // !!! THE NAME GIVEN TO THE MATERIAL MUST BE NONE, OTHERWISE WHEN CALLED 2x AND THE MATERIAL IS SET TO A UMG IMAGE, IT WILL CRASH !!!
    UMaterialInstanceDynamic* dynamic = UKismetMaterialLibrary::CreateDynamicMaterialInstance(worldContextObject, sourceMaterial, FName());

    for (const FRuntimeMeshImportExportMaterialParamScalar& scalarParam : materialInfo.scalars)
    {
        dynamic->SetScalarParameterValue(scalarParam.name, scalarParam.value);
    }

    for (const FRuntimeMeshImportExportMaterialParamVector& vectorParam : materialInfo.vectors)
    {
        dynamic->SetVectorParameterValue(vectorParam.name, vectorParam.value);
    }

    for (const FRuntimeMeshImportExportMaterialParamTexture& textureParam : materialInfo.textures)
    {
        UTexture* existingTexture = NULL;
        // Only convert the textureParam to a Texture2D if the parameter is present in the material!
        if (dynamic->GetTextureParameterValue(FMaterialParameterInfo(textureParam.name), existingTexture))
        {
            UTexture2D* texture = MaterialParamTextureToTexture2D(textureParam);
            if (texture)
            {
                dynamic->SetTextureParameterValue(textureParam.name, texture);
            }
            else
            {
                RMIE_LOG(Error, "Could not convert TextureParam %s from to UTexture2D for MaterialInfo %s", *textureParam.name.ToString(), *materialInfo.name.ToString());
            }
        }
    }

    return dynamic;
}

float URuntimeMeshImportExportLibrary::RotationCorrectionToValue(const ERotationCorrection correction)
{
    switch (correction)
    {
    case ERotationCorrection::Minus_90:
        return -90.f;
    case ERotationCorrection::Zero:
        return 0.f;
    case ERotationCorrection::Plus_90:
        return +90.f;
    default:
        checkNoEntry(); // Every case must be handled
    }
    return 0.f;
}

void URuntimeMeshImportExportLibrary::NewLineAndAppend(FString& appendTo, const FString& append)
{
    if (!appendTo.IsEmpty() && appendTo[appendTo.Len() - 1] != *TEXT("\n"))
    {
        appendTo += TEXT("\n");
    }
    appendTo.Append(append);
}

void URuntimeMeshImportExportLibrary::OffsetTriangleArray(int32 offset, TArray<int32>& triangles)
{
    for (int32& index : triangles)
    {
        index += offset;
    }
}

FTransform URuntimeMeshImportExportLibrary::AiTransformToFTransform(const aiMatrix4x4& transform)
{
    FMatrix tempMatrix;
    tempMatrix.M[0][0] = transform.a1;
    tempMatrix.M[0][1] = transform.b1;
    tempMatrix.M[0][2] = transform.c1;
    tempMatrix.M[0][3] = transform.d1;
    tempMatrix.M[1][0] = transform.a2;
    tempMatrix.M[1][1] = transform.b2;
    tempMatrix.M[1][2] = transform.c2;
    tempMatrix.M[1][3] = transform.d2;
    tempMatrix.M[2][0] = transform.a3;
    tempMatrix.M[2][1] = transform.b3;
    tempMatrix.M[2][2] = transform.c3;
    tempMatrix.M[2][3] = transform.d3;
    tempMatrix.M[3][0] = transform.a4;
    tempMatrix.M[3][1] = transform.b4;
    tempMatrix.M[3][2] = transform.c4;
    tempMatrix.M[3][3] = transform.d4;
    return FTransform(tempMatrix);
}

aiMatrix4x4 URuntimeMeshImportExportLibrary::FTransformToAiTransform(const FTransform& transform)
{
    FMatrix uMatrix = transform.ToMatrixWithScale();
    aiMatrix4x4 outMatrix;
    outMatrix.a1 = uMatrix.M[0][0];
    outMatrix.b1 = uMatrix.M[0][1];
    outMatrix.c1 = uMatrix.M[0][2];
    outMatrix.d1 = uMatrix.M[0][3];
    outMatrix.a2 = uMatrix.M[1][0];
    outMatrix.b2 = uMatrix.M[1][1];
    outMatrix.c2 = uMatrix.M[1][2];
    outMatrix.d2 = uMatrix.M[1][3];
    outMatrix.a3 = uMatrix.M[2][0];
    outMatrix.b3 = uMatrix.M[2][1];
    outMatrix.c3 = uMatrix.M[2][2];
    outMatrix.d3 = uMatrix.M[2][3];
    outMatrix.a4 = uMatrix.M[3][0];
    outMatrix.b4 = uMatrix.M[3][1];
    outMatrix.c4 = uMatrix.M[3][2];
    outMatrix.d4 = uMatrix.M[3][3];
    return outMatrix;
}

void URuntimeMeshImportExportLibrary::SendProgress_AnyThread(FRuntimeMeshImportExportProgressUpdate delegateProgress, FRuntimeMeshImportExportProgress progress)
{
    if (IsInGameThread())
    {
        delegateProgress.ExecuteIfBound(progress);
    }
    else
    {
        AsyncTask(ENamedThreads::GameThread, [delegateProgress, progress]() {
            delegateProgress.ExecuteIfBound(progress);
        });
    }
}

void URuntimeMeshImportExportLibrary::ImportScene_AnyThread(const FString file, const FTransform& transform, FRuntimeMeshImportExportProgressUpdate callbackProgress, FRuntimeMeshImportResult& result, const EPathType pathType /*= EPathType::Absolute */, const EImportMethodMesh importMethodMesh /*= EImportMethodMesh::Keep */, const EImportMethodSection importMethodSection /*= EImportMethodSection::MergeSameMaterial */, const bool bNormalizeScene /*= false */)
{
    result.bSuccess = false;
    result.meshInfos.Empty();
    result.materialInfos.Empty();

    if (file.IsEmpty())
    {
        RMIE_LOG(Warning, "No file specified.");
        return;
    }

    FString fileFinal;
    switch (pathType)
    {
    case EPathType::Absolute:
        fileFinal = file;
        break;
    case EPathType::ProjectRelative:
        fileFinal = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), file);
        break;
    case EPathType::ContentRelative:
        fileFinal = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), file);
        break;
    }

    FAssimpProgressHandler progressHandler(callbackProgress);
    Assimp::Importer importer;
    importer.SetProgressHandler(&progressHandler);

    const aiScene* scene = importer.ReadFile(TCHAR_TO_UTF8(*fileFinal), aiProcess_Triangulate | aiProcess_MakeLeftHanded | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_OptimizeMeshes);
    importer.SetProgressHandler(nullptr);
    FString importError = FString(importer.GetErrorString());
    if (importError.Len() > 0)
    {
        RMIE_LOG(Error, "Assimp failed to import file. File: %s, Error: %s", *fileFinal, *importError);
        return;
    }

    if (scene == nullptr)
    {
        RMIE_LOG(Error, "File was imported but scene pointer not valid. File: %s", *fileFinal);
        return;
    }

    bool bMeshImportSucces = false;
    if (scene->HasMeshes())
    {
        // Count scene nodes for progress update
        int32 numNodes = 0;
        IterateSceneNodes(scene->mRootNode, [&numNodes](aiNode* node) {
            numNodes++;
        });

        // Import mesh data
        FTransform composedParentTransform = transform;
        int32 nodeCounter = 0;
        IterateSceneNodes(scene->mRootNode, [scene, &result, &composedParentTransform, &nodeCounter, &numNodes, &callbackProgress](aiNode* node)
        {
			FTransform composedNodeTransform;
			BuildComposedNodeTransform(node, composedNodeTransform);
            ImportMeshesOfNode(scene, node, result, composedNodeTransform);
            //composedParentTransform = FTransform(URuntimeMeshImportExportLibrary::AiTransformToFTransform(node->mTransformation)) * composedParentTransform;
            ++nodeCounter;
            SendProgress_AnyThread(callbackProgress, FRuntimeMeshImportExportProgress(ERuntimeMeshImportExportProgressType::ImportingMeshes, nodeCounter, numNodes));
        });

        if (result.meshInfos.Num() > 0)
        {
            // Handle Mesh Import Methode
            switch (importMethodMesh)
            {
            case EImportMethodMesh::Keep:
                // Do Nothing
                break;
            case EImportMethodMesh::Merge:
                MergeMeshes(result.meshInfos);
                break;

            default:
                checkNoEntry();
            }

            // Handle Section Import Methode
            switch (importMethodSection)
            {
            case EImportMethodSection::Keep:
                // Do Nothing
                break;
            default:
                for (FRuntimeMeshImportMeshInfo& mesh : result.meshInfos)
                {
                    switch (importMethodSection)
                    {
                    case EImportMethodSection::Merge:
                        MergeAllSections(mesh.sections);
                        break;
                    case EImportMethodSection::MergeSameMaterial:
                        MergeSectionsSameMaterial(mesh.sections);
                        break;
                    default:
                        checkNoEntry();

                    }
                }
            }
        }

        if (bNormalizeScene)
        {
            // Get the total bounds of all mesh info
            FBox totalBounds;
            totalBounds.Min = FVector(0.f); // Should be initialized already to 0, but had trouble
            totalBounds.Max = FVector(0.f); // Should be initialized already to 0, but had trouble
            for (FRuntimeMeshImportMeshInfo& meshInfo : result.meshInfos)
            {
                for (FRuntimeMeshImportSectionInfo& sectionInfo : meshInfo.sections)
                {
                    FBox currentBounds(sectionInfo.vertices);
                    totalBounds += currentBounds;
                }
            }

            // Use the bounds to transform the scene
            float scaleFactor = 50.f / totalBounds.GetExtent().GetMax();
            FVector offset = -FBoxSphereBounds(totalBounds).Origin;
            for (FRuntimeMeshImportMeshInfo& meshInfo : result.meshInfos)
            {
                for (FRuntimeMeshImportSectionInfo& sectionInfo : meshInfo.sections)
                {
                    for (FVector& vertex : sectionInfo.vertices)
                    {
                        vertex += offset;
                        vertex *= scaleFactor;
                    }
                }
            }
        }

        bMeshImportSucces = true;
    }

    bool bMaterialImportSuccess = false;
    if (importMethodSection != EImportMethodSection::Merge && scene->HasMaterials())
    {
        ImportSceneMaterials(fileFinal, scene, result, callbackProgress);
        bMaterialImportSuccess = true;
    }
    else
    {
        bMaterialImportSuccess = true;
    }


    result.bSuccess = bMeshImportSucces && bMaterialImportSuccess;
    return;
}

