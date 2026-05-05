#pragma once

#include "CoreMinimal.h"
#include "GPUPVSScene.h"

class UWorld;

/**
 * GPUPVS 系统：一套“预计算可见性”离线构建流程（对齐 Lightmass 的 PrecomputedVisibility）。
 *
 * BuildPVS 流程：
 * - Collect：收集 PVS Volumes、Setup 几何与遮挡几何
 * - Setup：GPU 栅格化高度区间，CPU 生成 3D Cells
 * - Compute：计算每个 Cell 的可见性 bitset（group→mesh）
 * - WriteBack：写回资源/关卡数据（未实现）
 */
class GPUPVS_API FGPUPVS
{
public:
	struct FParams
	{
		float CellSize = 200.0f;
		float PlayAreaHeight = 220.0f;
		bool bPlaceCellsOnOpaqueOnly = true;

		int32 RasterMaxLayersLimit = 64;

		float VisibilityMaxDistance = 5000.0f;
		float VisibilityMeshBoundsScale = 1.2f;
		int32 VisibilityNumCellSamples = 24;
		int32 VisibilityMinMeshSamples = 14;
		int32 VisibilityMaxMeshSamples = 40;
		int32 VisibilityNumImportanceSamples = 40;
		int32 VisibilityBVHLeafTriLimit = 4;
	};

    FGPUPVS(UWorld* InWorld);
    ~FGPUPVS();

	/** 执行一次完整构建（主要用于调试/迭代）。 */
    void BuildPVS();
	const TArray<FPVSCell>& GetCells() const { return Scene.GetCells(); }
	void SetParams(const FParams& InParams);
	/** 打印当前缓存数据概览（cells/meshes/groups/bitset）。 */
	void DumpSummary() const;

private:
    UWorld* World;
    FPVSScene Scene;

	FParams Params;

	/**
	 * Setup 阶段栅格化结果（CPU 侧缓存）。
	 * - RasterLayerCount：每个像素的层数
	 * - RasterHeightRanges：每个像素每层的 (ZMin,ZMax)，索引为 [PixelIndex * RasterMaxLayers + LayerIndex]
	 */
    uint32 RasterGridSizeX;
    uint32 RasterGridSizeY;
    uint32 RasterMaxLayers;
    FVector3f RasterGridOrigin;
    float RasterCellSize;
    TArray<uint32> RasterLayerCount;
    TArray<FVector2f> RasterHeightRanges;

    /** 当前构建参数（插件内硬编码 + 面板可覆盖）。 */
    int32 RasterMaxLayersLimit;
    int32 VisibilityRayCount;
    float VisibilityMaxDistance;
	int32 VisibilityNumCellSamples;
	int32 VisibilityMinMeshSamples;
	int32 VisibilityMaxMeshSamples;
	int32 VisibilityNumImportanceSamples;
	float VisibilityMeshBoundsScale;
	int32 VisibilityBVHLeafTriLimit;

	mutable int32 LastGroupsBuilt = 0;
	mutable int32 LastMappedMeshes = 0;

	void ResetRasterResults();
    void InitDefaultParams();

	/** Setup：计算/读取高度区间，并生成 Cells。 */
	void SetupPrecomputedVisibility();
	/** Compute：计算每个 Cell 的可见性。 */
	void ComputeVisibility();
	/** Write：写回资源/关卡数据（未实现）。 */
	void WriteBack();
};
