// Copyright Epic Games, Inc. All Rights Reserved.
/**
 * GPUPVS 编辑器模块
 *
 * - 注册菜单/按钮
 * - 提供调试入口
 */
#include "GPUPVSEditorModule.h"
#include "GPUPVSModule.h"
#include "GPUPVS.h"
#include "GPUPVSLog.h"
#include "GPUPVSSceneData.h"
#include "GPUPVSEditorSettings.h"

#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ToolMenus.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#endif

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
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

namespace
{
	static const TCHAR* GetActionLogLabel(FGPUPVSEditorModule::EEditorAction Action)
	{
		switch (Action)
		{
		case FGPUPVSEditorModule::EEditorAction::Build: return TEXT("Build");
		case FGPUPVSEditorModule::EEditorAction::Load: return TEXT("Load");
		case FGPUPVSEditorModule::EEditorAction::Visualize: return TEXT("Visualize");
		case FGPUPVSEditorModule::EEditorAction::Culling: return TEXT("Culling");
		case FGPUPVSEditorModule::EEditorAction::Restore: return TEXT("Restore");
		case FGPUPVSEditorModule::EEditorAction::Summary: return TEXT("Summary");
		default: return TEXT("Unknown");
		}
	}

	static FSlateColor MakeTintColor(bool bActive, const FLinearColor& ActiveColor)
	{
		return bActive ? FSlateColor(ActiveColor) : FSlateColor::UseForeground();
	}

	static TSharedRef<SWidget> MakePanelCard(const FText& Title, const FText& Subtitle, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBorder)
			.Padding(12)
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Title)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4, 0, 10)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.0f)))
					.Text(Subtitle)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					Content
				]
			];
	}

	static TSharedRef<SWidget> MakeActionButton(
		const FText& Title,
		const FText& Subtitle,
		const FText& ToolTip,
		const FOnClicked& OnClicked,
		const TAttribute<FSlateColor>& ButtonColor = TAttribute<FSlateColor>())
	{
		return SNew(SButton)
			.ToolTipText(ToolTip)
			.ContentPadding(FMargin(10.0f, 8.0f))
			.ButtonColorAndOpacity(ButtonColor)
			.OnClicked(OnClicked)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Title)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 4, 0, 0)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))
					.Text(Subtitle)
				]
			];
	}
}

#define LOCTEXT_NAMESPACE "GPUPVSEditor"

IMPLEMENT_MODULE(FGPUPVSEditorModule, GPUPVSEditor)

void FGPUPVSEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGPUPVSEditorModule::RegisterMenus));
	FGPUPVSModule::Get().RegisterSceneDataSaver(FGPUPVSSceneDataSaver::CreateRaw(this, &FGPUPVSEditorModule::SaveSceneDataAsset));

	// Ticker 做每帧 debug 绘制
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGPUPVSEditorModule::Tick), 0.0f);

	// 会话内调参（transient）
	SettingsObject = TStrongObjectPtr<UGPUPVSEditorSettings>(NewObject<UGPUPVSEditorSettings>(GetTransientPackage()));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		PanelTabName,
		FOnSpawnTab::CreateRaw(this, &FGPUPVSEditorModule::SpawnPanelTab))
		.SetDisplayName(LOCTEXT("GPUPVSPanelTitle", "GPUPVS 面板 (Panel)"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FGPUPVSEditorModule::ShutdownModule()
{
	// Cleanup
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
	FGPUPVSModule::Get().UnregisterSceneDataSaver();
}

void FGPUPVSEditorModule::ButtonOpenPanelClicked()
{
	GPUPVS_LOG(Log, TEXT("[Editor] OpenPanel button clicked."));
	OpenPanel();
}

void FGPUPVSEditorModule::RegisterMenus()
{
	// Owner scope
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("GPUPVS");
			Section.AddMenuEntry(
				"GPUPVS.OpenPanel",
				LOCTEXT("GPUPVS_OpenPanel_Menu", "打开 GPUPVS 面板 (Open Panel)"),
				LOCTEXT("GPUPVS_OpenPanel_MenuTooltip", "打开 GPUPVS 工具面板。"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FGPUPVSEditorModule::ButtonOpenPanelClicked)));
		}
	}
}

void FGPUPVSEditorModule::OpenPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PanelTabName);
}

TSharedRef<SWidget> FGPUPVSEditorModule::MakeEditorActionButton(
	const FText& Title,
	const FText& Subtitle,
	const FText& ToolTip,
	EEditorAction Action,
	bool bUseTint,
	EButtonTint Tint)
{
	const TAttribute<FSlateColor> ButtonColor = bUseTint
		? TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateRaw(this, &FGPUPVSEditorModule::GetActionButtonColor, Tint))
		: TAttribute<FSlateColor>();

	return MakeActionButton(
		Title,
		Subtitle,
		ToolTip,
		FOnClicked::CreateRaw(this, &FGPUPVSEditorModule::HandleActionClicked, Action),
		ButtonColor);
}

UWorld* FGPUPVSEditorModule::GetEditorWorld() const
{
#if WITH_EDITOR
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
#endif
	return nullptr;
}

bool FGPUPVSEditorModule::GetActiveViewLocation(FVector& OutViewLocation) const
{
#if WITH_EDITOR
	if (GEditor && GEditor->GetActiveViewport())
	{
		if (FViewportClient* ViewportClientBase = GEditor->GetActiveViewport()->GetClient())
		{
			if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(ViewportClientBase))
			{
				OutViewLocation = ViewportClient->GetViewLocation();
				return true;
			}
		}
	}
#endif

	OutViewLocation = FVector::ZeroVector;
	return false;
}

FText FGPUPVSEditorModule::BuildStatusText() const
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return LOCTEXT("GPUPVS_Status_NoWorld", "状态：未找到编辑器世界");
	}

	const FGPUPVS* System = FGPUPVSModule::Get().FindPVSForWorld(World);
	if (!System)
	{
		return FText::Format(
			LOCTEXT("GPUPVS_Status_NoSystem", "状态：{0} | 尚未创建 PVS 会话"),
			FText::FromString(World->GetName()));
	}

	const bool bDrawCells = CVarGPUPVSDrawCells.GetValueOnGameThread() != 0;
	return FText::Format(
		LOCTEXT("GPUPVS_Status_Ready", "状态：{0} | Cells {1} | 数据 {2} | 可视化 {3} | 剔除 {4} | 线框 {5}"),
		FText::FromString(World->GetName()),
		FText::AsNumber(System->GetCells().Num()),
		System->HasLoadedSceneDataAsset() ? LOCTEXT("GPUPVS_Status_Loaded", "已加载") : LOCTEXT("GPUPVS_Status_Unloaded", "未加载"),
		System->IsPVSVisualizationEnabled() ? LOCTEXT("GPUPVS_Status_On", "开") : LOCTEXT("GPUPVS_Status_Off", "关"),
		System->IsPVSCullingEnabled() ? LOCTEXT("GPUPVS_Status_On2", "开") : LOCTEXT("GPUPVS_Status_Off2", "关"),
		bDrawCells ? LOCTEXT("GPUPVS_Status_On3", "开") : LOCTEXT("GPUPVS_Status_Off3", "关"));
}

FSlateColor FGPUPVSEditorModule::GetActionButtonColor(EButtonTint Tint)
{
	if (const FGPUPVS* System = GetSystemForEditorWorld(false))
	{
		switch (Tint)
		{
		case EButtonTint::Load:
			return MakeTintColor(System->HasLoadedSceneDataAsset(), FLinearColor(0.18f, 0.30f, 0.55f, 1.0f));
		case EButtonTint::Visualization:
			return MakeTintColor(System->IsPVSVisualizationEnabled(), FLinearColor(0.12f, 0.42f, 0.28f, 1.0f));
		case EButtonTint::Culling:
			return MakeTintColor(System->IsPVSCullingEnabled(), FLinearColor(0.55f, 0.24f, 0.12f, 1.0f));
		default:
			break;
		}
	}

	return FSlateColor::UseForeground();
}

FGPUPVS* FGPUPVSEditorModule::GetSystemForEditorWorld(bool bCreateIfMissing)
{
#if WITH_EDITOR
	if (UWorld* World = GetEditorWorld())
	{
		FGPUPVSModule& Module = FGPUPVSModule::Get();
		return bCreateIfMissing ? Module.GetOrCreatePVSForWorld(World) : Module.FindPVSForWorld(World);
	}
#endif
	return nullptr;
}

void FGPUPVSEditorModule::RefreshPreviewForActiveView(FGPUPVS& System) const
{
	FVector ViewLocation = FVector::ZeroVector;
	if (GetActiveViewLocation(ViewLocation))
	{
		System.TickPreviewForView(ViewLocation);
	}
}

FReply FGPUPVSEditorModule::HandleActionClicked(EEditorAction Action)
{
#if WITH_EDITOR
	const bool bCreateIfMissing = Action != EEditorAction::Restore && Action != EEditorAction::Summary;
	FGPUPVS* System = GetSystemForEditorWorld(bCreateIfMissing);
	if (!System)
	{
		GPUPVS_LOG(Warning, TEXT("[Editor] %s failed. No %s was available."),
			GetActionLogLabel(Action),
			bCreateIfMissing ? TEXT("editor world") : TEXT("active PVS system"));
		return FReply::Handled();
	}
	
	switch (Action)
	{
	case EEditorAction::Build:
		ApplySettingsToSystem(*System);
		GPUPVS_LOG(Log, TEXT("[Editor] Build started. World=%s"), *GetNameSafe(GetEditorWorld()));
		System->BuildPVS();
		break;
	case EEditorAction::Load:
		if (UGPUPVSSceneData* DataAsset = System->LoadSceneDataAsset())
		{
			GPUPVS_LOG(Log, TEXT("[Editor] Load succeeded. World=%s Asset=%s Cells=%d Targets=%d"),
				*GetNameSafe(GetEditorWorld()),
				*DataAsset->GetPathName(),
				DataAsset->Cells.Num(),
				DataAsset->Targets.Num());
		}
		else
		{
			GPUPVS_LOG(Warning, TEXT("[Editor] Load failed. World=%s No valid SceneData asset was found."),
				*GetNameSafe(GetEditorWorld()));
		}
		break;
	case EEditorAction::Visualize:
		{
		const bool bEnable = !System->IsPVSVisualizationEnabled();
		System->TogglePVSVisualization(bEnable);
		RefreshPreviewForActiveView(*System);
		GPUPVS_LOG(Log, TEXT("[Editor] Visualize %s. World=%s"),
			bEnable ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetEditorWorld()));
		break;
		}
	case EEditorAction::Culling:
		{
		const bool bEnable = !System->IsPVSCullingEnabled();
		System->TogglePVSCulling(bEnable);
		RefreshPreviewForActiveView(*System);
		GPUPVS_LOG(Log, TEXT("[Editor] Culling %s. World=%s"),
			bEnable ? TEXT("true") : TEXT("false"),
			*GetNameSafe(GetEditorWorld()));
		break;
		}
	case EEditorAction::Restore:
		System->TogglePVSCulling(false);
		System->TogglePVSVisualization(false);
		GPUPVS_LOG(Log, TEXT("[Editor] Restore completed. World=%s"), *GetNameSafe(GetEditorWorld()));
		break;
	case EEditorAction::Summary:
		System->DumpSummary();
		GPUPVS_LOG(Log, TEXT("[Editor] Summary requested. World=%s"), *GetNameSafe(GetEditorWorld()));
		break;
	default:
		break;
	}
#endif
	return FReply::Handled();
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
		DetailsView->OnFinishedChangingProperties().AddRaw(this, &FGPUPVSEditorModule::HandleSettingsChanged);
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8, 8, 8, 0)
			[
				MakePanelCard(
					LOCTEXT("GPUPVS_HeroTitle", "GPUPVS"),
					LOCTEXT("GPUPVS_HeroSubtitle", "先构建，再加载，然后围绕当前视口做预览或剔除。"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.88f, 1.0f, 1.0f)))
						.AutoWrapText(true)
						.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FGPUPVSEditorModule::BuildStatusText)))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 8, 0, 0)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("GPUPVS_PrerequisiteHint", "前置条件：关卡内需要放置 PrecomputedVisibilityVolume。"))
					]
				)
			]
			+ SScrollBox::Slot()
			.Padding(8, 8, 8, 0)
			[
				MakePanelCard(
					LOCTEXT("GPUPVS_BuildCardTitle", "构建与加载"),
					LOCTEXT("GPUPVS_BuildCardSubtitle", "先生成结果，再把当前关卡对应的 SceneData 载入面板。"),
					SNew(SUniformGridPanel)
					.SlotPadding(6)
					+ SUniformGridPanel::Slot(0, 0)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_Build", "开始构建"),
							LOCTEXT("GPUPVS_BuildSub", "执行完整的 PVS 烘焙流程。"),
							LOCTEXT("GPUPVS_Build_Tooltip", "执行完整 PVS 构建流程：Collect、Setup、Compute、WriteBack。"),
							EEditorAction::Build)
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_LoadSceneData", "加载数据"),
							LOCTEXT("GPUPVS_LoadSceneDataSub", "读取当前关卡对应的 SceneData。"),
							LOCTEXT("GPUPVS_LoadSceneData_Tooltip", "加载当前关卡对应的 SceneData 资产，用于可视化和剔除。"),
							EEditorAction::Load,
							true,
							EButtonTint::Load)
					])
			]
			+ SScrollBox::Slot()
			.Padding(8, 8, 8, 0)
			[
				MakePanelCard(
					LOCTEXT("GPUPVS_PreviewCardTitle", "预览与剔除"),
					LOCTEXT("GPUPVS_PreviewCardSubtitle", "围绕当前编辑器视口位置工作。"),
					SNew(SUniformGridPanel)
					.SlotPadding(6)
					+ SUniformGridPanel::Slot(0, 0)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_ToggleVisualize", "可视化"),
							LOCTEXT("GPUPVS_ToggleVisualizeSub", "显示当前所在 Cell 的可见性结果。"),
							LOCTEXT("GPUPVS_ToggleVisualize_Tooltip", "根据当前相机所在 Cell 绘制可见性调试结果。"),
							EEditorAction::Visualize,
							true,
							EButtonTint::Visualization)
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_ToggleCulling", "剔除"),
							LOCTEXT("GPUPVS_ToggleCullingSub", "按当前视口所在 Cell 隐藏不可见目标。"),
							LOCTEXT("GPUPVS_ToggleCulling_Tooltip", "根据当前相机所在 Cell 临时隐藏不可见目标。"),
							EEditorAction::Culling,
							true,
							EButtonTint::Culling)
					]
					+ SUniformGridPanel::Slot(0, 1)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_RestoreCulling", "恢复"),
							LOCTEXT("GPUPVS_RestoreCullingSub", "撤销 PVS 临时修改过的显示状态。"),
							LOCTEXT("GPUPVS_RestoreCulling_Tooltip", "恢复被 PVS 临时修改过的组件显示状态。"),
							EEditorAction::Restore)
					])
			]
			+ SScrollBox::Slot()
			.Padding(8, 8, 8, 0)
			[
				MakePanelCard(
					LOCTEXT("GPUPVS_DiagnoseCardTitle", "诊断"),
					LOCTEXT("GPUPVS_DiagnoseCardSubtitle", "默认保持安静，需要时再主动看摘要。"),
					SNew(SUniformGridPanel)
					.SlotPadding(6)
					+ SUniformGridPanel::Slot(0, 0)
					[
						MakeEditorActionButton(
							LOCTEXT("GPUPVS_Summary", "查看摘要"),
							LOCTEXT("GPUPVS_SummarySub", "打印 Cells、Targets 与可见性统计。"),
							LOCTEXT("GPUPVS_Summary_Tooltip", "输出当前 PVS 系统的统计摘要，用于快速诊断。"),
							EEditorAction::Summary)
					])
			]
			+ SScrollBox::Slot()
			.Padding(8)
			[
				MakePanelCard(
					LOCTEXT("GPUPVS_SettingsCardTitle", "参数"),
					LOCTEXT("GPUPVS_SettingsCardSubtitle", "常用参数优先，高级项折叠在 Advanced 中。"),
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.Padding(0)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GPUPVS_SettingsExpandTitle", "参数面板"))
					]
					.BodyContent()
					[
						SNew(SBox)
						.MinDesiredHeight(420.0f)
						[
							DetailsView.ToSharedRef()
						]
					])
			]
		];
}

void FGPUPVSEditorModule::HandleSettingsChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	(void)PropertyChangedEvent;
	SyncEditorDebugSettings();
}

void FGPUPVSEditorModule::SyncEditorDebugSettings() const
{
	if (SettingsObject.IsValid())
	{
		CVarGPUPVSDrawCells->Set(SettingsObject->bDrawCells ? 1 : 0, ECVF_SetByCode);
	}
}

UGPUPVSSceneData* FGPUPVSEditorModule::SaveSceneDataAsset(FGPUPVS& System) const
{
#if WITH_EDITOR
	const FPVSScene& Scene = System.GetScene();
	const TArray<FPVSCell>& Cells = Scene.GetCells();
	const TArray<FBox3f>& MeshBounds = Scene.GetVisibilityMeshBounds();
	const TArray<FSoftObjectPath>& MeshPaths = Scene.GetVisibilityMeshComponentPaths();
	const TArray<uint32>& VisibilityWords = Scene.GetVisibilityWords();
	const uint32 WordsPerCell = Scene.GetVisibilityWordsPerCell();
	const int32 BytesPerCell = (int32)(WordsPerCell * sizeof(uint32));

	if (Cells.IsEmpty() || MeshBounds.IsEmpty() || VisibilityWords.IsEmpty() || BytesPerCell <= 0 || MeshBounds.Num() != MeshPaths.Num())
	{
		return nullptr;
	}

	const FString AssetPath = System.GetDefaultSceneDataAssetPath();
	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

	UPackage* Package = CreatePackage(*AssetPath);
	UGPUPVSSceneData* DataAsset = FindObject<UGPUPVSSceneData>(Package, *AssetName);
	if (!DataAsset)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		DataAsset = Cast<UGPUPVSSceneData>(AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UGPUPVSSceneData::StaticClass(), nullptr));
	}

	if (!DataAsset)
	{
		return nullptr;
	}

	DataAsset->Reset();
	DataAsset->CellSize = Scene.GetSettings().CellSize;
	DataAsset->PlayAreaHeight = Scene.GetSettings().PlayAreaHeight;
	FBox3f SceneBounds(ForceInit);
	for (const FPVSCell& Cell : Cells)
	{
		SceneBounds += Cell.Bounds;
	}
	DataAsset->SceneBounds = (FBox)SceneBounds;
	DataAsset->BytesPerCell = BytesPerCell;

	DataAsset->Cells.Reserve(Cells.Num());
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const FPVSCell& Cell = Cells[CellIndex];
		FGPUPVSStoredCell StoredCell;
		StoredCell.Index = Cell.Index;
		StoredCell.Center = Cell.Center;
		StoredCell.BoundsMin = Cell.Bounds.Min;
		StoredCell.BoundsMax = Cell.Bounds.Max;
		StoredCell.DataOffset = CellIndex * BytesPerCell;
		DataAsset->Cells.Add(StoredCell);
	}

	DataAsset->Targets.Reserve(MeshBounds.Num());
	for (int32 TargetIndex = 0; TargetIndex < MeshBounds.Num(); ++TargetIndex)
	{
		FGPUPVSStoredTarget StoredTarget;
		StoredTarget.VisibilityId = TargetIndex;
		StoredTarget.BoundsMin = MeshBounds[TargetIndex].Min;
		StoredTarget.BoundsMax = MeshBounds[TargetIndex].Max;
		StoredTarget.ComponentPath = MeshPaths[TargetIndex];
		DataAsset->Targets.Add(StoredTarget);
	}

	DataAsset->VisibilityBitmap.SetNumUninitialized(VisibilityWords.Num() * (int32)sizeof(uint32));
	FMemory::Memcpy(DataAsset->VisibilityBitmap.GetData(), VisibilityWords.GetData(), DataAsset->VisibilityBitmap.Num());
	DataAsset->MarkPackageDirty();

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(DataAsset->GetOutermost());
	UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);

	System.SetLoadedSceneDataAsset(DataAsset);
	return DataAsset;
#else
	return nullptr;
#endif
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
	Params.Setup.RasterMaxLayersLimit = SettingsObject->RasterMaxLayersLimit;
	Params.Setup.RangeMergeEpsilon = SettingsObject->RangeMergeEpsilon;
	Params.Setup.MaxCellsPerColumn = SettingsObject->MaxCellsPerColumn;
	Params.Setup.CellBottomOffset = SettingsObject->CellBottomOffset;
	Params.Setup.PlacementProbeHeightA = SettingsObject->PlacementProbeHeightA;
	Params.Setup.PlacementProbeHeightB = SettingsObject->PlacementProbeHeightB;
	Params.Setup.PlacementProbeHeightC = SettingsObject->PlacementProbeHeightC;
	Params.Setup.RasterReadbackTimeoutSeconds = SettingsObject->RasterReadbackTimeoutSeconds;
	Params.Setup.RasterReadbackPollSeconds = SettingsObject->RasterReadbackPollSeconds;
	Params.Compute.MaxDistance = SettingsObject->MaxDistance;
	Params.Compute.MeshBoundsScale = SettingsObject->MeshBoundsScale;
	Params.Compute.NumCellSamples = SettingsObject->NumCellSamples;
	Params.Compute.MinMeshSamples = SettingsObject->MinMeshSamples;
	Params.Compute.MaxMeshSamples = SettingsObject->MaxMeshSamples;
	Params.Compute.NumImportanceSamples = SettingsObject->NumImportanceSamples;
	Params.Compute.BVHLeafTriLimit = SettingsObject->BVHLeafTriLimit;
	Params.Compute.VisibilityReadbackTimeoutSeconds = SettingsObject->VisibilityReadbackTimeoutSeconds;
	Params.Compute.VisibilityReadbackPollSeconds = SettingsObject->VisibilityReadbackPollSeconds;
	Params.WriteBack.bEnablePreviewLog = SettingsObject->bEnableWriteBackPreviewLog;
	Params.WriteBack.bEnableEngineWriteBack = SettingsObject->bEnableEngineWriteBack;
	System.SetParams(Params);
	SyncEditorDebugSettings();
}

bool FGPUPVSEditorModule::Tick(float DeltaSeconds)
{
#if WITH_EDITOR
	(void)DeltaSeconds;
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return true;
	}

	FGPUPVS* System = FGPUPVSModule::Get().FindPVSForWorld(World);
	if (!System)
	{
		return true;
	}

	FVector ViewLocation = FVector::ZeroVector;
	if (GetActiveViewLocation(ViewLocation))
	{
		System->TickPreviewForView(ViewLocation);
	}

	if (CVarGPUPVSDrawCells.GetValueOnGameThread() == 0)
	{
		return true;
	}

	const TArray<FPVSCell>& Cells = System->GetCells();
	if (Cells.IsEmpty())
	{
		return true;
	}

	for (const FPVSCell& Cell : Cells)
	{
		const FVector Center = (FVector)Cell.Bounds.GetCenter();
		const FVector Extent = (FVector)Cell.Bounds.GetExtent();
		DrawDebugBox(World, Center, Extent, FColor(0, 255, 255), false, 0.0f, 0, 1.0f);
	}
#endif
	return true;
}

#undef LOCTEXT_NAMESPACE
