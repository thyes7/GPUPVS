#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "GPUPVSEditorSettings.generated.h"

/**
 * 编辑器面板参数
 */
UCLASS()
class UGPUPVSEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "基础 Basic", meta = (DisplayName = "Cell 尺寸 (Cell Size)", ToolTip = "控制 Cell 的边长，决定平面采样密度。", ClampMin = "1.0"))
	float CellSize = 200.0f;

	UPROPERTY(EditAnywhere, Category = "基础 Basic", meta = (DisplayName = "可行走高度 (Play Area Height)", ToolTip = "控制每个 Cell 的垂直覆盖高度。", ClampMin = "1.0"))
	float PlayAreaHeight = 220.0f;

	UPROPERTY(EditAnywhere, Category = "基础 Basic", meta = (DisplayName = "仅不透明表面放置 Cell (Opaque Only)", ToolTip = "开启后，仅在不透明遮挡表面上生成候选 Cell。"))
	bool bPlaceCellsOnOpaqueOnly = true;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "最大层数 (Max Layers)", ToolTip = "限制每个像素列记录的高度层数量。", AdvancedDisplay, ClampMin = "1"))
	int32 RasterMaxLayersLimit = 64;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "合并阈值 (Merge Epsilon)", ToolTip = "高度区间合并阈值，越小越保留细节。", AdvancedDisplay, ClampMin = "0.0"))
	float RangeMergeEpsilon = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "每列最大 Cell 数 (Max Cells Per Column)", ToolTip = "限制单个网格列生成的 Cell 数量上限。", AdvancedDisplay, ClampMin = "1"))
	int32 MaxCellsPerColumn = 32;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "底部偏移 (Cell Bottom Offset)", ToolTip = "Cell 底面相对采样高度的抬升偏移。", AdvancedDisplay, ClampMin = "0.0"))
	float CellBottomOffset = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "探针高度 A (Probe Height A)", ToolTip = "第一组补点探针的相对高度。", AdvancedDisplay, ClampMin = "0.0"))
	float PlacementProbeHeightA = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "探针高度 B (Probe Height B)", ToolTip = "第二组补点探针的相对高度。", AdvancedDisplay, ClampMin = "0.0"))
	float PlacementProbeHeightB = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "探针高度 C (Probe Height C)", ToolTip = "第三组补点探针的相对高度。", AdvancedDisplay, ClampMin = "0.0"))
	float PlacementProbeHeightC = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "栅格回读超时 (Raster Timeout)", ToolTip = "Setup 阶段等待 GPU 栅格结果回读的超时时间，单位秒。", AdvancedDisplay, ClampMin = "0.0"))
	float RasterReadbackTimeoutSeconds = 30.0f;

	UPROPERTY(EditAnywhere, Category = "Setup 构建", meta = (DisplayName = "栅格轮询间隔 (Raster Poll)", ToolTip = "Setup 阶段轮询 GPU 回读完成状态的时间间隔，单位秒。", AdvancedDisplay, ClampMin = "0.0"))
	float RasterReadbackPollSeconds = 0.005f;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "最大距离 (Max Distance)", ToolTip = "超过该距离的目标将直接视为不可见。", ClampMin = "0.0"))
	float MaxDistance = 5000.0f;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "包围盒缩放 (Bounds Scale)", ToolTip = "可见性测试时对目标包围盒施加的缩放系数。", AdvancedDisplay, ClampMin = "0.0"))
	float MeshBoundsScale = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "Cell 采样数 (Cell Samples)", ToolTip = "每个 Cell 用于可见性计算的基础采样数量。", ClampMin = "0"))
	int32 NumCellSamples = 24;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "最少网格采样 (Min Mesh Samples)", ToolTip = "单个目标最少执行的采样次数。", AdvancedDisplay, ClampMin = "0"))
	int32 MinMeshSamples = 14;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "最多网格采样 (Max Mesh Samples)", ToolTip = "单个目标最多执行的采样次数。", AdvancedDisplay, ClampMin = "0"))
	int32 MaxMeshSamples = 40;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "重要性采样数 (Importance Samples)", ToolTip = "用于提升边界目标判定稳定性的额外采样次数。", AdvancedDisplay, ClampMin = "0"))
	int32 NumImportanceSamples = 40;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "BVH 叶子三角形上限 (BVH Leaf Limit)", ToolTip = "BVH 每个叶节点容纳的最大三角形数量。", AdvancedDisplay, ClampMin = "1"))
	int32 BVHLeafTriLimit = 4;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "可见性回读超时 (Visibility Timeout)", ToolTip = "Compute 阶段等待 GPU 可见性结果回读的超时时间，单位秒。", AdvancedDisplay, ClampMin = "0.0"))
	float VisibilityReadbackTimeoutSeconds = 60.0f;

	UPROPERTY(EditAnywhere, Category = "Compute 采样", meta = (DisplayName = "可见性轮询间隔 (Visibility Poll)", ToolTip = "Compute 阶段轮询 GPU 回读完成状态的时间间隔，单位秒。", AdvancedDisplay, ClampMin = "0.0"))
	float VisibilityReadbackPollSeconds = 0.005f;

	UPROPERTY(EditAnywhere, Category = "输出 Output", meta = (DisplayName = "输出摘要日志 (Preview Log)", ToolTip = "开启后，WriteBack 阶段输出预览统计日志。", AdvancedDisplay))
	bool bEnableWriteBackPreviewLog = true;

	UPROPERTY(EditAnywhere, Category = "输出 Output", meta = (DisplayName = "写入资产 (Save SceneData)", ToolTip = "开启后，将烘焙结果保存为 SceneData 资产。"))
	bool bEnableEngineWriteBack = false;

	UPROPERTY(EditAnywhere, Category = "调试 Debug", meta = (DisplayName = "绘制 Cell 线框 (Draw Cells)", ToolTip = "在编辑器视口中绘制当前构建得到的 Cell 线框。", AdvancedDisplay))
	bool bDrawCells = false;
};
