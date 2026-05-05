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
#include "GPUPVS.h"
#include "GPUPVSEditorSettings.h"

#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

#if WITH_EDITOR
#include "Editor.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#endif

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "PropertyEditorModule.h"
#include "IDetailsView.h"

static TAutoConsoleVariable<int32> CVarGPUPVSDrawCells(
	TEXT("gpupvs.DrawCells"),
	0,
	TEXT("Draw GPUPVS cells as wire boxes in editor viewport.\n0: Off\n1: On"),
	ECVF_Default);

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

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGPUPVSEditorModule::Tick), 0.0f);

	SettingsObject = TStrongObjectPtr<UGPUPVSEditorSettings>(NewObject<UGPUPVSEditorSettings>(GetTransientPackage()));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		PanelTabName,
		FOnSpawnTab::CreateRaw(this, &FGPUPVSEditorModule::SpawnPanelTab))
		.SetDisplayName(LOCTEXT("GPUPVSPanelTitle", "GPUPVS"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FGPUPVSEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	if (TickHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanelTabName);
	DetailsView.Reset();
	SettingsObject.Reset();

	FGPUPVSCommands::Unregister();
}

void FGPUPVSEditorModule::Button1Clicked()
{
	OpenPanel();
}

void FGPUPVSEditorModule::Button2Clicked()
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	if (FGPUPVS* System = FGPUPVSModule::Get().GetOrCreatePVSForWorld(World))
	{
		ApplySettingsToSystem(*System);
		System->BuildPVS();
	}
#endif
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

void FGPUPVSEditorModule::OpenPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PanelTabName);
}

TSharedRef<SDockTab> FGPUPVSEditorModule::SpawnPanelTab(const FSpawnTabArgs& Args)
{
	if (!DetailsView.IsValid())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bAllowSearch = true;
		DetailsArgs.bHideSelectionTip = true;
		DetailsArgs.bShowScrollBar = true;
		DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

		DetailsView = PropertyEditorModule.CreateDetailView(DetailsArgs);
		DetailsView->SetObject(SettingsObject.Get());
		DetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent& PropertyChangedEvent)
		{
			if (SettingsObject.IsValid())
			{
				CVarGPUPVSDrawCells->Set(SettingsObject->bDrawCells ? 1 : 0, ECVF_SetByCode);
			}
		});
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBorder)
			.Padding(6)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					DetailsView.ToSharedRef()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 6, 0, 0)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 6, 0, 0)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(4)
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("GPUPVS_Build", "Build PVS"))
						.OnClicked_Lambda([this]()
						{
#if WITH_EDITOR
							if (GEditor)
							{
								if (UWorld* World = GEditor->GetEditorWorldContext().World())
								{
									if (FGPUPVS* System = FGPUPVSModule::Get().GetOrCreatePVSForWorld(World))
									{
										ApplySettingsToSystem(*System);
										System->BuildPVS();
									}
								}
							}
#endif
							return FReply::Handled();
						})
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("GPUPVS_Summary", "Dump Summary"))
						.OnClicked_Lambda([this]()
						{
#if WITH_EDITOR
							if (GEditor)
							{
								if (UWorld* World = GEditor->GetEditorWorldContext().World())
								{
									if (FGPUPVS* System = FGPUPVSModule::Get().FindPVSForWorld(World))
									{
										System->DumpSummary();
									}
								}
							}
#endif
							return FReply::Handled();
						})
					]
				]
			]
		];
}

void FGPUPVSEditorModule::ApplySettingsToSystem(FGPUPVS& System)
{
	if (!SettingsObject.IsValid())
	{
		return;
	}

	FGPUPVS::FParams Params;
	Params.CellSize = SettingsObject->CellSize;
	Params.PlayAreaHeight = SettingsObject->PlayAreaHeight;
	Params.bPlaceCellsOnOpaqueOnly = SettingsObject->bPlaceCellsOnOpaqueOnly;
	Params.RasterMaxLayersLimit = SettingsObject->RasterMaxLayersLimit;
	Params.VisibilityMaxDistance = SettingsObject->VisibilityMaxDistance;
	Params.VisibilityMeshBoundsScale = SettingsObject->MeshBoundsScale;
	Params.VisibilityNumCellSamples = SettingsObject->NumCellSamples;
	Params.VisibilityMinMeshSamples = SettingsObject->MinMeshSamples;
	Params.VisibilityMaxMeshSamples = SettingsObject->MaxMeshSamples;
	Params.VisibilityNumImportanceSamples = SettingsObject->NumImportanceSamples;
	Params.VisibilityBVHLeafTriLimit = SettingsObject->BVHLeafTriLimit;
	System.SetParams(Params);

	CVarGPUPVSDrawCells->Set(SettingsObject->bDrawCells ? 1 : 0, ECVF_SetByCode);
}

bool FGPUPVSEditorModule::Tick(float DeltaSeconds)
{
#if WITH_EDITOR
	if (CVarGPUPVSDrawCells.GetValueOnGameThread() == 0)
	{
		return true;
	}

	if (!GEditor)
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return true;
	}

	FGPUPVS* System = FGPUPVSModule::Get().FindPVSForWorld(World);
	if (!System)
	{
		return true;
	}

	const TArray<FPVSCell>& Cells = System->GetCells();
	if (Cells.IsEmpty())
	{
		return true;
	}

	const FColor Color(0, 255, 255);
	for (const FPVSCell& Cell : Cells)
	{
		const FVector Center = (FVector)Cell.Bounds.GetCenter();
		const FVector Extent = (FVector)Cell.Bounds.GetExtent();
		DrawDebugBox(World, Center, Extent, Color, false, 0.0f, 0, 1.0f);
	}
#endif
	return true;
}

#undef LOCTEXT_NAMESPACE

