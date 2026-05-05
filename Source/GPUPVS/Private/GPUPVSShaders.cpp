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

bool FVisibilityGroupCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FVisibilityGroupCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
	OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
}

IMPLEMENT_GLOBAL_SHADER(FVisibilityGroupCS, "/Plugin/GPUPVS/Private/GPUPVSVisibilityCS.usf", "MainGroupCS", SF_Compute);

bool FVisibilityMeshCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FVisibilityMeshCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
	OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
}

IMPLEMENT_GLOBAL_SHADER(FVisibilityMeshCS, "/Plugin/GPUPVS/Private/GPUPVSVisibilityCS.usf", "MainMeshCS", SF_Compute);

bool FVisibilityCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FVisibilityCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
	OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
}

IMPLEMENT_GLOBAL_SHADER(FVisibilityCS, "/Plugin/GPUPVS/Private/GPUPVSVisibilityCS.usf", "MainCS", SF_Compute);

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

FShaderDispatcher::FVisibilityOutputs FShaderDispatcher::DispatchVisibility(
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
	float MaxDistance)
{
	FVisibilityOutputs Outputs;

	const uint32 NumCells = (uint32)Cells.Num();
	const uint32 NumMeshes = (uint32)Meshes.Num();
	const uint32 NumTriangles = (uint32)(Indices.Num() / 3);
	if (NumCells == 0 || NumMeshes == 0 || NumTriangles == 0 || VerticesWS.IsEmpty() || BVHNodes.IsEmpty() || BVHLeafTriIndices.IsEmpty())
	{
		return Outputs;
	}

	const uint32 WordsPerCell = FMath::DivideAndRoundUp(NumMeshes, 32u);
	const uint32 TotalWords = NumCells * WordsPerCell;

	FRDGBufferRef CellsBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Cells"),
		sizeof(FGPUPVSBox3f),
		Cells.Num(),
		Cells.GetData(),
		Cells.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef CellsSRV = GraphBuilder.CreateSRV(CellsBuffer);

	FRDGBufferRef MeshesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Meshes"),
		sizeof(FGPUPVSBox3f),
		Meshes.Num(),
		Meshes.GetData(),
		Meshes.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef MeshesSRV = GraphBuilder.CreateSRV(MeshesBuffer);

	FRDGBufferRef BVHNodesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHNodes"),
		sizeof(FGPUPVSBVHNodeGPU),
		BVHNodes.Num(),
		BVHNodes.GetData(),
		BVHNodes.Num() * sizeof(FGPUPVSBVHNodeGPU));
	FRDGBufferSRVRef BVHNodesSRV = GraphBuilder.CreateSRV(BVHNodesBuffer);

	FRDGBufferRef BVHLeafTrisBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHLeafTriIndices"),
		sizeof(uint32),
		BVHLeafTriIndices.Num(),
		BVHLeafTriIndices.GetData(),
		BVHLeafTriIndices.Num() * sizeof(uint32));
	FRDGBufferSRVRef BVHLeafTrisSRV = GraphBuilder.CreateSRV(BVHLeafTrisBuffer, PF_R32_UINT);

	FRDGBufferRef VerticesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Vertices"),
		sizeof(FVector3f),
		VerticesWS.Num(),
		VerticesWS.GetData(),
		VerticesWS.Num() * sizeof(FVector3f));
	FRDGBufferSRVRef VerticesSRV = GraphBuilder.CreateSRV(VerticesBuffer);

	FRDGBufferRef IndicesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Indices"),
		sizeof(uint32),
		Indices.Num(),
		Indices.GetData(),
		Indices.Num() * sizeof(uint32));
	FRDGBufferSRVRef IndicesSRV = GraphBuilder.CreateSRV(IndicesBuffer, PF_R32_UINT);

	FRDGBufferRef VisibilityWordsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalWords),
		TEXT("GPUPVS.Visibility.OutWords"));
	FRDGBufferUAVRef VisibilityWordsUAV = GraphBuilder.CreateUAV(VisibilityWordsBuffer);
	AddClearUAVPass(GraphBuilder, VisibilityWordsUAV, 0);

	FRDGBufferRef StatsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 8),
		TEXT("GPUPVS.Visibility.OutStats"));
	FRDGBufferUAVRef StatsUAV = GraphBuilder.CreateUAV(StatsBuffer);
	AddClearUAVPass(GraphBuilder, StatsUAV, 0);

	FVisibilityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVisibilityCS::FParameters>();
	Parameters->NumCells = NumCells;
	Parameters->NumMeshes = NumMeshes;
	Parameters->WordsPerCell = WordsPerCell;
	Parameters->NumCellSamples = NumCellSamples;
	Parameters->MinMeshSamples = MinMeshSamples;
	Parameters->MaxMeshSamples = MaxMeshSamples;
	Parameters->NumImportanceSamples = NumImportanceSamples;
	Parameters->MeshBoundsScale = MeshBoundsScale;
	Parameters->MaxDistance = MaxDistance;
	Parameters->EnableStats = 1u;
	Parameters->InCells = CellsSRV;
	Parameters->InMeshes = MeshesSRV;
	Parameters->InBVHNodes = BVHNodesSRV;
	Parameters->InBVHLeafTriIndices = BVHLeafTrisSRV;
	Parameters->InVertices = VerticesSRV;
	Parameters->InIndices = IndicesSRV;
	Parameters->OutVisibilityWords = VisibilityWordsUAV;
	Parameters->OutStats = StatsUAV;

	TShaderMapRef<FVisibilityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 GroupCountX = FMath::DivideAndRoundUp(NumMeshes, 8u);
	const uint32 GroupCountY = FMath::DivideAndRoundUp(NumCells, 8u);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GPUPVS.Visibility"),
		Shader,
		Parameters,
		FIntVector((int32)GroupCountX, (int32)GroupCountY, 1));

	Outputs.VisibilityWords = VisibilityWordsBuffer;
	Outputs.Stats = StatsBuffer;
	Outputs.NumMeshes = NumMeshes;
	Outputs.WordsPerCell = WordsPerCell;
	return Outputs;
}

FShaderDispatcher::FVisibilityGroupOutputs FShaderDispatcher::DispatchVisibilityGroups(
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
	float MaxDistance)
{
	FVisibilityGroupOutputs Outputs;

	const uint32 NumCells = (uint32)Cells.Num();
	const uint32 NumGroups = (uint32)Groups.Num();
	const uint32 NumTriangles = (uint32)(Indices.Num() / 3);
	if (NumCells == 0 || NumGroups == 0 || NumTriangles == 0 || VerticesWS.IsEmpty() || BVHNodes.IsEmpty() || BVHLeafTriIndices.IsEmpty())
	{
		return Outputs;
	}

	const uint32 WordsPerCell = FMath::DivideAndRoundUp(NumGroups, 32u);
	const uint32 TotalWords = NumCells * WordsPerCell;

	FRDGBufferRef CellsBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Cells"),
		sizeof(FGPUPVSBox3f),
		Cells.Num(),
		Cells.GetData(),
		Cells.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef CellsSRV = GraphBuilder.CreateSRV(CellsBuffer);

	FRDGBufferRef GroupsBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Groups"),
		sizeof(FGPUPVSBox3f),
		Groups.Num(),
		Groups.GetData(),
		Groups.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef GroupsSRV = GraphBuilder.CreateSRV(GroupsBuffer);

	FRDGBufferRef BVHNodesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHNodes"),
		sizeof(FGPUPVSBVHNodeGPU),
		BVHNodes.Num(),
		BVHNodes.GetData(),
		BVHNodes.Num() * sizeof(FGPUPVSBVHNodeGPU));
	FRDGBufferSRVRef BVHNodesSRV = GraphBuilder.CreateSRV(BVHNodesBuffer);

	FRDGBufferRef BVHLeafTrisBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHLeafTriIndices"),
		sizeof(uint32),
		BVHLeafTriIndices.Num(),
		BVHLeafTriIndices.GetData(),
		BVHLeafTriIndices.Num() * sizeof(uint32));
	FRDGBufferSRVRef BVHLeafTrisSRV = GraphBuilder.CreateSRV(BVHLeafTrisBuffer, PF_R32_UINT);

	FRDGBufferRef VerticesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Vertices"),
		sizeof(FVector3f),
		VerticesWS.Num(),
		VerticesWS.GetData(),
		VerticesWS.Num() * sizeof(FVector3f));
	FRDGBufferSRVRef VerticesSRV = GraphBuilder.CreateSRV(VerticesBuffer);

	FRDGBufferRef IndicesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Indices"),
		sizeof(uint32),
		Indices.Num(),
		Indices.GetData(),
		Indices.Num() * sizeof(uint32));
	FRDGBufferSRVRef IndicesSRV = GraphBuilder.CreateSRV(IndicesBuffer, PF_R32_UINT);

	FRDGBufferRef VisibilityWordsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalWords),
		TEXT("GPUPVS.Visibility.GroupWords"));
	FRDGBufferUAVRef VisibilityWordsUAV = GraphBuilder.CreateUAV(VisibilityWordsBuffer);
	AddClearUAVPass(GraphBuilder, VisibilityWordsUAV, 0);

	FRDGBufferRef StatsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 8),
		TEXT("GPUPVS.Visibility.GroupStats"));
	FRDGBufferUAVRef StatsUAV = GraphBuilder.CreateUAV(StatsBuffer);
	AddClearUAVPass(GraphBuilder, StatsUAV, 0);

	FVisibilityGroupCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVisibilityGroupCS::FParameters>();
	Parameters->NumCells = NumCells;
	Parameters->NumGroups = NumGroups;
	Parameters->WordsPerCell = WordsPerCell;
	Parameters->NumCellSamples = NumCellSamples;
	Parameters->MinMeshSamples = MinMeshSamples;
	Parameters->MaxMeshSamples = MaxMeshSamples;
	Parameters->NumImportanceSamples = NumImportanceSamples;
	Parameters->MeshBoundsScale = MeshBoundsScale;
	Parameters->MaxDistance = MaxDistance;
	Parameters->EnableStats = 1u;
	Parameters->InCells = CellsSRV;
	Parameters->InGroups = GroupsSRV;
	Parameters->InBVHNodes = BVHNodesSRV;
	Parameters->InBVHLeafTriIndices = BVHLeafTrisSRV;
	Parameters->InVertices = VerticesSRV;
	Parameters->InIndices = IndicesSRV;
	Parameters->OutVisibilityWords = VisibilityWordsUAV;
	Parameters->OutStats = StatsUAV;

	TShaderMapRef<FVisibilityGroupCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const uint32 GroupCountX = FMath::DivideAndRoundUp(NumGroups, 8u);
	const uint32 GroupCountY = FMath::DivideAndRoundUp(NumCells, 8u);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GPUPVS.Visibility.Groups"),
		Shader,
		Parameters,
		FIntVector((int32)GroupCountX, (int32)GroupCountY, 1));

	Outputs.GroupVisibilityWords = VisibilityWordsBuffer;
	Outputs.Stats = StatsBuffer;
	Outputs.NumGroups = NumGroups;
	Outputs.GroupWordsPerCell = WordsPerCell;
	return Outputs;
}

FShaderDispatcher::FVisibilityOutputs FShaderDispatcher::DispatchVisibilityMeshesWithGroups(
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
	float MaxDistance)
{
	FVisibilityOutputs Outputs;

	const uint32 NumCells = (uint32)Cells.Num();
	const uint32 NumMeshes = (uint32)Meshes.Num();
	const uint32 NumTriangles = (uint32)(Indices.Num() / 3);
	if (NumCells == 0 || NumMeshes == 0 || NumTriangles == 0 || VerticesWS.IsEmpty() || BVHNodes.IsEmpty() || BVHLeafTriIndices.IsEmpty() || MeshToGroup.Num() != Meshes.Num() || !GroupVisibilityWords)
	{
		return Outputs;
	}

	const uint32 WordsPerCell = FMath::DivideAndRoundUp(NumMeshes, 32u);
	const uint32 TotalWords = NumCells * WordsPerCell;

	FRDGBufferRef CellsBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Cells"),
		sizeof(FGPUPVSBox3f),
		Cells.Num(),
		Cells.GetData(),
		Cells.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef CellsSRV = GraphBuilder.CreateSRV(CellsBuffer);

	FRDGBufferRef MeshesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Meshes"),
		sizeof(FGPUPVSBox3f),
		Meshes.Num(),
		Meshes.GetData(),
		Meshes.Num() * sizeof(FGPUPVSBox3f));
	FRDGBufferSRVRef MeshesSRV = GraphBuilder.CreateSRV(MeshesBuffer);

	FRDGBufferRef MeshToGroupBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.MeshToGroup"),
		sizeof(uint32),
		MeshToGroup.Num(),
		MeshToGroup.GetData(),
		MeshToGroup.Num() * sizeof(uint32));
	FRDGBufferSRVRef MeshToGroupSRV = GraphBuilder.CreateSRV(MeshToGroupBuffer, PF_R32_UINT);

	FRDGBufferSRVRef GroupVisibilitySRV = GraphBuilder.CreateSRV(GroupVisibilityWords, PF_R32_UINT);

	FRDGBufferRef BVHNodesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHNodes"),
		sizeof(FGPUPVSBVHNodeGPU),
		BVHNodes.Num(),
		BVHNodes.GetData(),
		BVHNodes.Num() * sizeof(FGPUPVSBVHNodeGPU));
	FRDGBufferSRVRef BVHNodesSRV = GraphBuilder.CreateSRV(BVHNodesBuffer);

	FRDGBufferRef BVHLeafTrisBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.BVHLeafTriIndices"),
		sizeof(uint32),
		BVHLeafTriIndices.Num(),
		BVHLeafTriIndices.GetData(),
		BVHLeafTriIndices.Num() * sizeof(uint32));
	FRDGBufferSRVRef BVHLeafTrisSRV = GraphBuilder.CreateSRV(BVHLeafTrisBuffer, PF_R32_UINT);

	FRDGBufferRef VerticesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Vertices"),
		sizeof(FVector3f),
		VerticesWS.Num(),
		VerticesWS.GetData(),
		VerticesWS.Num() * sizeof(FVector3f));
	FRDGBufferSRVRef VerticesSRV = GraphBuilder.CreateSRV(VerticesBuffer);

	FRDGBufferRef IndicesBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("GPUPVS.Visibility.Indices"),
		sizeof(uint32),
		Indices.Num(),
		Indices.GetData(),
		Indices.Num() * sizeof(uint32));
	FRDGBufferSRVRef IndicesSRV = GraphBuilder.CreateSRV(IndicesBuffer, PF_R32_UINT);

	FRDGBufferRef VisibilityWordsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalWords),
		TEXT("GPUPVS.Visibility.OutWords"));
	FRDGBufferUAVRef VisibilityWordsUAV = GraphBuilder.CreateUAV(VisibilityWordsBuffer);
	AddClearUAVPass(GraphBuilder, VisibilityWordsUAV, 0);

	FRDGBufferRef StatsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 8),
		TEXT("GPUPVS.Visibility.MeshStats"));
	FRDGBufferUAVRef StatsUAV = GraphBuilder.CreateUAV(StatsBuffer);
	AddClearUAVPass(GraphBuilder, StatsUAV, 0);

	FVisibilityMeshCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVisibilityMeshCS::FParameters>();
	Parameters->NumCells = NumCells;
	Parameters->NumMeshes = NumMeshes;
	Parameters->WordsPerCell = WordsPerCell;
	Parameters->NumGroups = NumGroups;
	Parameters->GroupWordsPerCell = GroupWordsPerCell;
	Parameters->NumCellSamples = NumCellSamples;
	Parameters->MinMeshSamples = MinMeshSamples;
	Parameters->MaxMeshSamples = MaxMeshSamples;
	Parameters->NumImportanceSamples = NumImportanceSamples;
	Parameters->MeshBoundsScale = MeshBoundsScale;
	Parameters->MaxDistance = MaxDistance;
	Parameters->EnableStats = 1u;
	Parameters->InCells = CellsSRV;
	Parameters->InMeshes = MeshesSRV;
	Parameters->InMeshToGroup = MeshToGroupSRV;
	Parameters->InGroupVisibilityWords = GroupVisibilitySRV;
	Parameters->InBVHNodes = BVHNodesSRV;
	Parameters->InBVHLeafTriIndices = BVHLeafTrisSRV;
	Parameters->InVertices = VerticesSRV;
	Parameters->InIndices = IndicesSRV;
	Parameters->OutVisibilityWords = VisibilityWordsUAV;
	Parameters->OutStats = StatsUAV;

	TShaderMapRef<FVisibilityMeshCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const uint32 GroupCountX = FMath::DivideAndRoundUp(NumMeshes, 8u);
	const uint32 GroupCountY = FMath::DivideAndRoundUp(NumCells, 8u);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GPUPVS.Visibility.Meshes"),
		Shader,
		Parameters,
		FIntVector((int32)GroupCountX, (int32)GroupCountY, 1));

	Outputs.VisibilityWords = VisibilityWordsBuffer;
	Outputs.Stats = StatsBuffer;
	Outputs.NumMeshes = NumMeshes;
	Outputs.WordsPerCell = WordsPerCell;
	return Outputs;
}
