# OpaqueForward 仅接收原生 RT Shadow：替换方案

日期：2026-09-26。用户要求移除本地 OpaqueForward 的原生 CSM／VSM 接收，只接收 RT Shadow。

状态：已核对当前源码，尚未实施。用户已经批准替换目标；以下新增 SRV 消费和逐灯绘制形式，按 AGENTS.md 需要具体审批。此前的编译授权继续沿用。本方案取代旧 RT/Lumen 方案中的“保留 CSM/VSM”部分，不实施 Lumen 接入。

## 行为与范围

- 作用于本地定制 OpaqueForward 接收端，包括 Kajiya；不删除 UE 原生 Deferred、半透明或其他功能的 CSM／VSM 生产与接收代码。
- OpaqueForward 的动态投射阴影只来自原生 Ray Traced Shadows。灯未选择 RT、RT 不可用或阴影关闭时，该灯仍照明，但此接收端不退回 CSM／VSM。
- 按原生逐灯选择判断：硬件／视图能力、灯型开关、Cast Ray Traced Shadows 三态、阴影 ShowFlag 和实际阴影生成条件共同决定。不能只检查全局 r.RayTracing.Shadows。
- MegaLights 有独立 occlusion 枚举和渲染路径，不将其自动视为经典 Ray Traced Shadows；不借本次改造强制改变场景或组件设置。
- 移除 OpaqueForward 专用 CSM／VSM 查询、SMRT 软硬分支、VSM 参数结构／绑定／编译定义、OpaqueForwardShadows.BlueNoise 绑定与 Shader 别名。既有引擎 BlueNoise 资源继续服务其他原生功能。
- 只接收 RT 的分支不额外叠加本地 CSM、VSM、预计算阴影因子或屏幕 Contact Shadow，避免名为 RT-only 实际仍使用另一种投射阴影。材质 AO、环境遮挡、Kajiya 对给定 visibility 的响应不在此次删除范围。
- Light Function、Kajiya 双瓣与环境响应保持当前语义。Light Function 仍独立乘入射光，不能混入非线性的 shadow visibility。

## 当前源码约束

1. `DeferredShadingRenderer.cpp` 当前在 RenderLights／RenderMegaLights／TranslucencyLightingVolume 之后调用 OpaqueForward，随后才进行后半段间接光合成。
2. OpaqueForward 当前以覆盖混合写 SceneColor。因此不能直接在更早的逐灯阶段加上 RT 照明，再让原 Pass 将它覆盖。
3. `LightRendering.cpp` 的 RT 结果存在批处理／逐灯、降噪／不降噪、多视图处理。随后还可能原地混入 HairStrands deep shadow、第一人称阴影、体积阴影及 Light Function。保留资源引用不会保留这些写入之前的 texel。
4. `RayTracingOcclusionRGS.usf` 已从 GBuffer／深度识别有效表面并按 Lighting Channels 发射原生阴影射线。Kajiya 的 GBuffer 解码为现有表面代理，当前不是将此类像素全部跳过。
5. 当前 Kajiya 的两套方向、双瓣参数等只在材质求值位置完整可用；普通 GBuffer 没有足够信息直接重用标准 DeferredLight Shader 得到相同响应。
6. `LightDefinitions.h` 当前 packed light 标记使用 27 位，剩余 5 位。计划在已有灯记录中加入明确命名的 RT 单灯接管位；沿已有灯数据读取，不增加灯索引 Buffer 或扩展灯记录大小。生产／消费须共用同一选择条件，方向光和局部光均覆盖。

## 建议实现形式

新增一个 OpaqueForward 逐灯加法绘制路径，使用现有材质着色逻辑和现成的原生 RT 阴影结果。代码放在现有 OpaqueForward C++／Shader 文件及必要的 MeshPass 注册位置，不新增独立 .ush/.usf，不增加阴影缓存数组。

1. 将主 OpaqueForward 放到原有间接光／AO 准备之后、RenderLights 之前；主 Pass 写环境、自发光及未交给 RT 单灯路径的直接光。
2. 由相同的逐灯接管条件让主 Pass 跳过将由 RT 路径绘制的灯，避免加两次。保持现有 OpaqueForward 的灯集合、光照通道、局部灯范围和主方向光选择，不借此扩张受光语义。
3. 原生 RT tracing／可选 denoising 产出某灯的结果后，在其他附加阴影及 Light Function 改写它之前，立即提交该灯的 OpaqueForward 着色。非批处理多视图可直接消费该视图对应的结果，不能省略未降噪分支。
4. 重新求值参与该灯的材质，以取得完整输入。按原生格式解码 surface／transmission 通道，保留灯的颜色、形状、距离／锥角衰减、面积光、IES／光源贴图及光照通道语义，将直接光加到已有 SceneColor。
5. 普通 Deferred 直接光仍遵守现有 OpaqueForward 标记；后续环境合成仍遵守既有分工。单灯绘制不重复写环境、Emissive 或 debug 最终输出。

## 本次需要批准的资源访问及成本

| 项目 | 具体范围 |
| --- | --- |
| 新增阴影消费者 | 上述 OpaqueForward 逐灯 Raster／Mesh Pass，每次绑定当前灯、当前视图原生 RT 阴影结果的一张 Texture2D SRV |
| 原生资源 | 未降噪为 RayTracingOcclusion；启用降噪则使用原生对应 Mask 输出。内置路径格式 PF_FloatRGBA，即 RGBA16F；尺寸为 SceneTextures.Config.Extent，单采样。第三方降噪器沿其既有输出，不自行复制或转换 |
| 分配／生命周期 | 不新建阴影纹理、逐灯数组、历史、UAV 或 Kajiya 分类 RT；不增加为了转交阴影的 clear／copy。在该灯结果原生存活阶段消费；新增 RDG 读依赖约束后续改写和释放，不将所有灯的纹理统一留到帧末 |
| 已有读取的重复消费 | 单灯重绘再次读取现有网格／蒙皮、材质贴图、ForwardLight 数据、按当前排列需要的 IES／Rect 资源及已有 Light Function；方向灯背光沿用当前环境基底。没有新增 Lumen 体积、TLAS 或 BlueNoise 读取 |
| 输出与同步 | SceneColor 加法混合，现有 SceneDepth 测试；不新增 MRT。RDG 负责 RT／降噪输出到 SRV 的顺序及转换；提前主 Pass 和增加消费可能改变异步重叠，不能称为零同步开销 |
| 存储参考 | 原生 RGBA16F 纹理 1080p 为 15.82 MiB，4K 为 63.28 MiB；这是现有纹理的有效 texel 数据量，不是新增分配量 |
| 读取量参考 | 每个有效片元、每个相关 RT 灯一次屏幕阴影读取，逻辑 texel 为 8 B。假设 4K 覆盖率 10%、一次有效覆盖，约 6.33 MiB／灯的逻辑 texel 读取；不含 overdraw、材质重采样、几何和 SceneColor blend，不能等同 DRAM 带宽 |
| 绘制成本 | 随参与的 OpaqueForward draw 数量 × 相关 RT 灯数量增长，新增单灯 Shader／PSO／绘制命令及材质求值。多灯时可能昂贵，尚无实测 GPU 毫秒或内存峰值，不能承诺优于 CSM／VSM |
| 关闭／不使用 | 没有可见 OpaqueForward 接收者，或没有实际参与的经典 RT 阴影灯时，不提交新增单灯绘制和该阴影读取；原生其他材质所需 RT 生产不受本功能关闭影响。现有 Light Function 成本不因本功能关闭而自动消失 |

未选：重新建逐灯全屏阴影数组；将所有灯纹理一直保存到原主 Pass；在材质里重复发 RT 射线；未经用户同意给灯数量加四通道上限。这些均不是本次审批内容。

## 验证和完成条件

- 静态核对 OpaqueForward 不再编译／绑定／调用自身的 CSM、VSM、SMRT 和 BlueNoise 接收逻辑，同时原生其他路径保持不变。
- 方向／点／聚光／矩形 RT 灯、开关阴影、软硬光源、降噪开关、批处理／非批处理、多视图、逐灯三态覆盖、光照通道、Masked 边缘与带／不带 Light Function 的对照。
- GPU 事件与资源表证明：新消费者读取本灯原生结果；没有新阴影数组、复制或重复射线；RT-only 接收端不读 VSM／CSM／BlueNoise；灯贡献只计算一次。
- 固定相机和画质，预热后对 0／1／4／8 RT 灯记录主 OpaqueForward、单灯绘制、原生 RT／降噪、全帧 GPU 时间和资源存活量。性能检查独立于功能通过。
- 编译和运行使用已有授权；若需重启且有未保存场景，另按实际状态处理。禁止借验收增加断言、失败门槛或静默兼容分支。
- 全局 Forward／MSAA、MegaLights、Substrate、Nanite、Mobile、Path Tracer 不能由 desktop Deferred 结果外推为支持。

## 当前完成情况

删除范围、原生 RT 资源、消费时机、替换形式和成本已整理。生产代码尚未修改；等待新增资源消费与逐灯绘制形式的明确批准后，成套实施、编译并独立 review，不能将当前文档视为功能已交付。
