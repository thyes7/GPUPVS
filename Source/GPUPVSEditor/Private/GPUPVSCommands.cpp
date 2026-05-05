// Copyright Epic Games, Inc. All Rights Reserved.
/**
 * 编辑器命令注册。
 *
 * 说明：
 * - 这里只负责声明 UI_COMMAND，与具体执行逻辑解耦。
 * - 执行逻辑在 GPUPVSEditorModule 中绑定（MapAction）。
 */

#include "GPUPVSCommands.h"

#define LOCTEXT_NAMESPACE "GPUPVS"

void FGPUPVSCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction1, "GPUPVS", "Open GPUPVS panel", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PluginAction2, "Build PVS", "Build PVS once", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
