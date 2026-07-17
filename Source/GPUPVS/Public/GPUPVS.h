#pragma once

#include "CoreMinimal.h"
#include "GPUPVSScene.h"

class UWorld;
class UGPUPVSSceneData;

/**
 * GPUPVS 系统
 *
 * - Collect：采集 PVS Volumes / 几何
 * - Setup：栅格化高度区间 + 生成 Cells
 * - Compute：Cell 可见性
 * - WriteBack：写回 SceneData 资产
 */
class GPUPVS_API FGPUPVS
{
public:
	struct FParams
	{
		struct FSetupParams
		{
			int32 RasterMaxLayersLimit = 64;
			float RangeMergeEpsilon = 1.0f;
			int32 MaxCellsPerColumn = 32;
			float CellBottomOffset = 1.0f;
			float PlacementProbeHeightA = 0.4f;
			float PlacementProbeHeightB = 0.6f;
			float PlacementProbeHeightC = 0.8f;
			float RasterReadbackTimeoutSeconds = 30.0f;
			float RasterReadbackPollSeconds = 0.005f;
		};

		struct FComputeParams
		{
			float MaxDistance = 5000.0f;
			float MeshBoundsScale = 1.2f;
			int32 NumCellSamples = 24;
			int32 MinMeshSamples = 14;
			int32 MaxMeshSamples = 40;
			int32 NumImportanceSamples = 40;
			int32 BVHLeafTriLimit = 4;
			float VisibilityReadbackTimeoutSeconds = 60.0f;
			float VisibilityReadbackPollSeconds = 0.005f;
		};

		struct FWriteBackParams
		{
			bool bEnablePreviewLog = true;
			bool bEnableEngineWriteBack = false;
		};

		float CellSize = 200.0f;
		float PlayAreaHeight = 220.0f;
		bool bPlaceCellsOnOpaqueOnly = true;

		FSetupParams Setup;
		FComputeParams Compute;
		FWriteBackParams WriteBack;
	};

    FGPUPVS(UWorld* InWorld);
    ~FGPUPVS();

	// 执行一次完整构建
    void BuildPVS();
	const TArray<FPVSCell>& GetCells() const { return Scene.GetCells(); }
	void SetParams(const FParams& InParams);
	// 打印调试信息
	void DumpSummary() const;
	const FPVSScene& GetScene() const { return Scene; }
	UGPUPVSSceneData* LoadSceneDataAsset();
	bool HasLoadedSceneDataAsset() const { return LoadedSceneDataAsset.IsValid(); }
	void SetLoadedSceneDataAsset(UGPUPVSSceneData* InAsset) { LoadedSceneDataAsset = InAsset; }
	bool IsVisibleFromLoadedAsset(int32 ViewerCellIndex, int32 TargetVisibilityId) const;
	int32 FindLoadedCellIndexFromPosition(const FVector& Position) const;
	bool TogglePVSCulling(bool bEnable);
	bool IsPVSCullingEnabled() const { return bIsApplyingPVSCulling; }
	void TogglePVSVisualization(bool bEnable);
	bool IsPVSVisualizationEnabled() const { return bIsVisualizingPVS; }
	void TickPreviewForView(const FVector& ViewLocation);
	void RestorePVSCulledComponents();
	FString GetSceneDataLevelName() const;
	FString GetDefaultSceneDataAssetPath() const;
	FString GetDefaultSceneDataObjectPath() const;

private:
    UWorld* World;
    FPVSScene Scene;

	FParams Params;

	/**
	 * Setup 阶段栅格化结果
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

	// 当前构建参数（硬编码 + 面板覆盖）
	FParams::FSetupParams SetupParams;
	FParams::FComputeParams ComputeParams;
	FParams::FWriteBackParams WriteBackParams;
	mutable TWeakObjectPtr<UGPUPVSSceneData> LoadedSceneDataAsset;
	bool bIsApplyingPVSCulling = false;
	bool bIsVisualizingPVS = false;
	mutable int32 LastPreviewCellIndex = INDEX_NONE;
	mutable int32 LastPVSCullingCellIndex = INDEX_NONE;
	mutable TMap<FSoftObjectPath, bool> OriginalComponentVisibility;
	mutable TMap<FSoftObjectPath, bool> OriginalComponentHiddenInGame;
	mutable TMap<FSoftObjectPath, TWeakObjectPtr<UPrimitiveComponent>> ResolvedTargetComponents;

	mutable int32 LastGroupsBuilt = 0;
	mutable int32 LastMappedMeshes = 0;

	void ResetRasterResults();
    void InitDefaultParams();

	// Setup：高度区间 + Cells
	void SetupPrecomputedVisibility();
	// Compute：Cell 可见性
	void ComputeVisibility();
	// Write：写回 SceneData 资产
	void WriteBack();
	void DrawPVSVisualizationForView(const FVector& ViewLocation);
	void ApplyPVSCullingForView(const FVector& ViewLocation);
	UPrimitiveComponent* ResolveTargetComponent(const FSoftObjectPath& ComponentPath) const;
};
