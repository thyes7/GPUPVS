# GPUPVS

`GPUPVS` 是一个面向 Unreal Editor 的 GPU 驱动 PVS 预计算插件。

当前插件以编辑器工作流为核心，围绕以下主流程工作：

1. `Collect`：采集 `PrecomputedVisibilityVolume`、静态几何、遮挡几何与可见性目标
2. `Setup`：栅格化高度区间并生成候选 `Cells`
3. `Compute`：为每个 `Cell` 计算目标可见性
4. `WriteBack`：将结果写回 `SceneData` 资产，并供编辑器预览/剔除消费

## 当前状态

- 已具备完整的构建、加载、可视化、剔除和摘要诊断闭环
- 已完成一轮代码精简，删除废弃 shader/旧链路并收口 Editor 样板代码
- 已将 `SceneData` 资产保存职责迁移到 `Editor` 模块
- 当前仍存在若干已确认但未完全优化的技术问题，详见 `Docs/KnownIssues.md`

## 快速使用

1. 在关卡中放置 `PrecomputedVisibilityVolume`
2. 打开 `Window -> GPUPVS`
3. 点击 `开始构建`
4. 点击 `加载数据`
5. 使用 `可视化` 或 `剔除` 围绕当前编辑器视口进行预览

## 目录入口

- `Source/GPUPVS`：运行时核心与构建主流程
- `Source/GPUPVSEditor`：编辑器面板、交互与资产保存
- `Shaders/Private`：Setup 与 Compute 阶段使用的 shader
- `Docs`：架构、目录、问题与变更说明

## 文档索引

- [Architecture](file:///e:/EpicGames/UnrealProjects/StylizedParisStreet/Plugins/GPUPVS/Docs/Architecture.md)
- [DirectoryLayout](file:///e:/EpicGames/UnrealProjects/StylizedParisStreet/Plugins/GPUPVS/Docs/DirectoryLayout.md)
- [KnownIssues](file:///e:/EpicGames/UnrealProjects/StylizedParisStreet/Plugins/GPUPVS/Docs/KnownIssues.md)
- [Changelog](file:///e:/EpicGames/UnrealProjects/StylizedParisStreet/Plugins/GPUPVS/Docs/Changelog.md)
