/**
 * 场景采集与扁平化
 * 仅保留 PVS 构建所需的数据：Volumes、Setup 几何、遮挡几何、可见性目标 Bounds
 */
#include "GPUPVSScene.h"

#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

#include "GPUPVSLog.h"

namespace
{
	static bool ContainsSkySphereToken(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}

		const FString LowerValue = Value.ToLower();
		return LowerValue.Contains(TEXT("skysphere"))
			|| LowerValue.Contains(TEXT("sky sphere"))
			|| LowerValue.Contains(TEXT("sm_skysphere"));
	}

	static bool ShouldSkipSetupGeometryForSkySphere(const AActor* Actor, const UStaticMeshComponent* StaticMeshComponent, const UStaticMesh* StaticMesh)
	{
		return (Actor && (ContainsSkySphereToken(Actor->GetName()) || ContainsSkySphereToken(Actor->GetClass()->GetName())))
			|| (StaticMeshComponent && ContainsSkySphereToken(StaticMeshComponent->GetName()))
			|| (StaticMesh && ContainsSkySphereToken(StaticMesh->GetName()));
	}
}

FPVSScene::FPVSScene()
{
}

void FPVSScene::Reset()
{
	VisibilityBounds = FBox3f(ForceInit);
	Cells.Reset();
	PrecomputedVisibilityVolumes.Reset();

	SceneVertices.Reset();
	SceneIndices.Reset();

	OccluderVertices.Reset();
	OccluderIndices.Reset();
	OccluderTriangleTargetIds.Reset();

	VisibilityMeshBounds.Reset();
	VisibilityMeshComponentPaths.Reset();
	VisibilityWords.Reset();
	VisibilityNumMeshes = 0;
	VisibilityWordsPerCell = 0;
}

void FPVSScene::CollectFromWorld(UWorld* InWorld)
{
	Reset();

	if (!InWorld)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	FBox3f TotalPVSBounds(ForceInit);

	// 一些统计信息，用于调试
	int32 NumVolumes = 0;
	int32 NumActors = 0;
	int32 NumPrimitiveComponents = 0;
	int32 NumStaticMeshComponents = 0;
	int32 NumSkippedUnregisteredOrTemplate = 0;
	int32 NumSkippedMovable = 0;
	int32 NumSkippedNonStaticMesh = 0;
	int64 NumSetupTrianglesAdded = 0;
	int64 NumOccluderTrianglesAdded = 0;
	int64 NumSetupTrianglesSkippedByMaterial = 0;
	int64 NumSetupTrianglesSkippedBySky = 0;
	int64 NumOccluderTrianglesSkippedByMaterial = 0;

	// 遍历收集APrecomputedVisibilityVolume
	for (TActorIterator<APrecomputedVisibilityVolume> It(InWorld); It; ++It)
	{
		if (UBrushComponent* Brush = It->GetBrushComponent()) // UBrushComponent是UPrimitiveComponent子类
		{
			const FBox3f Box = (FBox3f)Brush->Bounds.GetBox();
			if (Box.IsValid)
			{
				FPrecomputedVisibilityVolume volume;
				volume.Bounds = Box;
				PrecomputedVisibilityVolumes.Add(volume);
				TotalPVSBounds += Box;
				NumVolumes++;
			}
		}
	}

	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		NumActors++;
		AActor* Actor = *It;
		
		// 过滤 APrecomputedVisibilityVolume
		if (Actor->IsA<APrecomputedVisibilityVolume>())
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> Prims; // 组件专用栈上数组，临时、快
		Actor->GetComponents(Prims);

		if (Prims.IsEmpty())
		{
			continue;
		}

		for (UPrimitiveComponent* Prim : Prims)
		{
			NumPrimitiveComponents++;
			if (!Prim || Prim->IsTemplate() || !Prim->IsRegistered())
			{
				NumSkippedUnregisteredOrTemplate++;
				continue;		
			}
			if (Prim->Mobility == EComponentMobility::Movable)
			{
				NumSkippedMovable++;
				continue;
			}
			UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Prim);  // TODO: 参照GPULightmass：InstancedStaticMesh，Landscape，BSP后续也应该考虑进来
			if (StaticMeshComp)
			{
				NumStaticMeshComponents++;
				UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

				if (StaticMesh && StaticMesh->GetRenderData() && StaticMesh->GetRenderData()->LODResources.Num() > 0)
				{
					const FBox3f WorldBox(Prim->Bounds.GetBox());

					const int32 VisibilityId = VisibilityMeshBounds.Num();
					VisibilityMeshBounds.Add(WorldBox);
					VisibilityMeshComponentPaths.Add(FSoftObjectPath(StaticMeshComp));

					const FStaticMeshLODResources& LOD = StaticMesh->GetRenderData()->LODResources[0];
					const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;
					const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
					FIndexArrayView IndexView = LOD.IndexBuffer.GetArrayView();

					// Setup 只取 volume 附近几何，先用 bounds 粗过滤
					const bool bForSetup = DoesBoxIntersectVisibilityVolume(WorldBox);
					const bool bSkipSetupForSkySphere = ShouldSkipSetupGeometryForSkySphere(Actor, StaticMeshComp, StaticMesh);
					const bool bUseForSetup = bForSetup && !bSkipSetupForSkySphere;

					auto AppendOccluderVertices = [&]()
					{
						// 遮挡几何
						FTransform WorldTransform = StaticMeshComp->GetComponentTransform();
						const uint32 NumVertices = PositionBuffer.GetNumVertices();

						const uint32 VertexOffset = (uint32)OccluderVertices.Num();
						OccluderVertices.Reserve(OccluderVertices.Num() + NumVertices);
						for (uint32 i = 0; i < NumVertices; ++i)
						{
							FVector WorldPos = WorldTransform.TransformPosition((FVector)PositionBuffer.VertexPosition(i));
							OccluderVertices.Add((FVector3f)WorldPos);
						}
						return VertexOffset;
					};

					auto AppendSetupVertices = [&]()
					{
						FTransform WorldTransform = StaticMeshComp->GetComponentTransform();
						const uint32 NumVertices = PositionBuffer.GetNumVertices();

						const uint32 VertexOffset = (uint32)SceneVertices.Num();
						SceneVertices.Reserve(SceneVertices.Num() + NumVertices);

						for (uint32 i = 0; i < NumVertices; ++i)
						{
							FVector WorldPos = WorldTransform.TransformPosition((FVector)PositionBuffer.VertexPosition(i));
							SceneVertices.Add((FVector3f)WorldPos);
						}
						return VertexOffset;
					};

					const uint32 OccluderVertexOffset = AppendOccluderVertices();
					const uint32 SetupVertexOffset = bUseForSetup ? AppendSetupVertices() : 0u;

					for (int32 SectionIdx = 0; SectionIdx < LOD.Sections.Num(); ++SectionIdx)
					{
						const FStaticMeshSection& Section = LOD.Sections[SectionIdx];
						UMaterialInterface* Mat = StaticMeshComp->GetMaterial(Section.MaterialIndex);

						// 遮挡语义：静态 + 不透明
						const bool bIsOpaque = !(Mat && Mat->GetBlendMode() != BLEND_Opaque);
						const bool bAddOccluder = bIsOpaque;
						const bool bAddSetup = bUseForSetup && (bIsOpaque || !VisibilitySettings.bPlaceCellsOnOpaqueOnly);

						if (bForSetup && bSkipSetupForSkySphere)
						{
							NumSetupTrianglesSkippedBySky += (int64)Section.NumTriangles;
						}

						if (!bIsOpaque)
						{
							NumOccluderTrianglesSkippedByMaterial += (int64)Section.NumTriangles;
							if (bUseForSetup && VisibilitySettings.bPlaceCellsOnOpaqueOnly)
							{
								NumSetupTrianglesSkippedByMaterial += (int64)Section.NumTriangles;
							}
						}

						for (uint32 TriangleIdx = 0; TriangleIdx < Section.NumTriangles; ++TriangleIdx)
						{
							const uint32 LocalIndex0 = IndexView[Section.FirstIndex + TriangleIdx * 3 + 0];
							const uint32 LocalIndex1 = IndexView[Section.FirstIndex + TriangleIdx * 3 + 1];
							const uint32 LocalIndex2 = IndexView[Section.FirstIndex + TriangleIdx * 3 + 2];

							const uint32 OccIndex0 = LocalIndex0 + OccluderVertexOffset;
							const uint32 OccIndex1 = LocalIndex1 + OccluderVertexOffset;
							const uint32 OccIndex2 = LocalIndex2 + OccluderVertexOffset;

							if (bAddOccluder)
							{
								OccluderIndices.Add(OccIndex0);
								OccluderIndices.Add(OccIndex1);
								OccluderIndices.Add(OccIndex2);
								OccluderTriangleTargetIds.Add(VisibilityId);
								NumOccluderTrianglesAdded++;
							}

							if (bAddSetup)
							{
								const uint32 GlobalIndex0 = LocalIndex0 + SetupVertexOffset;
								const uint32 GlobalIndex1 = LocalIndex1 + SetupVertexOffset;
								const uint32 GlobalIndex2 = LocalIndex2 + SetupVertexOffset;

								SceneIndices.Add(GlobalIndex0);
								SceneIndices.Add(GlobalIndex1);
								SceneIndices.Add(GlobalIndex2);
								NumSetupTrianglesAdded++;
							}
						}
					}

				}
			}
			else {
				NumSkippedNonStaticMesh++;
			}
		}
	}

	const double EndTime = FPlatformTime::Seconds();
	const int64 NumVertices = (int64)SceneVertices.Num();
	const int64 NumTriangles = (int64)SceneIndices.Num() / 3;
	GPUPVS_LOG(Log, TEXT("[Collect] World=%s Volumes=%d TotalPVSBoundsValid=%s BoundsMin=%s BoundsMax=%s CellSize=%.2f"),
		*InWorld->GetName(),
		NumVolumes,
		TotalPVSBounds.IsValid ? TEXT("true") : TEXT("false"),
		*TotalPVSBounds.Min.ToString(),
		*TotalPVSBounds.Max.ToString(),
		VisibilitySettings.CellSize);
	GPUPVS_LOG(Log, TEXT("[Collect] Actors=%d Prims=%d SMComps=%d Skipped(Unreg/Template)=%d Skipped(Movable)=%d Skipped(NonSM)=%d"),
		NumActors,
		NumPrimitiveComponents,
		NumStaticMeshComponents,
		NumSkippedUnregisteredOrTemplate,
		NumSkippedMovable,
		NumSkippedNonStaticMesh);
	GPUPVS_LOG(Log, TEXT("[Collect] Vertices=%lld Triangles=%lld SetupTrianglesAdded=%lld OccluderTrianglesAdded=%lld SkippedTrianglesByMaterial=%lld Time=%.3fs"),
		NumVertices,
		NumTriangles,
		NumSetupTrianglesAdded,
		NumOccluderTrianglesAdded,
		NumSetupTrianglesSkippedByMaterial,
		EndTime - StartTime);
	GPUPVS_LOG(Log, TEXT("[Collect] PlaceCellsOnOpaqueOnly=%s OccluderSkippedByMaterial=%lld SetupSkippedByMaterial=%lld SetupSkippedBySky=%lld"),
		VisibilitySettings.bPlaceCellsOnOpaqueOnly ? TEXT("true") : TEXT("false"),
		NumOccluderTrianglesSkippedByMaterial,
		NumSetupTrianglesSkippedByMaterial,
		NumSetupTrianglesSkippedBySky);

	if (OccluderTriangleTargetIds.Num() != OccluderIndices.Num() / 3)
	{
		GPUPVS_LOG(Warning, TEXT("[Collect] OccluderTriangleTargetIds mismatch. Owners=%d OccluderTriangles=%d"),
			OccluderTriangleTargetIds.Num(),
			OccluderIndices.Num() / 3);
	}

	VisibilityNumMeshes = (uint32)VisibilityMeshBounds.Num();
}

bool FPVSScene::DoesBoxIntersectVisibilityVolume(const FBox3f& Box) const
{
	if (!Box.IsValid || PrecomputedVisibilityVolumes.IsEmpty())
	{
		return false;
	}

	for (const FPrecomputedVisibilityVolume& Volume : PrecomputedVisibilityVolumes)
	{
		const FBox3f& V = Volume.Bounds;
		if (!V.IsValid)
		{
			continue;
		}

		const bool bIntersects =
			(Box.Min.X <= V.Max.X && Box.Max.X >= V.Min.X) &&
			(Box.Min.Y <= V.Max.Y && Box.Max.Y >= V.Min.Y) &&
			(Box.Min.Z <= V.Max.Z && Box.Max.Z >= V.Min.Z);

		if (bIntersects)
		{
			return true;
		}
	}

	return false;
}

FBox3f FPVSScene::GetAlignedVisibilityVolumeBounds() const
{
	FBox3f Bounds(ForceInit);
	for (const FPrecomputedVisibilityVolume& Volume : PrecomputedVisibilityVolumes)
	{
		Bounds += Volume.Bounds;
	}

	if (PrecomputedVisibilityVolumes.Num() > 0)
	{
		FVector3f DoubleExtent = Bounds.GetExtent() * 2.0f;
		DoubleExtent.X = DoubleExtent.X - FMath::Fmod(DoubleExtent.X, VisibilitySettings.CellSize) + VisibilitySettings.CellSize;
		DoubleExtent.Y = DoubleExtent.Y - FMath::Fmod(DoubleExtent.Y, VisibilitySettings.CellSize) + VisibilitySettings.CellSize;
		Bounds.Max = Bounds.Min + DoubleExtent;
		return Bounds;
	}

	return FBox3f(FVector3f::ZeroVector, FVector3f::ZeroVector);
}
