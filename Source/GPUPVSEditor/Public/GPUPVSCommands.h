// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

/**
 * GPUPVS 编辑器命令集合。
 * 仅负责定义命令与快捷键信息，不包含具体执行逻辑。
 */
class FGPUPVSCommands : public TCommands<FGPUPVSCommands>
{
public:

	FGPUPVSCommands()
		: TCommands<FGPUPVSCommands>(TEXT("GPUPVS"), NSLOCTEXT("Contexts", "GPUPVS", "GPUPVS Plugin"), NAME_None, FName("EditorStyle"))
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction1;
	TSharedPtr< FUICommandInfo > PluginAction2;
};
