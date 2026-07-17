/**
 * GPUPVS 主流程实现。
 *
 * 流水线：
 * - Collect / Setup / Compute / WriteBack
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

#if WITH_EDITOR
#endif

FGPUPVS::FGPUPVS(UWorld* InWorld)
    : World(InWorld), Scene()
{
    InitDefaultParams();
}

FGPUPVS::~FGPUPVS()
{
}

// -----------------------------------------------------------------------------
// Core State
// -----------------------------------------------------------------------------

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
	SetupParams = Params.Setup;
	ComputeParams = Params.Compute;
	WriteBackParams = Params.WriteBack;

	Scene.SetCellSize(Params.CellSize);
	Scene.SetPlayAreaHeight(Params.PlayAreaHeight);
	Scene.SetPlaceCellsOnOpaqueOnly(Params.bPlaceCellsOnOpaqueOnly);
}

// -----------------------------------------------------------------------------
// Build Pipeline
// -----------------------------------------------------------------------------

void FGPUPVS::BuildPVS()
{
	if (!World)
	{
		return;
	}

	const double BuildStart = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Build] Start World=%s"), *World->GetName());
	// 打印最终生效参数
	GPUPVS_LOG(Log, TEXT("[Params] CellSize=%.2f PlayAreaHeight=%.2f PlaceCellsOnOpaqueOnly=%s MaxLayers=%d MergeEpsilon=%.2f MaxCellsPerColumn=%d CellBottomOffset=%.2f MeshBoundsScale=%.2f NumCellSamples=%d MinMeshSamples=%d MaxMeshSamples=%d NumImportanceSamples=%d MaxDistance=%.2f WriteBackPreview=%s EngineWriteBack=%s"),
		Scene.GetSettings().CellSize,
		Scene.GetSettings().PlayAreaHeight,
		Scene.GetSettings().bPlaceCellsOnOpaqueOnly ? TEXT("true") : TEXT("false"),
		SetupParams.RasterMaxLayersLimit,
		SetupParams.RangeMergeEpsilon,
		SetupParams.MaxCellsPerColumn,
		SetupParams.CellBottomOffset,
		ComputeParams.MeshBoundsScale,
		ComputeParams.NumCellSamples,
		ComputeParams.MinMeshSamples,
		ComputeParams.MaxMeshSamples,
		ComputeParams.NumImportanceSamples,
		ComputeParams.MaxDistance,
		WriteBackParams.bEnablePreviewLog ? TEXT("true") : TEXT("false"),
		WriteBackParams.bEnableEngineWriteBack ? TEXT("true") : TEXT("false"));

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

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

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
	const uint32 MaxLayers = (uint32)SetupParams.RasterMaxLayersLimit;

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

	// 等待 GPU 完成
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
		FPlatformProcess::Sleep(FMath::Max(SetupParams.RasterReadbackPollSeconds, 0.0f));
		// 超时保护：避免 GPU hang 卡死
		if (FPlatformTime::Seconds() - WaitStart > FMath::Max(SetupParams.RasterReadbackTimeoutSeconds, 0.0f))
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
		TArray<FVector2f> MergedHitRanges;
		TArray<FVector2f> PlacedHeightRanges;
		HitRanges.Reserve((int32)MaxLayers);
		MergedHitRanges.Reserve((int32)MaxLayers);
		PlacedHeightRanges.Reserve((int32)MaxLayers * 2);
		const float MergeEpsilon = SetupParams.RangeMergeEpsilon;
		const int32 MaxCellsPerColumn = FMath::Max(1, SetupParams.MaxCellsPerColumn);
		const float CellBottomOffset = SetupParams.CellBottomOffset;
		const float TestHeights[3] =
		{
			SetupParams.PlacementProbeHeightA,
			SetupParams.PlacementProbeHeightB,
			SetupParams.PlacementProbeHeightC
		};

		uint32 PixelsWithMergedRanges = 0;
		uint32 MaxMergedRangeCountObserved = 0;

		for (uint32 Y = 0; Y < GridSizeY; ++Y)
		{
			for (uint32 X = 0; X < GridSizeX; ++X)
			{
				// 多层高度区间 -> 放置多个 cells
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
					return A.X < B.X;
				});

				// Raster 阶段输出的是“每个三角形在该像素上的高度段”，不是已经合并好的真实垂直层。
				// 这里先把重叠/相邻的高度段并起来，再按合并后的层生成 cells。
				MergedHitRanges.Reset();
				for (const FVector2f& Range : HitRanges)
				{
					if (MergedHitRanges.IsEmpty())
					{
						MergedHitRanges.Add(Range);
						continue;
					}

					FVector2f& LastRange = MergedHitRanges.Last();
					if (Range.X <= LastRange.Y + MergeEpsilon)
					{
						LastRange.Y = FMath::Max(LastRange.Y, Range.Y);
					}
					else
					{
						MergedHitRanges.Add(Range);
					}
				}

				if (MergedHitRanges.Num() > 1)
				{
					PixelsWithMergedRanges++;
				}
				MaxMergedRangeCountObserved = FMath::Max(MaxMergedRangeCountObserved, (uint32)MergedHitRanges.Num());

				const FVector2f CurrentPosition(
					(float)RasterGridOrigin.X + ((float)X + 0.5f) * CellSize,
					(float)RasterGridOrigin.Y + ((float)Y + 0.5f) * CellSize);

				auto AddCellAtBottom = [&](float CellBottom)
				{
					if (PlacedHeightRanges.Num() >= MaxCellsPerColumn)
					{
						return;
					}

					const float FinalCellBottom = CellBottom + CellBottomOffset;
					FPVSCell NewCell;
					NewCell.Index = NewCells.Num();
					NewCell.Bounds = FBox3f(
						FVector3f(
							CurrentPosition.X - CellSize * 0.5f,
							CurrentPosition.Y - CellSize * 0.5f,
							FinalCellBottom),
						FVector3f(
							CurrentPosition.X + CellSize * 0.5f,
							CurrentPosition.Y + CellSize * 0.5f,
							FinalCellBottom + PlayAreaHeight));
					NewCell.Center = NewCell.Bounds.GetCenter();
					NewCells.Add(NewCell);
					PlacedHeightRanges.Add(FVector2f(NewCell.Bounds.Min.Z, NewCell.Bounds.Max.Z));
				};

				float LastSampleHeight = -FLT_MAX;
				PlacedHeightRanges.Reset();

				for (int32 HeightIndex = 0; HeightIndex < MergedHitRanges.Num(); ++HeightIndex)
				{
					const float CurrentMaxHeight = MergedHitRanges[HeightIndex].Y;

					if (HeightIndex + 1 == MergedHitRanges.Num()
						|| ((MergedHitRanges[HeightIndex + 1].Y - CurrentMaxHeight) > PlayAreaHeight
							&& CurrentMaxHeight - LastSampleHeight > PlayAreaHeight))
					{
						AddCellAtBottom(CurrentMaxHeight);
						LastSampleHeight = CurrentMaxHeight;
					}
				}

				for (int32 HeightIndex = 0; HeightIndex < MergedHitRanges.Num() - 1; ++HeightIndex)
				{
					for (int32 ExtremaIndex = 0; ExtremaIndex < 2; ++ExtremaIndex)
					{
						const float CurrentMaxHeight = ExtremaIndex == 0 ? MergedHitRanges[HeightIndex].X : MergedHitRanges[HeightIndex].Y;
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

							if (!bInsideCell && PlacedHeightRanges.Num() < MaxCellsPerColumn)
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

		GPUPVS_LOG(Log, TEXT("[Setup] MergedRanges PixelsWithMulti=%u/%u MaxMergedRanges=%u"),
			PixelsWithMergedRanges,
			PixelCount,
			MaxMergedRangeCountObserved);
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
	/**
	 * Compute：
	 * - Group pass：粗可见性（输出 group bitset）
	 * - Mesh pass：细可见性（依赖 group bitset 做快速跳过，输出 mesh bitset）
	 */
	const TArray<FPVSCell>& CellList = Scene.GetCells();
	const TArray<FBox3f>& VisibilityMeshBounds = Scene.GetVisibilityMeshBounds();
	const TArray<FVector3f>& OccluderVertices = Scene.GetOccluderVertices();
	const TArray<uint32>& OccluderIndices = Scene.GetOccluderIndices();
	const TArray<int32>& OccluderTriangleTargetIds = Scene.GetOccluderTriangleTargetIds();

	if (CellList.IsEmpty() || VisibilityMeshBounds.IsEmpty() || OccluderVertices.IsEmpty() || OccluderIndices.IsEmpty())
	{
		GPUPVS_LOG(Warning, TEXT("[Compute] EarlyOut Cells=%d Meshes=%d OccVertices=%d OccTriangles=%d"),
			CellList.Num(),
			VisibilityMeshBounds.Num(),
			OccluderVertices.Num(),
			OccluderIndices.Num() / 3);
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const double BuildStartTime = FPlatformTime::Seconds();

	TArray<FGPUPVSBVHNode> BvhNodes;
	TArray<uint32> BvhLeafTriIndices;
	FGPUPVSBVHBuilder::Build(
		OccluderVertices,
		OccluderIndices,
		ComputeParams.BVHLeafTriLimit,
		BvhNodes,
		BvhLeafTriIndices);

	if (BvhNodes.IsEmpty() || BvhLeafTriIndices.IsEmpty())
	{
		GPUPVS_LOG(Error, TEXT("[Compute] BVH build failed. Nodes=%d LeafTris=%d"),
			BvhNodes.Num(),
			BvhLeafTriIndices.Num());
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const int32 BVHNodesCount = BvhNodes.Num();
	const int32 BVHLeafTriCount = BvhLeafTriIndices.Num();
	const uint32 OccluderTriangleCount = (uint32)(OccluderIndices.Num() / 3);
	if ((uint32)BVHLeafTriCount != OccluderTriangleCount)
	{
		GPUPVS_LOG(Warning, TEXT("[Compute] BVH leaf triangle count mismatch. BVHLeafTris=%d OccluderTriangles=%u"),
			BVHLeafTriCount,
			OccluderTriangleCount);
	}
	if ((uint32)OccluderTriangleTargetIds.Num() != OccluderTriangleCount)
	{
		GPUPVS_LOG(Error, TEXT("[Compute] OccluderTriangleTargetIds mismatch. Owners=%d OccluderTriangles=%u"),
			OccluderTriangleTargetIds.Num(),
			OccluderTriangleCount);
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	TArray<FGPUPVSBox3f> CellBoxes;
	CellBoxes.Reserve(CellList.Num());
	for (const FPVSCell& Cell : CellList)
	{
		FGPUPVSBox3f B;
		B.Min = Cell.Bounds.Min;
		B.Max = Cell.Bounds.Max;
		CellBoxes.Add(B);
	}

	TArray<FGPUPVSBox3f> MeshBoxes;
	MeshBoxes.Reserve(VisibilityMeshBounds.Num());
	for (const FBox3f& B0 : VisibilityMeshBounds)
	{
		FGPUPVSBox3f B;
		B.Min = B0.Min;
		B.Max = B0.Max;
		MeshBoxes.Add(B);
	}

	TArray<FGPUPVSBox3f> OccluderBoxes;
	OccluderBoxes.Reserve(OccluderTriangleCount);
	for (uint32 TriIndex = 0; TriIndex < OccluderTriangleCount; ++TriIndex)
	{
		const uint32 I0 = OccluderIndices[(int32)TriIndex * 3 + 0];
		const uint32 I1 = OccluderIndices[(int32)TriIndex * 3 + 1];
		const uint32 I2 = OccluderIndices[(int32)TriIndex * 3 + 2];

		FBox3f TriBounds(ForceInit);
		TriBounds += OccluderVertices[(int32)I0];
		TriBounds += OccluderVertices[(int32)I1];
		TriBounds += OccluderVertices[(int32)I2];

		FGPUPVSBox3f B;
		B.Min = TriBounds.Min;
		B.Max = TriBounds.Max;
		OccluderBoxes.Add(B);
	}

	auto BuildVisibilityGroups = [&](TConstArrayView<FBox3f> InMeshBounds, TArray<FGPUPVSBox3f>& OutGroups, TArray<uint32>& OutMeshToGroup)
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

		TArray<FSortInfo> SortedBounds;
		SortedBounds.Reserve(InMeshBounds.Num());
		for (const FBox3f& B : InMeshBounds)
		{
			if (B.IsValid)
			{
				FSortInfo Info;
				Info.Bounds = B;
				SortedBounds.Add(Info);
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

		for (FSortInfo& M : SortedBounds)
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

		SortedBounds.Sort([](const FSortInfo& A, const FSortInfo& B)
		{
			return A.Distance < B.Distance;
		});

		FBox3f VisibilityGridBounds(ForceInit);
		// 对齐 Lightmass：剔除最远 10% 的 mesh，避免远处巨物（天空盒）撑爆网格范围
		const int32 MaxMeshIndex = FMath::Min(FMath::Max(FMath::TruncToInt(0.9f * SortedBounds.Num()), 1), SortedBounds.Num() - 1);
		for (int32 i = 0; i < MaxMeshIndex; ++i)
		{
			VisibilityGridBounds += SortedBounds[i].Bounds;
		}

		const float TargetNumGroupsAsFractionOfMeshes = 0.3f;
		const int32 ZDimensionDivisor = 4;
		const int32 GridXY = FMath::Max(FMath::TruncToInt(FMath::Pow((float)ZDimensionDivisor * TargetNumGroupsAsFractionOfMeshes * (float)SortedBounds.Num(), 1.0f / 3.0f)), 1);
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

	TArray<FGPUPVSBox3f> GroupBoxes;
	TArray<uint32> MeshToGroup;
	BuildVisibilityGroups(VisibilityMeshBounds, GroupBoxes, MeshToGroup);
	int32 NumMappedMeshes = 0;
	for (uint32 G : MeshToGroup)
	{
		if (G != 0xFFFFFFFFu)
		{
			NumMappedMeshes++;
		}
	}
	LastGroupsBuilt = GroupBoxes.Num();
	LastMappedMeshes = NumMappedMeshes;
	GPUPVS_LOG(Log, TEXT("[Compute] GroupsBuilt=%d Meshes=%d MappedMeshes=%d"),
		GroupBoxes.Num(),
		VisibilityMeshBounds.Num(),
		NumMappedMeshes);

	TArray<FGPUPVSBVHNodeGPU> BvhNodesGpu;
	BvhNodesGpu.Reserve(BvhNodes.Num());
	for (const FGPUPVSBVHNode& N0 : BvhNodes)
	{
		FGPUPVSBVHNodeGPU N;
		N.BoundsMin = N0.BoundsMin;
		N.BoundsMax = N0.BoundsMax;
		N.LeftChildOrFirstTri = N0.LeftChildOrFirstTri;
		N.RightChildOrTriCount = N0.RightChildOrTriCount;
		BvhNodesGpu.Add(N);
	}

	const uint32 NumMeshes = (uint32)MeshBoxes.Num();
	const uint32 WordsPerCell = FMath::DivideAndRoundUp(NumMeshes, 32u);
	const uint32 TotalWords = (uint32)CellBoxes.Num() * WordsPerCell;
	const uint32 NumGroups = (uint32)GroupBoxes.Num();

	struct FVisibilityReadback
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

	const TSharedRef<FVisibilityReadback, ESPMode::ThreadSafe> Readback = MakeShared<FVisibilityReadback, ESPMode::ThreadSafe>();
	Readback->NumMeshes = NumMeshes;
	Readback->WordsPerCell = WordsPerCell;
	Readback->TotalWords = TotalWords;
	Readback->NumGroups = NumGroups;

	ENQUEUE_RENDER_COMMAND(GPUPVS_Visibility)(
		[CellBoxesData = MoveTemp(CellBoxes),
		 MeshBoxesData = MoveTemp(MeshBoxes),
		 GroupBoxesData = MoveTemp(GroupBoxes),
		 OccluderBoxesData = MoveTemp(OccluderBoxes),
		 MeshToGroupData = MoveTemp(MeshToGroup),
		 NodesData = MoveTemp(BvhNodesGpu),
		 LeafTrisData = MoveTemp(BvhLeafTriIndices),
		 OccluderVerticesCopy = OccluderVertices,
		 OccluderIndicesCopy = OccluderIndices,
		 TriangleTargetIdsCopy = OccluderTriangleTargetIds,
		 NumCellSamples = (uint32)FMath::Max(0, ComputeParams.NumCellSamples),
		 MinMeshSamples = (uint32)FMath::Max(1, ComputeParams.MinMeshSamples),
		 MaxMeshSamples = (uint32)FMath::Max(1, ComputeParams.MaxMeshSamples),
		 NumImportanceSamples = (uint32)FMath::Max(0, ComputeParams.NumImportanceSamples),
		 MeshBoundsScale = ComputeParams.MeshBoundsScale,
		 MaxDistance = ComputeParams.MaxDistance,
		 Readback](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			const auto GroupOutputs = FShaderDispatcher::DispatchVisibilityGroups(
				GraphBuilder,
				CellBoxesData,
				GroupBoxesData,
				OccluderBoxesData,
				NodesData,
				LeafTrisData,
				OccluderVerticesCopy,
				OccluderIndicesCopy,
				TriangleTargetIdsCopy,
				NumCellSamples,
				MinMeshSamples,
				MaxMeshSamples,
				NumImportanceSamples,
				MeshBoundsScale,
				MaxDistance);

			Readback->GroupWordsPerCell = GroupOutputs.GroupWordsPerCell;

			const auto Outputs = FShaderDispatcher::DispatchVisibilityMeshesWithGroups(
				GraphBuilder,
				CellBoxesData,
				MeshBoxesData,
				MeshToGroupData,
				GroupOutputs.GroupVisibilityWords,
				GroupOutputs.NumGroups,
				GroupOutputs.GroupWordsPerCell,
				OccluderBoxesData,
				NodesData,
				LeafTrisData,
				OccluderVerticesCopy,
				OccluderIndicesCopy,
				TriangleTargetIdsCopy,
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

				Readback->WordsReadback = WordsReadback;
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
				Readback->StatsReadback = StatsReadback;
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
				Readback->GroupStatsReadback = GroupStatsReadback;
			}

			GraphBuilder.Execute();
		});

	FlushRenderingCommands();

	if (!Readback->WordsReadback || !Readback->StatsReadback || !Readback->GroupStatsReadback)
	{
		GPUPVS_LOG(Error, TEXT("[Compute] Readback object not created."));
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	const double WaitStart = FPlatformTime::Seconds();
	while (!Readback->WordsReadback->IsReady() || !Readback->StatsReadback->IsReady() || !Readback->GroupStatsReadback->IsReady())
	{
		FPlatformProcess::Sleep(FMath::Max(ComputeParams.VisibilityReadbackPollSeconds, 0.0f));
		if (FPlatformTime::Seconds() - WaitStart > FMath::Max(ComputeParams.VisibilityReadbackTimeoutSeconds, 0.0f))
		{
			FRHIGPUBufferReadback* RB = Readback->WordsReadback;
			FRHIGPUBufferReadback* StatsRB = Readback->StatsReadback;
			FRHIGPUBufferReadback* GroupStatsRB = Readback->GroupStatsReadback;
			ENQUEUE_RENDER_COMMAND(GPUPVS_VisibilityTimeoutCleanup)(
				[RB, StatsRB, GroupStatsRB](FRHICommandListImmediate& RHICmdList)
				{
					delete RB;
					delete StatsRB;
					delete GroupStatsRB;
				});
			FlushRenderingCommands();
			Readback->WordsReadback = nullptr;
			Readback->StatsReadback = nullptr;
			Readback->GroupStatsReadback = nullptr;
			GPUPVS_LOG(Error, TEXT("[Compute] GPU readback timeout (>60s)."));
			Scene.SetVisibilityWords({}, 0, 0);
			return;
		}
	}

	TArray<uint32> WordsCPU;
	WordsCPU.SetNumUninitialized((int32)Readback->TotalWords);
	const uint64 Bytes64 = uint64(Readback->TotalWords) * sizeof(uint32);
	const uint32 Bytes = (Bytes64 > MAX_uint32) ? 0u : (uint32)Bytes64;
	if (Bytes == 0u)
	{
		FRHIGPUBufferReadback* RB = Readback->WordsReadback;
		ENQUEUE_RENDER_COMMAND(GPUPVS_VisibilityTooLargeCleanup)(
			[RB](FRHICommandListImmediate& RHICmdList)
			{
				delete RB;
			});
		FlushRenderingCommands();
		Readback->WordsReadback = nullptr;
		GPUPVS_LOG(Error, TEXT("[Compute] Readback size too large. TotalWords=%u"), Readback->TotalWords);
		Scene.SetVisibilityWords({}, 0, 0);
		return;
	}

	FRHIGPUBufferReadback* RB = Readback->WordsReadback;
	FRHIGPUBufferReadback* StatsRB = Readback->StatsReadback;
	FRHIGPUBufferReadback* GroupStatsRB = Readback->GroupStatsReadback;
	TArray<uint32> MeshPassStats;
	MeshPassStats.SetNumZeroed(8);
	TArray<uint32> GroupPassStats;
	GroupPassStats.SetNumZeroed(8);
	ENQUEUE_RENDER_COMMAND(GPUPVS_CopyVisibilityReadback)(
		[RB, StatsRB, GroupStatsRB, Bytes, &WordsCPU, &MeshPassStats, &GroupPassStats](FRHICommandListImmediate& RHICmdList)
		{
			if (void* Ptr = RB->Lock(Bytes))
			{
				FMemory::Memcpy(WordsCPU.GetData(), Ptr, Bytes);
				RB->Unlock();
			}
			const uint32 StatsBytes = (uint32)(MeshPassStats.Num() * sizeof(uint32));
			if (void* StatsPtr = StatsRB->Lock(StatsBytes))
			{
				FMemory::Memcpy(MeshPassStats.GetData(), StatsPtr, StatsBytes);
				StatsRB->Unlock();
			}
			const uint32 GroupStatsBytes = (uint32)(GroupPassStats.Num() * sizeof(uint32));
			if (void* GroupStatsPtr = GroupStatsRB->Lock(GroupStatsBytes))
			{
				FMemory::Memcpy(GroupPassStats.GetData(), GroupStatsPtr, GroupStatsBytes);
				GroupStatsRB->Unlock();
			}
			delete RB;
			delete StatsRB;
			delete GroupStatsRB;
		});

	FlushRenderingCommands();
	Readback->WordsReadback = nullptr;
	Readback->StatsReadback = nullptr;
	Readback->GroupStatsReadback = nullptr;

	uint32 NonZeroWords = 0;
	uint32 CellsWithAnyVisible = 0;
	uint32 MinVisibleMeshes = MAX_uint32;
	uint32 MaxVisibleMeshes = 0;
	uint64 TotalVisibleMeshes = 0;
	int32 FirstNonZeroCell = INDEX_NONE;

	for (int32 CellIndex = 0; CellIndex < CellList.Num(); ++CellIndex)
	{
		const uint32 BaseWord = (uint32)CellIndex * Readback->WordsPerCell;
		uint32 VisibleInCell = 0;
		for (uint32 WordIndex = 0; WordIndex < Readback->WordsPerCell; ++WordIndex)
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

	if (CellList.IsEmpty())
	{
		MinVisibleMeshes = 0;
	}
	else if (MinVisibleMeshes == MAX_uint32)
	{
		MinVisibleMeshes = 0;
	}

	if (FirstNonZeroCell != INDEX_NONE)
	{
		const uint32 BaseWord = (uint32)FirstNonZeroCell * Readback->WordsPerCell;
		const uint32 W0 = WordsCPU[(int32)(BaseWord + 0)];
		const uint32 W1 = Readback->WordsPerCell > 1 ? WordsCPU[(int32)(BaseWord + 1)] : 0u;
		const uint32 W2 = Readback->WordsPerCell > 2 ? WordsCPU[(int32)(BaseWord + 2)] : 0u;
		const uint32 W3 = Readback->WordsPerCell > 3 ? WordsCPU[(int32)(BaseWord + 3)] : 0u;
		GPUPVS_LOG(Verbose, TEXT("[Compute] FirstNonZero Cell=%d Words[0..3]=0x%08X 0x%08X 0x%08X 0x%08X"),
			FirstNonZeroCell,
			W0, W1, W2, W3);
	}

	Scene.SetVisibilityWords(MoveTemp(WordsCPU), Readback->NumMeshes, Readback->WordsPerCell);

	const double BuildEndTime = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Compute] BVHNodes=%d BVHLeafTris=%d Cells=%d Meshes=%u Groups=%u WordsPerCell=%u TotalWords=%u Time=%.3fs"),
		BVHNodesCount,
		BVHLeafTriCount,
		CellList.Num(),
		Readback->NumMeshes,
		Readback->NumGroups,
		Readback->WordsPerCell,
		Readback->TotalWords,
		BuildEndTime - BuildStartTime);

	const double AvgVisibleMeshes = CellList.Num() > 0 ? (double)TotalVisibleMeshes / (double)CellList.Num() : 0.0;
	GPUPVS_LOG(Log, TEXT("[Compute] VisibleStats CellsWithAny=%u/%d MinVisible=%u MaxVisible=%u AvgVisible=%.2f NonZeroWords=%u/%u"),
		CellsWithAnyVisible,
		CellList.Num(),
		MinVisibleMeshes,
		MaxVisibleMeshes,
		AvgVisibleMeshes,
		NonZeroWords,
		Readback->TotalWords);

	if (MeshPassStats.Num() == 8)
	{
		const uint32 Pairs = MeshPassStats[0];
		const uint32 VisiblePairs = MeshPassStats[1];
		const uint32 DistanceCulled = MeshPassStats[2];
		const uint32 SizeRatioVisible = MeshPassStats[3];
		const uint32 GroupSkipped = MeshPassStats[4];
		const uint32 VisibleExplicit = MeshPassStats[5];
		const uint32 VisibleImportance = MeshPassStats[6];
		const uint32 Rays = MeshPassStats[7];
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

	if (GroupPassStats.Num() == 8)
	{
		const uint32 Pairs = GroupPassStats[0];
		const uint32 VisiblePairs = GroupPassStats[1];
		const uint32 DistanceCulled = GroupPassStats[2];
		const uint32 SizeRatioVisible = GroupPassStats[3];
		const uint32 Rays = GroupPassStats[7];
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
