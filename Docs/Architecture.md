# GPUPVS 架构说明

## 目标

`GPUPVS` 的目标是在编辑器内完成 GPU 驱动的 PVS 预计算，并提供可回看、可加载、可预览的结果资产。

当前插件以编辑器工作流为主，不以打包后的 Runtime 模块能力为目标。

## 总体流水线

### 1. Collect

入口：

- `FGPUPVS::BuildPVS()`
- `FPVSScene::CollectFromWorld()`

职责：

- 收集 `PrecomputedVisibilityVolume`
- 收集 Setup 用静态几何
- 收集 Compute 用遮挡几何
- 收集可见性目标包围盒与组件路径

主要文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Source/GPUPVS/Private/GPUPVSScene.cpp`
- `Source/GPUPVS/Public/GPUPVSScene.h`

### 2. Setup

入口：

- `FGPUPVS::SetupPrecomputedVisibility()`

职责：

- 计算对齐后的可见性体积边界
- 使用 `GPUPVSRasterAABBCS.usf` 栅格化三角形高度区间
- 合并高度层并生成最终 `Cells`

主要文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Source/GPUPVS/Private/GPUPVSShaders.cpp`
- `Shaders/Private/GPUPVSRasterAABBCS.usf`

### 3. Compute

入口：

- `FGPUPVS::ComputeVisibility()`

职责：

- 构建 GPU 侧 BVH
- 执行 `Group` 可见性 pass
- 执行 `Mesh` 可见性 pass
- 回读 bitset 并写回 `FPVSScene`

主要文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Source/GPUPVS/Private/GPUPVSBVH.cpp`
- `Source/GPUPVS/Private/GPUPVSShaders.cpp`
- `Shaders/Private/GPUPVSVisibilityCS.usf`

### 4. WriteBack

入口：

- `FGPUPVS::WriteBack()`

职责：

- 输出构建摘要
- 在启用时请求 `Editor` 模块保存 `SceneData` 资产
- 维护已加载资产缓存

主要文件：

- `Source/GPUPVS/Private/GPUPVSSceneDataIO.cpp`
- `Source/GPUPVS/Public/GPUPVSModule.h`
- `Source/GPUPVSEditor/Private/GPUPVSEditorModule.cpp`

## 模块分工

### `GPUPVS`

职责：

- 持有 `FGPUPVS` 系统对象
- 执行 `Collect -> Setup -> Compute -> WriteBack`
- 管理每个 `UWorld` 对应的系统实例
- 提供 `SceneData` 加载、剔除应用和预览消费逻辑

边界：

- 不再直接依赖 `AssetTools` 或 `UnrealEd`
- 通过模块回调把资产保存交给 `GPUPVSEditor`

### `GPUPVSEditor`

职责：

- 注册菜单与面板
- 持有会话内参数对象
- 驱动构建、加载、可视化和剔除入口
- 保存 `SceneData` 资产

边界：

- 负责编辑器视口位置获取与面板交互
- 不直接参与核心算法实现

## 当前代码组织原则

- `FGPUPVS` 负责核心流程编排
- `FPVSScene` 负责扁平化场景数据快照
- `GPUPVSConsumer.cpp` 负责已加载结果的消费与应用
- `GPUPVSSceneDataIO.cpp` 负责 `SceneData` 路径、加载与 writeback 协调
- `GPUPVSEditorModule.cpp` 负责面板与编辑器入口

## 已确认的工程边界

- Runtime 与 Editor 的职责已做第一轮拆分，资产保存放在 Editor
- 废弃的 `SimpleComputeShader` 与旧的单阶段 `Visibility` 链路已删除
- 当前 shader 主链路只保留：
  - `GPUPVSRasterAABBCS.usf`
  - `GPUPVSVisibilityCS.usf`

## 仍待继续优化的部分

- Setup 仍采用厚区间语义，尚未完全对齐参考工程
- Compute 单 pair 成本仍偏重
- Group pass 目前还不是有效性能优化
- Consumer 的可视化与剔除逻辑仍在 Runtime 内，后续如需更洁癖可继续细拆
