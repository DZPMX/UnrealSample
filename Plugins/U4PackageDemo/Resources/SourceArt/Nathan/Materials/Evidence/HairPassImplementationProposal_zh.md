# 头发 Pass 扩展方案（待实现形式及编译授权）

范围：修复 Nathan 严格审查中已证实的覆盖、连续透明、独立深度、阴影和速度差异。现有两瓣 Kajiya 光照接口继续共用。本文是具体实施方案，不是已写入或验证的引擎补丁。

## 当前代码及原始证据

- `Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp` 只接收 Opaque/Masked，绑定一个 SceneColor，深度只读；固定状态不启用 SrcAlpha/InvSrcAlpha。
- `Engine/Shaders/Private/BasePassPixelShader.usf` 与 `ForwardLightingCommon.ush` 已有公共 Kajiya 光照接入，包括 Surface Forward Shading 分支。应优先利用这个已有消费者。
- `Nathan_Hair_StrictReview_20260925/DrawAudit.json`：7 组 GBuffer、4 组连续 Alpha 主绘制，4 组专门的透明头发深度，以及 11 段几何对应的阴影绘制。
- 透明主绘制：32326、32354、32364、32383；独立深度：29239、29267、29277、29296。
- 29267 的完整源运算是两次 alpha 采样及其原有组合，经过覆盖 saturate、材质 threshold 的 epsilon 保护和相除、再次 saturate，再与独立深度阈值比较。其他三个须各自保持分层和 UV 条件。
- 颜色、普通深度、透明深度、阴影和 velocity 的裁剪/权重不同。不能把同一个 OpacityMask 传给所有 Pass。

## 拟采用的实现形式

1. 材质仍以原有输入布局家族及 MI 复用。为四个前向来源启用真正的透明混合，优先使用 MI 的 BlendMode 覆盖和 UE 已有 Surface Forward Shading，不按 EID 增建母材质。
2. 扩展公共 Kajiya 材质输出，显式传递已经确认的颜色覆盖、深度覆盖、阴影覆盖及速度覆盖/阈值。保留各项独立语义，不把某个未使用分量重新解释为另一用途；旧材质未连接的新项必须保持原行为。
3. 在 Renderer 中增加/接入透明头发深度预绘制，使上述透明 section 可以在主颜色前按自身覆盖算法写入场景深度。Shader 优先复用现有 DepthOnly 和材质计算，C++ 需接入材质筛选、MeshPass 注册、渲染顺序与 PSO。
4. 在现有阴影和速度消费端接入对应覆盖条件；核对深度访问、混合权重和输出目标。不能用开启 `OutputTranslucentVelocity` 代替源条件的实现。
5. 原随机覆盖的纹理 Load、屏幕坐标、整数寻址、相位和条件在已授权的材质图/Custom 形式中恢复；需要每帧的输入由引擎现有 View/帧数据传入，具体映射必须在源输入台账验证后再连接。

不新增角色专属 ShadingModel。公共接口默认兼容检查包括 Elena；Nathan 资产继续独立。公共接口变化会要求对应 Engine C++ 和 Shader 编译，可能需要关闭编辑器，重启时另行保护当前未保存关卡。

## 实施及验收顺序

先完成当前可独立的材质图修复和完整输入映射，再写引擎改动。按一个透明输入家族验证后推广到四个实例：比较主 Alpha、深度 mask、shadow mask、velocity 的有效像素及混合。旧版 Elena/Nathan 材质需验证未连接新输入时的行为。

纹理 mip、TBN、灯光/阴影/间接光、源 GBuffer 精度、每帧湿润/法线生成等仍有各自的源验证工作。此 Pass 扩展不意味着这些项目已经等价，也不将“全部严格还原完成”作为编译成功的结论。

## 当前授权边界

本对话已经授权 Nathan 材质 Shader 编译及前一项公共 Kajiya 第二粗糙度节点 C++ 扩展。新的 Renderer Pass 接入不是那个节点扩展的同一实施范围，需要按项目 `Global Implementation-Form Approval Rule` 和 `Global No-Proactive-Compilation Rule` 明确允许后实施/编译。
