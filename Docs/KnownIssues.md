# GPUPVS 已知问题

本文档用于收口当前已经确认、但尚未彻底优化完成的问题。

## 高优先级

### 1. Setup 语义仍未完全对齐参考工程

现状：

- 当前 `Setup` 阶段仍是厚区间语义
- 参考工程更接近离散深度点语义

影响：

- Cell 垂直分布可能偏厚
- 可能生成低价值或冗余 cell
- 会放大后续 Compute 工作量

涉及文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Source/GPUPVS/Private/GPUPVSScene.cpp`
- `Shaders/Private/GPUPVSRasterAABBCS.usf`

### 2. Compute 单个 `cell x mesh` pair 成本偏高

现状：

- 当前 advanced visibility 采样结构较重
- 在范围扩大后更容易触发 `DXGI_ERROR_DEVICE_HUNG`

为什么参考项目 `cell` 达到 `1000+` 也不崩，而当前项目约 `200~570` 就可能崩：

- `cell` 数本身不是两套实现之间可直接横向比较的核心指标
- 真正决定 GPU 是否会被拖进 `TDR` 的，是单个 `cell x mesh` 配对的计算成本，以及这些配对是否还叠加了额外的全局争用
- 当前项目的 `ComputeAdvancedBoxVisibility()` 比参考项目更重，因此即使 `cell` 更少，也可能更早触发 GPU hang

#### 2.1 当前项目的单 pair 射线规模远高于参考项目

当前项目 `Shaders/Private/GPUPVSVisibilityCS.usf` 的主采样结构是：

- 固定保守采样：`7 x 15 = 105` 根
- 主采样：`NumCellSamples x NumPrimaryRays`
- 重要性采样：`NumImportanceSamples`

也就是说，当前项目的主采样不是单层 `NumRays` 循环，而是双层乘法结构：

- 外层按 `Cell` 面采样
- 内层按目标面采样
- 二者相乘后，单 pair 成本会快速放大

以实际使用过的一组参数为例：

- `NumCellSamples = 24`
- `MinMeshSamples = 14`
- `MaxMeshSamples = 40`
- `NumImportanceSamples = 40`

则当前项目单个 `cell x mesh` pair 的理论射线规模大约是：

- `105 + 24 x [14..40] + 40`
- 即约 `481..1105` 根射线

参考项目 `c:\Users\24127\Desktop\project-code\Shaders\Private\GPUPVSVisibilityCS.usf` 的结构则不同：

- 同样保留 `105` 根固定保守采样
- 主采样是单层 `for (RayIdx < NumRays)` 循环，而不是 `NumCellSamples x NumPrimaryRays`
- 重要性采样被硬性限制到最多 `8` 根

因此参考项目单个 pair 的理论规模大约是：

- `105 + [64..256] + [1..8]`
- 即约 `170..369` 根射线

结论：

- 当前项目单个 pair 的成本，大约已经是参考项目的 `3~6` 倍
- 所以“参考项目 `1000+ cell` 不崩，而当前项目 `200~570 cell` 就崩”并不矛盾
- 当前项目并不是 `cell` 更多，而是“每个 pair 更贵”

#### 2.2 当前项目还叠加了高频全局原子统计

当前项目的 group pass 和 mesh pass 里都保留了大量 `StatAdd()` 调用，用于写入极少量全局计数槽位。

这会带来两个问题：

- 所有线程会争用同几个全局地址
- GPU 实际耗时会同时混入“真实可见性计算成本”和“调试统计成本”

这类全局原子统计在 `Cells x Meshes` 放大后，会成为非常差的放大器：

- 几何和 BVH trace 已经很重
- 再叠加原子争用，会进一步拉长单次 dispatch 时间
- 更容易逼近或超过驱动超时阈值

参考项目没有当前这套高频全局统计负担，因此即使 `cell` 数更多，也未必更容易崩。

#### 2.3 当前项目的 group pass 还没有真正帮 mesh pass 减负

当前项目虽然已经引入了 group pass，但在 mesh pass 中：

- 即使 group 不可见
- 也不会直接硬跳过 mesh 可见性测试

当前逻辑更偏保守正确性和诊断意义，而不是性能优化。

这意味着：

- `GroupsBuilt` 只是一个中间统计
- 大量 mesh 仍会继续进入完整 `ComputeAdvancedBoxVisibility()`
- `cell` 数一旦变大，mesh pass 的总配对工作量几乎会线性跟着放大

因此当前项目在大范围下的 GPU 风险，更接近：

- `Cells x Meshes x 每个 pair 的高成本`

而不是简单的：

- `Cells` 越多越危险

#### 2.4 Setup 差异会放大问题，但不是这次现象的第一根因

当前项目与参考项目在 `Setup` 上确实存在语义差异：

- 当前项目偏厚区间语义
- 参考项目更接近离散深度点语义

这会影响：

- cell 的空间分布
- 每列高度层数量
- 是否会产生低价值 cell

但这更像是“输入规模和输入质量的放大器”，不是“为什么别人 `1000 cell` 不崩、你这里 `200~570 cell` 却崩”的第一根因。

更直接的根因仍然是：

- 当前项目单 pair 成本比参考项目更重
- 当前项目还有额外的全局原子争用
- 当前项目的 group pass 尚未转化成真正的 mesh pass 减负机制

#### 2.5 当前判断结论

现阶段应当明确：

- 不能再把 `cell` 数作为两套实现之间的直接性能对比指标
- 当前项目的主要问题不是“`cell` 比参考项目更多”
- 而是“当前每个 `cell x mesh` 配对更重，所以总体 GPU 工作量更容易失控”

后续优化时，优先级应保持为：

1. 先降低单 pair 成本
2. 再关闭或弱化高频全局统计
3. 再让 group pass 真正参与 coarse reject
4. 最后再整体重构 Setup 语义

影响：

- GPU 稳定性下降
- 构建大场景时更容易超时

涉及文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Source/GPUPVS/Private/GPUPVSShaders.cpp`
- `Shaders/Private/GPUPVSVisibilityCS.usf`

### 3. Group pass 目前还不是有效性能优化

现状：

- 组可见性已计算
- 但 mesh pass 并没有真正大规模硬跳过

影响：

- `GroupsBuilt` 目前更偏诊断意义
- 不能有效降低 mesh pass 开销

涉及文件：

- `Source/GPUPVS/Private/GPUPVS.cpp`
- `Shaders/Private/GPUPVSVisibilityCS.usf`

### 4. 缺少工作量预估和防炸保护

现状：

- 构建前不会明确提示本次 `Cells x Meshes` 工作量
- 也没有在高风险参数下自动阻止或降级

影响：

- GPU crash 只能在事后排查
- 用户无法在面板阶段提前感知风险

建议方向：

- 增加工作量估算日志
- 超阈值时 `Warning` 或 `EarlyOut`
- 必要时自动切换到保守配置

## 中优先级

### 5. Setup 边界扩张可解释性不足

现状：

- `Collect` 与 `Setup` 打印的边界不同
- 原因是 `Setup` 使用按 `CellSize` 对齐后的边界

影响：

- 容易被误判为新 bug

建议方向：

- 日志同时打印原始边界、对齐后边界和增加量

### 6. 低价值 Cell 仍缺乏系统识别

现状：

- 虽然已修过伪层、天空盒污染和极端高度问题
- 但仍缺乏统一的 cell 质量分类

影响：

- 一些低收益 cell 仍可能进入完整计算

建议方向：

- 增加 cell 质量统计
- 将低价值 cell 的诊断和主流程区分开

## 需要长期保留的工程约束

以下内容虽然部分已经修复，但后续优化时不能破坏：

- 小三角形过滤阈值应保持在 `1e-4` 或以上
- 法线方向需保持 `cross(V1 - V0, V2 - V0)`
- Setup 阶段必须过滤天空盒几何
- `SceneData` 资产保存职责应保留在 `Editor` 模块
- 所有按钮都应保留至少一条明确日志

## 当前建议顺序

如果后续继续优化，建议按以下顺序推进：

1. 先降低 Compute 开销
2. 再增加工作量预估与保护
3. 再考虑分批 dispatch
4. 最后整体重构 Setup 语义
