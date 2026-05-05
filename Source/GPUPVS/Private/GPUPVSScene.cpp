/**
 * 场景数据采集与扁平化实现。
 *
 * 输出数据面向“预计算可见性”的最小闭环：
 * - PrecomputedVisibilityVolumes：PVS 体积边界（Brush Bounds）。
 * - SceneVertices / SceneIndices：世界空间三角形网格（当前仅收集 StaticMeshComponent 的 LOD0，且跳过非 Opaque section）。
 * - TriangleBounds：每三角形包围盒，用于后续加速结构或粗剔除。
 *
 * 约束：
 * - 目前只处理 Static/Stationary（Movable 直接跳过）。
 * - 暂未支持 ISM/HISM/Landscape（可按需要补充 Geometry Provider）。
 * - 目前仅处理 Opaque section，跳过透明或不透明的 section。
 * - Setup 阶段几何做了筛选：仅采集与任意 VisibilityVolume 相交的三角形。
 */
#include "GPUPVSScene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Math/UnrealMath.h"
#include "Math/BoxSphereBounds.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectIterator.h"
#include "GPUPVSLog.h"

FPVSScene::FPVSScene()
{
	PrecomputedVisibilitySettings.CellSize = 200.0f;
	PrecomputedVisibilitySettings.PlayAreaHeight = 220.0f;
	PrecomputedVisibilitySettings.bPlaceCellsOnOpaqueOnly = true;
}

void FPVSScene::Reset()
{
	VisibilityBounds = FBox3f(ForceInit);
	Cells.Reset();
	PrecomputedVisibilityVolumes.Reset();

	SceneVertices.Reset();
	SceneIndices.Reset();
	SceneNormals.Reset();
	TriangleBounds.Reset();

	OccluderVertices.Reset();
	OccluderIndices.Reset();

	VisibilityMeshBounds.Reset();
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

					VisibilityMeshBounds.Add(WorldBox);

					const FStaticMeshLODResources& LOD = StaticMesh->GetRenderData()->LODResources[0];
					const FPositionVertexBuffer& PositionBuffer = LOD.VertexBuffers.PositionVertexBuffer;
					const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
					FIndexArrayView IndexView = LOD.IndexBuffer.GetArrayView();

					const bool bForSetup = DoesBoxIntersectVisibilityVolume(WorldBox);

					auto AppendOccluderVertices = [&]()
					{
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
						SceneNormals.Reserve(SceneNormals.Num() + NumVertices);

						for (uint32 i = 0; i < NumVertices; ++i)
						{
							FVector WorldPos = WorldTransform.TransformPosition((FVector)PositionBuffer.VertexPosition(i));
							SceneVertices.Add((FVector3f)WorldPos);

							FVector WorldNormal = WorldTransform.TransformVector((FVector)StaticMeshVertexBuffer.VertexTangentZ(i));
							WorldNormal.Normalize();
							SceneNormals.Add((FVector3f)WorldNormal);
						}
						return VertexOffset;
					};

					const uint32 OccluderVertexOffset = AppendOccluderVertices();
					const uint32 SetupVertexOffset = bForSetup ? AppendSetupVertices() : 0u;

					for (int32 SectionIdx = 0; SectionIdx < LOD.Sections.Num(); ++SectionIdx)
					{
						const FStaticMeshSection& Section = LOD.Sections[SectionIdx];
						UMaterialInterface* Mat = StaticMeshComp->GetMaterial(Section.MaterialIndex);

						const bool bIsOpaque = !(Mat && Mat->GetBlendMode() != BLEND_Opaque);
						const bool bAddOccluder = bIsOpaque;
						const bool bAddSetup = bForSetup && (bIsOpaque || !PrecomputedVisibilitySettings.bPlaceCellsOnOpaqueOnly);

						if (!bIsOpaque)
						{
							NumOccluderTrianglesSkippedByMaterial += (int64)Section.NumTriangles;
							if (bForSetup && PrecomputedVisibilitySettings.bPlaceCellsOnOpaqueOnly)
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

								FBox3f TriBounds(ForceInit);
								TriBounds += SceneVertices[GlobalIndex0];
								TriBounds += SceneVertices[GlobalIndex1];
								TriBounds += SceneVertices[GlobalIndex2];
								TriangleBounds.Add(TriBounds);
							}
						}
					}

					// 提取compute阶段信息
					// TODO
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
		PrecomputedVisibilitySettings.CellSize);
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
	GPUPVS_LOG(Log, TEXT("[Collect] PlaceCellsOnOpaqueOnly=%s OccluderSkippedByMaterial=%lld SetupSkippedByMaterial=%lld"),
		PrecomputedVisibilitySettings.bPlaceCellsOnOpaqueOnly ? TEXT("true") : TEXT("false"),
		NumOccluderTrianglesSkippedByMaterial,
		NumSetupTrianglesSkippedByMaterial);

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
		DoubleExtent.X = DoubleExtent.X - FMath::Fmod(DoubleExtent.X, PrecomputedVisibilitySettings.CellSize) + PrecomputedVisibilitySettings.CellSize;
		DoubleExtent.Y = DoubleExtent.Y - FMath::Fmod(DoubleExtent.Y, PrecomputedVisibilitySettings.CellSize) + PrecomputedVisibilitySettings.CellSize;
		Bounds.Max = Bounds.Min + DoubleExtent;
		return Bounds;
	}

	return FBox3f(FVector3f::ZeroVector, FVector3f::ZeroVector);
}
