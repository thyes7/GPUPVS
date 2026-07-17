// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class FGPUPVS;
class UGPUPVSSceneData;
class UWorld;
DECLARE_DELEGATE_RetVal_OneParam(UGPUPVSSceneData*, FGPUPVSSceneDataSaver, FGPUPVS&);

/**
 * GPUPVS Runtime 模块：
 * - 注册 Shader 目录映射（/Plugin/GPUPVS）
 * - 管理每个 UWorld 的 FGPUPVS 实例
 */
class GPUPVS_API FGPUPVSModule : public IModuleInterface
{
public:
	static inline FGPUPVSModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FGPUPVSModule>(TEXT("GPUPVS"));
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FGPUPVS* GetOrCreatePVSForWorld(UWorld* World);
	FGPUPVS* FindPVSForWorld(UWorld* World) const;
	void DestroyPVSForWorld(UWorld* World);
	void RegisterSceneDataSaver(FGPUPVSSceneDataSaver InSaver);
	void UnregisterSceneDataSaver();
	UGPUPVSSceneData* SaveSceneDataAsset(FGPUPVS& System) const;

private:
	TMap<UWorld*, class FGPUPVS*> WorldToPVSMap;
	FGPUPVSSceneDataSaver SceneDataSaver;
};
