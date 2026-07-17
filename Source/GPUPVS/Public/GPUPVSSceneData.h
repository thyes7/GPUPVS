#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPath.h"
#include "GPUPVSSceneData.generated.h"

USTRUCT(BlueprintType)
struct FGPUPVSStoredCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	int32 Index = 0;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FVector3f Center = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FVector3f BoundsMin = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FVector3f BoundsMax = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	int32 DataOffset = 0;
};

USTRUCT(BlueprintType)
struct FGPUPVSStoredTarget
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	int32 VisibilityId = 0;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FVector3f BoundsMin = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FVector3f BoundsMax = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "PVS")
	FSoftObjectPath ComponentPath;
};

UCLASS(BlueprintType)
class GPUPVS_API UGPUPVSSceneData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "PVS | Storage")
	TArray<FGPUPVSStoredCell> Cells;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Storage")
	TArray<FGPUPVSStoredTarget> Targets;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Storage")
	TArray<uint8> VisibilityBitmap;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Metadata")
	FBox SceneBounds;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Metadata")
	float CellSize = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Metadata")
	float PlayAreaHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Metadata")
	int32 BytesPerCell = 0;

	UPROPERTY(VisibleAnywhere, Category = "PVS | Metadata")
	int32 DataVersion = 1;

	UFUNCTION(BlueprintCallable, Category = "PVS | Query")
	bool IsVisible(int32 ViewerCellIndex, int32 TargetVisibilityId) const
	{
		if (!Cells.IsValidIndex(ViewerCellIndex) || !Targets.IsValidIndex(TargetVisibilityId) || BytesPerCell <= 0)
		{
			return false;
		}

		const int32 ByteIdx = Cells[ViewerCellIndex].DataOffset + (TargetVisibilityId / 8);
		if (!VisibilityBitmap.IsValidIndex(ByteIdx))
		{
			return false;
		}

		const uint8 BitMask = (uint8)(1 << (TargetVisibilityId % 8));
		return (VisibilityBitmap[ByteIdx] & BitMask) != 0;
	}

	UFUNCTION(BlueprintCallable, Category = "PVS | Query")
	void Reset()
	{
		Cells.Reset();
		Targets.Reset();
		VisibilityBitmap.Reset();
		SceneBounds = FBox(ForceInit);
		CellSize = 0.0f;
		PlayAreaHeight = 0.0f;
		BytesPerCell = 0;
		DataVersion = 1;
	}
};
