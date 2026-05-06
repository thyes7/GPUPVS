#pragma once

#include "CoreMinimal.h"

/**
 * 简单 BVH 节点（CPU 构建，GPU 遍历）。
 * - 叶子节点：RightChildOrTriCount 高位为 1，低位为 TriCount；LeftChildOrFirstTri 为 leaf tri 列表起始
 * - 内部节点：LeftChildOrFirstTri/RightChildOrTriCount 分别为左右子节点索引
 */
struct FGPUPVSBVHNode
{
	FVector3f BoundsMin = FVector3f::ZeroVector;
	uint32 LeftChildOrFirstTri = 0;
	FVector3f BoundsMax = FVector3f::ZeroVector;
	uint32 RightChildOrTriCount = 0;

	static constexpr uint32 LeafBit = 0x80000000u;
};

// BVH 构建器：遮挡三角形 -> BVH（GPU any-hit 遍历）
class FGPUPVSBVHBuilder
{
public:
	static void Build(
		TConstArrayView<FVector3f> VerticesWS,
		TConstArrayView<uint32> Indices,
		int32 LeafTriangleLimit,
		TArray<FGPUPVSBVHNode>& OutNodes,
		TArray<uint32>& OutLeafTriIndices);
};
