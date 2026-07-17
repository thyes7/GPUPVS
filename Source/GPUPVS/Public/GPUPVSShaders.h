// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"

class FShaderDispatcher;

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

/**
 * GPU 侧用到的 AABB（float3 min/max）。
 * 用 StructuredBuffer 传输，避免 FBox3f 的布局差异。
 */
struct FGPUPVSBox3f
{
	FVector3f Min = FVector3f::ZeroVector;
	float Pad0 = 0.0f;
	FVector3f Max = FVector3f::ZeroVector;
	float Pad1 = 0.0f;
};

/**
 * BVH 节点（GPU 侧布局，必须与 USF 里结构体字段顺序一致）。
 * - RightChildOrTriCount 最高位为 Leaf 标记
 * - 叶子：LeftChildOrFirstTri=FirstTri，RightChildOrTriCount=(LeafBit | TriCount)
 * - 内部节点：LeftChildOrFirstTri=LeftChild，RightChildOrTriCount=RightChild
 */
struct FGPUPVSBVHNodeGPU
{
	FVector3f BoundsMin = FVector3f::ZeroVector;
	uint32 LeftChildOrFirstTri = 0;
	FVector3f BoundsMax = FVector3f::ZeroVector;
	uint32 RightChildOrTriCount = 0;
};

/**
 * Group 可见性查询（对齐 Lightmass 的 group→mesh 两阶段）。
 * 输出每个 Cell 的 group bitset，用于 mesh pass 的快速跳过。
 */
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InOccluders)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBVHNodeGPU>, InBVHNodes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBVHLeafTriIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int32>, InTriangleTargetIds)

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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBox3f>, InOccluders)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGPUPVSBVHNodeGPU>, InBVHNodes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBVHLeafTriIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector3f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<int32>, InTriangleTargetIds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVisibilityWords)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutStats)
	END_SHADER_PARAMETER_STRUCT()
};

// ========================================
// Shader 调度入口
// ========================================
/**
 * 统一封装 RDG Buffer 创建、参数绑定与 Dispatch
 * 所有输出 buffer 都是 RDG buffer
 */
class FShaderDispatcher
{
public:
    struct FRasterAABBOutputs
    {
        FRDGBufferRef LayerCount = nullptr;
        FRDGBufferRef HeightRanges = nullptr;
    };

	/**
	 * Setup：将三角形栅格化到 (X,Y) 网格，输出每个像素的高度区间（多层）
	 *
	 * 输出：
	 * - LayerCount：每个像素的有效层数
	 * - HeightRanges：每像素每层的 (ZMin,ZMax)，索引为 [PixelIndex * MaxLayers + LayerIndex]
	 *
	 * 说明：
	 * - 当前实现采用三角形 AABB 覆盖范围内的 9 点采样（对齐 Lightmass 的 supersample + edge pullback）
	 * - MaxLayers 是硬上限，超过会被截断并在 CPU 侧统计
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

	struct FVisibilityGroupOutputs
	{
		FRDGBufferRef GroupVisibilityWords = nullptr;
		FRDGBufferRef Stats = nullptr;
		uint32 NumGroups = 0;
		uint32 GroupWordsPerCell = 0;
	};

	// Group 可见性 pass（输出 group bitset）
	static FVisibilityGroupOutputs DispatchVisibilityGroups(
		FRDGBuilder& GraphBuilder,
		TConstArrayView<FGPUPVSBox3f> Cells,
		TConstArrayView<FGPUPVSBox3f> Groups,
		TConstArrayView<FGPUPVSBox3f> OccluderBoxes,
		TConstArrayView<FGPUPVSBVHNodeGPU> BVHNodes,
		TConstArrayView<uint32> BVHLeafTriIndices,
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		TConstArrayView<int32> TriangleTargetIds,
		uint32 NumCellSamples,
		uint32 MinMeshSamples,
		uint32 MaxMeshSamples,
		uint32 NumImportanceSamples,
		float MeshBoundsScale,
		float MaxDistance);

	/**
	 * Mesh 可见性 pass
	 * - MeshToGroup=0xFFFFFFFF 表示不入组，直接 mesh query
	 */
	static FVisibilityOutputs DispatchVisibilityMeshesWithGroups(
		FRDGBuilder& GraphBuilder,
		TConstArrayView<FGPUPVSBox3f> Cells,
		TConstArrayView<FGPUPVSBox3f> Meshes,
		TConstArrayView<uint32> MeshToGroup,
		FRDGBufferRef GroupVisibilityWords,
		uint32 NumGroups,
		uint32 GroupWordsPerCell,
		TConstArrayView<FGPUPVSBox3f> OccluderBoxes,
		TConstArrayView<FGPUPVSBVHNodeGPU> BVHNodes,
		TConstArrayView<uint32> BVHLeafTriIndices,
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		TConstArrayView<int32> TriangleTargetIds,
		uint32 NumCellSamples,
		uint32 MinMeshSamples,
		uint32 MaxMeshSamples,
		uint32 NumImportanceSamples,
		float MeshBoundsScale,
		float MaxDistance);
};
