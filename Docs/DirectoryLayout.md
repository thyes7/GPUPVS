# GPUPVS 目录结构

## 目标

本目录结构文档用于说明当前插件目录应该如何理解，以及后续新增文件时应尽量放在什么位置。

## 根目录

```text
GPUPVS/
├─ Docs/
├─ Shaders/
├─ Source/
├─ Binaries/
├─ GPUPVS.uplugin
└─ README.md
```

说明：

- `README.md`：对外入口，总览插件用途、流程和文档
- `Docs/`：长期维护的技术文档
- `Shaders/`：GPU 计算使用的 shader
- `Source/`：模块源码
- `Binaries/`：构建产物，不承载设计信息

## `Docs`

建议只保留少量高价值文档：

- `Architecture.md`：架构和模块边界
- `DirectoryLayout.md`：目录说明
- `KnownIssues.md`：当前未解决问题
- `Changelog.md`：阶段性变更记录

规则：

- 问题清单、修复摘要、架构说明不要再散落到根目录
- 新的技术记录优先补进现有文档，而不是不断新增独立 `md`

## `Shaders`

当前主链路只保留：

```text
Shaders/
└─ Private/
   ├─ GPUPVSRasterAABBCS.usf
   └─ GPUPVSVisibilityCS.usf
```

规则：

- 仅保留当前真正参与主流程的 shader
- 已废弃的实验 shader 不应继续留在目录里
- shader 文件命名应尽量与调用侧一一对应

## `Source`

```text
Source/
├─ GPUPVS/
│  ├─ Public/
│  ├─ Private/
│  └─ GPUPVS.Build.cs
└─ GPUPVSEditor/
   ├─ Public/
   ├─ Private/
   └─ GPUPVSEditor.Build.cs
```

### `Source/GPUPVS`

职责：

- Runtime 核心
- Build 流程编排
- SceneData 加载与消费
- GPU shader 调度与 BVH

当前关键文件：

- `GPUPVS.cpp`：主流程编排
- `GPUPVSScene.cpp`：场景采集
- `GPUPVSConsumer.cpp`：可视化与剔除消费
- `GPUPVSSceneDataIO.cpp`：SceneData 路径、加载和 writeback 协调
- `GPUPVSShaders.cpp`：RDG/shader 调度
- `GPUPVSBVH.cpp`：BVH 构建

### `Source/GPUPVSEditor`

职责：

- 面板 UI
- 菜单入口
- 会话参数
- SceneData 资产保存

当前关键文件：

- `GPUPVSEditorModule.cpp`
- `GPUPVSEditorModule.h`
- `GPUPVSEditorSettings.h`

## 后续新增文件建议

### 新增 Runtime 文件时

- 与场景采集相关：放到 `Source/GPUPVS/Private`
- 与消费/剔除相关：优先并入或靠近 `GPUPVSConsumer.cpp`
- 与 shader 调度相关：优先并入或靠近 `GPUPVSShaders.cpp`

### 新增 Editor 文件时

- 面板 UI、菜单、保存逻辑：放到 `Source/GPUPVSEditor`
- 不要再把编辑器专用逻辑塞回 Runtime 模块

### 新增文档时

- 优先补充到 `Docs/` 中已有文档
- 只有在职责明显不同的情况下才新增文档文件

## 当前整理结论

这次目录级整理采用的是“轻量整理”而不是“大搬家”：

- 不物理拆分大量源码文件
- 先把根目录收干净
- 先把文档入口收统一
- 先明确目录职责和后续放置规则

这样风险最小，但成品感已经能明显提升。
