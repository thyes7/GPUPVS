// Copyright Epic Games, Inc. All Rights Reserved.
/**
 * 编辑器命令注册：
 * - UI_COMMAND 声明
 * - 绑定逻辑在 GPUPVSEditorModule（MapAction）
 */

#include "GPUPVSCommands.h"

#define LOCTEXT_NAMESPACE "GPUPVS"

void FGPUPVSCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction1, "GPUPVS", "Open GPUPVS panel", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PluginAction2, "Build PVS", "Build PVS once", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
