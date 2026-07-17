#include "GPUPVS.h"

#include "GPUPVSLog.h"
#include "GPUPVSModule.h"
#include "GPUPVSSceneData.h"
#include "Misc/PackageName.h"

namespace
{
	static FString NormalizeSceneDataLevelName(FString LevelName)
	{
		if (LevelName.StartsWith(TEXT("UEDPIE_")))
		{
			const int32 FirstUnderscore = LevelName.Find(TEXT("_"));
			const int32 SecondUnderscore = LevelName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstUnderscore + 1);
			if (SecondUnderscore != INDEX_NONE)
			{
				LevelName = LevelName.RightChop(SecondUnderscore + 1);
			}
		}

		return LevelName;
	}

	static FString BuildSceneDataObjectPath(const FString& AssetPath)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
		return FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
	}

	static bool IsSceneDataValid(const UGPUPVSSceneData* DataAsset)
	{
		return DataAsset
			&& DataAsset->Cells.Num() > 0
			&& DataAsset->Targets.Num() > 0
			&& DataAsset->VisibilityBitmap.Num() > 0
			&& DataAsset->BytesPerCell > 0;
	}
}

void FGPUPVS::WriteBack()
{
	const int32 NumCells = Scene.GetCells().Num();
	const uint32 NumMeshes = Scene.GetVisibilityNumMeshes();
	const uint32 WordsPerCell = Scene.GetVisibilityWordsPerCell();
	const int32 TotalWords = Scene.GetVisibilityWords().Num();

	if (NumCells == 0 || NumMeshes == 0 || WordsPerCell == 0 || TotalWords == 0)
	{
		GPUPVS_LOG(Warning, TEXT("[WriteBack] Skipped Cells=%d Meshes=%u WordsPerCell=%u TotalWords=%d"),
			NumCells,
			NumMeshes,
			WordsPerCell,
			TotalWords);
		return;
	}

	if (WriteBackParams.bEnablePreviewLog)
	{
		GPUPVS_LOG(Log, TEXT("[WriteBack] Preview Cells=%d Meshes=%u WordsPerCell=%u TotalWords=%d"),
			NumCells,
			NumMeshes,
			WordsPerCell,
			TotalWords);
	}

	if (!WriteBackParams.bEnableEngineWriteBack)
	{
		GPUPVS_LOG(Log, TEXT("[WriteBack] SceneData asset writeback disabled."));
		return;
	}

	if (UGPUPVSSceneData* SavedAsset = FGPUPVSModule::Get().SaveSceneDataAsset(*this))
	{
		SetLoadedSceneDataAsset(SavedAsset);
		GPUPVS_LOG(Log, TEXT("[WriteBack] Saved SceneData asset=%s Cells=%d Targets=%u BitmapBytes=%d"),
			*SavedAsset->GetPathName(),
			SavedAsset->Cells.Num(),
			SavedAsset->Targets.Num(),
			SavedAsset->VisibilityBitmap.Num());
	}
	else
	{
		GPUPVS_LOG(Error, TEXT("[WriteBack] Failed to save SceneData asset. No editor saver was available."));
	}
}

FString FGPUPVS::GetSceneDataLevelName() const
{
	if (!World)
	{
		return TEXT("Unknown");
	}

	const FString LevelName = FPackageName::GetShortName(World->GetOutermost()->GetName());
	return NormalizeSceneDataLevelName(LevelName);
}

FString FGPUPVS::GetDefaultSceneDataAssetPath() const
{
	const FString LevelName = GetSceneDataLevelName();
	return FString::Printf(TEXT("/Game/PVSData/PVS_%s"), *LevelName);
}

FString FGPUPVS::GetDefaultSceneDataObjectPath() const
{
	return BuildSceneDataObjectPath(GetDefaultSceneDataAssetPath());
}

UGPUPVSSceneData* FGPUPVS::LoadSceneDataAsset()
{
	if (IsSceneDataValid(LoadedSceneDataAsset.Get()))
	{
		return LoadedSceneDataAsset.Get();
	}

	const FString AssetPath = GetDefaultSceneDataAssetPath();
	const FString ObjectPath = GetDefaultSceneDataObjectPath();

	UGPUPVSSceneData* LoadedAsset = FindObject<UGPUPVSSceneData>(nullptr, *ObjectPath);
	if (!LoadedAsset)
	{
		LoadedAsset = LoadObject<UGPUPVSSceneData>(nullptr, *ObjectPath);
	}
	if (!LoadedAsset)
	{
		LoadedAsset = Cast<UGPUPVSSceneData>(StaticLoadObject(UGPUPVSSceneData::StaticClass(), nullptr, *AssetPath));
	}

	if (IsSceneDataValid(LoadedAsset))
	{
		LoadedSceneDataAsset = LoadedAsset;
		GPUPVS_LOG(Log, TEXT("[WriteBack] Loaded SceneData asset=%s Cells=%d Targets=%d BitmapBytes=%d BytesPerCell=%d"),
			*LoadedAsset->GetPathName(),
			LoadedAsset->Cells.Num(),
			LoadedAsset->Targets.Num(),
			LoadedAsset->VisibilityBitmap.Num(),
			LoadedAsset->BytesPerCell);
		return LoadedAsset;
	}

	if (LoadedAsset)
	{
		GPUPVS_LOG(Warning, TEXT("[WriteBack] SceneData asset is invalid: %s Cells=%d Targets=%d BitmapBytes=%d BytesPerCell=%d"),
			*LoadedAsset->GetPathName(),
			LoadedAsset->Cells.Num(),
			LoadedAsset->Targets.Num(),
			LoadedAsset->VisibilityBitmap.Num(),
			LoadedAsset->BytesPerCell);
	}
	else
	{
		GPUPVS_LOG(Warning, TEXT("[WriteBack] SceneData asset not found. Level=%s Asset=%s Object=%s"),
			*GetSceneDataLevelName(),
			*AssetPath,
			*ObjectPath);
	}

	LoadedSceneDataAsset.Reset();
	return nullptr;
}

bool FGPUPVS::IsVisibleFromLoadedAsset(int32 ViewerCellIndex, int32 TargetVisibilityId) const
{
	if (LoadedSceneDataAsset.IsValid())
	{
		return LoadedSceneDataAsset->IsVisible(ViewerCellIndex, TargetVisibilityId);
	}

	return false;
}
