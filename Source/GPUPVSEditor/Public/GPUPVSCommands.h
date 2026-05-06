// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

/**
 * 编辑器命令
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
