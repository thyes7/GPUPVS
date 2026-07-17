#include "GPUPVSModule.h"
#include "GPUPVS.h"
#include "GPUPVSLog.h"

#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/World.h"

/**
 * GPUPVS Runtime 模块入口
 * - LogGPUPVS
 * - Shader 路径映射（/Plugin/GPUPVS）
 */
DEFINE_LOG_CATEGORY(LogGPUPVS);
	
IMPLEMENT_MODULE(FGPUPVSModule, GPUPVS)

void FGPUPVSModule::StartupModule()
{
	// 插件 shader 使用虚拟路径（供 ShaderCompileWorker）
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GPUPVS"));
	if (Plugin.IsValid())
	{
		FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPUPVS"), PluginShaderDir);
	}
	else
	{
		GPUPVS_LOG(Error, TEXT("[Module] StartupModule failed to find plugin descriptor for GPUPVS."));
	}

	GPUPVS_LOG(Verbose, TEXT("[Module] StartupModule"));
}

// 清理 GPUPVS 实例
void FGPUPVSModule::ShutdownModule()
{
	// UWorld* 作为 key，切图时需要清理
	for (TPair<UWorld*, FGPUPVS*>& Pair : WorldToPVSMap)
	{
		delete Pair.Value;
		Pair.Value = nullptr;
	}

	WorldToPVSMap.Empty();
	SceneDataSaver.Unbind();
}

// 为当前 UWorld 创建/复用 GPUPVS 实例
FGPUPVS* FGPUPVSModule::GetOrCreatePVSForWorld(UWorld* InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	if (WorldToPVSMap.Find(InWorld) == nullptr)
	{
		FGPUPVS* GPUPVS = new FGPUPVS(InWorld);

		WorldToPVSMap.Add(InWorld, GPUPVS);

		return GPUPVS;
	}
	else return *WorldToPVSMap.Find(InWorld);
}

FGPUPVS* FGPUPVSModule::FindPVSForWorld(UWorld* InWorld) const
{
	FGPUPVS* const* Found = WorldToPVSMap.Find(InWorld);
	return Found ? *Found : nullptr;
}

// 运行中删除 PVS 实例用
void FGPUPVSModule::DestroyPVSForWorld(UWorld* InWorld)
{
	if (!InWorld)
	{
		return;
	}

	FGPUPVS** Found = WorldToPVSMap.Find(InWorld);
	if (Found && *Found)
	{
		delete *Found;
		*Found = nullptr;
	}

	WorldToPVSMap.Remove(InWorld);
}

void FGPUPVSModule::RegisterSceneDataSaver(FGPUPVSSceneDataSaver InSaver)
{
	SceneDataSaver = MoveTemp(InSaver);
}

void FGPUPVSModule::UnregisterSceneDataSaver()
{
	SceneDataSaver.Unbind();
}

UGPUPVSSceneData* FGPUPVSModule::SaveSceneDataAsset(FGPUPVS& System) const
{
	return SceneDataSaver.IsBound() ? SceneDataSaver.Execute(System) : nullptr;
}
