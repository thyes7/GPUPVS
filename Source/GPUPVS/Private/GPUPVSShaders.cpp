/**
 * GPUPVS RDG/Shader 调度实现。
 *
 * 当前包含：
 * - SimpleComputeShader：用于验证 RDG + Compute 流程的最小样例。
 * - GPUPVSRasterAABBCS：将三角形数据栅格化为 (X,Y) 高度层（Setup 阶段使用）。
 */
#include "GPUPVSShaders.h"

#include "RenderGraphUtils.h"
#include "RHICommandList.h"

// ========================================
// FSimpleCS 实现
// ========================================
bool FSimpleCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FSimpleCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
    OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
    OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
}

// 着色器注册
IMPLEMENT_GLOBAL_SHADER(FSimpleCS, "/Plugin/GPUPVS/Private/SimpleComputeShader.usf", "MainCS", SF_Compute);

bool FRasterAABBCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FRasterAABBCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

IMPLEMENT_GLOBAL_SHADER(FRasterAABBCS, "/Plugin/GPUPVS/Private/GPUPVSRasterAABBCS.usf", "MainCS", SF_Compute);

// ========================================
// FShaderDispatcher 实现
// ========================================
void FShaderDispatcher::DispatchSimpleCS(FRDGBuilder& GraphBuilder, uint32 IntA, uint32 IntB)
{
	// 1. 创建 RDG 结构化缓冲（1 个 int32），作为输出
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), 1);
	FRDGBufferRef  OutputBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("GPUPVS_Result"));
	FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(OutputBuffer);

	// 2. 设置着色器参数
	FSimpleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSimpleCS::FParameters>();
	Parameters->IntA = IntA;
	Parameters->IntB = IntB;
	Parameters->OutputBuffer = OutputUAV;  // 这里仍然是 RDG UAV，和以前一致

	// 3. 获取 shader
	TShaderMapRef<FSimpleCS> SimpleShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	// 4. 添加 Compute Pass（不做任何 readback）
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("SimpleComputePass"),
		SimpleShader,
		Parameters,
		FIntVector(1, 1, 1));
}

FShaderDispatcher::FRasterAABBOutputs FShaderDispatcher::DispatchRasterAABB(
    FRDGBuilder& GraphBuilder,
    TConstArrayView<FVector3f> VerticesWS,
    TConstArrayView<uint32> Indices,
    const FVector3f& GridOrigin,
    float CellSize,
    uint32 GridSizeX,
    uint32 GridSizeY,
    uint32 MaxLayers)
{
    FRasterAABBOutputs Outputs;

    const uint32 NumTriangles = Indices.Num() / 3;
    if (VerticesWS.Num() == 0 || Indices.Num() == 0 || NumTriangles == 0 || GridSizeX == 0 || GridSizeY == 0 || MaxLayers == 0)
    {
        return Outputs;
    }

    FRDGBufferRef VerticesBuffer = CreateStructuredBuffer(
        GraphBuilder,
        TEXT("GPUPVS.RasterAABB.Vertices"),
        sizeof(FVector3f),
        VerticesWS.Num(),
        VerticesWS.GetData(),
        VerticesWS.Num() * sizeof(FVector3f));
    FRDGBufferSRVRef VerticesSRV = GraphBuilder.CreateSRV(VerticesBuffer);

    FRDGBufferRef IndicesBuffer = CreateStructuredBuffer(
        GraphBuilder,
        TEXT("GPUPVS.RasterAABB.Indices"),
        sizeof(uint32),
        Indices.Num(),
        Indices.GetData(),
        Indices.Num() * sizeof(uint32));
    FRDGBufferSRVRef IndicesSRV = GraphBuilder.CreateSRV(IndicesBuffer, PF_R32_UINT);

    const uint32 PixelCount = GridSizeX * GridSizeY;

    FRDGBufferRef LayerCountBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PixelCount),
        TEXT("GPUPVS.RasterAABB.LayerCount"));
    FRDGBufferUAVRef LayerCountUAV = GraphBuilder.CreateUAV(LayerCountBuffer);
    AddClearUAVPass(GraphBuilder, LayerCountUAV, 0);

    FRDGBufferRef HeightRangesBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), PixelCount * MaxLayers),
        TEXT("GPUPVS.RasterAABB.HeightRanges"));
    FRDGBufferUAVRef HeightRangesUAV = GraphBuilder.CreateUAV(HeightRangesBuffer);
    AddClearUAVPass(GraphBuilder, HeightRangesUAV, 0);

    FRasterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FRasterAABBCS::FParameters>();
    Parameters->NumTriangles = NumTriangles;
    Parameters->GridOrigin = GridOrigin;
    Parameters->CellSize = CellSize;
    Parameters->GridSizeX = GridSizeX;
    Parameters->GridSizeY = GridSizeY;
    Parameters->MaxLayers = MaxLayers;
    Parameters->InVertices = VerticesSRV;
    Parameters->InIndices = IndicesSRV;
    Parameters->OutLayerCount = LayerCountUAV;
    Parameters->OutHeightRanges = HeightRangesUAV;

    TShaderMapRef<FRasterAABBCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    const uint32 GroupCountX = FMath::DivideAndRoundUp(NumTriangles, 64u);

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("GPUPVS.RasterAABB"),
        Shader,
        Parameters,
        FIntVector((int32)GroupCountX, 1, 1));

    Outputs.LayerCount = LayerCountBuffer;
    Outputs.HeightRanges = HeightRangesBuffer;
    return Outputs;
}
