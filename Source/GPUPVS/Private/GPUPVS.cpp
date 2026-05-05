/**
 * GPUPVS ������ʵ�֡�
 *
 * ��ǰ�׶�Ŀ�꣺
 * - CollectFromWorld���� UWorld �ռ���̬���񼸺��� PrecomputedVisibilityVolume��
 * - SetupPrecomputedVisibility������/��ȡÿ�� (X,Y) �ĸ߶Ȳ㣬�ٰ� Lightmass ���ù������� 3D Cells��
 * - ComputeVisibility / WriteBack�������ɼ��Լ���������д����ʵ�֣���
 */
#include "GPUPVS.h"
#include "Engine/World.h"
#include "GPUPVSScene.h"
#include "GPUPVSShaders.h"
#include "GPUPVSLog.h"
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
//todo
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
	RasterMaxLayersLimit = 64;
	VisibilityRayCount = 1024;
	VisibilityMaxDistance = 5000.0f;

	ResetRasterResults();
}

void FGPUPVS::BuildPVS()
{
	if (!World)
	{
		return;
	}

	const double BuildStart = FPlatformTime::Seconds();
	GPUPVS_LOG(Log, TEXT("[Build] Start World=%s"), *World->GetName());

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
}

void FGPUPVS::WriteBack()
{
}
