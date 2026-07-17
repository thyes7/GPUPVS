// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"

class UGPUPVSEditorSettings;
class UGPUPVSSceneData;
class IDetailsView;

/**
 * GPUPVS 编辑器模块接口
 *
 * 接入编辑器菜单/按钮，触发运行时模块的 BuildPVS
 */
class FGPUPVSEditorModule : public IModuleInterface
{
public:
	enum class EEditorAction : uint8
	{
		Build,
		Load,
		Visualize,
		Culling,
		Restore,
		Summary
	};

	enum class EButtonTint : uint8
	{
		Load,
		Visualization,
		Culling
	};

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	// Command callbacks
	void ButtonOpenPanelClicked();
	
private:
	void RegisterMenus();
	bool Tick(float DeltaSeconds);
	void OpenPanel();
	TSharedRef<class SDockTab> SpawnPanelTab(const class FSpawnTabArgs& Args);
	TSharedRef<class SWidget> MakeEditorActionButton(
		const FText& Title,
		const FText& Subtitle,
		const FText& ToolTip,
		EEditorAction Action,
		bool bUseTint = false,
		EButtonTint Tint = EButtonTint::Load);
	void ApplySettingsToSystem(class FGPUPVS& System);
	void HandleSettingsChanged(const struct FPropertyChangedEvent& PropertyChangedEvent);
	void SyncEditorDebugSettings() const;
	class UWorld* GetEditorWorld() const;
	FText BuildStatusText() const;
	FSlateColor GetActionButtonColor(EButtonTint Tint);
	bool GetActiveViewLocation(FVector& OutViewLocation) const;
	class FGPUPVS* GetSystemForEditorWorld(bool bCreateIfMissing);
	void RefreshPreviewForActiveView(class FGPUPVS& System) const;
	FReply HandleActionClicked(EEditorAction Action);
	UGPUPVSSceneData* SaveSceneDataAsset(class FGPUPVS& System) const;

private:
	FTSTicker::FDelegateHandle TickHandle;
	TStrongObjectPtr<UGPUPVSEditorSettings> SettingsObject;
	TSharedPtr<IDetailsView> DetailsView;
	FName PanelTabName = FName("GPUPVS.Panel");
};
