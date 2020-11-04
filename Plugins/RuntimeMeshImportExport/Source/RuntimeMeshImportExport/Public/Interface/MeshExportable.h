// MIT License
//
// Copyright (c) 2019 Lucid Layers

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RuntimeMeshImportExportTypes.h"
#include "MeshExportable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UMeshExportable : public UInterface
{
    GENERATED_BODY()
};

/**
 *
 */
class RUNTIMEMESHIMPORTEXPORT_API IMeshExportable 
{
    GENERATED_BODY()

    // Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
    /**
     *	Return the hierarchical name of the Node of this exportable. e.g.: Outer1.Outer2.Outer3.MyNode
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MeshExportable")
    FString GetHierarchicalNodeName() const;

    /**
     *	@param forLod					The LOD of the mesh to return
     *	@param bSkipLodNotValid			If true and 'forLod' is not available, do not return mesh data.
     *									If false and 'forLod' is not available, return the next possible LOD.
     *	@param outMeshToWorld			Return the transform that transforms the mesh data to WorldSpace
     *  @param outSectionData			Return the mesh sections defined by 'forLod' and 'bSkipLodNotValid'
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MeshExportable")
    bool GetMeshData(const int32 forLod, const bool bSkipLodNotValid, TArray<FExportableMeshSection>& outSectionData) const;

};
