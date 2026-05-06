// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

class FToolBarBuilder;
class FMenuBuilder;
class UGPUPVSEditorSettings;
class IDetailsView;

/**
 * GPUPVS 编辑器模块接口
 *
 * 接入编辑器菜单/按钮，触发运行时模块的 BuildPVS
 */
class FGPUPVSEditorModule : public IModuleInterface
{
public:

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	// Command callbacks
	void Button1Clicked();
	void Button2Clicked();
	
private:

	void RegisterMenus();
	bool Tick(float DeltaSeconds);
	void OpenPanel();
	TSharedRef<class SDockTab> SpawnPanelTab(const class FSpawnTabArgs& Args);
	void ApplySettingsToSystem(class FGPUPVS& System);


private:
	TSharedPtr<class FUICommandList> PluginCommands;
	FTSTicker::FDelegateHandle TickHandle;
	TStrongObjectPtr<UGPUPVSEditorSettings> SettingsObject;
	TSharedPtr<IDetailsView> DetailsView;
	FName PanelTabName = FName("GPUPVS.Panel");
};
