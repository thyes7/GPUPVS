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
	UPROPERTY(EditAnywhere, Category = "Cell")
	float CellSize = 200.0f;

	UPROPERTY(EditAnywhere, Category = "Cell")
	float PlayAreaHeight = 220.0f;

	UPROPERTY(EditAnywhere, Category = "Cell")
	bool bPlaceCellsOnOpaqueOnly = true;

	UPROPERTY(EditAnywhere, Category = "Setup")
	int32 RasterMaxLayersLimit = 64;

	UPROPERTY(EditAnywhere, Category = "Compute")
	// 距离裁剪（0=不裁剪）
	float VisibilityMaxDistance = 5000.0f;

	UPROPERTY(EditAnywhere, Category = "Compute")
	float MeshBoundsScale = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Compute")
	// 采样强度
	int32 NumCellSamples = 24;

	UPROPERTY(EditAnywhere, Category = "Compute")
	int32 MinMeshSamples = 14;

	UPROPERTY(EditAnywhere, Category = "Compute")
	int32 MaxMeshSamples = 40;

	UPROPERTY(EditAnywhere, Category = "Compute")
	int32 NumImportanceSamples = 40;

	UPROPERTY(EditAnywhere, Category = "Compute")
	int32 BVHLeafTriLimit = 4;

	UPROPERTY(EditAnywhere, Category = "Visualize")
	bool bDrawCells = false;
};
