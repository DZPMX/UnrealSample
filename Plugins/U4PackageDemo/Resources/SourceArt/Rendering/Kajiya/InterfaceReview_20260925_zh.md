# Kajiya 当前接口重做与全路径 review

日期：2026-09-25。对象：本地 UE 5.7.4 分支中的自定义 Kajiya Hair Forward，而非 UE 原生 Hair/Marschner。

本次完成一套当前接口的迁移，并检查模型注册、编译、材质输入、直接光、环境光、OpaqueForward、GBuffer 代理和后续合成。代码审查覆盖这些连接；运行验证的覆盖范围另列，不把代码审查等同于所有渲染配置均已通过 GPU 验证。

## 修复结果

1. 删除自定义输出节点的 `SpecularLobes`、`Parameters` 打包引脚，统一为 11 个有明确语义的输入。声明、编译输出索引、Shader 数据结构及读取顺序同步；主、次高光分别按强度、粗糙度、偏移组织。
2. 删除 `>= 5` / `>= 6` 旧布局分支，删除次粗糙度未接线时读取旧 `Parameters.x` 的逻辑。新次粗糙度具有独立默认值 0.4。
3. 迁移 5 个实际母材质。Nathan 四个母材质直接复用打包前的标量生产节点，移除本轮造成的无引用打包节点；共享声明仍被使用时保留。Elena 的现有 Custom 生产节点也输出明确标量，所有普通材质根输出的接线一起迁移。没有通过另建计算链来拆包。
4. 修复方向光和局部光的模型分派：原先仅用编译宏决定是否调用 Kajiya，现按像素 `GBufferData.ShadingModelID` 决定。混合材质中的 Default Lit 像素进入其正常分支；Kajiya 的阴影、Light Function 准备及调用整体置于自己的分支内。
5. 在现有 Shading Model 表达式的 `ValidEnumValues` 中补上 `MSM_KajiyaHair`。此前模型枚举和编译宏已经注册，选择节点的编辑器列表遗漏了它。

## 当前输入契约

| 索引 | 名称 | 类型、空间与用途 | 未接线默认值 |
|---|---|---|---|
| 0 | SurfaceNormal | float3，世界空间表面法线；节点不偷偷归一化 | (0,0,1) |
| 1 | StrandDirection | float3，世界空间发丝方向 | (0,1,0) |
| 2 | LinearBaseColor | float3，线性 RGB | 黑色 |
| 3 | PrimarySpecular | float，主高光强度，约定 [0,1] | 0 |
| 4 | PrimaryRoughness | float，主瓣粗糙度；同时参与现有环境 specular occlusion | 0.4 |
| 5 | PrimaryShift | float，主瓣偏移，约定 [-0.5,0.5]，消费端乘 2 | 0 |
| 6 | SecondarySpecular | float，次高光强度，约定 [0,1] | 0 |
| 7 | SecondaryRoughness | float，次瓣粗糙度，独立输入 | 0.4 |
| 8 | SecondaryShift | float，次瓣偏移，约定 [-0.5,0.5]，消费端乘 2 | 0 |
| 9 | AmbientOcclusion | float，材质 AO，[0,1] | 1 |
| 10 | Backlit | float，背光强度，[0,1] | 0 |

以上范围是输入契约，节点没有新增 clamp、assert、错误拒绝或修复输入的分支。11 个 `GetKajiyaHairOutputN` 是 UE Custom Output 编译器生成的函数名，不是并存的版本协议。

仍有一个 `NUM_MATERIAL_OUTPUTS_GETKAJIYAHAIROUTPUT > 0` 条件，用于已有的“放了 Custom Output 节点／没有该节点”功能。没有节点时，沿用材质主输出的默认路径：N=WorldNormal，T=TangentToWorld[1]，BaseColor=BaseColor，两瓣强度=Specular，两瓣粗糙度=Roughness，两偏移=0，AO=GBufferAO，Backlit=0。它没有识别五输出或六输出，也没有让新节点的 SecondaryRoughness 偷读 PrimaryRoughness。现有 5 个母材质均已显式连接全部 11 个输入。

## Review 覆盖与完整数据流定位

| 层次 | 当前实现位置 | 核查结果与边界 |
|---|---|---|
| 模型标识 | EngineTypes.h、ShadingCommon.ush、MaterialShader.cpp | MSM_KajiyaHair / Shader ID 13；NUM 14，仍在四位 ID 掩码范围内；显示名和调试色已注册 |
| 编译 | HLSLMaterialTranslator.cpp、MaterialIRToHLSLTranslator.cpp、MaterialExpressionKajiyaHairOutput.h/.cpp | 两个 translator 有模型宏登记；当前节点实际验证的是既有 HLSL Compile 路径。仅有 IR 模型宏不等于完整 MIR Custom Output 支持，未声称后者已经实现 |
| 材质生产 | 5 份完整前后图、KajiyaHairMaterial.ush | 55 个输入值及依赖链逐一对应，静态分支和采样路径保留。既有源编码计算另见下文 |
| 像素传递 | ShadingModelsMaterial.ush、DeferredShadingCommon.ush | FKajiyaHairData 是本次 Shader 求值中的局部数据，不是新增 GBuffer MRT 中的全套材质参数；主粗糙度命名和全部消费者同步 |
| GBuffer 代理 | ShadingModelsMaterial.ush | 主 N、BaseColor、Metallic=0、主粗糙度、AO 写入代理；非 OpaqueForward 且非全局 Forward 时使用 Default Lit ID。这维持 UE 后续系统的代理输入，不代表二次射线已经调用 Kajiya |
| 材质分派 | MaterialShared.cpp、OpaqueForwardPassRendering.cpp、OpaqueForwardPassShaderTypes.h | Kajiya 材质请求 OpaqueForward；现有 Surface、Opaque/Masked、非 Substrate/Sky/Decal/SingleLayerWater/原生 Hair 的资格规则保留。未重新引入此前已撤销的半透扩展 |
| 直接光模型 | KajiyaHair.ush、KajiyaHairForwardLighting.ush、ForwardLightingCommon.ush | 两瓣各用各自粗糙度与 Shift；保留 AO、局部阴影着色、sun visibility、Backlit、源散射函数、光照通道、云/大气、方向阴影淡化和完整 RGBA 阴影解码；本轮改的是模型分派和字段名 |
| 环境光输入 | BasePassVertexShader.usf、BasePassCommon.ush、BasePassPixelShader.usf | 动态 SkyLight 的 UE SH；顶点法线求值后插值；像素用亮度加权的一阶 SH 估计 D，正反方向 SH 差得到 ProbeColor。它是既有 UE 适配器，不是已恢复的原作 CPU 探针算法 |
| 环境响应 | KajiyaHair.ush | Base/Directional 分离、背面重分配、AO、双瓣、specOcc 和门控按当前完整实现保留；未将与直接光不同的运算顺序、pow/exp2-log2 路径合并 |
| 环境归属 | OpaqueForwardPassRendering.cpp、SceneTextures.h、BasePassPixelShader.usf、PixelShaderOutputCommon.ush | 支持配置下每帧清零 R8_UINT，和 SceneColor 同绘制的 depth/mask/discard/coverage 写归属；RDG 持有资源，SceneTextures 保存当帧借用。模式切换时附件格式保持一致 |
| 合成消费者 | DiffuseIndirectComposite.usf、ReflectionEnvironmentPixelShader.usf、AmbientCubemapComposite.usf、IndirectLightRendering.cpp | 被接管像素的加法贡献为 0，乘法贡献为 1；非 dual-source 路径保留输入 SceneColor；分离 SSS 输出仍为 0；无资源使用既有 ZeroUIntDummy 加 Enabled 条件 |
| PSO／平台 | BasePassRendering.cpp、OpaqueForwardPassShaderTypes.h、PixelShaderOutputCommon.ush | R8_UINT 附件的运行时分配、Shader 输出格式、预缓存格式及输出声明保持一致；全局 Forward 不添加该附件 |
| UE 原生 Hair | HairBsdf.ush、RayTracingSkyLightEvaluation.ush 等 | 名称中的 KajiyaKayDiffuseAttenuation 属于 UE 原生 Hair 的路径，不等于本次自定义 Kajiya 模型；未混为一谈或全局替换 |

完整当前算法代码的只读快照及输入清单见同目录 `InterfaceReview_20260925_Source.md`。它保留实际函数、分支、顺序、系数和坐标变换；上表仅用于定位，不替代算法正文。其余大文件的相关实现与本轮改动可在证据目录的 `Before/`、`Final.diff` 和当前引擎对应文件中核对。

## 材质迁移证据

注册表扫描了 709 个 Material 资产，找到 5 个使用该节点/模型的实际母材质，无扫描错误：

- `/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward`
- `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine`
- `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered`
- `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped`
- `/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked`

证据根目录：`G:/UnrealSample/Saved/Codex/Kajiya_InterfaceReview_20260925`。

- `MigrationPlan.json`：旧打包分量到新输入的显式来源映射。
- `GraphsBefore/`、`GraphsAfter/`：完整图，包括精确浮点参数、对象路径、Named Reroute 声明和接线。
- `GraphVerification.json`：55/55 输入展开后的计算依赖签名相同；普通根输出、Material Attributes、Tangent Output 与材质设置相同。Custom 节点核对采样/Settings/求值部分及语义输出，尾部拆包改写单独对照。
- `LiveAfter.json`：15 个实例的父材质、scalar/vector/texture overrides、base-property overrides 与之前相同；没有改动实例参数文件来掩盖接口变化。
- `MigrationResult.json` 记录初次执行；Elena 最终修正以 `ElenaMigrationResult.json` 和重新导出的 `GraphsAfter` 为准。其 Custom 输出数组最初未刷新内部输出名，已用属性通知重建缓存并重接所有消费者，最终 11/11 接线和根输出对照通过。
- 临时测试材质使用 `/Engine/Transient`，测试 Actor 已销毁，不留下新测试资产。

`Saved/Codex` 内之前日期的一次性迁移脚本和报告是历史记录，不是运行时兼容层。本次没有批量改写那些历史记录。项目 Source / SourceArt / Plugins 中没有另一个仍在写旧 Kajiya 布局的生产代码；实际生产者为本轮迁移的材质资产及上述 Shader。

## 已执行验证及其限制

- 引擎构建通过，见 `Build.log`；补齐选择节点元数据后的最终构建 `FinalBuild.log` 也成功，最终编辑器进程已加载。初次构建因编辑器退出脚本 API 调用失败、Live Coding 仍活动而被 UBT 拒绝；正常退出后构建成功，没有通过绕过模块锁解决。
- 5 个母材质更新 Shader 并保存，当前诊断 `bIsCompiledOk=True`；实例输入快照一致。
- 混合模型回归：独立 Default Lit 与混合材质选择 Default Lit 的 RGBA16F 输出最大差 **0**；独立 Kajiya 与混合材质选择 Kajiya 的 262,144 个通道中只有 **2 个通道不同**，最大绝对差 **0.0625**，按半浮点位模式核对均为 **1 ULP**（79.6875→79.75、61.65625→61.625）。Default Lit 与 Kajiya 输出明显不同，测试没有退化成两者都为零。该夹具包含独立局部光，未据此声称所有方向光和平台排列逐项 GPU 验证通过。
- 热身后的酒店第 1122 帧，960×540 浮点捕获：接管 **8,519 像素**，全部输出有限；模式切换最大差约 **0.01236**，非接管像素最大差 **0**。见 `RuntimeVerification.json`、`CaptureWarm.json`、`Warm_*.f32`。
- `CaptureComparison.json` 中冷启动时立即加载场景取得的前/后快照没有形成有效同状态基线，归属像素均为 0，不能用它证明迁移前后最终画面逐像素相同。没有将它包装为成功证据。输入等价和当前运行验证是各自独立的证据。
- 环境模式的其余测试与已知局限沿用 `Environment/Implementation_20260925_zh.md` 的明确范围。本轮没有声称重新完成其全部测试。
- 最终重新加载已保存资产后，再次验证 55/55 输入、根输出及 15 个实例一致。`RestoredFinal.json` 和 `FinalSmoke/Result.json` 记录酒店 Sequence 第 1122 帧、相机锁定、mode=1/debug=0、无 dirty map/content。最终视口 PNG 的自动化请求 30 秒内未落盘，未将它算作通过的截图检查；浮点运行证据来自此前已完成的 SceneCapture。最终日志没有新增 Kajiya Shader 编译错误或 Fatal。

## Review 后仍需明确的已有行为

**当前“输入明晰”不等于输入算法已经统一为 UE 标准色彩管理。** Elena 的 `KajiyaHairMaterial.ush` 仍执行源方向八面体量化、8/6 位数值量化、UNorm 采样后的颜色计算及 `pow(..., 2.2)`；Nathan 的图仍有源 Forward/Deferred 输入选择。这些是现有资产在本轮迁移前的有效计算，不能把它们当成没用的兼容代码直接删除，同时宣称效果不变。本轮保留并逐值验证了这些输入。此前标准 UE 材质流程的去量化/颜色管理要求，不能据此宣称已经覆盖后来新增的全部 Kajiya 材质。

Elena 当前生产函数确实只计算一个源粗糙度，并把该数值传给两瓣。这是材质生产者的既有数据定义。新通用节点的两瓣输入仍完全独立，Nathan 的独立第二瓣保持各自分支，没有通用节点级别的主瓣代用。

专用环境模式仍有已说明的适配边界：原 CPU 探针选择/插值、逐实例系数、原 ambientShadow.G、SSR alpha、源 bounce 不是完整原作输入；均匀或一阶抵消的 Sky 可产生零方向高光。直接光的灯光选择、UE 单位、距离/锥形/矩形光源衰减与滤波仍属 UE 适配层。这些不能称“原引擎完整一比一”。

Global Forward/MSAA、Substrate、移动、MIR Custom Output、Path Tracing／所有二次射线、实时 Sky、多视图/动态分辨率、极端 AO/方向输入及性能，没有在本轮取得完整运行验收。没有为了覆盖未验证场景新增断言、强制 clamp、拒绝输入或旧布局回退。

## 独立最终复核

实施后重新核对用户请求及开工清单，复读本轮 **7 个引擎文件完整 diff**、5 份材质图的全部新增/删除/重接关系、15 个实例快照。字段顺序、类型、独立默认值、控制流和每个消费者一致；两处直接光分支中原阴影与 Light Function 运算顺序保留。完整 review 与实施分开执行。

`DiffReview.json` 的接口与资产检查全部通过；`AssetDiffReview.json` 确认 Elena 只修改 Custom 输出及 Kajiya 节点，两者语义接线同步；Nathan 每个母材质移除 13 个旧打包/引用节点，新增 16 个输出声明/引用/注释节点，保留的计算节点没有参数修改。新增行没有 assert/check/ensure/fatal 或隐式 lambda capture，没有新增 C++ raw-pointer 字段或存储。继承的 `Compile(FMaterialCompiler*)` 为既有 UE API 边界，函数内部继续使用已有引用。指定文件 `git diff --check` 通过。

对照请求：重做当前接口、迁移实际资产、移除旧布局兼容、完整路径代码 review 均已完成；所有引擎路径/原作画面的运行等价仅在上述证据范围内确认，未验证范围明确保留。
