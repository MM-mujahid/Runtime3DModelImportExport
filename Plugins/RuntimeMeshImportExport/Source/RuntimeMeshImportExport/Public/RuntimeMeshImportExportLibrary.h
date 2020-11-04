// MIT License
//
// Copyright (c) 2017 Eric Liu
// Copyright (c) 2019 Lucid Layers

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "LatentActions.h"
#include "Engine/LatentActionManager.h"
#include "Interface/MeshExportable.h"
#include "assimp/matrix4x4.h"
#include "RuntimeMeshImportExportTypes.h"
#include "ProceduralMeshComponent.h"
#include "RuntimeMeshImportExportLibrary.generated.h"


/**
 * Library to import meshes from disk at runtime using Assimp library.
 */
UCLASS()
class RUNTIMEMESHIMPORTEXPORT_API URuntimeMeshImportExportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    /**
     *	Import a mesh from various scene description files like fbx, gltf, obj, ... . @see GetSupportedExtensionsImport
     *	Note: The hierarchy of the scene is not retained on import
     *
     *	@param file					Depending on 'pathType'
     *	@param transform			A transform that is applied to the imported scene
     *	@param pathType				Choose whether the 'file' provided is absolute or relative
     *	@param importMethodMesh		Choose how meshes shall be treated on import (applied before 'importMethodSection')
     *	@param importMethodSection	Choose how mesh sections are treated on import (applied after 'importMethodMesh')
     *	@param bNormalizeScene		When checked, the scene is transformed to fit into a 100cm cube, objects placed around the center.
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Import")
    static void ImportScene(const FString file, const FTransform& transform
                            , FRuntimeMeshImportResult& result
                            , const EPathType pathType = EPathType::Absolute
                            , const EImportMethodMesh importMethodMesh = EImportMethodMesh::Keep
                            , const EImportMethodSection importMethodSection = EImportMethodSection::MergeSameMaterial
                            , const bool bNormalizeScene = false);

    /**
     *	Import a mesh from various scene description files like fbx, gltf, obj, ... . @see GetSupportedExtensionsImport
     *	Note: The hierarchy of the scene is not retained on import
     *
     *	@param file					Depending on 'pathType'
     *	@param transform			A transform that is applied to the imported scene
     *	@param progressDelegate		Callback for a progress update of the import
     *	@param pathType				Choose whether the 'file' provided is absolute or relative
     *	@param importMethod			Choose if multiple mesh sections should get combined
     *	@param bNormalizeScene		When checked, the object is transformed to fit into a 100cm cube, objects placed around the center.
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Import", meta = (Latent, WorldContext = "WorldContextObject", LatentInfo = "latentInfo"))
    static void	ImportScene_Async(UObject* worldContextObject, FLatentActionInfo latentInfo
                                  , const FString file
                                  , const FTransform& transform
                                  , FRuntimeMeshImportExportProgressUpdateDyn progressDelegate
                                  , FRuntimeMeshImportResult& result, const EPathType pathType = EPathType::Absolute
                                  , const EImportMethodMesh importMethodMesh = EImportMethodMesh::Keep
                                  , const EImportMethodSection importMethodSection = EImportMethodSection::MergeSameMaterial
                                  , const bool bNormalizeScene = false);

    /**
     *	Import a mesh from various scene description files like fbx, gltf, obj, ... . @see GetSupportedExtensionsImpor
     *	Note: The hierarchy of the scene is not retained on import
     *
     *	@param file					Depending on 'pathType'
     *	@param transform			A transform that is applied to the imported scene
     *	@param callbackFinished     Called when the Import is finished
     *  @param progressDelegate		Callback for a progress update of the import
     *	@param pathType				Choose whether the 'file' provided is absolute or relative
     *	@param importMethod			Choose if multiple mesh sections should get combined
     *	@param bNormalizeScene		When checked, the object is transformed to fit into a 100cm cube, objects placed around the center.
     */
    static void ImportScene_Async_Cpp(const FString file, const FTransform& transform
                                      , FRuntimeImportFinished callbackFinished
                                      , FRuntimeMeshImportExportProgressUpdate callbackProgress
                                      , const EPathType pathType = EPathType::Absolute
                                      , const EImportMethodMesh importMethodMesh = EImportMethodMesh::Keep
                                      , const EImportMethodSection importMethodSection = EImportMethodSection::MergeSameMaterial
                                      , const bool bNormalizeMesh = false );

    // Returns all supported extensions for import
    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport|Import")
    static bool GetIsExtensionSupportedImport(FString extension);

    // Returns the extensions that are supported for Import
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Import")
    static void GetSupportedExtensionsImport(TArray<FString>& extensions);

    // Returns all supported extensions for export
    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport|Export")
    static bool GetIsExtensionSupportedExport(FString extension);

    // Returns the extensions that are supported for export
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Export")
    static void GetSupportedExtensionsExport(TArray<FAssimpExportFormat>& formats);

    // Returns presets for transform corrections that can be used for export
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport|Export")
    static void GetTransformCorrectionPresetsExport(TMap<FString, FTransformCorrection>& corrections);

    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport")
    static FTransform TransformCorrectionToTransform(const FTransformCorrection& correction);

    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport")
    static float RotationCorrectionToValue(const ERotationCorrection correction);

    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport")
    static void ConvertVectorToProceduralMeshTangent(const TArray<FVector>& tangents, const bool bFlipTangentY, TArray<FProcMeshTangent>& procTangents);
    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport")
    static void ConvertProceduralMeshTangentToVector(const TArray<FProcMeshTangent>& procTangents, TArray<FVector>& tangents);

    // Converts the whole MaterialInfo to a string for debugging purpose.
    UFUNCTION(BlueprintPure, Category = "RuntimeMeshImportExport")
    static FString MaterialInfoToLogString(const FRuntimeMeshImportMaterialInfo& materialInfo);

    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport")
    static UTexture2D* MaterialParamTextureToTexture2D(const FRuntimeMeshImportExportMaterialParamTexture& textureParam);

    /**
     * Create a DynamicMaterialInstance from a given SourceMaterial and pass in parameters from MaterialInfo.
     * Only parameters from MaterialInfo that also exist in SourceMaterial can be assigned.
     * TextureParameter from MaterialInfo will only be loaded as UTexture2D if the parameter is present in SourceMaterial.
     * The Dnamic material will be name: SOURCEMATERIALNAME_MATERIALINFONAME
     */
    UFUNCTION(BlueprintCallable, Category = "RuntimeMeshImportExport", meta = (WorldContext = "worldContextObject"))
    static UMaterialInstanceDynamic* MaterialInfoToDynamicMaterial(UObject* worldContextObject, const FRuntimeMeshImportMaterialInfo& materialInfo, UMaterialInterface* sourceMaterial);

    // Append 'append' to 'appendTo'. Add a newline before appending if last character of 'appendTo' is not already a newline
    static void NewLineAndAppend(FString& appendTo, const FString& append);

    static void OffsetTriangleArray(int32 offset, TArray<int32>& triangles);

    static FTransform AiTransformToFTransform(const aiMatrix4x4& transform);
    static aiMatrix4x4 FTransformToAiTransform(const FTransform& transform);

	static void SendProgress_AnyThread(FRuntimeMeshImportExportProgressUpdate delegateProgress, FRuntimeMeshImportExportProgress progress);

private:
    /**
    *	Import a mesh from various scene description files like fbx, gltf, obj, ... . @see GetSupportedExtensionsImport
    *	Note: The hierarchy of the scene is not retained on import
    *
    *	@param file					Depending on 'pathType'
    *	@param transform			A transform that is applied to the imported scene
    *	@param callbackProgress     Callback for a progress update of the import
    *	@param pathType				Choose whether the 'file' provided is absolute or relative
    *	@param importMethodMesh		Choose how meshes shall be treated on import (applied before 'importMethodSection')
    *	@param importMethodSection	Choose how mesh sections are treated on import (applied after 'importMethodMesh')
    *	@param bNormalizeScene		When checked, the scene is transformed to fit into a 100cm cube, objects placed around the center.
    */
    static void ImportScene_AnyThread(const FString file, const FTransform& transform
                                        , FRuntimeMeshImportExportProgressUpdate callbackProgress
                                        , FRuntimeMeshImportResult& result
                                        , const EPathType pathType = EPathType::Absolute
                                        , const EImportMethodMesh importMethodMesh = EImportMethodMesh::Keep
                                        , const EImportMethodSection importMethodSection = EImportMethodSection::MergeSameMaterial
                                        , const bool bNormalizeScene = false);
};
