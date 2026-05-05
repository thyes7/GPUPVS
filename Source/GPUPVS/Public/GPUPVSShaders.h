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
};
