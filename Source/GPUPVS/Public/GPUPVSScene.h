#pragma once

#include "CoreMinimal.h"

class AActor;

class FStaticLightingMesh;
class FStaticLightingMapping;

class UWorld;

/**
 * 单个可见性 Cell 的最小描述。
 *
 * 约定：
 * - Bounds 的 XY 尺寸为 CellSize
 * - Bounds 的 Z 尺寸为 PlayAreaHeight
 * - Center 用于后续采样点生成（射线/重要性采样等）
 */
struct FPVSCell
{
	int32 Index = 0;
	FVector3f Center = FVector3f::ZeroVector;
	FBox3f Bounds = FBox3f(ForceInit);
};

/**
 * 预计算可见性体积：对应关卡内的 APrecomputedVisibilityVolume。
 */
class FPrecomputedVisibilityVolume
{
public:
	FBox3f Bounds;
	TArray<FPlane4f> Planes;
};

/**
 * 预计算可见性参数（插件内使用）。
 */
class FGPUPVSVisibilitySettings
{
public:
    float CellSize;
	float PlayAreaHeight;
	bool bPlaceCellsOnOpaqueOnly = true;
};

/**
 * 场景快照：只保留 PVS 所需的最小信息，并以“扁平化数组”形式提供给 CPU/GPU 算法。
 *
 * 当前输出：
 * - PrecomputedVisibilityVolumes：所有 PVS volume bounds
 * - SceneVertices / SceneIndices：合并后的世界空间三角网格（仅 StaticMeshComponent LOD0）
 * - SceneNormals：顶点法线（用于后续朝向过滤/采样分布等）
 * - TriangleBounds：每三角形包围盒（用于加速/剔除）
 */
class FPVSScene
{
public:
	FPVSScene();

	/** 清理所有缓存数据，回到初始状态。 */
	void Reset();
	/** 从 UWorld 采集静态几何与 PVS volume。 */
	void CollectFromWorld(UWorld* InWorld);

	/**
	 * 计算所有 PVS volume 合并后的 Bounds，并在 XY 维度按 CellSize 对齐。
	 * 该 Bounds 用于构建 (X,Y) 栅格。
	 */
	FBox3f GetAlignedVisibilityVolumeBounds() const;
	bool DoesBoxIntersectVisibilityVolume(const FBox3f& Box) const;

	const FGPUPVSVisibilitySettings& GetSettings() const { return VisibilitySettings; }
	void SetCellSize(float InCellSize) { VisibilitySettings.CellSize = InCellSize; }
	void SetPlayAreaHeight(float InPlayAreaHeight) { VisibilitySettings.PlayAreaHeight = InPlayAreaHeight; }
	void SetPlaceCellsOnOpaqueOnly(bool bInPlaceCellsOnOpaqueOnly) { VisibilitySettings.bPlaceCellsOnOpaqueOnly = bInPlaceCellsOnOpaqueOnly; }
	void SetVisibilityBounds(const FBox3f& InBounds) { VisibilityBounds = InBounds; }

    const TArray<FPVSCell>&   GetCells()   const { return Cells; }

	void SetCells(TArray<FPVSCell>&& InCells) { Cells = MoveTemp(InCells); }

	/** 合并后的世界空间顶点数组。 */
	const TArray<FVector3f>& GetSceneVertices() const { return SceneVertices; }
	/** 合并后的三角形索引数组（3 个索引为一个三角形）。 */
	const TArray<uint32>& GetSceneIndices() const { return SceneIndices; }
	/** 与 SceneVertices 对齐的顶点法线数组。 */
	const TArray<FVector3f>& GetSceneNormals() const { return SceneNormals; }
	/** 与三角形对齐的包围盒数组（TriangleBounds.Num() == SceneIndices.Num()/3）。 */
	const TArray<FBox3f>& GetTriangleBounds() const { return TriangleBounds; }
	/** PVS Volumes 列表。 */
	const TArray<FPrecomputedVisibilityVolume>& GetPrecomputedVisibilityVolumes() const { return PrecomputedVisibilityVolumes; }

	/** 遮挡几何（用于光追遮挡，静态不透明）。 */
	const TArray<FVector3f>& GetOccluderVertices() const { return OccluderVertices; }
	const TArray<uint32>& GetOccluderIndices() const { return OccluderIndices; }

	/** 可见性目标列表（每个目标一个 Bounds，对应 bitset 的索引）。 */
	const TArray<FBox3f>& GetVisibilityMeshBounds() const { return VisibilityMeshBounds; }

	uint32 GetVisibilityNumMeshes() const { return VisibilityNumMeshes; }
	uint32 GetVisibilityWordsPerCell() const { return VisibilityWordsPerCell; }
	const TArray<uint32>& GetVisibilityWords() const { return VisibilityWords; }
	void SetVisibilityWords(TArray<uint32>&& InVisibilityWords, uint32 InNumMeshes, uint32 InWordsPerCell)
	{
		VisibilityWords = MoveTemp(InVisibilityWords);
		VisibilityNumMeshes = InNumMeshes;
		VisibilityWordsPerCell = InWordsPerCell;
	}

private:
    FGPUPVSVisibilitySettings VisibilitySettings;

	FBox3f VisibilityBounds = FBox3f(ForceInit);

    TArray<FPVSCell>   Cells;

	TArray<FPrecomputedVisibilityVolume> PrecomputedVisibilityVolumes;

	TArray<FVector3f> SceneVertices;
	TArray<uint32> SceneIndices;
	TArray<FVector3f> SceneNormals;
	TArray<FBox3f> TriangleBounds;

	TArray<FVector3f> OccluderVertices;
	TArray<uint32> OccluderIndices;

	TArray<FBox3f> VisibilityMeshBounds;
	TArray<uint32> VisibilityWords;
	uint32 VisibilityNumMeshes = 0;
	uint32 VisibilityWordsPerCell = 0;
};
