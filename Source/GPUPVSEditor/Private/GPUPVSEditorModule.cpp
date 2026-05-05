// Copyright Epic Games, Inc. All Rights Reserved.
/**
 * GPUPVS 编辑器模块。
 *
 * 当前阶段作用：
 * - 注册编辑器菜单/按钮（用于快速触发调试入口）。
 * - 后续可逐步把控制台命令、调试 UI、可视化输出收拢到这里。
 */
#include "GPUPVSEditorModule.h"
#include "GPUPVSCommands.h"
#include "GPUPVSModule.h"

#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#endif

#define LOCTEXT_NAMESPACE "GPUPVSEditor"

IMPLEMENT_MODULE(FGPUPVSEditorModule, GPUPVSEditor)

void FGPUPVSEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FGPUPVSCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FGPUPVSCommands::Get().PluginAction1,
		FExecuteAction::CreateRaw(this, &FGPUPVSEditorModule::Button1Clicked),
		FCanExecuteAction());
	PluginCommands->MapAction(
		FGPUPVSCommands::Get().PluginAction2,
		FExecuteAction::CreateRaw(this, &FGPUPVSEditorModule::Button2Clicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGPUPVSEditorModule::RegisterMenus));
}

void FGPUPVSEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FGPUPVSCommands::Unregister();
}

void FGPUPVSEditorModule::Button1Clicked()
{
	// NOTE: Runner 测试链路已删除，这里统一走 GPUPVS.BuildPVS 的同一套入口（更利于维护/复用）。
/*
#if WITH_EDITOR
	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			if (FGPUPVS* System = FGPUPVSModule::Get().GetOrCreatePVSForWorld(World))
			{
				System->BuildPVS();
			}
		}
	}
#endif
*/
}

void FGPUPVSEditorModule::Button2Clicked()
{
	// Put your "OnButtonClicked" stuff here
	FText DialogText = FText::Format(
							LOCTEXT("Button1DialogText", "Add code to {0} in {1} to override this button's actions"),
							FText::FromString(TEXT("FGPUPVSEditorModule::Button2Clicked()")),
							FText::FromString(TEXT("GPUPVSEditorModule.cpp"))
					   );
	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

void FGPUPVSEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FGPUPVSCommands::Get().PluginAction1, PluginCommands);
			Section.AddMenuEntryWithCommandList(FGPUPVSCommands::Get().PluginAction2, PluginCommands);
		}
	}
}

#undef LOCTEXT_NAMESPACE

