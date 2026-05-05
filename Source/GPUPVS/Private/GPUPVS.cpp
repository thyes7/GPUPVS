/**
 * GPUPVS 主流程实现。
 *
 * 流水线（对齐 Lightmass 的 PrecomputedVisibility）：
 * - Collect：从 UWorld 收集 PVS Volumes、Setup 几何与遮挡几何
 * - Setup：GPU 栅格化得到每个 (X,Y) 的高度区间，CPU 生成 3D Cells
 * - Compute：对每个 Cell 计算可见性 bitset（group→mesh，any-hit 软光追）
 * - WriteBack：写回资源/关卡数据（未实现）
 */
#include "GPUPVS.h"
#include "Engine/World.h"
#include "GPUPVSScene.h"
#include "GPUPVSShaders.h"
#include "GPUPVSLog.h"
#include "GPUPVSBVH.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"

FGPUPVS::FGPUPVS(UWorld* InWorld)
    : World(InWorld), Scene()
{
    InitDefaultParams();
}

FGPUPVS::~FGPUPVS()
{
}

void FGPUPVS::ResetRasterResults()
{
	RasterGridSizeX = 0;
	RasterGridSizeY = 0;
	RasterMaxLayers = 0;
	RasterGridOrigin = FVector3f::ZeroVector;
	RasterCellSize = 0.0f;
	RasterLayerCount.Reset();
	RasterHeightRanges.Reset();
}

void FGPUPVS::InitDefaultParams()
{
	Params = FParams();
	SetParams(Params);

	ResetRasterResults();
}

void FGPUPVS::SetParams(const FParams& InParams)
{
	Params = InParams;

	Scene.SetCellSize(Params.CellSize);
	Scene.SetPlayAreaHeight(Params.PlayAreaHeight);
	Scene.SetPlaceCellsOnOpaqueOnly(Params.bPlaceCellsOnOpaqueOnly);

	RasterMaxLayersLimit = Params.RasterMaxLayersLimit;

	VisibilityMaxDistance = Params.VisibilityMaxDistance;
	VisibilityNumCellSamples = Params.VisibilityNumCellSamples;
	VisibilityMinMeshSamples = Params.VisibilityMinMeshSamples;
	VisibilityMaxMeshSamples = Params.VisibilityMaxMeshSamples;
	VisibilityNumImportanceSamples = Params.VisibilityNumImportanceSamples;
	VisibilityMeshBoundsScale = Params.VisibilityMeshBoundsScale;
	VisibilityBVHLeafTriLimit = Params.VisibilityBVHLeafTriLimit;

	VisibilityRayCount = VisibilityNumCellSamples * VisibilityMaxMeshSamples + VisibilityNumImportanceSamples;
}

void FGPUPVS::BuildPVS()
{
	if (!World)
	{
		return;
	}

	const double BuildStart = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Build] Start World=%s"), *World->GetName());
	GPUPVS_LOG(Log, TEXT("[Params] CellSize=%.2f PlayAreaHeight=%.2f PlaceCellsOnOpaqueOnly=%s MeshBoundsScale=%.2f NumCellSamples=%d MinMeshSamples=%d MaxMeshSamples=%d NumImportanceSamples=%d MaxDistance=%.2f"),
		Scene.GetSettings().CellSize,
		Scene.GetSettings().PlayAreaHeight,
		Scene.GetSettings().bPlaceCellsOnOpaqueOnly ? TEXT("true") : TEXT("false"),
		VisibilityMeshBoundsScale,
		VisibilityNumCellSamples,
		VisibilityMinMeshSamples,
		VisibilityMaxMeshSamples,
		VisibilityNumImportanceSamples,
		VisibilityMaxDistance);

	Scene.CollectFromWorld(World);
	SetupPrecomputedVisibility();
	ComputeVisibility();
	WriteBack();

	const double BuildEnd = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Build] End Time=%.3fs RasterReady=%s Grid=%ux%u MaxLayers=%u"),
		BuildEnd - BuildStart,
		(RasterLayerCount.Num() > 0 && RasterHeightRanges.Num() > 0) ? TEXT("true") : TEXT("false"),
		RasterGridSizeX,
		RasterGridSizeY,
		RasterMaxLayers);
}

void FGPUPVS::DumpSummary() const
{
	const int32 CellsCount = Scene.GetCells().Num();
	const int32 MeshesCount = Scene.GetVisibilityMeshBounds().Num();
	const int32 OccTriangles = Scene.GetOccluderIndices().Num() / 3;
	const int32 SetupTriangles = Scene.GetSceneIndices().Num() / 3;
	const int32 WordsPerCell = (int32)Scene.GetVisibilityWordsPerCell();
	const int32 TotalWords = Scene.GetVisibilityWords().Num();

	GPUPVS_LOG(Log, TEXT("[Summary] Cells=%d Meshes=%d Groups=%d MappedMeshes=%d SetupTriangles=%d OccluderTriangles=%d WordsPerCell=%d TotalWords=%d"),
		CellsCount,
		MeshesCount,
		LastGroupsBuilt,
		LastMappedMeshes,
		SetupTriangles,
		OccTriangles,
		WordsPerCell,
		TotalWords);

	if (CellsCount <= 0 || MeshesCount <= 0 || WordsPerCell <= 0 || TotalWords <= 0)
	{
		return;
	}

	const TArray<uint32>& Words = Scene.GetVisibilityWords();
	const uint32 NumMeshes = (uint32)MeshesCount;

	uint32 CellsWithAnyVisible = 0;
	uint32 MinVisibleMeshes = MAX_uint32;
	uint32 MaxVisibleMeshes = 0;
	uint64 TotalVisibleMeshes = 0;

	for (int32 CellIndex = 0; CellIndex < CellsCount; ++CellIndex)
	{
		const int32 Base = CellIndex * WordsPerCell;
		uint32 VisibleInCell = 0;
		for (int32 WordIndex = 0; WordIndex < WordsPerCell; ++WordIndex)
		{
			const uint32 W = Words[Base + WordIndex];
			if (W != 0)
			{
				VisibleInCell += (uint32)FPlatformMath::CountBits(W);
			}
		}
		if (VisibleInCell > 0)
		{
			CellsWithAnyVisible++;
		}
		MinVisibleMeshes = FMath::Min(MinVisibleMeshes, VisibleInCell);
		MaxVisibleMeshes = FMath::Max(MaxVisibleMeshes, VisibleInCell);
		TotalVisibleMeshes += (uint64)VisibleInCell;
	}

	if (MinVisibleMeshes == MAX_uint32)
	{
		MinVisibleMeshes = 0;
	}

	const double AvgVisible = CellsCount > 0 ? (double)TotalVisibleMeshes / (double)CellsCount : 0.0;
	GPUPVS_LOG(Log, TEXT("[Summary] VisibleStats CellsWithAny=%u/%d MinVisible=%u MaxVisible=%u AvgVisible=%.2f (Meshes=%u)"),
		CellsWithAnyVisible,
		CellsCount,
		MinVisibleMeshes,
		MaxVisibleMeshes,
		AvgVisible,
		NumMeshes);
}

void FGPUPVS::SetupPrecomputedVisibility()
{
	const float CellSize = Scene.GetSettings().CellSize;
	const float PlayAreaHeight = Scene.GetSettings().PlayAreaHeight;
	const double SetupStartTime = FPlatformTime::Seconds();
	ResetRasterResults();

    const FBox3f VisibilityVolumeBounds = Scene.GetAlignedVisibilityVolumeBounds();
	const FVector4f VolumeSizes = (FVector4f)VisibilityVolumeBounds.GetExtent() * 2.0f / CellSize;
	const int32 SizeX = FMath::TruncToInt(VolumeSizes.X + DELTA) + 1;
	const int32 SizeY = FMath::TruncToInt(VolumeSizes.Y + DELTA) + 1;

	const TArray<FVector3f>& SceneVertices = Scene.GetSceneVertices();
	const TArray<uint32>& SceneIndices = Scene.GetSceneIndices();
	const TArray<FPrecomputedVisibilityVolume>& Volumes = Scene.GetPrecomputedVisibilityVolumes();

	if (SceneVertices.IsEmpty() || SceneIndices.IsEmpty() || Volumes.IsEmpty() || SizeX <= 0 || SizeY <= 0)
	{
		GPUPVS_LOG(Warning, TEXT("[Setup] EarlyOut Vertices=%d Indices=%d Volumes=%d SizeX=%d SizeY=%d"),
			SceneVertices.Num(),
			SceneIndices.Num(),
			Volumes.Num(),
			SizeX,
			SizeY);
		return;
	}

	TArray<FVector3f> VerticesCopy = SceneVertices;
	TArray<uint32> IndicesCopy = SceneIndices;

	const FVector3f GridOrigin = VisibilityVolumeBounds.Min;
	const uint32 GridSizeX = (uint32)SizeX;
	const uint32 GridSizeY = (uint32)SizeY;
	const uint32 MaxLayers = (uint32)RasterMaxLayersLimit;

	GPUPVS_LOG(Log, TEXT("[Setup] BoundsMin=%s BoundsMax=%s CellSize=%.2f Grid=%ux%u MaxLayers=%u Vertices=%d Triangles=%d Volumes=%d"),
		*VisibilityVolumeBounds.Min.ToString(),
		*VisibilityVolumeBounds.Max.ToString(),
		CellSize,
		GridSizeX,
		GridSizeY,
		MaxLayers,
		SceneVertices.Num(),
		SceneIndices.Num() / 3,
		Volumes.Num());

	struct FRasterReadbackState
	{
		FRHIGPUBufferReadback* LayerCountReadback = nullptr;
		FRHIGPUBufferReadback* HeightRangesReadback = nullptr;
	};

	const TSharedRef<FRasterReadbackState, ESPMode::ThreadSafe> ReadbackState = MakeShared<FRasterReadbackState, ESPMode::ThreadSafe>();

	const double EnqueueStart = FPlatformTime::Seconds();
	ENQUEUE_RENDER_COMMAND(GPUPVS_RasterAABB)(
		[Vertices = MoveTemp(VerticesCopy),
		 Indices = MoveTemp(IndicesCopy),
		 GridOrigin,
		 CellSize,
		 GridSizeX,
		 GridSizeY,
		 MaxLayers,
		 ReadbackState](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const auto Outputs = FShaderDispatcher::DispatchRasterAABB(
				GraphBuilder,
				Vertices,
				Indices,
				GridOrigin,
				CellSize,
				GridSizeX,
				GridSizeY,
				MaxLayers);

			if (Outputs.LayerCount && Outputs.HeightRanges)
			{
				FRHIGPUBufferReadback* LayerCountReadback = new FRHIGPUBufferReadback(TEXT("GPUPVS.RasterAABB.LayerCount"));
				FRHIGPUBufferReadback* HeightRangesReadback = new FRHIGPUBufferReadback(TEXT("GPUPVS.RasterAABB.HeightRanges"));

				AddReadbackBufferPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPUPVS.Readback.LayerCount"),
					Outputs.LayerCount,
					[LayerCountReadback, LayerCountBuffer = Outputs.LayerCount](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						LayerCountReadback->EnqueueCopy(RHICmdList, LayerCountBuffer->GetRHI(), 0u);
					});

				AddReadbackBufferPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPUPVS.Readback.HeightRanges"),
					Outputs.HeightRanges,
					[HeightRangesReadback, HeightRangesBuffer = Outputs.HeightRanges](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						HeightRangesReadback->EnqueueCopy(RHICmdList, HeightRangesBuffer->GetRHI(), 0u);
					});

				ReadbackState->LayerCountReadback = LayerCountReadback;
				ReadbackState->HeightRangesReadback = HeightRangesReadback;
			}

			GraphBuilder.Execute();
		});

	FlushRenderingCommands();
	const double EnqueueEnd = FPlatformTime::Seconds();

	if (!ReadbackState->LayerCountReadback || !ReadbackState->HeightRangesReadback)
	{
		GPUPVS_LOG(Error, TEXT("[Setup] Readback objects not created."));
		return;
	}

	const double WaitStart = FPlatformTime::Seconds();
	while (!ReadbackState->LayerCountReadback->IsReady() || !ReadbackState->HeightRangesReadback->IsReady())
	{
		FPlatformProcess::Sleep(0.005f);
		if (FPlatformTime::Seconds() - WaitStart > 30.0)
		{
			FRHIGPUBufferReadback* LayerRB = ReadbackState->LayerCountReadback;
			FRHIGPUBufferReadback* HeightRB = ReadbackState->HeightRangesReadback;
			ENQUEUE_RENDER_COMMAND(GPUPVS_ReadbackTimeoutCleanup)(
				[LayerRB, HeightRB](FRHICommandListImmediate& RHICmdList)
				{
					delete LayerRB;
					delete HeightRB;
				});
			FlushRenderingCommands();
			ReadbackState->LayerCountReadback = nullptr;
			ReadbackState->HeightRangesReadback = nullptr;
			GPUPVS_LOG(Error, TEXT("[Setup] GPU readback timeout (>30s)."));
			return;
		}
	}
	const double WaitEnd = FPlatformTime::Seconds();

	TArray<uint32> LayerCountCPU;
	TArray<FVector2f> HeightRangesCPU;
	const uint32 PixelCount = GridSizeX * GridSizeY;
	const uint64 LayerCountBytes64 = uint64(PixelCount) * sizeof(uint32);
	const uint64 HeightRangesBytes64 = uint64(PixelCount) * uint64(MaxLayers) * sizeof(FVector2f);
	if (LayerCountBytes64 > MAX_uint32 || HeightRangesBytes64 > MAX_uint32)
	{
		FRHIGPUBufferReadback* LayerRB = ReadbackState->LayerCountReadback;
		FRHIGPUBufferReadback* HeightRB = ReadbackState->HeightRangesReadback;
		ENQUEUE_RENDER_COMMAND(GPUPVS_ReadbackTooLargeCleanup)(
			[LayerRB, HeightRB](FRHICommandListImmediate& RHICmdList)
			{
				delete LayerRB;
				delete HeightRB;
			});
		FlushRenderingCommands();
		ReadbackState->LayerCountReadback = nullptr;
		ReadbackState->HeightRangesReadback = nullptr;
		GPUPVS_LOG(Error, TEXT("[Setup] Readback size too large. PixelCount=%u MaxLayers=%u"), PixelCount, MaxLayers);
		return;
	}
	const uint32 LayerCountBytes = (uint32)LayerCountBytes64;
	const uint32 HeightRangesBytes = (uint32)HeightRangesBytes64;

	LayerCountCPU.SetNumUninitialized((int32)PixelCount);
	HeightRangesCPU.SetNumUninitialized((int32)(PixelCount * MaxLayers));

	FRHIGPUBufferReadback* LayerRB = ReadbackState->LayerCountReadback;
	FRHIGPUBufferReadback* HeightRB = ReadbackState->HeightRangesReadback;

	ENQUEUE_RENDER_COMMAND(GPUPVS_CopyReadback)(
		[LayerRB, HeightRB, LayerCountBytes, HeightRangesBytes, &LayerCountCPU, &HeightRangesCPU](FRHICommandListImmediate& RHICmdList)
		{
			if (void* LayerPtr = LayerRB->Lock(LayerCountBytes))
			{
				FMemory::Memcpy(LayerCountCPU.GetData(), LayerPtr, LayerCountBytes);
				LayerRB->Unlock();
			}

			if (void* HeightPtr = HeightRB->Lock(HeightRangesBytes))
			{
				FMemory::Memcpy(HeightRangesCPU.GetData(), HeightPtr, HeightRangesBytes);
				HeightRB->Unlock();
			}

			delete LayerRB;
			delete HeightRB;
		});

	FlushRenderingCommands();
	const double CopyEnd = FPlatformTime::Seconds();

	ReadbackState->LayerCountReadback = nullptr;
	ReadbackState->HeightRangesReadback = nullptr;

	RasterGridSizeX = GridSizeX;
	RasterGridSizeY = GridSizeY;
	RasterMaxLayers = MaxLayers;
	RasterGridOrigin = GridOrigin;
	RasterCellSize = CellSize;
	RasterLayerCount = MoveTemp(LayerCountCPU);
	RasterHeightRanges = MoveTemp(HeightRangesCPU);

	if (PlayAreaHeight <= 0.0f)
	{
		GPUPVS_LOG(Error, TEXT("[Setup] Invalid PlayAreaHeight=%.3f"), PlayAreaHeight);
		Scene.SetCells({});
		return;
	}

	{
		TArray<FPVSCell> NewCells;
		NewCells.Reserve((int32)(GridSizeX * GridSizeY * 2u));

		TArray<FVector2f> HitRanges;
		TArray<FVector2f> PlacedHeightRanges;
		HitRanges.Reserve((int32)MaxLayers);
		PlacedHeightRanges.Reserve((int32)MaxLayers * 2);

		for (uint32 Y = 0; Y < GridSizeY; ++Y)
		{
			for (uint32 X = 0; X < GridSizeX; ++X)
			{
				const uint32 PixelIndex = Y * GridSizeX + X;
				const uint32 LayerCount = RasterLayerCount[(int32)PixelIndex];
				const uint32 ClampedLayerCount = FMath::Min(LayerCount, MaxLayers);
				if (ClampedLayerCount == 0)
				{
					continue;
				}

				HitRanges.Reset((int32)ClampedLayerCount);
				for (uint32 LayerIndex = 0; LayerIndex < ClampedLayerCount; ++LayerIndex)
				{
					HitRanges.Add(RasterHeightRanges[(int32)(PixelIndex * MaxLayers + LayerIndex)]);
				}

				HitRanges.Sort([](const FVector2f& A, const FVector2f& B)
				{
					return A.Y < B.Y;
				});

				const FVector2f CurrentPosition(
					(float)RasterGridOrigin.X + ((float)X + 0.5f) * CellSize,
					(float)RasterGridOrigin.Y + ((float)Y + 0.5f) * CellSize);

				auto AddCellAtBottom = [&](float CellBottom)
				{
					FPVSCell NewCell;
					NewCell.Index = NewCells.Num();
					NewCell.Bounds = FBox3f(
						FVector3f(
							CurrentPosition.X - CellSize * 0.5f,
							CurrentPosition.Y - CellSize * 0.5f,
							CellBottom),
						FVector3f(
							CurrentPosition.X + CellSize * 0.5f,
							CurrentPosition.Y + CellSize * 0.5f,
							CellBottom + PlayAreaHeight));
					NewCell.Center = NewCell.Bounds.GetCenter();
					NewCells.Add(NewCell);
					PlacedHeightRanges.Add(FVector2f(NewCell.Bounds.Min.Z, NewCell.Bounds.Max.Z));
				};

				float LastSampleHeight = -FLT_MAX;
				PlacedHeightRanges.Reset();

				for (int32 HeightIndex = 0; HeightIndex < HitRanges.Num(); ++HeightIndex)
				{
					const float CurrentMaxHeight = HitRanges[HeightIndex].Y;

					if (HeightIndex + 1 == HitRanges.Num()
						|| ((HitRanges[HeightIndex + 1].Y - CurrentMaxHeight) > PlayAreaHeight
							&& CurrentMaxHeight - LastSampleHeight > PlayAreaHeight))
					{
						AddCellAtBottom(CurrentMaxHeight);
						LastSampleHeight = CurrentMaxHeight;
					}
				}

				const float TestHeights[3] = { 0.4f, 0.6f, 0.8f };

				for (int32 HeightIndex = 0; HeightIndex < HitRanges.Num() - 1; ++HeightIndex)
				{
					for (int32 ExtremaIndex = 0; ExtremaIndex < 2; ++ExtremaIndex)
					{
						const float CurrentMaxHeight = ExtremaIndex == 0 ? HitRanges[HeightIndex].X : HitRanges[HeightIndex].Y;
						const float CompareHeight = CurrentMaxHeight + 0.5f * PlayAreaHeight;

						for (int32 TestIndex = 0; TestIndex < UE_ARRAY_COUNT(TestHeights); ++TestIndex)
						{
							const float TestHeight = CurrentMaxHeight + TestHeights[TestIndex] * PlayAreaHeight;

							int32 ClosestCellInZIndex = INDEX_NONE;
							float ClosestCellInZDistance = FLT_MAX;
							bool bInsideCell = false;

							for (int32 PlacedIndex = 0; PlacedIndex < PlacedHeightRanges.Num(); ++PlacedIndex)
							{
								const FVector2f CellHeightRange = PlacedHeightRanges[PlacedIndex];

								if (TestHeight > CellHeightRange.X && TestHeight < CellHeightRange.Y)
								{
									bInsideCell = true;
									break;
								}

								const float AbsDistance = FMath::Min(
									FMath::Abs(CompareHeight - CellHeightRange.X),
									FMath::Abs(CompareHeight - CellHeightRange.Y));

								if (AbsDistance < ClosestCellInZDistance)
								{
									ClosestCellInZDistance = AbsDistance;
									ClosestCellInZIndex = PlacedIndex;
								}
							}

							if (!bInsideCell)
							{
								float DesiredCellBottom = CurrentMaxHeight;

								if (ClosestCellInZIndex != INDEX_NONE)
								{
									const FVector2f NearestCellHeightRange = PlacedHeightRanges[ClosestCellInZIndex];
									const float NearestCellCompareHeight = (NearestCellHeightRange.X + NearestCellHeightRange.Y) * 0.5f;

									if (CompareHeight < NearestCellCompareHeight)
									{
										DesiredCellBottom = FMath::Min(DesiredCellBottom, NearestCellHeightRange.X - PlayAreaHeight);
									}
									else if (CompareHeight > NearestCellCompareHeight)
									{
										DesiredCellBottom = FMath::Max(DesiredCellBottom, NearestCellHeightRange.Y);
									}
								}

								AddCellAtBottom(DesiredCellBottom);
							}
						}
					}
				}
			}
		}

		Scene.SetCells(MoveTemp(NewCells));
	}

	uint32 PixelsWithAnyLayer = 0;
	uint32 MaxLayerCountObserved = 0;
	uint32 PixelsOverMaxLayers = 0;
	int32 FirstHitPixel = INDEX_NONE;
	uint32 FirstHitLayers = 0;

	for (uint32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		const uint32 LayerCount = RasterLayerCount[(int32)PixelIndex];
		if (LayerCount > 0)
		{
			PixelsWithAnyLayer++;
			if (FirstHitPixel == INDEX_NONE)
			{
				FirstHitPixel = (int32)PixelIndex;
				FirstHitLayers = LayerCount;
			}
		}
		MaxLayerCountObserved = FMath::Max(MaxLayerCountObserved, LayerCount);
		if (LayerCount > MaxLayers)
		{
			PixelsOverMaxLayers++;
		}
	}

	if (FirstHitPixel != INDEX_NONE)
	{
		const FVector2f FirstRange = RasterHeightRanges[FirstHitPixel * (int32)MaxLayers + 0];
		GPUPVS_LOG(Verbose, TEXT("[Setup] FirstHit Pixel=%d Layers=%u Layer0Z=[%.3f, %.3f]"),
			FirstHitPixel,
			FirstHitLayers,
			FirstRange.X,
			FirstRange.Y);
	}

	const double SetupEndTime = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Setup] PixelCount=%u LayerCountBytes=%.2fKB HeightRangesBytes=%.2fKB AnyHitPixels=%u MaxLayerCountObserved=%u PixelsOverMaxLayers=%u"),
		PixelCount,
		(double)LayerCountBytes / 1024.0,
		(double)HeightRangesBytes / 1024.0,
		PixelsWithAnyLayer,
		MaxLayerCountObserved,
		PixelsOverMaxLayers);
	GPUPVS_LOG(Log, TEXT("[Setup] Cells=%d PlayAreaHeight=%.2f"), Scene.GetCells().Num(), PlayAreaHeight);
	GPUPVS_LOG(Log, TEXT("[Setup] Time Total=%.3fs Enqueue+Execute=%.3fs WaitGPU=%.3fs Copy=%.3fs"),
		SetupEndTime - SetupStartTime,
		EnqueueEnd - EnqueueStart,
		WaitEnd - WaitStart,
		CopyEnd - WaitEnd);
}

void FGPUPVS::ComputeVisibility()
{
	const TArray<FPVSCell>& Cells = Scene.GetCells();
	const TArray<FBox3f>& MeshBounds = Scene.GetVisibilityMeshBounds();
	const TArray<FVector3f>& OccVertices = Scene.GetOccluderVertices();
	const TArray<uint32>& OccIndices = Scene.GetOccluderIndices();

	if (Cells.IsEmpty() || MeshBounds.IsEmpty() || OccVertices.IsEmpty() || OccIndices.IsEmpty())
	{
		GPUPVS_LOG(Warning, TEXT("[Compute] EarlyOut Cells=%d Meshes=%d OccVertices=%d OccTriangles=%d"),
			Cells.Num(),
			MeshBounds.Num(),
			OccVertices.Num(),
			OccIndices.Num() / 3);
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const double BuildStartTime = FPlatformTime::Seconds();

	TArray<FGPUPVSBVHNode> BVHNodesCPU;
	TArray<uint32> BVHLeafTriIndicesCPU;
	FGPUPVSBVHBuilder::Build(
		OccVertices,
		OccIndices,
		VisibilityBVHLeafTriLimit,
		BVHNodesCPU,
		BVHLeafTriIndicesCPU);

	if (BVHNodesCPU.IsEmpty() || BVHLeafTriIndicesCPU.IsEmpty())
	{
		GPUPVS_LOG(Error, TEXT("[Compute] BVH build failed. Nodes=%d LeafTris=%d"),
			BVHNodesCPU.Num(),
			BVHLeafTriIndicesCPU.Num());
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const int32 BVHNodesCount = BVHNodesCPU.Num();
	const int32 BVHLeafTriCount = BVHLeafTriIndicesCPU.Num();
	const uint32 OccluderTriangleCount = (uint32)(OccIndices.Num() / 3);
	if ((uint32)BVHLeafTriCount != OccluderTriangleCount)
	{
		GPUPVS_LOG(Warning, TEXT("[Compute] BVH leaf triangle count mismatch. BVHLeafTris=%d OccluderTriangles=%u"),
			BVHLeafTriCount,
			OccluderTriangleCount);
	}

	TArray<FGPUPVSBox3f> CellsCPU;
	CellsCPU.Reserve(Cells.Num());
	for (const FPVSCell& Cell : Cells)
	{
		FGPUPVSBox3f B;
		B.Min = Cell.Bounds.Min;
		B.Max = Cell.Bounds.Max;
		CellsCPU.Add(B);
	}

	TArray<FGPUPVSBox3f> MeshesCPU;
	MeshesCPU.Reserve(MeshBounds.Num());
	for (const FBox3f& B0 : MeshBounds)
	{
		FGPUPVSBox3f B;
		B.Min = B0.Min;
		B.Max = B0.Max;
		MeshesCPU.Add(B);
	}

	auto BuildGroups = [&](TConstArrayView<FBox3f> InMeshBounds, TArray<FGPUPVSBox3f>& OutGroups, TArray<uint32>& OutMeshToGroup)
	{
		OutGroups.Reset();
		OutMeshToGroup.SetNumUninitialized(InMeshBounds.Num());
		for (int32 i = 0; i < OutMeshToGroup.Num(); ++i)
		{
			OutMeshToGroup[i] = 0xFFFFFFFFu;
		}

		if (InMeshBounds.Num() == 0)
		{
			return;
		}

		struct FSortInfo
		{
			FBox3f Bounds;
			float Distance = 0.0f;
		};

		TArray<FSortInfo> SortMeshes;
		SortMeshes.Reserve(InMeshBounds.Num());
		for (const FBox3f& B : InMeshBounds)
		{
			if (B.IsValid)
			{
				FSortInfo Info;
				Info.Bounds = B;
				SortMeshes.Add(Info);
			}
		}

		static const FVector3f CubeCorners[8] = {
			FVector3f(1, 1, 1),
			FVector3f(-1, 1, 1),
			FVector3f(1, -1, 1),
			FVector3f(-1, -1, 1),
			FVector3f(1, 1, -1),
			FVector3f(-1, 1, -1),
			FVector3f(1, -1, -1),
			FVector3f(-1, -1, -1),
		};

		for (FSortInfo& M : SortMeshes)
		{
			const FVector3f BoxCenter = M.Bounds.GetCenter();
			const FVector3f BoxExtent = M.Bounds.GetExtent();
			float LocalDistance = 0.0f;
			for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
			{
				const FVector3f Corner = BoxCenter + BoxExtent * CubeCorners[CornerIndex];
				LocalDistance = FMath::Max(LocalDistance, Corner.SizeSquared());
			}
			M.Distance = LocalDistance;
		}

		SortMeshes.Sort([](const FSortInfo& A, const FSortInfo& B)
		{
			return A.Distance < B.Distance;
		});

		FBox3f VisibilityGridBounds(ForceInit);
		const int32 MaxMeshIndex = FMath::Min(FMath::Max(FMath::TruncToInt(0.9f * SortMeshes.Num()), 1), SortMeshes.Num() - 1);
		for (int32 i = 0; i < MaxMeshIndex; ++i)
		{
			VisibilityGridBounds += SortMeshes[i].Bounds;
		}

		const float TargetNumGroupsAsFractionOfMeshes = 0.3f;
		const int32 ZDimensionDivisor = 4;
		const int32 GridXY = FMath::Max(FMath::TruncToInt(FMath::Pow((float)ZDimensionDivisor * TargetNumGroupsAsFractionOfMeshes * (float)SortMeshes.Num(), 1.0f / 3.0f)), 1);
		const int32 GridZ = FMath::Max(GridXY / ZDimensionDivisor, 1);

		const FVector3f Denom = FVector3f(
			FMath::Max(VisibilityGridBounds.Max.X - VisibilityGridBounds.Min.X, 1.0f),
			FMath::Max(VisibilityGridBounds.Max.Y - VisibilityGridBounds.Min.Y, 1.0f),
			FMath::Max(VisibilityGridBounds.Max.Z - VisibilityGridBounds.Min.Z, 1.0f));

		const FVector3f CellSize = VisibilityGridBounds.GetExtent() / FVector3f((float)GridXY, (float)GridXY, (float)GridZ);
		const float GridCellBoundingRadius = CellSize.Size();
		const float MeshGroupingCellRadiusThreshold = 0.5f;

		TArray<int32> GroupGrid;
		GroupGrid.Init(-1, GridXY * GridXY * GridZ);

		TArray<FBox3f> GroupBoundsBoxes;

		auto GetGroupCellIndex = [&](const FVector3f& BoxCenter) -> int32
		{
			const FVector3f GridPosition = FVector3f((float)GridXY, (float)GridXY, (float)GridZ) * (BoxCenter - VisibilityGridBounds.Min) / Denom;
			const int32 X = FMath::TruncToInt(GridPosition.X);
			const int32 Y = FMath::TruncToInt(GridPosition.Y);
			const int32 Z = FMath::TruncToInt(GridPosition.Z);
			if (X < 0 || Y < 0 || Z < 0 || X >= GridXY || Y >= GridXY || Z >= GridZ)
			{
				return -1;
			}
			return (Z * GridXY + Y) * GridXY + X;
		};

		for (int32 MeshIndex = 0; MeshIndex < InMeshBounds.Num(); ++MeshIndex)
		{
			const FBox3f& MeshB = InMeshBounds[MeshIndex];
			if (!MeshB.IsValid)
			{
				continue;
			}

			const float MeshBoundingRadiusSqr = MeshB.GetExtent().SizeSquared();
			const bool bPutInGroup = MeshBoundingRadiusSqr < FMath::Square(GridCellBoundingRadius * MeshGroupingCellRadiusThreshold);
			if (!bPutInGroup)
			{
				continue;
			}

			const int32 CellIndex = GetGroupCellIndex(MeshB.GetCenter());
			if (CellIndex < 0)
			{
				continue;
			}

			int32 GroupIndex = GroupGrid[CellIndex];
			if (GroupIndex == -1)
			{
				GroupIndex = GroupBoundsBoxes.Num();
				GroupGrid[CellIndex] = GroupIndex;
				GroupBoundsBoxes.Add(FBox3f(ForceInit));
			}

			GroupBoundsBoxes[GroupIndex] += MeshB;
			OutMeshToGroup[MeshIndex] = (uint32)GroupIndex;
		}

		OutGroups.Reserve(GroupBoundsBoxes.Num());
		TArray<int32> OldToNew;
		OldToNew.Init(-1, GroupBoundsBoxes.Num());

		for (int32 OldIndex = 0; OldIndex < GroupBoundsBoxes.Num(); ++OldIndex)
		{
			const FBox3f& GB = GroupBoundsBoxes[OldIndex];
			if (!GB.IsValid)
			{
				continue;
			}
			FGPUPVSBox3f OutB;
			OutB.Min = GB.Min;
			OutB.Max = GB.Max;
			OldToNew[OldIndex] = OutGroups.Num();
			OutGroups.Add(OutB);
		}

		for (int32 MeshIndex = 0; MeshIndex < OutMeshToGroup.Num(); ++MeshIndex)
		{
			const uint32 OldGroup = OutMeshToGroup[MeshIndex];
			if (OldGroup == 0xFFFFFFFFu)
			{
				continue;
			}
			const int32 NewGroup = (OldGroup < (uint32)OldToNew.Num()) ? OldToNew[(int32)OldGroup] : -1;
			OutMeshToGroup[MeshIndex] = NewGroup >= 0 ? (uint32)NewGroup : 0xFFFFFFFFu;
		}
	};

	TArray<FGPUPVSBox3f> GroupsCPU;
	TArray<uint32> MeshToGroupCPU;
	BuildGroups(MeshBounds, GroupsCPU, MeshToGroupCPU);
	int32 NumMappedMeshes = 0;
	for (uint32 G : MeshToGroupCPU)
	{
		if (G != 0xFFFFFFFFu)
		{
			NumMappedMeshes++;
		}
	}
	LastGroupsBuilt = GroupsCPU.Num();
	LastMappedMeshes = NumMappedMeshes;
	GPUPVS_LOG(Log, TEXT("[Compute] GroupsBuilt=%d Meshes=%d MappedMeshes=%d"),
		GroupsCPU.Num(),
		MeshBounds.Num(),
		NumMappedMeshes);

	TArray<FGPUPVSBVHNodeGPU> BVHNodesGPU;
	BVHNodesGPU.Reserve(BVHNodesCPU.Num());
	for (const FGPUPVSBVHNode& N0 : BVHNodesCPU)
	{
		FGPUPVSBVHNodeGPU N;
		N.BoundsMin = N0.BoundsMin;
		N.BoundsMax = N0.BoundsMax;
		N.LeftChildOrFirstTri = N0.LeftChildOrFirstTri;
		N.RightChildOrTriCount = N0.RightChildOrTriCount;
		BVHNodesGPU.Add(N);
	}

	const uint32 NumMeshes = (uint32)MeshesCPU.Num();
	const uint32 WordsPerCell = FMath::DivideAndRoundUp(NumMeshes, 32u);
	const uint32 TotalWords = (uint32)CellsCPU.Num() * WordsPerCell;
	const uint32 NumGroups = (uint32)GroupsCPU.Num();

	struct FVisibilityReadbackState
	{
		FRHIGPUBufferReadback* WordsReadback = nullptr;
		FRHIGPUBufferReadback* StatsReadback = nullptr;
		FRHIGPUBufferReadback* GroupStatsReadback = nullptr;
		uint32 NumMeshes = 0;
		uint32 WordsPerCell = 0;
		uint32 TotalWords = 0;
		uint32 NumGroups = 0;
		uint32 GroupWordsPerCell = 0;
	};

	const TSharedRef<FVisibilityReadbackState, ESPMode::ThreadSafe> ReadbackState = MakeShared<FVisibilityReadbackState, ESPMode::ThreadSafe>();
	ReadbackState->NumMeshes = NumMeshes;
	ReadbackState->WordsPerCell = WordsPerCell;
	ReadbackState->TotalWords = TotalWords;
	ReadbackState->NumGroups = NumGroups;

	ENQUEUE_RENDER_COMMAND(GPUPVS_Visibility)(
		[CellsData = MoveTemp(CellsCPU),
		 MeshesData = MoveTemp(MeshesCPU),
		 GroupsData = MoveTemp(GroupsCPU),
		 MeshToGroupData = MoveTemp(MeshToGroupCPU),
		 NodesData = MoveTemp(BVHNodesGPU),
		 LeafTrisData = MoveTemp(BVHLeafTriIndicesCPU),
		 OccVerticesCopy = OccVertices,
		 OccIndicesCopy = OccIndices,
		 NumCellSamples = (uint32)FMath::Max(0, VisibilityNumCellSamples),
		 MinMeshSamples = (uint32)FMath::Max(1, VisibilityMinMeshSamples),
		 MaxMeshSamples = (uint32)FMath::Max(1, VisibilityMaxMeshSamples),
		 NumImportanceSamples = (uint32)FMath::Max(0, VisibilityNumImportanceSamples),
		 MeshBoundsScale = VisibilityMeshBoundsScale,
		 MaxDistance = VisibilityMaxDistance,
		 ReadbackState](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			const auto GroupOutputs = FShaderDispatcher::DispatchVisibilityGroups(
				GraphBuilder,
				CellsData,
				GroupsData,
				NodesData,
				LeafTrisData,
				OccVerticesCopy,
				OccIndicesCopy,
				NumCellSamples,
				MinMeshSamples,
				MaxMeshSamples,
				NumImportanceSamples,
				MeshBoundsScale,
				MaxDistance);

			ReadbackState->GroupWordsPerCell = GroupOutputs.GroupWordsPerCell;

			const auto Outputs = FShaderDispatcher::DispatchVisibilityMeshesWithGroups(
				GraphBuilder,
				CellsData,
				MeshesData,
				MeshToGroupData,
				GroupOutputs.GroupVisibilityWords,
				GroupOutputs.NumGroups,
				GroupOutputs.GroupWordsPerCell,
				NodesData,
				LeafTrisData,
				OccVerticesCopy,
				OccIndicesCopy,
				NumCellSamples,
				MinMeshSamples,
				MaxMeshSamples,
				NumImportanceSamples,
				MeshBoundsScale,
				MaxDistance);

			if (Outputs.VisibilityWords)
			{
				FRHIGPUBufferReadback* WordsReadback = new FRHIGPUBufferReadback(TEXT("GPUPVS.Visibility.Words"));
				AddReadbackBufferPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPUPVS.Readback.VisibilityWords"),
					Outputs.VisibilityWords,
					[WordsReadback, WordsBuffer = Outputs.VisibilityWords](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						WordsReadback->EnqueueCopy(RHICmdList, WordsBuffer->GetRHI(), 0u);
					});

				ReadbackState->WordsReadback = WordsReadback;
			}

			if (Outputs.Stats)
			{
				FRHIGPUBufferReadback* StatsReadback = new FRHIGPUBufferReadback(TEXT("GPUPVS.Visibility.Stats"));
				AddReadbackBufferPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPUPVS.Readback.VisibilityStats"),
					Outputs.Stats,
					[StatsReadback, StatsBuffer = Outputs.Stats](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						StatsReadback->EnqueueCopy(RHICmdList, StatsBuffer->GetRHI(), 0u);
					});
				ReadbackState->StatsReadback = StatsReadback;
			}

			if (GroupOutputs.Stats)
			{
				FRHIGPUBufferReadback* GroupStatsReadback = new FRHIGPUBufferReadback(TEXT("GPUPVS.Visibility.GroupStats"));
				AddReadbackBufferPass(
					GraphBuilder,
					RDG_EVENT_NAME("GPUPVS.Readback.VisibilityGroupStats"),
					GroupOutputs.Stats,
					[GroupStatsReadback, StatsBuffer = GroupOutputs.Stats](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						GroupStatsReadback->EnqueueCopy(RHICmdList, StatsBuffer->GetRHI(), 0u);
					});
				ReadbackState->GroupStatsReadback = GroupStatsReadback;
			}

			GraphBuilder.Execute();
		});

	FlushRenderingCommands();

	if (!ReadbackState->WordsReadback || !ReadbackState->StatsReadback || !ReadbackState->GroupStatsReadback)
	{
		GPUPVS_LOG(Error, TEXT("[Compute] Readback object not created."));
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const double WaitStart = FPlatformTime::Seconds();
	while (!ReadbackState->WordsReadback->IsReady() || !ReadbackState->StatsReadback->IsReady() || !ReadbackState->GroupStatsReadback->IsReady())
	{
		FPlatformProcess::Sleep(0.005f);
		if (FPlatformTime::Seconds() - WaitStart > 60.0)
		{
			FRHIGPUBufferReadback* RB = ReadbackState->WordsReadback;
			FRHIGPUBufferReadback* StatsRB = ReadbackState->StatsReadback;
			FRHIGPUBufferReadback* GroupStatsRB = ReadbackState->GroupStatsReadback;
			ENQUEUE_RENDER_COMMAND(GPUPVS_VisibilityTimeoutCleanup)(
				[RB, StatsRB, GroupStatsRB](FRHICommandListImmediate& RHICmdList)
				{
					delete RB;
					delete StatsRB;
					delete GroupStatsRB;
				});
			FlushRenderingCommands();
			ReadbackState->WordsReadback = nullptr;
			ReadbackState->StatsReadback = nullptr;
			ReadbackState->GroupStatsReadback = nullptr;
			GPUPVS_LOG(Error, TEXT("[Compute] GPU readback timeout (>60s)."));
			Scene.SetVisibilityWords({}, 0, 0);
			return;
		}
	}

	TArray<uint32> WordsCPU;
	WordsCPU.SetNumUninitialized((int32)ReadbackState->TotalWords);
	const uint64 Bytes64 = uint64(ReadbackState->TotalWords) * sizeof(uint32);
	const uint32 Bytes = (Bytes64 > MAX_uint32) ? 0u : (uint32)Bytes64;
	if (Bytes == 0u)
	{
		FRHIGPUBufferReadback* RB = ReadbackState->WordsReadback;
		ENQUEUE_RENDER_COMMAND(GPUPVS_VisibilityTooLargeCleanup)(
			[RB](FRHICommandListImmediate& RHICmdList)
			{
				delete RB;
			});
		FlushRenderingCommands();
		ReadbackState->WordsReadback = nullptr;
		GPUPVS_LOG(Error, TEXT("[Compute] Readback size too large. TotalWords=%u"), ReadbackState->TotalWords);
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	FRHIGPUBufferReadback* RB = ReadbackState->WordsReadback;
	FRHIGPUBufferReadback* StatsRB = ReadbackState->StatsReadback;
	FRHIGPUBufferReadback* GroupStatsRB = ReadbackState->GroupStatsReadback;
	TArray<uint32> GPUStats;
	GPUStats.SetNumZeroed(8);
	TArray<uint32> GroupGPUStats;
	GroupGPUStats.SetNumZeroed(8);
	ENQUEUE_RENDER_COMMAND(GPUPVS_CopyVisibilityReadback)(
		[RB, StatsRB, GroupStatsRB, Bytes, &WordsCPU, &GPUStats, &GroupGPUStats](FRHICommandListImmediate& RHICmdList)
		{
			if (void* Ptr = RB->Lock(Bytes))
			{
				FMemory::Memcpy(WordsCPU.GetData(), Ptr, Bytes);
				RB->Unlock();
			}
			const uint32 StatsBytes = (uint32)(GPUStats.Num() * sizeof(uint32));
			if (void* StatsPtr = StatsRB->Lock(StatsBytes))
			{
				FMemory::Memcpy(GPUStats.GetData(), StatsPtr, StatsBytes);
				StatsRB->Unlock();
			}
			const uint32 GroupStatsBytes = (uint32)(GroupGPUStats.Num() * sizeof(uint32));
			if (void* GroupStatsPtr = GroupStatsRB->Lock(GroupStatsBytes))
			{
				FMemory::Memcpy(GroupGPUStats.GetData(), GroupStatsPtr, GroupStatsBytes);
				GroupStatsRB->Unlock();
			}
			delete RB;
			delete StatsRB;
			delete GroupStatsRB;
		});

	FlushRenderingCommands();
	ReadbackState->WordsReadback = nullptr;
	ReadbackState->StatsReadback = nullptr;
	ReadbackState->GroupStatsReadback = nullptr;

	uint32 NonZeroWords = 0;
	uint32 CellsWithAnyVisible = 0;
	uint32 MinVisibleMeshes = MAX_uint32;
	uint32 MaxVisibleMeshes = 0;
	uint64 TotalVisibleMeshes = 0;
	int32 FirstNonZeroCell = INDEX_NONE;

	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const uint32 BaseWord = (uint32)CellIndex * ReadbackState->WordsPerCell;
		uint32 VisibleInCell = 0;
		for (uint32 WordIndex = 0; WordIndex < ReadbackState->WordsPerCell; ++WordIndex)
		{
			const uint32 W = WordsCPU[(int32)(BaseWord + WordIndex)];
			if (W != 0)
			{
				NonZeroWords++;
				VisibleInCell += (uint32)FPlatformMath::CountBits(W);
			}
		}

		if (VisibleInCell > 0)
		{
			CellsWithAnyVisible++;
			if (FirstNonZeroCell == INDEX_NONE)
			{
				FirstNonZeroCell = CellIndex;
			}
		}

		MinVisibleMeshes = FMath::Min(MinVisibleMeshes, VisibleInCell);
		MaxVisibleMeshes = FMath::Max(MaxVisibleMeshes, VisibleInCell);
		TotalVisibleMeshes += (uint64)VisibleInCell;
	}

	if (Cells.IsEmpty())
	{
		MinVisibleMeshes = 0;
	}
	else if (MinVisibleMeshes == MAX_uint32)
	{
		MinVisibleMeshes = 0;
	}

	if (FirstNonZeroCell != INDEX_NONE)
	{
		const uint32 BaseWord = (uint32)FirstNonZeroCell * ReadbackState->WordsPerCell;
		const uint32 W0 = WordsCPU[(int32)(BaseWord + 0)];
		const uint32 W1 = ReadbackState->WordsPerCell > 1 ? WordsCPU[(int32)(BaseWord + 1)] : 0u;
		const uint32 W2 = ReadbackState->WordsPerCell > 2 ? WordsCPU[(int32)(BaseWord + 2)] : 0u;
		const uint32 W3 = ReadbackState->WordsPerCell > 3 ? WordsCPU[(int32)(BaseWord + 3)] : 0u;
		GPUPVS_LOG(Verbose, TEXT("[Compute] FirstNonZero Cell=%d Words[0..3]=0x%08X 0x%08X 0x%08X 0x%08X"),
			FirstNonZeroCell,
			W0, W1, W2, W3);
	}

	Scene.SetVisibilityWords(MoveTemp(WordsCPU), ReadbackState->NumMeshes, ReadbackState->WordsPerCell);

	const double BuildEndTime = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Compute] BVHNodes=%d BVHLeafTris=%d Cells=%d Meshes=%u Groups=%u WordsPerCell=%u TotalWords=%u Time=%.3fs"),
		BVHNodesCount,
		BVHLeafTriCount,
		Cells.Num(),
		ReadbackState->NumMeshes,
		ReadbackState->NumGroups,
		ReadbackState->WordsPerCell,
		ReadbackState->TotalWords,
		BuildEndTime - BuildStartTime);

	const double AvgVisibleMeshes = Cells.Num() > 0 ? (double)TotalVisibleMeshes / (double)Cells.Num() : 0.0;
	GPUPVS_LOG(Log, TEXT("[Compute] VisibleStats CellsWithAny=%u/%d MinVisible=%u MaxVisible=%u AvgVisible=%.2f NonZeroWords=%u/%u"),
		CellsWithAnyVisible,
		Cells.Num(),
		MinVisibleMeshes,
		MaxVisibleMeshes,
		AvgVisibleMeshes,
		NonZeroWords,
		ReadbackState->TotalWords);

	if (GPUStats.Num() == 8)
	{
		const uint32 Pairs = GPUStats[0];
		const uint32 VisiblePairs = GPUStats[1];
		const uint32 DistanceCulled = GPUStats[2];
		const uint32 SizeRatioVisible = GPUStats[3];
		const uint32 GroupSkipped = GPUStats[4];
		const uint32 VisibleExplicit = GPUStats[5];
		const uint32 VisibleImportance = GPUStats[6];
		const uint32 Rays = GPUStats[7];
		const double AvgRaysPerPair = Pairs > 0 ? (double)Rays / (double)Pairs : 0.0;
		GPUPVS_LOG(Log, TEXT("[Compute] GPUStats(Mesh) Pairs=%u VisiblePairs=%u DistanceCulled=%u SizeRatioVisible=%u GroupSkipped=%u VisibleExplicit=%u VisibleImportance=%u Rays=%u AvgRaysPerPair=%.2f"),
			Pairs,
			VisiblePairs,
			DistanceCulled,
			SizeRatioVisible,
			GroupSkipped,
			VisibleExplicit,
			VisibleImportance,
			Rays,
			AvgRaysPerPair);
	}

	if (GroupGPUStats.Num() == 8)
	{
		const uint32 Pairs = GroupGPUStats[0];
		const uint32 VisiblePairs = GroupGPUStats[1];
		const uint32 DistanceCulled = GroupGPUStats[2];
		const uint32 SizeRatioVisible = GroupGPUStats[3];
		const uint32 Rays = GroupGPUStats[7];
		const double AvgRaysPerPair = Pairs > 0 ? (double)Rays / (double)Pairs : 0.0;
		GPUPVS_LOG(Log, TEXT("[Compute] GPUStats(Group) Pairs=%u VisiblePairs=%u DistanceCulled=%u SizeRatioVisible=%u Rays=%u AvgRaysPerPair=%.2f"),
			Pairs,
			VisiblePairs,
			DistanceCulled,
			SizeRatioVisible,
			Rays,
			AvgRaysPerPair);
	}
}

void FGPUPVS::WriteBack()
{
}
