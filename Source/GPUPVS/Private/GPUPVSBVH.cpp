#include "GPUPVSBVH.h"

#include "Algo/Sort.h"

namespace
{
	struct FTriRef
	{
		uint32 TriIndex = 0;
		FVector3f Centroid = FVector3f::ZeroVector;
		FBox3f Bounds = FBox3f(ForceInit);
	};

	static FBox3f ComputeBounds(TConstArrayView<FTriRef> Tris)
	{
		FBox3f Bounds(ForceInit);
		for (const FTriRef& T : Tris)
		{
			Bounds += T.Bounds;
		}
		return Bounds;
	}

	static FBox3f ComputeCentroidBounds(TConstArrayView<FTriRef> Tris)
	{
		FBox3f Bounds(ForceInit);
		for (const FTriRef& T : Tris)
		{
			Bounds += T.Centroid;
		}
		return Bounds;
	}

	static int32 BuildNodeRecursive(
		TArray<FTriRef>& TriRefs,
		int32 Start,
		int32 Count,
		int32 LeafTriangleLimit,
		TArray<FGPUPVSBVHNode>& Nodes,
		TArray<uint32>& LeafTriIndices)
	{
		const int32 NodeIndex = Nodes.AddDefaulted();

		const TConstArrayView<FTriRef> RangeView(TriRefs.GetData() + Start, Count);
		const FBox3f Bounds = ComputeBounds(RangeView);

		FGPUPVSBVHNode& Node = Nodes[NodeIndex];
		Node.BoundsMin = Bounds.Min;
		Node.BoundsMax = Bounds.Max;

		if (Count <= LeafTriangleLimit)
		{
			const uint32 FirstTri = (uint32)LeafTriIndices.Num();
			LeafTriIndices.Reserve(LeafTriIndices.Num() + Count);
			for (int32 i = 0; i < Count; ++i)
			{
				LeafTriIndices.Add(TriRefs[Start + i].TriIndex);
			}

			Node.LeftChildOrFirstTri = FirstTri;
			Node.RightChildOrTriCount = FGPUPVSBVHNode::LeafBit | (uint32)Count;
			return NodeIndex;
		}

		const FBox3f CentroidBounds = ComputeCentroidBounds(RangeView);
		const FVector3f Extent = (FVector3f)CentroidBounds.GetExtent();

		int32 Axis = 0;
		if (Extent.Y > Extent.X) Axis = 1;
		if (Extent.Z > (Axis == 0 ? Extent.X : Extent.Y)) Axis = 2;

		TArrayView<FTriRef> SortView(TriRefs.GetData() + Start, Count);
		Algo::Sort(SortView, [Axis](const FTriRef& A, const FTriRef& B)
		{
			if (Axis == 0) return A.Centroid.X < B.Centroid.X;
			if (Axis == 1) return A.Centroid.Y < B.Centroid.Y;
			return A.Centroid.Z < B.Centroid.Z;
		});

		const int32 LeftCount = Count / 2;
		const int32 RightCount = Count - LeftCount;

		const int32 LeftChild = BuildNodeRecursive(TriRefs, Start, LeftCount, LeafTriangleLimit, Nodes, LeafTriIndices);
		const int32 RightChild = BuildNodeRecursive(TriRefs, Start + LeftCount, RightCount, LeafTriangleLimit, Nodes, LeafTriIndices);

		Node.LeftChildOrFirstTri = (uint32)LeftChild;
		Node.RightChildOrTriCount = (uint32)RightChild;
		return NodeIndex;
	}
}

void FGPUPVSBVHBuilder::Build(
	TConstArrayView<FVector3f> VerticesWS,
	TConstArrayView<uint32> Indices,
	int32 LeafTriangleLimit,
	TArray<FGPUPVSBVHNode>& OutNodes,
	TArray<uint32>& OutLeafTriangleIndices)
{
	OutNodes.Reset();
	OutLeafTriangleIndices.Reset();

	const uint32 NumTriangles = (uint32)(Indices.Num() / 3);
	if (VerticesWS.IsEmpty() || Indices.IsEmpty() || NumTriangles == 0 || LeafTriangleLimit <= 0)
	{
		return;
	}

	TArray<FTriRef> TriRefs;
	TriRefs.SetNumUninitialized((int32)NumTriangles);

	for (uint32 TriIndex = 0; TriIndex < NumTriangles; ++TriIndex)
	{
		const uint32 I0 = Indices[(int32)TriIndex * 3 + 0];
		const uint32 I1 = Indices[(int32)TriIndex * 3 + 1];
		const uint32 I2 = Indices[(int32)TriIndex * 3 + 2];

		const FVector3f V0 = VerticesWS[(int32)I0];
		const FVector3f V1 = VerticesWS[(int32)I1];
		const FVector3f V2 = VerticesWS[(int32)I2];

		FBox3f TriBounds(ForceInit);
		TriBounds += V0;
		TriBounds += V1;
		TriBounds += V2;

		FTriRef& Ref = TriRefs[(int32)TriIndex];
		Ref.TriIndex = TriIndex;
		Ref.Bounds = TriBounds;
		Ref.Centroid = (V0 + V1 + V2) * (1.0f / 3.0f);
	}

	OutNodes.Reserve((int32)NumTriangles * 2);
	OutLeafTriangleIndices.Reserve((int32)NumTriangles);
	BuildNodeRecursive(TriRefs, 0, (int32)NumTriangles, LeafTriangleLimit, OutNodes, OutLeafTriangleIndices);
}

