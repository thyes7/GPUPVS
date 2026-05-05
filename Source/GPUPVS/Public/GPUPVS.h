#pragma once

#include "CoreMinimal.h"
#include "GPUPVSScene.h"

class UWorld;

/**
 * GPUPVS ����ࣺ����һ�Ρ�Ԥ����ɼ��ԣ�PVS����������ˮ�ߡ�
 *
 * ��ˮ�ߣ����� Lightmass �� PrecomputedVisibility �ṹ����
 * - BuildPVS��
 *   - Scene.CollectFromWorld���ռ� PVS Volume �뾲̬���Σ�����ռ������Σ���
 *   - SetupPrecomputedVisibility��
 *     - GPU դ�񻯣�����ÿ�� (X,Y) �ĸ߶Ȳ㣨LayerCount + HeightRanges����
 *     - CPU ���ã����� Lightmass ���򣬽��߶Ȳ�ת���� 3D Cells�����ں����ɼ��Բ�������
 *   - ComputeVisibility����ÿ�� Cell ����ɼ� Mesh ������ʵ�֣���
 *   - WriteBack��д���ʲ�������������ʵ�֣���
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

	/** ����һ������������ͨ����������/�༭����ť���á� */
    void BuildPVS();
	const TArray<FPVSCell>& GetCells() const { return Scene.GetCells(); }
	void SetParams(const FParams& InParams);
	void DumpSummary() const;

private:
    UWorld* World;
    FPVSScene Scene;

	FParams Params;

	/**
	 * Setup �׶ε�դ�������CPU �˻��棩��
	 * - RasterLayerCount��ÿ�����صĲ���
	 * - RasterHeightRanges��ÿ������ÿ��� (ZMin,ZMax)������Ϊ [PixelIndex * RasterMaxLayers + LayerIndex]
	 */
    uint32 RasterGridSizeX;
    uint32 RasterGridSizeY;
    uint32 RasterMaxLayers;
    FVector3f RasterGridOrigin;
    float RasterCellSize;
    TArray<uint32> RasterLayerCount;
    TArray<FVector2f> RasterHeightRanges;

    /** PVS ��������ǰ��Ӳ���룬�����ɽ��빤�����û����̨�������� */
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

	/** Setup������/��ȡ�߶Ȳ㣬������ Cells�� */
	void SetupPrecomputedVisibility();
	/** Compute����ÿ�� Cell ����ɼ��ԡ� */
	void ComputeVisibility();
	/** Write�������д�ص��ؿ����ʲ��� */
	void WriteBack();
};
