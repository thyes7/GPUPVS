#include "GPUPVSModule.h"
#include "GPUPVS.h"
#include "GPUPVSLog.h"

#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

/**
 * GPUPVS Runtime 模块入口
 * - LogGPUPVS
 * - Shader 路径映射（/Plugin/GPUPVS）
 * - Console: GPUPVS.BuildPVS（临时）
 */
DEFINE_LOG_CATEGORY(LogGPUPVS);
	
IMPLEMENT_MODULE(FGPUPVSModule, GPUPVS)

void FGPUPVSModule::StartupModule()
{
	// 插件 shader 使用虚拟路径（供 ShaderCompileWorker）
    FString PluginShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("GPUPVS"))->GetBaseDir(),
		TEXT("Shaders")
	);
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPUPVS"), PluginShaderDir);

	GPUPVS_LOG(Log, TEXT("[Module] StartupModule"));
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
}

// 为当前 UWorld 创建/复用 GPUPVS 实例
FGPUPVS* FGPUPVSModule::GetOrCreatePVSForWorld(UWorld* InWorld)
{
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
	FGPUPVS** Found = WorldToPVSMap.Find(InWorld);
	if (Found && *Found)
	{
		delete *Found;
		*Found = nullptr;
	}

	WorldToPVSMap.Remove(InWorld);
}

// 注册 command 命令：仅供前期快捷调试用（最终建议收敛到 Editor 面板/按钮）
static FAutoConsoleCommand GPUPVSBuildCommand(
    TEXT("GPUPVS.BuildPVS"),
    TEXT("Build PVS once using GPUPVS"),
    FConsoleCommandDelegate::CreateStatic([]()
    {
#if WITH_EDITOR
		// Console command：用 EditorWorld（非 PIE）
        if (GEditor)
        {
            if (UWorld* World = GEditor->GetEditorWorldContext().World())
            {
                FGPUPVSModule& Module = FGPUPVSModule::Get();
                if (FGPUPVS* System = Module.GetOrCreatePVSForWorld(World))
                {
                    System->BuildPVS();
                }
            }
        }
#endif
    })
);
