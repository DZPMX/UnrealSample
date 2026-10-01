# Kajiya：RT Shadow 与 Lumen 接入方案

状态：源码与资源方案已核对，等待新增资源访问及逐灯绘制形式审批；尚未修改生产代码、编译或调整用户场景。

## 目标与范围

- 为桌面 Deferred 渲染器中的 Kajiya OpaqueForward 接收原生 Ray Traced Shadows。
- 继续保留 CSM/VSM；遵守每盏灯的 Cast Ray Traced Shadows、项目设置及原生支持条件。
- 为 Kajiya 环境漫反射、双瓣环境高光和方向光背光的 AmbientBase 输入提供 Lumen 场景相关输入。
- 不新增逐灯全分辨率阴影数组，不修改 Light Function 算法，不把 UE 通用环境光与 Kajiya 专用环境重复相加。
- 本方案不把全局 Forward Renderer、Substrate、MegaLights 或 Path Tracer 宣称为已经支持。Kajiya 在反射命中位置的完整材质着色也不属于仅接收 GI 即可完成的功能。

## 已核对的源码事实

当前本地引擎为 5.7.4（Engine/Build/Build.version）。在线 Epic 页面目前显示 5.8；公开资料只用于背景核对，具体接口、格式及顺序以本地源码为准。

1. ForwardLightingCommon.ush 的 OPAQUE_FORWARD_PASS && !FORWARD_SHADING 分支不读取普通 Forward 的屏幕阴影结果；它查询原生 CSM/VSM。
2. LightRendering.cpp 的 RenderLights 原生生成 RayTracingOcclusion，并可通过 DenoiseShadowVisibilityMasks 得到 ScreenShadowMaskTexture。批处理结果按灯取出；原生非批处理、多视图、关闭降噪分支不能省略。
3. 阴影之后可能继续写头发深度阴影、第一人称遮挡和体积阴影，再写 Light Function/预览指示。保留 FRDGTextureRef 不是保留某个历史版本的 texel。Kajiya 的局部阴影 sqrt/tint 与 Light Function 的乘法不能混为一个输入。
4. 当前主 OpaqueForward 覆盖 SceneColor，而不是加法混合；若逐灯着色先发生，后续主 Pass 会覆盖它。需要同时调整执行顺序，不能只在 RenderLights 中加一次 Draw。
5. Lumen 的 ComputeLumenTranslucencyGIVolume 在 Screen Probe Gather 相关工作中生成当前帧体积与当前帧时间滤波后的 NewHistory 体积。不是让 Kajiya 直接读取上一帧纹理。
6. 原生半透明 GetSkyLighting 使用 GetTranslucencyGIVolumeLighting(..., true)，读取两张时间滤波的 Texture3D，恢复 TwoBand SH，再用 CalcDiffuseTransferSH / DotSH / PI 做漫反射卷积。
7. 体积含场景与天空贡献。不能再把完整 SkyLight 环境相加，也不能再加普通 DiffuseIndirectComposite、ReflectionEnvironment、AmbientCubemap 的同一份能量。
8. SH 体积生产时去除 View.PreExposure。NewHistory 在生产路径内有原生强度归一化步骤；消费端必须沿用其定义，不附加凭视觉确定的倍率。
9. 当前 Kajiya 完整的 SurfaceNormal、StrandDirection、两瓣强度/粗糙度/偏移、AO、Backlit 在材质着色位置可用，通用 GBuffer 不保存这些全部输入。

## A. RT Shadow：消费原生结果，按 RT 灯立即绘制 Kajiya

建议增加一个仅用于 Kajiya 的逐灯材质绘制路径，复用原生追踪与降噪结果。

执行顺序：

1. 原生阴影深度准备及 Lumen 数据准备维持原生生产端。
2. 主 OpaqueForward 提前到 RenderLights 之前：写入环境光、自发光和其他当前直接光，排除交给后续 RT 逐灯路径处理的灯。
3. RenderLights 为某盏灯生成/降噪阴影；在 Light Function 修改该结果之前，Kajiya 逐灯绘制读取这张现成阴影。
4. 调用现有 Kajiya 直接光响应；纯阴影送 GetShadowTerms/Kajiya visibility，现有独立 Light Function 继续调制入射光。只把该灯的直接光加法混合到现有 SceneColor。
5. 原生 Deferred 灯光及后续间接光合成按各自已有像素所有权运行，不再给 Kajiya 加这盏灯第二次。

每灯筛选以实际原生阴影选择、视图可见性、光照通道、影响范围为依据。主 Pass 排除标记必须与逐灯消费使用同一条件；不能只检查全局 r.RayTracing.Shadows。计划把选择标志放入现有逐视图灯光记录的空闲位及方向光标量，不增加灯光索引 SRV。

### RT 资源与成本审批项

- 新消费：一张原生 ScreenShadowMaskTexture，Texture2D，原生内置 RT/降噪路径为 PF_FloatRGBA（RGBA16F），分辨率为 SceneTextures.Config.Extent，单采样。第三方降噪器的实际输出格式由其已有输出决定，不转换或复制。
- 分配/生命周期：不为 Kajiya 创建阴影纹理、数组或跨帧历史；在当前灯的原生资源存活期间消费。新增 RDG 读依赖约束其后续写入/释放，不把所有灯的结果一直保留到最终主 Pass。
- Producer：原生 Ray Traced Shadows、原生降噪器及既有阴影修改流程。Consumer：新增 Kajiya 逐灯 Raster Pass。每次绘制绑定当前灯的一个阴影 SRV。
- 既有资源复用：重新绘制会再次使用当前网格/蒙皮/材质输入，以及现有灯光、IES/Rect atlas、独立 Light Function 等已用资源；不新增其副本或采样算法。方向灯背光还需读取所选环境输入。
- 输出：现有 SceneColor 加法混合，现有 SceneDepth 只读测试；不增加 MRT、清屏、阴影复制或 UAV。
- 原生全屏 RGBA16F 阴影尺寸示例：1920×1080 约 15.82 MiB；3840×2160 约 63.28 MiB。这是原生既有资源尺寸，不是本方案新增分配。
- 增量带宽估算：每个参与该灯着色的 Kajiya fragment 一次阴影读取，逻辑 texel 大小约 8 B（RGBA16F）；另有 SceneColor 混合、材质重采样和几何读取。不能把逻辑 texel 大小当作实测显存带宽，缓存、overdraw、压缩和 blend 都会影响结果。
- Draw/计算成本随 Kajiya draw 数量 × 相关 RT 灯数量增长，并受灯光屏幕范围、深度测试和遮罩影响。没有新增阴影存储不代表 GPU 更快；多 RT 灯时可能比保留纹理的做法更贵。
- 无 Kajiya、无原生 RT 阴影灯或 RT 阴影关闭时，不提交该逐灯 Draw，不新增本功能的阴影读取/分配/清屏；原生其他材质需要的阴影工作不变。
- 执行顺序风险：提前消费 Lumen 体积可能缩短异步计算重叠；GPU timeline 必须测量，不能声称零同步代价。

没有选择的做法：

- 为每盏灯新建全分辨率 Texture2DArray：违反此次内存设计目标，并增加复制/清屏。
- 只保留原生结果到主 OpaqueForward：仍延长多张大纹理生命周期；Light Function 原地写入还使纯阴影值不再可用。
- 在材质内重新发 RT 射线：重复原生追踪，且绕开原生降噪；此次不采用。
- 四通道阴影打包：原生全局 Forward 的重叠灯限制不能未经批准成为本功能的新限制。

## B. Lumen：以既有方向性 GI 体积驱动现有 Kajiya 环境响应

推荐作为完整环境响应的第一版适配，区别于“只让 Lumen 漫反射穿过后合成”。

1. 在 OpaqueForward 中按材质位置采样当前视图 Lumen 的时间滤波 SH 体积。
2. 使用原生 SH 解码、坐标转换与漫反射归一化；由 SH 的实际世界坐标基恢复主方向，不把 V.yzw 直接当作 XYZ。
3. 从同一组 SH 得到沿 SurfaceNormal 的环境输入、主方向以及正负方向的亮度差，作为现有 Kajiya 环境计算的 UE 输入适配。
4. 保留当前 Kajiya 的 base/directional 分离、正反面行为、两瓣各自参数、AO/视角遮挡和 gate，以及 AmbientBase 对方向光背光的作用。复用同一份采样结果，不按各个输出重复采样。
5. Lumen 已含天空贡献，不再乘 SkyLight 颜色/强度，也不再加原 SkyLight 整套环境。后续通用 GI/反射合成继续排除由此路径拥有的 Kajiya 像素。
6. 不要求一定有 SkyLight 才启用 Lumen：室内反弹光/自发光本身也可以提供输入。
7. 现有旧 UE 模式和专用 SkyLight 模式保留。新增明确的 Lumen 环境选择；Lumen 不可用或体积被用户关闭时，退回已有专用 SkyLight 行为并在诊断中显示实际来源。不自动替用户开启 GI 或修改画质档。
8. 视图、相机切换、范围外取样和时间滤波沿原生约定处理；实例化双目不能让第二只眼误采第一只眼的坐标。不能仅凭单视图成功宣称双目兼容。

这是低频方向性环境适配，不是按 Kajiya BxDF 对 Lumen 完整入射辐射作双瓣积分；也不等同 UE 不透明 Lumen Reflections。它保留 Kajiya 双瓣形状并让方向/颜色随 Lumen 场景改变，但空间分辨率和方向颜色压缩限制仍存在。多方向窄亮源、贴头部细节遮挡、快速移动及室内漏光需实测。此限制必须作为实现定义的一部分批准。

### Lumen 资源与成本审批项

- 新消费：当前 View 已有的 Lumen.TranslucencyVolume.SHLightingNewHistory0 和对应 NewHistory1，两张 Texture3D SRV。代码中第二张的 RDG debug name 当前也写成 SHLightingNewHistory0，识别资源要按 Texture0/Texture1 身份，不能只看名字。
- 格式：0 号为 Lumen::GetLightingDataFormat()，默认 R11G11B10_FLOAT（4 B/voxel），可随既有 CVar 变为 RGBA16F（8 B）或 RGBA32F（16 B）；1 号为 RGBA16F（8 B）。不改变格式。
- 尺寸：XY = ceil(ViewRect.Size / 原生 GridPixelSize)，Z 沿原生对数深度划分，单采样。GridPixelSize 的代码默认 32，但画质档可为 64 等值。
- 例：1920×1080、GridPixelSize=32、近裁面约 10、原生默认远距离 8000 和 Z 参数时约 60×34×26；两张合计约 0.607 MiB。3840×2160 相同参数约 120×68×26，合计约 2.428 MiB。这些是现有体积大小示例，不是新增分配；实际必须记录运行时 Desc。
- 分配/写入：不建立 Kajiya GI RT/历史，不增加追踪、清屏、复制或 UAV。仅复用原生已启用 Lumen 时产生的体积；不会为 Kajiya 强制打开本来关闭的体积。
- 绑定：最小环境参数只绑定上述两张纹理和必要的 Grid 参数/采样器；不把整套 Radiance Cache、Front Layer Reflections 或 TLAS 一并绑定进来。
- 读取：每次 Kajiya 环境求值两次三线性 Texture3D 采样；当前 shader 通道随后共享解码结果。RT 方向灯逐灯绘制为背光重新求 AmbientBase 时，可能产生同样两次采样，需计入成本。
- 带宽估算：默认格式，两次三线性采样对应最多 8×(4+8)=96 B 的源 texel 访问量/求值（假设无缓存复用）；这是 texel 访问量估算，不是 DRAM 流量或 GPU 时间测量。
- 生命周期/同步：依赖当前帧 Lumen producer 完成；RDG 声明 UAV→SRV/跨队列依赖，不手动 flush GPU。会影响重叠程度，需测量。
- 关闭/不适用：不执行两次采样、不创建 Kajiya 资源；原生其他功能使用的 Lumen 体积仍由原生条件决定。

### 与其他 Lumen 方案的比较

| 方案 | 能解决什么 | 主要代价/限制 |
| --- | --- | --- |
| 只复用现有 Screen Probe 漫反射后合成 | 高质量不透明漫反射反弹光 | 双瓣仍由 SkyLight 驱动，AmbientBase 也未统一，不是完整方向性环境支持 |
| 两张已有低阶 GI 体积（本提案） | 局部环境漫反射、双瓣方向/颜色、AmbientBase 统一 | 低频体积及压缩的方向颜色；须新增这两张已有 SRV 的消费 |
| 按两瓣对原始 Screen Probe / Radiance Cache 辐射积分 | 更完整的入射方向响应 | 更多数据绑定与采样、滤波/历史设计；不能在当前两张 SRV 审批内擅自扩大 |

## 验证与性能验收

RT：方向/点/聚光/矩形灯；硬/软光源；开启/关闭降噪；每灯三态覆盖全局开关；多盏混合 VSM/RT；光照通道；Masked 边缘；带/不带 Light Function；同一纯阴影结果取样比较；单视图与多视图；资源生命周期及 RHI validation。

Lumen：仅天空/仅室内反弹/自发光；Software/Hardware Lumen；没有 SkyLight；反射单开而 GI 关闭；体积关闭/范围外/镜头切换；参数与前后面；环境 diffuse/specular/AmbientBase 分项截图；通用合成前后 Kajiya 像素不重复增亮。

性能：固定分辨率、相机和 GPU 配置，预热后分别统计 0/1/4/8 RT 灯、相同 Kajiya 覆盖率及相同场景的 GPU 时间。记录新增 draw、native shadow RT 峰值存活量、Kajiya Pass、RenderLights 与全帧时间；比较 Lumen 体积 enabled/disabled 的增量与 async wait。现阶段没有任何本方案实测 GPU 时间，不能承诺耗时范围。

实施后独立检查完整 diff、生产与消费条件、Light Function/阴影顺序、SceneColor 叠加、关闭分支、UB 生命周期、指针与 lambda 规则。未验证的渲染配置单独列出；成功编译不替代功能或性能验证。

## 参考

- Engine/Source/Runtime/Renderer/Private/LightRendering.cpp（RT 生成/降噪/逐灯消费/Light Function 顺序）
- Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp（主渲染时序）
- Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp（主 Pass、资源和混合）
- Engine/Shaders/Private/KajiyaHair.ush；KajiyaHairForwardLighting.ush（现有响应）
- Engine/Shaders/Private/BasePassPixelShader.usf（现有专用环境及原生半透明 SH 消费）
- Engine/Source/Runtime/Renderer/Private/Lumen/LumenTranslucencyVolumeLighting.cpp/.h
- Engine/Shaders/Private/Lumen/LumenTranslucencyVolumeLighting.usf；LumenTranslucencyVolumeShared.ush；SHCommon.ush
- https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-ray-tracing-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/forward-shading-renderer-in-unreal-engine
