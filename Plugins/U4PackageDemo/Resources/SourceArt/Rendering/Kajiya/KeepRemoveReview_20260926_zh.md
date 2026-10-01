# Kajiya 与相关引擎改动：保留／撤除评估

日期：2026-09-26。范围由用户指定为 Kajiya 和相关引擎改动。本文是取舍清单，不是删除方案的执行记录。本轮仅读取生产源码、资产文件、Git 历史和既有验证记录，并写入审查文档与证据；没有修改生产代码、材质、配置或场景，没有启动 UE、编译或回退。

**建议：停止扩张支持范围。若目标是恢复可维护的 UE 工作流，保留原有 OpaqueForward 和独立修复，先将已知头发材质迁回 UE Hair，再撤掉 Kajiya 专用运行路径。若仍需要双瓣外观，可选择保留受限的 Kajiya；这仍需维护一套自定义模型，不能称为已经兼容完整 UE 灯光系统。**

以下是用户要求的功能与取舍摘要，并非对原 Shader 算法的简化替代或全域等价证明。完整当前差异与原始提交另存供逐行核对。

## 1. 改动实际分成三层

| 层 | 核实结果 | 对取舍的影响 |
| --- | --- | --- |
| 先于 Kajiya 的 OpaqueForward | Git 中已有 2026-06-28 起的 0.1、1.0、1.1，以及 2026-07-01 的 1.2；当前包含材质路由、BasePass 标记、前向直接光和后续间接光合成约定 | 不应随着 Kajiya 整块删除；也不把它全部归因于本对话 |
| 已提交的 Kajiya 基础 | `99b3ed7b4d7b`，2026-09-06，18 文件，448 增／15 删；模型注册、自定义节点、直接光、材质解码、自动前向路由等 | 只回退当前工作区不会删除模型本体 |
| 当前未提交增量 | 相对引擎 HEAD `92f7f0c1cc5f`：32 文件，565 增／118 删；与上述基础文件取并集为 39 文件 | 包含环境、11 输入接口、阴影接入、独立崩溃修复；不能整体 reset |

39 是本次“核心提交＋当前差异”的文件并集，不是整个定制引擎相对 Epic 的改动总数，也不是 39 个独立功能。`LightRendering.cpp` 当前差异只剩一处空行；不能据文件名认定其中仍保留逐灯阴影缓存。

证据：

- [完整清单、哈希、资产状态](G:/UnrealSample/Saved/Codex/Kajiya_KeepRemoveReview_20260926/Inventory.json)
- [当前引擎差异](G:/UnrealSample/Saved/Codex/Kajiya_KeepRemoveReview_20260926/CurrentEngine.diff)
- [已提交的 Kajiya 基础差异](G:/UnrealSample/Saved/Codex/Kajiya_KeepRemoveReview_20260926/CommittedKajiya.diff)
- [39 文件逐项归类与共享代码边界](G:/UnrealSample/Saved/Codex/Kajiya_KeepRemoveReview_20260926/FileScope.md)

## 2. 按功能决定保留什么

| 编号 | 当前内容与作用 | 当前代价／边界 | 建议及撤除前提 |
| --- | --- | --- | --- |
| A | **原有 OpaqueForward 基础**：Opaque/Masked 的额外前向着色、路由、深度、BasePass 标记、通用 GI／反射合成分工 | 本来就有额外着色工作；不是 Kajiya 才出现 | **保留，独立于本次 Kajiya 取舍**。任何共享文件都只删除 Kajiya 对应部分 |
| B | **Kajiya 模型本体**：`MSM_KajiyaHair`、ID 13、双瓣直接高光、DiffuseScatter、Backlit、局部／方向光 visibility、注册和编译宏、自动进入 OpaqueForward | 自定义直接光响应；光源选择、衰减、面积光近似与阴影输入仍来自 UE。维护跨 Engine／RenderCore／Renderer／Shader | 是否保留由是否继续需要此专用外观决定。彻底退出时，**先迁材质，再撤模型、注册、路由与消费者** |
| C | **11 项材质输出**：表面法线、发丝方向、线性颜色，主次强度／粗糙度／偏移，AO、Backlit | 3 个 float3＋8 个标量，在材质着色寄存器中使用；没有为其增加 11 项 GBuffer 存储。不是旧 5／6 输出兼容代码 | 保留 B 就保留这一套明确接口；撤 B 时连同资产节点撤除。不要回退到隐藏在 `Parameters`／`SpecularLobes` 中的旧布局 |
| D | **Kajiya 专用环境响应**：SkyLight SH 顶点输入、主方向与能量估算、base/directional 分离、环境漫反射／双瓣、方向光背光所用 AmbientBase、debug | SkyLight 输入适配，不是原游戏完整探针算法；不提供 Lumen 局部反弹光或完整原作 SSR 输入；没有独立环境 RT | **优先考虑撤除或停用的定制部分**。可与 B 分开取舍；撤除后失去专用环境双瓣及其背光基底，转回既有通用间接光路径，不保证外观不变 |
| E | **环境合成排除及 GBuffer 身份**：写入既有 ID13、解码时保留 Stored ID 并提供 Default Lit 代理、逐 view 状态、4 个合成器不重复加光 | 不新建 GBuffer、分类纹理或 SRV；但改动面横跨 BasePass、自动解码、GI、反射、AmbientCubemap、RT SkyLight | **与 D 成套处理**。D 生效时不能单删 E，否则重复加光；撤除 D 后可回到先前的代理约定，成套撤掉专用身份传递与排除 |
| F | **OpaqueForward 原生阴影接收**：CSM、VSM 软／硬查询、原生页表／物理页池、嵌套 BlueNoise 参数与别名 | VSM 过滤成本随可见片元、灯数和 SMRT 参数增长；复用已有资源，不再保存逐灯全屏阴影副本。当前不接收原生硬件 RT 直接光阴影结果 | **有条件保留**。保留 B 时需要这条已实现的阴影路径；保留其他 OpaqueForward 材质时也可能有价值，因为分支按 OPAQUE_FORWARD_PASS 生效，非仅 Kajiya。若所有相关路径都不再采样 VSM，才连同绑定撤除 |
| G | **逐灯 Light Function 缓存与 Kajiya 消费**：全分辨率 G8 数组、逐灯临时 G8、clear／绘制／copy、灯 ID→slice buffer、在 Kajiya 灯色上乘遮罩 | 当前仍存在。列表按全场景可渲染且带 Light Function 的灯准备，未按可见 Kajiya 受光范围过滤；存在任意 OpaqueForward draw 即可能进入准备，环境模式 0 不关闭它。RDG 实际执行受消费者／裁剪影响 | **优先撤除／重新评估**。若撤 B，其现有自定义 Shader 消费端不再需要。若保留 B 且删除 G，Kajiya 会失去当前 Light Function 效果；酒店的距离衰减／聚光柔边也会受影响，不能当作无视觉代价清理 |
| H | **材质输入与资产适配**：纹理通道、UV／mip、法线／发丝方向、遮罩、AO、参数语义、移除伪 GBuffer 量化、UE sRGB／线性流程 | 这些工作独立于是否使用 Kajiya；但现在部分生产者返回 Kajiya 专用结构，需迁移输出 | **保留有效数据与标准输入修正，迁移输出**。不要因撤模型丢掉纹理、UV、法线或颜色管理修复 |
| I | **VibeUE 材质诊断修复**：去掉诊断接口直接触发 `ForceRecompileForRendering()` 的副作用 | 独立于 Kajiya；原路径能使 cached mesh commands 持有失效 Material UB，导致 D3D12 SetTexture 崩溃 | **保留**。接口继续读取当前编译状态，正式重编译使用原生同步流程 |
| J | **先前已有的 UE Hair Backlit 修正**：`275f804e7125` 在 `HairBxDF` 设置 `bUseBacklit=true` | 影响普通 UE Hair，不属于自定义模型环境路径 | **单列、默认保留**。撤 Kajiya 不应顺带撤它；“迁回 UE Hair”也不意味着本地这项修改自动恢复为 Epic 原版 |
| K | **RT Shadow 与专用 Lumen 接入提案** | 只有方案，生产代码没有加入新的 RT 逐灯绘制或 Lumen 双 SH 体积读取 | **停止推进即可**，不存在需要从当前运行时回退的这两项新功能 |

F 中所谓 BlueNoise 复用是实际新增消费与绑定，已在历史对话单独获批，不能描述成零资源访问成本。既有 Scalar／Vec2 均为 128×8192、单层；此前 GPU 证据分别为 R8_UNORM 与 B8G8R8A8_TYPELESS。没有新建 Kajiya 噪声纹理、UAV 或逐灯噪声副本。

## 3. 当前最具体的性能负担：G 项

[当前分配和逐灯循环](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp:186) 的 `OpaqueForward.LightFunctions` 使用 `SceneTextures.Depth.Target->Desc.Extent`、`PF_G8`、Texture2DArray，每个 Light Function 灯一层。每灯另建临时 G8、clear、投射并 copy 到数组。映射 buffer 为 `uint`，按 `Scene->Lights.GetMaxIndex()` 准备；shader 用它查 slice。

| 分辨率 | 每层数组有效 texel 数据 | 10 层数组有效 texel 数据 |
| --- | ---: | ---: |
| 1920×1080 | 1.98 MiB | 19.78 MiB |
| 3840×2160 | 7.91 MiB | 79.10 MiB |

这是格式乘尺寸的推导，不含临时纹理、对齐、映射 buffer，也不是峰值驻留显存或实测带宽。临时资源是否别名复用、RDG 是否裁剪、实际灯光屏幕范围及压缩都会影响 GPU 执行；没有受控多帧 GPU 数据，不能声称撤除后快多少毫秒。

这部分与酒店资产中的 Light Function 材质／MPC 不应混为一件事。这里的 G 是为了让 Kajiya 消费它们而新增的引擎缓存。删除 G 不要求连带删除酒店其他材质仍使用的 Light Function 或 MPC。

## 4. 已经不存在的内容，不再作为当前问题重复计算

- `KajiyaEnvironmentMask`：独立全分辨率环境分类 RT、MRT 输出、对应绑定与读取已撤；当前改用已有 GBuffer ID。
- `OpaqueForward.Shadows`、ShadowSlices、CopyShadow／MissingVSMShadows：旧逐灯全屏 RGBA16F 阴影保存／复制／补投影已撤。
- 普通 Kajiya 环境光误读 SkyLight 的 `Affect Ray Tracing Reflections`／`Affect Ray Tracing Global Illumination`：已修正，不再由这两个光追属性控制普通环境响应。
- 独立 Ray Traced SkyLight 重复叠加：已补第 4 个合成排除消费者；这只解决天空合成，**不是 RT 直接光阴影接入**。
- `>=5 / >=6` 节点布局兼容：当前无此分支。`NUM_MATERIAL_OUTPUTS_GETKAJIYAHAIROUTPUT > 0` 是自定义输出节点存在条件。
- Elena 输入端伪量化与手动 Gamma：当前 `KajiyaHairMaterial.ush` 已移除；资产迁移和保存记录见后述。不能将必要的纹理数据解码或原生 GBuffer 解码取整混算成该问题仍存在。

这些结论通过当前源码／差异复核，不直接沿用旧报告的未修复列表。

## 5. 资产依赖与迁移代价

已知集合为 **5 个主材质、15 个实例**。当前对 Content 中 Material／Materials 目录内 `.uasset` 的完整字节检索得到相同 20 个 Kajiya 名称／类名候选；磁盘名称记录与历史父子关系相符。这不是运行中的 AssetRegistry 全项目引用证明，文件可能保留历史 name/import 字符串。UE 本轮未运行，未加载所有无 Kajiya 名称的材质或地图检查间接引用。

| 主材质 | 已知实例 |
| --- | --- |
| `/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward` | `MI_Hair_Masked_m0`、`MI_Hair_Masked_m3`、`MI_Hair_Masked_M4` |
| `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped` | `MI_Nathan_HairAlphaThin`、`MI_Nathan_HairAlphaThinDithered`、`MI_Nathan_HairThick`、`MI_Nathan_HairThickCutoff`、`MI_Nathan_HairThin`、`MI_Nathan_HairThinNoShadow` |
| `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked` | `MI_Nathan_HairThinAlpha`、`MI_Nathan_HairThinAlphaDithered` |
| `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered` | `MI_Nathan_HairThinMud`、`MI_Nathan_HairThinMudDithered`、`MI_Nathan_HairThinMudSnow` |
| `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine` | `MI_Nathan_Eyebrows` |

Elena 的 [M_Hair_Masked](G:/UnrealSample/Content/Demo/Character/Elena/Material/M_Hair_Masked.uasset) 原 UE Hair 资产仍在磁盘上，之前保存的完整图证据为 `MSM_Hair`。它可作为迁移参考，不证明将父材质改回它就能复原当前全部输入和效果。

迁移必须明确的差异：

1. 当前 Kajiya 区分 world-space SurfaceNormal 和 StrandDirection；本地普通 `HairShading` 的 N 是发丝方向。因此不能只换 Shading Model 枚举并沿用 Kajiya 的表面法线到 UE Hair Normal。
2. UE Hair 不能逐一承接这里主／次双瓣各自的强度、roughness 和 shift；回归原生响应意味着接受外观适配，需要重新验证参数范围，不能许诺同图像。
3. Opacity／coverage／dither、UV、多层贴图、AO、颜色管理仍有作用，应保留。Nathan 材质图节点多不等于计算重复或没有必要，不能按节点数量删层。
4. 材质、实例、SK 材质槽和任何场景覆写／Sequence 参数绑定要一起核对。材质名可保留以减少引用修改，但已无消费端的双瓣参数不能假装仍生效。
5. 最后才删除 `MaterialExpressionKajiyaHairOutput` 类、编译注册、独立 `.ush` 与模型枚举；先删类会留下资产加载／编译依赖。
6. 两张 `*_KajiyaUNorm` 纹理仍在磁盘，而且实例文件中仍能找到旧名称字符串。历史迁移记录明确曾将 DTex 改回原 sRGB 贴图并保存；字符串本身无法证明目前仍被有效参数引用，也不能证明已完全无引用。**暂不列为可直接删除的资产**，执行清理时需读回真实参数和 Registry。

当前 Git 资产状态也混合了已跟踪修改、staged 新增后再修改，以及未跟踪文件；`M_Nathan_HairKajiyaFine` 为未跟踪。工作区整体 checkout／clean 会丢掉不同层次的数据，不适合作为这次功能撤除操作。

## 6. 供选择的三个范围

| 选择 | 保留 | 撤除／停止 | 获得与失去 |
| --- | --- | --- | --- |
| **受限保留 Kajiya** | A/B/C/D/E/F/H/I/J，是否留 G 需单独决定 | K 不实施 | 保留当前双瓣和专用 SkyLight 外观；接受无 RT 直接阴影接收、无专用 Lumen 局部反弹，以及尚未完成性能验收 |
| **只保留 Kajiya 直接光** | A/B/C/F/H/I/J | D/E，K 不实施；G 单独决定 | 降低环境接入维护面；专用环境双瓣与 AmbientBase 行为退出，既有 Default Lit 代理／通用间接光按原路径工作，不会因此变成完整 Kajiya GI |
| **迁回 UE Hair，退出 Kajiya（倾向推荐）** | A/H/I/J；F 由其他 OpaqueForward 消费需求决定 | 材质迁移后撤 B/C/D/E/G 与对应胶合代码，K 不实施 | 减少后续追着 UE 各灯光路径补接口的需求；接受双瓣和原作特定环境／阴影响应的外观差异 |

上述推荐是基于用户希望控制维护成本的工程取舍，不是来自同条件性能实测的速度排名。

`r.KajiyaHair.Environment 0` 只退出 D 的运行时选择，不删除模型，也不停止 G 的准备。`r.OpaqueForwardPass 0` 会影响原有通用 OpaqueForward，并让 Kajiya 丢失自定义前向直接光；它不是等价“改回 UE Hair”。本轮未切任何 CVar。

## 7. 当前配置和验证边界

磁盘 [DefaultEngine.ini](G:/UnrealSample/Config/DefaultEngine.ini:23) 当前为：`r.Shadow.Virtual.Enable=0`、`r.RayTracing.Shadows=True`、`r.Lumen.TraceMeshSDFs=0`，并配置 Lumen GI／反射。VSM 从 1 改成 0、MeshSDF 开关是当前 Git 差异；RT Shadows=True 早已在 HEAD。无法仅凭此 diff 归因于哪一轮操作，未擅自改回。也不能从 ini 单独断言每盏灯都走 RT，因为组件覆盖和运行时设置仍参与决策。

这与此前方向灯显式 Disabled RT、VSM 打开的阴影验收条件不同。当前 Kajiya 不接收 RT 直接光阴影结果；若实际灯选择 RT，就不能用那组 VSM 成功测试保证此配置正确。

之前有效的证据包括：

- [阴影与输入修复报告](G:/UnrealSample/SourceArt/Rendering/Kajiya/ShadowAndInputFix_20260925_zh.md)：19 次有效隔离场景截图，方向／点／聚光 VSM 软硬、方向光 CSM；真实 GPU 绑定检查；不含完整灯型／渲染配置性能矩阵。
- [诊断回归结果](G:/UnrealSample/Saved/Codex/Kajiya_ShadowAndInputFix_20260925/DiagnosticsRegression.json)：500 次诊断调用、5 个材质正规重编译、继续渲染至第 240 tick，记录通过。
- [环境像素证据](G:/UnrealSample/Saved/Codex/Kajiya_ShadowAndInputFix_20260925/IsolatedEnvironment/GpuEvidence/CompositePixelEvidence.json)：独立 RT SkyLight 的真实开关与 ID13 像素合成排除。
- [材质输入迁移记录](G:/UnrealSample/Saved/Codex/Kajiya_ShadowAndInputFix_20260925/AssetMigration.json)：Elena 的三个实例线性 Tint、DTex 原 sRGB 纹理和主材质保存成功。

本轮将上一轮 `ReviewChecks.json` 中 11 个源码文件的 SHA-256 与当前磁盘重新比较，全部一致。这支持“那轮源码修复当前仍在”；不代替当前资产属性、已加载二进制和当前场景运行验证。

未验证：完整 UE 配置覆盖、Global Forward／MSAA、VR 实机、Substrate、Nanite、Path Tracer、Mobile、完整新材质生成器支持、硬件 RT 直接光阴影接收、专用 Lumen GI、与原截帧同条件最终一致性，以及受控 GPU 时间／峰值显存。未将历史崩溃自动归因于同一问题，也未声称现有实现绝无其他错误。

## 8. 逐项目标复核

| 用户目标 | 本轮结果 |
| --- | --- |
| 以 Kajiya 和相关引擎改动为主 | 已满足：按历史层次、当前源码和依赖清点；角色／动画／酒店作为边界依赖 |
| 弄清实际已经改了什么 | 已满足当前已知源码与磁盘证据；列明 32 当前文件、18 核心提交文件与 39 文件并集 |
| 让用户评估保留／删除 | 已提供 A–K 取舍表、迁移前提、三个范围及其效果损失；尚未执行任何选择 |
| 区分已完成、已撤掉、尚未做 | 已满足；旧环境 Mask、逐灯阴影缓存与 RT/Lumen 提案分别说明 |
| 成本与稳定性判断 | 已完成源码资源成本与历史修复证据审查；GPU 性能及当前运行验证未完成，明确保留边界 |
| 清理／回退／继续扩展 | 未执行；当前请求是 review，由用户评估后再决定范围 |

复核时重新对照用户最新范围、完整当前差异和核心提交，检查建议是否会误删共享 OpaqueForward、普通 Hair Backlit、诊断修复或未迁移的资产引用。结论是这份取舍审查已交付，不是 Kajiya 实现通过全面验收。
