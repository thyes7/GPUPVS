// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"

// ǰ������
class FShaderDispatcher;

// ========================================
// �򵥼�����ɫ��
// ========================================
class FSimpleCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSimpleCS);
    SHADER_USE_PARAMETER_STRUCT(FSimpleCS, FGlobalShader);

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, IntA)
        SHADER_PARAMETER(uint32, IntB)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, OutputBuffer)
    END_SHADER_PARAMETER_STRUCT()
};

class FRasterAABBCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FRasterAABBCS);
    SHADER_USE_PARAMETER_STRUCT(FRasterAABBCS, FGlobalShader);

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumTriangles)
        SHADER_PARAMETER(FVector3f, GridOrigin)
        SHADER_PARAMETER(float, CellSize)
        SHADER_PARAMETER(uint32, GridSizeX)
        SHADER_PARAMETER(uint32, GridSizeY)
        SHADER_PARAMETER(uint32, MaxLayers)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutLayerCount)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector2f>, OutHeightRanges)
    END_SHADER_PARAMETER_STRUCT()
};

struct FGPUPVSBox3f
{
	FVector3f Min = FVector3f::ZeroVector;
	FVector3f Max = FVector3f::ZeroVector;
};

struct FGPUPVSBVHNodeGPU
{
	FVector3f BoundsMin = FVector3f::ZeroVector;
	uint32 LeftChildOrFirstTri = 0;
	FVector3f BoundsMax = FVector3f::ZeroVector;
	uint32 RightChildOrTriCount = 0;
};

class FVisibilityGroupCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisibilityGroupCS);
	SHADER_USE_PARAMETER_STRUCT(FVisibilityGroupCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumCells)
		SHADER_PARAMETER(uint32, NumGroups)
		SHADER_PARAMETER(uint32, WordsPerCell)

		SHADER_PARAMETER(uint32, NumCellSamples)
		SHADER_PARAMETER(uint32, MinMeshSamples)
		SHADER_PARAMETER(uint32, MaxMeshSamples)
		SHADER_PARAMETER(uint32, NumImportanceSamples)

		SHADER_PARAMETER(float, MeshBoundsScale)
		SHADER_PARAMETER(float, MaxDistance)
		SHADER_PARAMETER(uint32, EnableStats)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InCells)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InGroups)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBVHNodeGPU>, InBVHNodes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBVHLeafTriIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVisibilityWords)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutStats)
	END_SHADER_PARAMETER_STRUCT()
};

class FVisibilityMeshCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisibilityMeshCS);
	SHADER_USE_PARAMETER_STRUCT(FVisibilityMeshCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumCells)
		SHADER_PARAMETER(uint32, NumMeshes)
		SHADER_PARAMETER(uint32, WordsPerCell)

		SHADER_PARAMETER(uint32, NumGroups)
		SHADER_PARAMETER(uint32, GroupWordsPerCell)

		SHADER_PARAMETER(uint32, NumCellSamples)
		SHADER_PARAMETER(uint32, MinMeshSamples)
		SHADER_PARAMETER(uint32, MaxMeshSamples)
		SHADER_PARAMETER(uint32, NumImportanceSamples)

		SHADER_PARAMETER(float, MeshBoundsScale)
		SHADER_PARAMETER(float, MaxDistance)
		SHADER_PARAMETER(uint32, EnableStats)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InCells)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InMeshes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InMeshToGroup)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InGroupVisibilityWords)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBVHNodeGPU>, InBVHNodes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBVHLeafTriIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVisibilityWords)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutStats)
	END_SHADER_PARAMETER_STRUCT()
};

class FVisibilityCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisibilityCS);
	SHADER_USE_PARAMETER_STRUCT(FVisibilityCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumCells)
		SHADER_PARAMETER(uint32, NumMeshes)
		SHADER_PARAMETER(uint32, WordsPerCell)

		SHADER_PARAMETER(uint32, NumCellSamples)
		SHADER_PARAMETER(uint32, MinMeshSamples)
		SHADER_PARAMETER(uint32, MaxMeshSamples)
		SHADER_PARAMETER(uint32, NumImportanceSamples)

		SHADER_PARAMETER(float, MeshBoundsScale)
		SHADER_PARAMETER(float, MaxDistance)
		SHADER_PARAMETER(uint32, EnableStats)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InCells)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InMeshes)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBVHNodeGPU>, InBVHNodes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBVHLeafTriIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVisibilityWords)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutStats)
	END_SHADER_PARAMETER_STRUCT()
};

// ========================================
// ��ɫ��������
// ========================================
class FShaderDispatcher
{
public:
    // ���ȼ���ɫ��
    static void DispatchSimpleCS(FRDGBuilder& GraphBuilder, uint32 IntA, uint32 IntB);

    struct FRasterAABBOutputs
    {
        FRDGBufferRef LayerCount = nullptr;
        FRDGBufferRef HeightRanges = nullptr;
    };

	/**
	 * ����������դ�񻯵� (X,Y) �������ÿ���صĸ߶Ȳ���Ϣ��
	 *
	 * Լ����
	 * - LayerCount��ÿ������Ч����
	 * - HeightRanges��ÿ�� (ZMin,ZMax)������Ϊ [PixelIndex * MaxLayers + LayerIndex]
	 *
	 * ˵����
	 * - ��ǰʵ��Ϊ���ؽ��ƣ�RasterAABB������������ͨ Setup ���̣�
	 * - ����������� Lightmass�����滻Ϊ�����ξ�ȷդ�����Ӳ�����
	 */
    static FRasterAABBOutputs DispatchRasterAABB(
        FRDGBuilder& GraphBuilder,
        TConstArrayView<FVector3f> VerticesWS,
        TConstArrayView<uint32> Indices,
        const FVector3f& GridOrigin,
        float CellSize,
        uint32 GridSizeX,
        uint32 GridSizeY,
        uint32 MaxLayers);

	struct FVisibilityOutputs
	{
		FRDGBufferRef VisibilityWords = nullptr;
		FRDGBufferRef Stats = nullptr;
		uint32 NumMeshes = 0;
		uint32 WordsPerCell = 0;
	};

	static FVisibilityOutputs DispatchVisibility(
		FRDGBuilder& GraphBuilder,
		TConstArrayView<FGPUPVSBox3f> Cells,
		TConstArrayView<FGPUPVSBox3f> Meshes,
		TConstArrayView<FGPUPVSBVHNodeGPU> BVHNodes,
		TConstArrayView<uint32> BVHLeafTriIndices,
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		uint32 NumCellSamples,
		uint32 MinMeshSamples,
		uint32 MaxMeshSamples,
		uint32 NumImportanceSamples,
		float MeshBoundsScale,
		float MaxDistance);

	struct FVisibilityGroupOutputs
	{
		FRDGBufferRef GroupVisibilityWords = nullptr;
		FRDGBufferRef Stats = nullptr;
		uint32 NumGroups = 0;
		uint32 GroupWordsPerCell = 0;
	};

	static FVisibilityGroupOutputs DispatchVisibilityGroups(
		FRDGBuilder& GraphBuilder,
		TConstArrayView<FGPUPVSBox3f> Cells,
		TConstArrayView<FGPUPVSBox3f> Groups,
		TConstArrayView<FGPUPVSBVHNodeGPU> BVHNodes,
		TConstArrayView<uint32> BVHLeafTriIndices,
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		uint32 NumCellSamples,
		uint32 MinMeshSamples,
		uint32 MaxMeshSamples,
		uint32 NumImportanceSamples,
		float MeshBoundsScale,
		float MaxDistance);

	static FVisibilityOutputs DispatchVisibilityMeshesWithGroups(
		FRDGBuilder& GraphBuilder,
		TConstArrayView<FGPUPVSBox3f> Cells,
		TConstArrayView<FGPUPVSBox3f> Meshes,
		TConstArrayView<uint32> MeshToGroup,
		FRDGBufferRef GroupVisibilityWords,
		uint32 NumGroups,
		uint32 GroupWordsPerCell,
		TConstArrayView<FGPUPVSBVHNodeGPU> BVHNodes,
		TConstArrayView<uint32> BVHLeafTriIndices,
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		uint32 NumCellSamples,
		uint32 MinMeshSamples,
		uint32 MaxMeshSamples,
		uint32 NumImportanceSamples,
		float MeshBoundsScale,
		float MaxDistance);
};
