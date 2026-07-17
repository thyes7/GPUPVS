#include "GPUPVS.h"

#include "Components/PrimitiveComponent.h"
#include "GPUPVSLog.h"
#include "GPUPVSSceneData.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif

namespace
{
	static UPrimitiveComponent* ResolvePrimitiveComponentInWorld(
		UWorld* World,
		const FSoftObjectPath& ComponentPath,
		TMap<FSoftObjectPath, TWeakObjectPtr<UPrimitiveComponent>>& InOutResolvedComponents)
	{
		if (TWeakObjectPtr<UPrimitiveComponent>* CachedComponent = InOutResolvedComponents.Find(ComponentPath))
		{
			if (UPrimitiveComponent* Component = CachedComponent->Get())
			{
				if (Component->GetWorld() == World)
				{
					return Component;
				}
			}
		}

		UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(ComponentPath.ResolveObject());
		if (Component && Component->GetWorld() == World)
		{
			InOutResolvedComponents.Add(ComponentPath, Component);
			return Component;
		}

		if (!World)
		{
			return nullptr;
		}

		const FString TargetSubPath = ComponentPath.GetSubPathString();
		for (UPrimitiveComponent* Candidate : TObjectRange<UPrimitiveComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage))
		{
			if (Candidate && Candidate->GetWorld() == World && FSoftObjectPath(Candidate).GetSubPathString() == TargetSubPath)
			{
				InOutResolvedComponents.Add(ComponentPath, Candidate);
				return Candidate;
			}
		}

		return nullptr;
	}

	static void CacheOriginalVisibilityState(
		UPrimitiveComponent* Component,
		TMap<FSoftObjectPath, bool>& InOutOriginalVisibility,
		TMap<FSoftObjectPath, bool>& InOutOriginalHiddenInGame)
	{
		const FSoftObjectPath RuntimePath(Component);
		if (!InOutOriginalVisibility.Contains(RuntimePath))
		{
			InOutOriginalVisibility.Add(RuntimePath, Component->IsVisible());
			InOutOriginalHiddenInGame.Add(RuntimePath, Component->bHiddenInGame);
		}
	}

	static void ApplyPVSToComponent(
		UPrimitiveComponent* Component,
		bool bVisibleByPVS,
		const TMap<FSoftObjectPath, bool>& InOriginalVisibility,
		const TMap<FSoftObjectPath, bool>& InOriginalHiddenInGame)
	{
		const FSoftObjectPath RuntimePath(Component);
		const bool bOriginalVisible = InOriginalVisibility.FindRef(RuntimePath);
		const bool bOriginalHiddenInGame = InOriginalHiddenInGame.FindRef(RuntimePath);
		Component->SetVisibility(bOriginalVisible && bVisibleByPVS, true);
		Component->SetHiddenInGame(bOriginalHiddenInGame || !bVisibleByPVS, true);
		Component->MarkRenderStateDirty();
	}

#if WITH_EDITOR
	static void DrawViewerCellBounds(UWorld* World, const FGPUPVSStoredCell& ViewerCell)
	{
		const FBox ViewerBox((FVector)ViewerCell.BoundsMin, (FVector)ViewerCell.BoundsMax);
		DrawDebugBox(World, ViewerBox.GetCenter(), ViewerBox.GetExtent(), FColor::Green, false, 0.0f, 0, 5.0f);
	}

	static void DrawHiddenTargetBounds(UWorld* World, const FGPUPVSStoredTarget& Target)
	{
		const FBox TargetBox((FVector)Target.BoundsMin, (FVector)Target.BoundsMax);
		DrawDebugBox(World, TargetBox.GetCenter(), TargetBox.GetExtent(), FColor::Red, false, 0.0f, 0, 2.0f);
	}

	static void CollectVisualizationStats(
		const UGPUPVSSceneData* DataAsset,
		int32 ViewerCellIndex,
		int32& OutVisibleCount,
		int32& OutHiddenCount,
		TFunctionRef<void(const FGPUPVSStoredTarget&)> OnHiddenTarget)
	{
		OutVisibleCount = 0;
		OutHiddenCount = 0;

		for (const FGPUPVSStoredTarget& Target : DataAsset->Targets)
		{
			if (DataAsset->IsVisible(ViewerCellIndex, Target.VisibilityId))
			{
				OutVisibleCount++;
				continue;
			}

			OutHiddenCount++;
			OnHiddenTarget(Target);
		}
	}
#endif
}

int32 FGPUPVS::FindLoadedCellIndexFromPosition(const FVector& Position) const
{
	UGPUPVSSceneData* DataAsset = LoadedSceneDataAsset.IsValid() ? LoadedSceneDataAsset.Get() : const_cast<FGPUPVS*>(this)->LoadSceneDataAsset();
	if (!DataAsset)
	{
		return INDEX_NONE;
	}

	const FVector3f Pos3f = (FVector3f)Position;
	for (int32 CellIndex = 0; CellIndex < DataAsset->Cells.Num(); ++CellIndex)
	{
		const FGPUPVSStoredCell& Cell = DataAsset->Cells[CellIndex];
		if (FBox3f(Cell.BoundsMin, Cell.BoundsMax).IsInside(Pos3f))
		{
			return CellIndex;
		}
	}

	return INDEX_NONE;
}

bool FGPUPVS::TogglePVSCulling(bool bEnable)
{
	if (bIsApplyingPVSCulling == bEnable)
	{
		GPUPVS_LOG(Verbose, TEXT("[Culling] Toggle ignored Enabled=%s"), bEnable ? TEXT("true") : TEXT("false"));
		return true;
	}

	if (!bEnable)
	{
		bIsApplyingPVSCulling = false;
		RestorePVSCulledComponents();
		LastPVSCullingCellIndex = INDEX_NONE;
		GPUPVS_LOG(Log, TEXT("[Culling] Disabled and restored component visibility."));
		return true;
	}

	if (!LoadSceneDataAsset())
	{
		GPUPVS_LOG(Warning, TEXT("[Culling] Enable failed because SceneData asset is unavailable."));
		return false;
	}

	bIsApplyingPVSCulling = true;
	LastPVSCullingCellIndex = INDEX_NONE;
	return true;
}

void FGPUPVS::TogglePVSVisualization(bool bEnable)
{
	if (bEnable && !LoadSceneDataAsset())
	{
		bIsVisualizingPVS = false;
		GPUPVS_LOG(Warning, TEXT("[Visualize] Enable failed because SceneData asset is unavailable."));
		return;
	}

	bIsVisualizingPVS = bEnable;
	LastPreviewCellIndex = INDEX_NONE;
#if WITH_EDITOR
	if (World)
	{
		FlushPersistentDebugLines(World);
	}
#endif
	GPUPVS_LOG(Log, TEXT("[Visualize] Enabled=%s"), bEnable ? TEXT("true") : TEXT("false"));
}

void FGPUPVS::TickPreviewForView(const FVector& ViewLocation)
{
	if (bIsVisualizingPVS)
	{
		DrawPVSVisualizationForView(ViewLocation);
	}

	if (bIsApplyingPVSCulling)
	{
		ApplyPVSCullingForView(ViewLocation);
	}
}

UPrimitiveComponent* FGPUPVS::ResolveTargetComponent(const FSoftObjectPath& ComponentPath) const
{
	return ResolvePrimitiveComponentInWorld(World, ComponentPath, ResolvedTargetComponents);
}

void FGPUPVS::ApplyPVSCullingForView(const FVector& ViewLocation)
{
	UGPUPVSSceneData* DataAsset = LoadSceneDataAsset();
	if (!World || !DataAsset)
	{
		return;
	}

	const int32 CurrentCellIndex = FindLoadedCellIndexFromPosition(ViewLocation);
	if (CurrentCellIndex == INDEX_NONE)
	{
		if (LastPVSCullingCellIndex != INDEX_NONE)
		{
			GPUPVS_LOG(Verbose, TEXT("[Culling] View left baked cells. Restoring visibility. View=%s"), *ViewLocation.ToString());
		}
		LastPVSCullingCellIndex = INDEX_NONE;
		RestorePVSCulledComponents();
		return;
	}

	int32 ResolvedCount = 0;
	int32 MissingCount = 0;
	int32 VisibleCount = 0;
	int32 HiddenCount = 0;

	for (const FGPUPVSStoredTarget& Target : DataAsset->Targets)
	{
		UPrimitiveComponent* Component = ResolveTargetComponent(Target.ComponentPath);
		if (!Component)
		{
			MissingCount++;
			continue;
		}

		ResolvedCount++;
		CacheOriginalVisibilityState(Component, OriginalComponentVisibility, OriginalComponentHiddenInGame);

		const bool bVisibleByPVS = DataAsset->IsVisible(CurrentCellIndex, Target.VisibilityId);
		ApplyPVSToComponent(Component, bVisibleByPVS, OriginalComponentVisibility, OriginalComponentHiddenInGame);

		if (bVisibleByPVS)
		{
			VisibleCount++;
		}
		else
		{
			HiddenCount++;
		}
	}

	if (CurrentCellIndex != LastPVSCullingCellIndex)
	{
		GPUPVS_LOG(Verbose, TEXT("[Culling] Applied View=%s Cell=%d Targets=%d Resolved=%d Missing=%d Visible=%d Hidden=%d"),
			*ViewLocation.ToString(),
			CurrentCellIndex,
			DataAsset->Targets.Num(),
			ResolvedCount,
			MissingCount,
			VisibleCount,
			HiddenCount);
	}

	LastPVSCullingCellIndex = CurrentCellIndex;
}

void FGPUPVS::RestorePVSCulledComponents()
{
	int32 RestoredCount = 0;
	int32 MissingCount = 0;

	for (const TPair<FSoftObjectPath, bool>& Pair : OriginalComponentVisibility)
	{
		if (UPrimitiveComponent* Component = ResolveTargetComponent(Pair.Key))
		{
			Component->SetVisibility(Pair.Value, true);
			Component->SetHiddenInGame(OriginalComponentHiddenInGame.FindRef(Pair.Key), true);
			Component->MarkRenderStateDirty();
			RestoredCount++;
		}
		else
		{
			MissingCount++;
		}
	}

	OriginalComponentVisibility.Reset();
	OriginalComponentHiddenInGame.Reset();
	ResolvedTargetComponents.Reset();

	if (RestoredCount > 0 || MissingCount > 0)
	{
		GPUPVS_LOG(Log, TEXT("[Culling] Restored components Restored=%d Missing=%d"), RestoredCount, MissingCount);
	}
}

void FGPUPVS::DrawPVSVisualizationForView(const FVector& ViewLocation)
{
#if WITH_EDITOR
	UGPUPVSSceneData* DataAsset = LoadSceneDataAsset();
	if (!World || !DataAsset)
	{
		return;
	}

	FlushPersistentDebugLines(World);

	const int32 CurrentCellIndex = FindLoadedCellIndexFromPosition(ViewLocation);
	if (CurrentCellIndex == INDEX_NONE)
	{
		DrawDebugCrosshairs(World, ViewLocation, FRotator::ZeroRotator, 100.0f, FColor::Yellow, false, 0.0f);
		if (LastPreviewCellIndex != INDEX_NONE)
		{
			GPUPVS_LOG(Verbose, TEXT("[Visualize] View outside baked cells View=%s"), *ViewLocation.ToString());
		}
		LastPreviewCellIndex = INDEX_NONE;
		return;
	}

	const FGPUPVSStoredCell& ViewerCell = DataAsset->Cells[CurrentCellIndex];
	DrawViewerCellBounds(World, ViewerCell);

	int32 VisibleCount = 0;
	int32 HiddenCount = 0;
	UWorld* CurrentWorld = World;
	CollectVisualizationStats(
		DataAsset,
		CurrentCellIndex,
		VisibleCount,
		HiddenCount,
		[CurrentWorld](const FGPUPVSStoredTarget& Target)
		{
			DrawHiddenTargetBounds(CurrentWorld, Target);
		});

	if (CurrentCellIndex != LastPreviewCellIndex)
	{
		GPUPVS_LOG(Verbose, TEXT("[Visualize] View=%s Cell=%d Visible=%d Hidden=%d"),
			*ViewLocation.ToString(),
			CurrentCellIndex,
			VisibleCount,
			HiddenCount);
	}

	LastPreviewCellIndex = CurrentCellIndex;
#endif
}
