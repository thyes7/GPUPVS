// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

class FToolBarBuilder;
class FMenuBuilder;

/**
 * GPUPVS 编辑器模块接口。
 *
 * 主要用于调试期快速接入编辑器菜单/按钮，触发运行时模块的 BuildPVS 流程。
 */
class FGPUPVSEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command. */
	void Button1Clicked();
	void Button2Clicked();
	
private:

	void RegisterMenus();
	bool Tick(float DeltaSeconds);


private:
	TSharedPtr<class FUICommandList> PluginCommands;
	FTSTicker::FDelegateHandle TickHandle;
};
