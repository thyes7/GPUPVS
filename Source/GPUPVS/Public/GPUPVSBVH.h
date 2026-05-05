#pragma once

#include "CoreMinimal.h"

struct FGPUPVSBVHNode
{
	FVector3f BoundsMin = FVector3f::ZeroVector;
	uint32 LeftChildOrFirstTri = 0;
	FVector3f BoundsMax = FVector3f::ZeroVector;
	uint32 RightChildOrTriCount = 0;

	static constexpr uint32 LeafBit = 0x80000000u;
};

class FGPUPVSBVHBuilder
{
public:
	static void Build(
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		int32 LeafTriangleLimit,
		TArray<FGPUPVSBVHNode>& OutNodes,
		TArray<uint32>& OutLeafTriangleIndices);
};

