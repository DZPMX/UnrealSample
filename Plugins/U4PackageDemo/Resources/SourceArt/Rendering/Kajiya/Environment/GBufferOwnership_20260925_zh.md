# Kajiya 环境合成改用现有 GBuffer 身份

2026-09-25。对应用户请求：删除独立环境 Mask，复用 DiffuseIndirect、ReflectionEnvironment、AmbientCubemap 已读取的 GBuffer 模型 ID。已有 C++ / Shader 实施与编译授权继续适用。本次未新增分类纹理、SRV/UAV Buffer、UAV/SRV 绑定或新的资源消费者；新增标量经 UE 原有常量参数上传机制传递。

## 交付状态

实现已写入引擎并构建，最终编辑器进程已加载。固定输入的新旧输出对照、单 draw 混合模型识别、无 SkyLight 行为及三个目标合成器的编译后资源/采样检查通过。GPU 耗时的受控多帧 A/B、VR 实机和完整渲染配置矩阵尚未验证，不宣称全平台像素等价或已测得帧率提升。

引擎修改共 18 个文件。没有改角色材质图、实例参数或持久化关卡。测试用材质和演员均为 transient；测试结束重新打开磁盘上的酒店关卡。独立复核使用本轮开始时的文件备份比较，保留此前已有的引擎修改。

## 当前数据契约

1. BasePass 仍按原有 Default Lit 代理计算 Kajiya 的通用 GBuffer 属性和 BasePass 照明。在编码已有 GBuffer MRT 之前，只有通过现有 OpaqueForward 路由资格判断的 draw，且当前像素模型确实是 Kajiya 时，才把现有低 4 位 ShadingModelID 写为 13。未被接管的路由仍保持原代理身份。
2. `KajiyaGBufferIdentityEnabled` 是每 draw 的 uint 标量，经过现有 BasePass shader binding 传递。资格判断复用 `ShouldMarkOpaqueForwardBasePassPixel`，包括材质路径、主 pass、Nanite 排除和模型资格。编译集合包含 Kajiya 不等于所有像素都是 Kajiya；另有逐像素 `ShadingModel == SHADINGMODELID_KAJIYA_HAIR` 判断。
3. `StoredShadingModelID` 是 Shader 内 `FGBufferData` 的局部解码结果。它没有对应的新 GBuffer slot、RT 或 Buffer。自动生成的解码函数先保留原始 ID，再执行现有 force-default 条件和代理适配；旧手写解码入口同步处理。没有手改 Intermediate 生成文件。
4. 解码后的 `ShadingModelID` 继续为现有通用 Deferred/Lumen/SSAO/SSR 算法提供 Default Lit 代理；三个环境合成器使用 `StoredShadingModelID` 判断真实存储身份。这样保留了现有 `bIsSimple`、BxDF 分派、采样及派生 GBuffer 属性的计算语义。只在 forward 材质寄存器中的 Kajiya 完整输入，仍不被当成 GBuffer 已存储的数据。
5. `bKajiyaEnvironmentRendered` 是每帧每 view 的 CPU bool，初始化为 false。只有实际安排了该 view 的 OpaqueForward 绘制，且原有环境启用条件满足，才设为 true。三个合成器接收其 uint 标量 `KajiyaEnvironmentEnabled`。这保留了 forward 前后两次间接光阶段的区别。
6. 单次立体绘制覆盖两个眼睛时，后续逐 view 合成沿用主视图的已提交状态。此分支对应实际渲染配置，不是旧接口兼容路径。
7. 原有启用条件保留：环境 CVar 为 1、受支持的 desktop deferred 路径、存在非静态且有处理后纹理或实时捕获的 SkyLight，以及有效 OpaqueForward draw。模式关闭或无可用 SkyLight 时不屏蔽通用环境。

代码入口：

- [BasePass 编码身份](G:/UnrealSourceCode/Engine/Shaders/Private/BasePassPixelShader.usf:2394)
- [每 draw 资格](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp:2134)
- [解码结果字段](G:/UnrealSourceCode/Engine/Shaders/Private/DeferredShadingCommon.ush:375)
- [自动解码生成器](G:/UnrealSourceCode/Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp:1358)
- [通用代理适配](G:/UnrealSourceCode/Engine/Shaders/Private/ShadingCommon.ush:50)
- [逐 view 状态及立体视图传播](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp:276)

## 删除内容与混合行为

已删除 `SceneTextures.KajiyaEnvironmentMask`、PF_R8_UINT 的分配与清除、OpaqueForward 的 MRT1 声明和写入、额外 blend write mask、PSO 目标格式、三个合成器的 Mask SRV/dummy 绑定及读取。源码全量检索已无该 Mask 的生产代码引用。原先其他用途的 Light Function、Shadow、LightingChannels 资源不属于这张 Mask，未在本次改动中增改其绑定。

跳过专用环境像素时，原混合契约保留：

| 消费端 | 跳过时输出 |
| --- | --- |
| DiffuseIndirect，GI 双源混合 | Add=0，Multiply=1 |
| DiffuseIndirect，GI 单输出 | 原 SceneColor；复用已有读取 |
| DiffuseIndirect，AO-only | Multiply=1 |
| ReflectionEnvironment | 已初始化的零加法贡献，保留独立 SSS 输出初始化 |
| AmbientCubemap | 零加法贡献 |

Deferred decals 的 GBuffer 路径使用 RGB write mask，保留已有模型 ID 的 alpha。BasePass 在既有材质 discard/coverage 之后输出身份；没有新增分类绘制。Masked 边缘与复杂遮挡的完整新旧场景 A/B 尚未执行。

## 实际验证

证据目录：[Kajiya_GBufferOwnership_20260925](G:/UnrealSample/Saved/Codex/Kajiya_GBufferOwnership_20260925)。

- 初次构建 60 actions 成功；补充立体视图状态传播后，最终增量构建 5 actions 成功。最终进程再次运行固定输入测试，结果一致。运行日志未发现材质/全局 Shader 编译错误或 fatal/assertion。
- 15 组 256×256 RGBA16F SceneCapture 数据以 float32 无归一化读回。覆盖纯 Default Lit、纯 Kajiya、混合材质选择 A/B、UV 分区，以及环境模式 0、1、ownership debug。新旧对应结果全部有限，所有组最大差值为 0。[逐组结果](G:/UnrealSample/Saved/Codex/Kajiya_GBufferOwnership_20260925/FixtureComparison.json)
- 最初的 UV.U 分区在固定球体机位下只有 Kajiya 一侧可见，因此上述组不能单独证明同 draw 的空间混合。随后另用 UV.V 验证：可见球体 16,088 像素，其中 8,044 个被专用环境接管，其他像素与纯 Default Lit 的输出差值为 0。实际 GBufferB 低 4 位包含 8,044 个 ID1 和 8,044 个 ID13。[空间分区结果](G:/UnrealSample/Saved/Codex/Kajiya_GBufferOwnership_20260925/SpatialOwnership.json)
- 临时禁用 SkyLight 后，五种材质情况下，模式 0、模式 1、ownership debug 的输出差值均为 0；随后恢复 SkyLight。[无 SkyLight 结果](G:/UnrealSample/Saved/Codex/Kajiya_GBufferOwnership_20260925/NoSkyComparison.json)
- GPU 截帧包含普通反射合成，以及启用 Lumen / AmbientCubemap 的临时捕获。OpaqueForward 仅有 SceneColor 一个颜色附件，资源表没有 `Kajiya.EnvironmentMask`。在三个实际编译的目标 Shader 中，GBufferB 各有一条 SampleLevel 指令，模型判断复用同次读回值，没有单独重采样 ID。[编译后资源检查](G:/UnrealSample/Saved/Codex/Kajiya_GBufferOwnership_20260925/CompiledResourceAudit.json)
- 截帧的 Lumen / AmbientCubemap 检查证明这些排列实际运行及其绑定/指令；它不是这两个模式完整的新旧画面对照。完整 GPU 截帧、反汇编、操作列表保存在证据目录。

首次测试中的一个 UV 节点连接失败已修正，并在旧二进制、原 Shader 源码下重新生成基线；失败数据不用于上述验收。GPU 原始纹理检查仅使用已到达有效生命周期、与实际 reader 对应的稳定事件，不把尚未被写入的纹理数据作为材质结果。

## 性能边界

独立全分辨率单采样 R8 Mask、clear、颜色附件写入和 Mask 读取已移除，功能关闭时同样不会创建这张资源。现有 GBuffer 格式、尺寸、样本数和数量没有扩展。4K 的单张 R8 有效 texel 数据为 7.91 MiB；这是格式推导，不是实测峰值显存下降。

旧路径可先读小 Mask，再在头发像素上提前退出；新路径需要先取得既有 GBuffer 身份。因此读取覆盖/顺序有变化，也增加了标量判断和代理 ID 适配。已验证没有重复 GBufferB 采样，但不能据此推断总 DRAM 带宽或全帧耗时必然改善。

本轮未完成受控多帧 GPU 时间 A/B。后续性能验收需使用这里保存的原始源码和固定场景，保持分辨率、相机、材质、GI、曝光及预热一致，分别统计 BasePass、OpaqueForward、三个 composite 和全帧时间及波动。不可将截帧回放开销、冷 PSO 创建耗时或 7.91 MiB 的逻辑数据量当成 GPU 收益。

## 独立复核与要求对应

实施后重新读取本轮完整 diff，逐项核对资格路由、逐像素模型、阶段先后、混合恒等输出、解码派生值、RDG 附件/PSO 一致性以及 view 生命周期。发现并修复单次立体绘制的状态传播、重复 SceneColor 读取、声明位置和孤立注释；修复后再次复核并构建。没有新增断言、失败门槛、旧布局兼容、裸指针存储或隐式 lambda capture。

| 用户要求/验收项 | 状态 |
| --- | --- |
| 使用现有 GBuffer ID 识别 Kajiya | 已满足，源码与 GPU 数据均验证 |
| 删除独立环境 Mask、附加 MRT 和其 SRV | 已满足，源码、PSO 配置和 GPU 截帧验证 |
| 不新增未经批准的 UAV/SRV 绑定或消费者 | 已满足，本轮只增加数值状态并删除资源 |
| 保留已有模式切换和普通材质结果 | 所列固定输入、空间混合、无 SkyLight 测试通过；非全输入域证明 |
| 构建并加载最终版本 | 已满足，最终进程复跑 15 组对照通过 |
| VR 实机、全局 Forward/MSAA、Substrate 全配置运行验证 | 本轮未运行；保留原支持范围，已复查对应条件 |
| 受控 GPU 性能 A/B | 未验证，不宣称已完成性能验收 |

UAV/SRV 审批规则继续保留在 [AGENTS.md](G:/UnrealSample/AGENTS.md:56)。使用方式仍是原有 `r.KajiyaHair.Environment 0/1`，本次没有新增用户开关；本轮测试编辑器结束时复位为默认 0，debug 为 0。
