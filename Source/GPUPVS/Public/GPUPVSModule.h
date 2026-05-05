// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class FGPUPVS;
class UWorld;

/**
 * GPUPVS Runtime 模块。
 *
 * 职责：
 * - 注册插件 Shader 目录映射（/Plugin/GPUPVS）。
 * - 为每个 UWorld 管理一个 FGPUPVS 实例（便于编辑器/命令行触发构建）。
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

private:
	TMap<UWorld*, class FGPUPVS*> WorldToPVSMap;
};
