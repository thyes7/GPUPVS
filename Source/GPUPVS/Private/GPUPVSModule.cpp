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

DEFINE_LOG_CATEGORY(LogGPUPVS);
	
IMPLEMENT_MODULE(FGPUPVSModule, GPUPVS)

void FGPUPVSModule::StartupModule()
{
    FString PluginShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("GPUPVS"))->GetBaseDir(),
		TEXT("Shaders")
	);
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPUPVS"), PluginShaderDir);

	GPUPVS_LOG(Log, TEXT("[Module] StartupModule"));
}

// 清理GPUPVS实例
void FGPUPVSModule::ShutdownModule()
{
	for (TPair<UWorld*, FGPUPVS*>& Pair : WorldToPVSMap)
	{
		delete Pair.Value;
		Pair.Value = nullptr;
	}

	WorldToPVSMap.Empty();
}

// 为当前UWorld创建GPUPVS实例
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

// 运行中删除PVS实例用
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

// 注册command命令，仅供前期快捷调试用，后期转editor命令
static FAutoConsoleCommand GPUPVSBuildCommand(
    TEXT("GPUPVS.BuildPVS"),
    TEXT("Build PVS once using GPUPVS"),
    FConsoleCommandDelegate::CreateStatic([]()
    {
#if WITH_EDITOR
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
