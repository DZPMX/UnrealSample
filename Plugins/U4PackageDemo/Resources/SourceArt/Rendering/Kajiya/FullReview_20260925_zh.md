# Kajiya Shading Model 全链路复审

日期：2026-09-25。请求：重新 review 整个 Kajiya Shading Model 的实现。

结论：当前实现不能作为已经完成正确性、源效果一致性和性能验收的版本。确认四项需要处理的问题，见 F1–F4。本轮是只读审查，没有修改引擎实现、材质、实例、纹理或场景，也没有编译、切换渲染设置或重启编辑器。

## 审查范围与证据

检查了模型注册、材质节点及编译入口、11 项输出的写入与消费者、直接光与环境光公式、OpaqueForward 路由、阴影和 Light Function 资源生命周期、GBuffer 代理及后续合成，以及当前迁移材质的完整导出图与实例读回。相关源码和依赖共保存 45 个文件快照；其中 27 个文件在本轮之前已有 Git 修改。快照数量不是独立 GPU 测试数量，也不代表对每个通用引擎函数作了完整形式化证明。

本轮证据目录：[Kajiya_FullReview_20260925](G:/UnrealSample/Saved/Codex/Kajiya_FullReview_20260925)。

- [EvidenceManifest.json](G:/UnrealSample/Saved/Codex/Kajiya_FullReview_20260925/EvidenceManifest.json)：引擎 HEAD、源码 SHA-256、完整节点数量、55 个接口输入的生产者、内存计算。
- [LiveReadback.json](G:/UnrealSample/Saved/Codex/Kajiya_FullReview_20260925/LiveReadback.json)：5 个母材质、15 个实例、34 张关联纹理的本次读回。
- [MaterialParameterLedger.json](G:/UnrealSample/Saved/Codex/Kajiya_FullReview_20260925/MaterialParameterLedger.json)：221 个参数节点的名字、精确默认值、属性；重复名称的节点仍分别保留，不将其计作 221 个独立用户参数。
- [GraphStructuralReview.json](G:/UnrealSample/Saved/Codex/Kajiya_FullReview_20260925/GraphStructuralReview.json)：展开 Named Reroute 后的完整接口表达式签名及采样结构检查。
- `Graphs` 和 `SourceSnapshot` 保存原始完整图及当前源码；`PreexistingEngineChanges.diff` 是既有修改的证据，不是本轮实施的 patch。

本轮读取时，编辑器在 `/Temp/Untitled_3`，该临时关卡已有未保存改动，内容资产无脏包。读取前后脏包状态相同。当前 SkyLight 是 Movable / Captured Scene / Real Time Capture，强度 1；环境模式为 1，Debug 为 0，OpaqueForward 开启，全局 Forward 与 Substrate 关闭。这不是一次酒店镜头的固定条件画面对照。

## F1 — P1：全分辨率逐灯缓存的创建范围过宽，性能设计仍未解决

位置：[LightRendering.cpp:1553](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:1553)、[OpaqueForwardPassRendering.cpp:201](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp:201)。

确认的执行条件与资源行为：

1. `HasAnyOpaqueForwardPassDraw(Views)` 判断是否存在任意 OpaqueForward draw，没有限定为实际需要该阴影数据的 Kajiya draw。
2. 对 `SortedLights[SimpleLightsEnd, MegaLightsLightStart)` 中所有带 `bShadowed` 的灯计数，按该数量分配全场景分辨率的 `PF_FloatRGBA` 数组 `OpaqueForward.Shadows`。没有先筛选哪些灯会影响当前可见 Kajiya 像素。
3. 常规屏幕阴影生成后、Light Function 乘入之前，将屏幕阴影全尺寸复制到数组层。原屏幕阴影可复用，这个数组会把逐灯结果保留到 OpaqueForward 消费阶段。
4. 对没有独立屏幕阴影的 VSM 灯，另外创建并清除 `PF_B8G8R8A8` 临时屏幕纹理、执行 VSM 投影，再复制到上述数组，见 [LightRendering.cpp:2413](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:2413)。
5. Light Function 列表按 `ShouldRenderLightViewIndependent` 与材质存在性收集；分配全分辨率 `PF_G8` 数组，每灯还会生成临时 G8、clear、绘制、copy。列表收集阶段没有按 Kajiya 受光范围过滤。
6. 两个数组及两个 light-id→slice StructuredBuffer 经 `FOpaqueForwardLightFunctionParameters` 绑定。自定义阴影与 Light Function 的 Shader 消费在 Kajiya 分支中，但资源准备条件比消费者范围宽。
7. 这些直接光资源的准备不受 `r.KajiyaHair.Environment=0` 关闭。环境 CVar 控制环境光；不能把它描述成全部 Kajiya 扩展资源的总开关。

格式数据量推导，单采样、每层，不含临时纹理、对齐、映射 buffer、其他灯光资源：

| 分辨率 | 每层 RGBA16F 阴影 | 每层 G8 Light Function |
| --- | ---: | ---: |
| 1920×1080 | 15.82 MiB | 1.98 MiB |
| 3840×2160 | 63.28 MiB | 7.91 MiB |

例如数组有 10 层时，仅 4K 阴影数组的逻辑 texel 数据量就是 632.81 MiB。复制还需要读取源、写入目标；后续逐像素光照再读取。RDG 可复用/裁剪的实际结果、峰值显存、压缩和 GPU 时间不能从这些格式数字直接推出，本轮没有测量它们。

问题不是“用了 RDG 就一定错”，而是一个主要服务头发的功能按广泛灯光列表和整个画面准备资源，且普通 OpaqueForward draw 也能触发准备。删除独立环境 Mask 没有解决这部分成本。

修正方向：先把资源准备范围与实际消费者范围对应起来，再评估复用原生阴影和 Light Function 路径的方案。不能直接把两者乘在一起代替：当前 Kajiya 对阴影施加非线性 visibility，而 Light Function 乘入入射光颜色，消费语义确实不同。涉及新增或扩大资源绑定/读取的替代方案，必须按项目规则另行说明并获得明确批准。

## F2 — P2：Ray Tracing 专用组件属性错误控制了普通 Kajiya 环境光

位置：[BasePassPixelShader.usf:1422](G:/UnrealSourceCode/Engine/Shaders/Private/BasePassPixelShader.usf:1422)。

完整属性链：

- [LightComponentBase.h:96](G:/UnrealSourceCode/Engine/Source/Runtime/Engine/Classes/Components/LightComponentBase.h:96) 的声明、Tooltip、DisplayName 将两个属性限定为 **Affect Ray Tracing Reflections** 和 **Affect Ray Tracing Global Illumination**。
- SceneProxy 保留属性；[SceneRendering.cpp:1924](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:1924) 写入 View flags；`ShouldSkyLightAffectReflection/GlobalIllumination` 读取这些 flags。
- 当前 Kajiya 的普通 OpaqueForward 环境漫反射和双瓣高光直接使用这两个 helper 进行置零，没有限定到 Ray Tracing 路径。

影响：即使普通光栅环境仍然应工作，取消相应光追属性也会让 Kajiya 漫反射或环境高光消失。GI flag 还会将传入直接光的 `DiffuseIndirectLighting` 清零，而方向光背光项继续读取其亮度，因此影响并非只限于一个环境输出。Debug 8/9/10/14 同样复制了错误语义。

修正应覆盖最终输出、传入方向光的环境基底以及相关 debug；不能只把界面描述改短或只改一个判断。普通环境光应依据实际普通渲染路径的启用条件，不应挪用这两个光追专用开关。

## F3 — P2：独立 Ray Traced SkyLight 合成未排除 Kajiya 专用环境像素

位置：[CompositeSkyLightPS.usf:65](G:/UnrealSourceCode/Engine/Shaders/Private/RayTracing/CompositeSkyLightPS.usf:65)、[DeferredShadingRenderer.cpp:3403](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3403)。

当前 `DiffuseIndirectComposite`、`ReflectionEnvironmentPixelShader`、`AmbientCubemapComposite` 已根据既有 GBuffer 的 `StoredShadingModelID` 与逐 view 专用环境状态跳过这些像素。但是引擎还存在单独的 `CompositeRayTracingSkyLight`，它在 OpaqueForward 之后执行，不经过上述三个 Shader。

这条路径中，raygen 从 GBuffer 解码出 Default Lit 代理，计算通用 diffuse sky；最终 `CompositeSkyLightPS` 读取 GBuffer 的颜色、金属度恢复 albedo，再将 SkyLightTexture 以加法混合写入 SceneColor。它没有 Kajiya 接管状态或 ID 排除条件。OpaqueForward 的专用环境启用条件也没有排除此配置。

触发需要硬件/引擎支持并实际启用独立 Ray Traced SkyLight、非 Lumen diffuse GI、GlobalIllumination ShowFlag 开启，以及专用 Kajiya 环境已生效。源码证明两条贡献可以并存；本轮没有切换到该配置做 GPU 画面对照，所以不将它认定为当前编辑器画面差异的已复现原因。

修正应使这条已有合成路径遵守同一环境接管约定。它已经读取 GBuffer，可以先评估复用该次读取；不应重新增加分类纹理。

## F4 — P2：Elena 当前 Kajiya 材质仍在模拟原 GBuffer 量化和手动 Gamma

位置：[KajiyaHairMaterial.ush:24](G:/UnrealSourceCode/Engine/Shaders/Private/KajiyaHairMaterial.ush:24)、[KajiyaHairMaterial.ush:56](G:/UnrealSourceCode/Engine/Shaders/Private/KajiyaHairMaterial.ush:56)、[KajiyaHairMaterial.ush:80](G:/UnrealSourceCode/Engine/Shaders/Private/KajiyaHairMaterial.ush:80)。

本次完整材质读回确认，`M_Hair_Masked_KajiyaForward` 的有效 Custom 节点仍调用 `EvaluateKajiyaHairMaterial`，并将结果连接到全部 11 个输入，因此不是一个无人引用的旧文件。

当前行为包括：法线 12/11 位 oct 量化、发丝方向 8/8 位 oct 量化；shift 和 AO/backlit 的 63 级量化；高光强度、roughness 和颜色的 255 级量化；颜色最后 `pow(..., 2.2)`。这些是输入准备阶段的实际计算。

这与用户之前明确要求去掉为模拟原 GBuffer 精度而添加的量化、整理到 UE 标准颜色输入流程的方向不一致。它本身不证明原捕获复刻算法算错，而是后来的 Kajiya 迁移仍沿用了用户已经要求移除的处理方式。

当前相关颜色贴图通过 UNorm/LinearColor 取样，所以不能把这里直接判为“双重 Gamma”，也不能单独删掉 `pow`。修正必须一起迁移颜色纹理的编码/导入、取样、tint/颜色处理所在空间以及最终线性输入，保留确实属于源材质外观计算的操作。删除量化也要区分存储精度模拟和必要的贴图数据解码。

## 已重新核实的实现约定

### 材质接口

当前节点共 11 项，声明、编译索引和 `ShadingModelsMaterial.ush` 消费顺序一致：

| 索引 | 输入 | 当前契约 | 未连接默认值 |
| --- | --- | --- | --- |
| 0 | SurfaceNormal | world-space float3 表面法线 | (0,0,1) |
| 1 | StrandDirection | world-space float3 发丝方向 | (0,1,0) |
| 2 | LinearBaseColor | 线性 RGB | (0,0,0) |
| 3 | PrimarySpecular | 主瓣强度，约定 0–1 | 0 |
| 4 | PrimaryRoughness | 主瓣 roughness，约定 0–1 | 0.4 |
| 5 | PrimaryShift | 主瓣 shift，约定 −0.5–0.5；求值时乘 2 | 0 |
| 6 | SecondarySpecular | 次瓣强度，约定 0–1 | 0 |
| 7 | SecondaryRoughness | 独立次瓣 roughness，约定 0–1 | 0.4 |
| 8 | SecondaryShift | 独立次瓣 shift，约定 −0.5–0.5；求值时乘 2 | 0 |
| 9 | AmbientOcclusion | 材质 AO，约定 0–1 | 1 |
| 10 | Backlit | 背光系数，约定 0–1 | 0 |

这些范围是当前接口和消费端的契约，不代表编译器自动 clamp/normalize 所有输入。主次粗糙度不是同一个字段。Elena 材质生产者主动从同一个源 roughness 生成两项，是该材质的数据选择，不是节点在兼容旧布局时偷偷替换。

当前源码没有 `>=5 / >=6` 输出布局兼容分支。`NUM_MATERIAL_OUTPUTS_GETKAJIYAHAIROUTPUT > 0` 表示是否存在自定义输出节点；无节点的普通根输入路径不属于多版本布局检测。

### 当前资产

| 母材质 | 节点数 | 自定义接口连接 | 显式 TextureSample 节点数 |
| --- | ---: | ---: | ---: |
| M_Hair_Masked_KajiyaForward | 54 | 11/11 | 1，另有 Custom 内 4 次采样 |
| M_Nathan_HairKajiyaFine | 248 | 11/11 | 7 |
| M_Nathan_HairKajiyaLayered | 1084 | 11/11 | 21 |
| M_Nathan_HairKajiyaMapped | 508 | 11/11 | 13 |
| M_Nathan_HairKajiyaPacked | 359 | 11/11 | 7 |

合计 2253 个节点。图结构、分支、精确默认值、Named Reroute 连接均保留在本轮证据中。15 个实例与 34 张纹理的属性读回无错误，根输出结构签名与已有迁移后快照一致。

没有仅凭“同名贴图采样两次”判为重复计算：本轮按 UV、Mip、采样属性及上游表达式检查，没有找到结构完全相同的显式采样组。这不构成最终编译 Shader 无冗余 ALU 的证明。Nathan 的多个 UV 样本、材质层和真实静态分支不能因为图较大就删除。

资产读回覆盖已知五个母材质及十五个实例，Registry 名称检索也返回这五个候选；本轮没有重新加载全项目所有无 Kajiya 名称的材质来证明绝无其他使用者。嵌套的 Coverage/Dither Material Function 未在本轮重新进行全部排列的执行验证。

### 模型身份、GBuffer 与环境排除

模型为 `MSM_KajiyaHair` / ID 13，与 UE 自带 `MSM_Hair` 不同。材质自动符合 OpaqueForward 路由，不要求 Nathan 母材质的 `OpaqueRenderingPath` 属性显示 Forward 才能生效。

完整 Kajiya 数据位于 forward 材质寄存器，未将 11 项额外写进 GBuffer。符合 OpaqueForward 资格的实际 Kajiya 像素，在既有 GBuffer 模型 ID 中保存 13；通常的屏幕空间算法解码为 Default Lit 代理，三个专用环境排除消费者另外使用保留的 `StoredShadingModelID`。

独立 `KajiyaEnvironmentMask` 的声明、创建、附件、读取已不在当前生产代码中。环境排除仍使用原 GBuffer，不是新的 RT。每 view 状态在实际提交 OpaqueForward 时设置，并对 single-pass stereo 传播主 view 状态。此处的源码一致性检查通过，不等于本轮重新完成了 VR 和全部边缘情况的 GPU 验证。

### 直接光和环境光的含义边界

已检查直接光所调用的 attenuation、rect/capsule 积分、光源纹理、阴影解码、LocalVisibility、SunVisibility、双瓣高光、DiffuseScatter 和 Backlit 消费链。Light Function 乘入灯色，阴影进入独立 visibility，照明通道作用于灯贡献；方向光背光读取环境基底亮度。仅改 SkyLight 强度不会改变这些差异。

当前专用环境是 UE SkyLight SH 的适配：顶点法线求 SH 并插值；像素从一阶 SH 求方向，正负方向 SH 差生成方向能量，再输入 Kajiya 环境响应。它不是已恢复的原作 CPU 探针选择与参数生产算法，不读取原作全部逐物体探针数据。无方向能量时不凭空产生方向双瓣；背面处理也保留在 Shader 中。

本轮核对了已有原作环境片段的分支、AO/NoV/roughness gate、双瓣及前后面行为；原作片段中的 `log2(AO+NoV)` 没有额外 NoV clamp，不能在审查中把自行增加 clamp 说成原算法。本轮不提供整个原游戏所有灯型、全部材质排列和所有输入域的等价证明。

## 当前支持与未验证范围

| 情况 | 源码层状态 / 本轮验证边界 |
| --- | --- |
| Desktop Deferred、非 Nanite、Surface Opaque/Masked、Substrate 关闭 | 当前主要接入路径；本轮完成源码和上述资产读回 |
| 环境模式 1 + 可用非静态 SkyLight | 专用 SH 适配；存在 F2、F3 |
| 环境模式 0 或没有可用动态 SkyLight | 使用通用 UE 环境路径；仍可能有 F1 的直接光资源 |
| 环境模式 1 下 Lumen 局部间接光/Reflection Capture/AmbientCubemap | Kajiya 像素按当前设计不叠加这些通用环境贡献；此前明确批准的第一版边界，不在本轮冒充新增算法错误 |
| 全局 Forward/MSAA | 专用环境被 `!FORWARD_SHADING` 排除；本轮未做运行验证 |
| 全局 Substrate | OpaqueForward 专用路径关闭，不能宣称同效果支持 |
| Nanite、Translucent、Path Tracing、Mobile | 不属于上述专用路径的完整效果保证；未运行完整矩阵 |
| VR / single-pass stereo | 检查了 CPU 状态传播，未在设备上验证 |
| 新 HLSL/MIR 材质生成器 | 存在模型宏注册不代表节点完整迁移；本轮未建立完整编译支持证明 |
| 原游戏画面一致性 | 当前 SH 数据源、UE 光衰减/阴影和原生路径仍有适配；未做同镜头线性输出逐像素对照 |
| GPU 性能 | 完成资源生命周期与格式成本审查；未取得受控多帧耗时和峰值显存 |

另一个需要明确的接口边界：普通根 BaseColor/Specular/Metallic/AO/Normal 仍会参与 UE 的代理与通用间接光计算。自定义 11 输入不能代替这些根输入在所有通用消费者中的作用。当前迁移图保留了根输入生产链；“随便一个新材质只接 Kajiya 输出就能在所有模式得到同一效果”没有依据，后续通用化必须把这层约定说清楚。

## 独立复核与请求完成情况

第二遍检查将发现分别回溯至声明、生产者、传输、消费者及实际开启条件，排除了以下误报：把现有 GBuffer 代理当成旧接口兼容；把同贴图不同 UV 的取样当成重复；把原作已有的未 clamp NoV 当成擅自遗漏；把空映射 buffer 的 UE dummy 分配当成无效资源；把控制台中文显示编码当成资产文字损坏。

重新检查 F1 的格式和计数边界，避免写成所有场景灯必然都分配；F3 明确限定独立 RT SkyLight 并与当前场景实测区分；F4 明确当前 UNorm 取样，避免错误建议直接删 Gamma。历史报告中的旧环境 Mask 设计未用于证明当前实现正确。

| 初始检查项 | 结果 |
| --- | --- |
| 清点 C++、Shader、材质节点和接线 | 已完成当前已知实现的清点、源码快照与资产完整导出 |
| 核对参数真实语义 | 11 输入、SkyLight 属性链、资源参数已复核；资产参数全部记录，原作全部排列参数的独立新证明未完成 |
| 审查直接光、环境光、阴影、GBuffer、路径和成本 | 已完成源码审查，确认 F1–F3；性能实测与全配置运行验证未完成 |
| 对照源材料和已有要求 | 完成关键环境片段与当前输入处理检查，确认 F4；不宣称整个原游戏全域等价 |
| 按严重程度提供位置、影响和修正建议 | 已完成，四项问题均未在本轮擅自实施修复 |

建议先解决 F1 的资源范围与方案，再修正 F2/F3 的环境接入语义，随后执行 F4 的完整颜色/输入迁移。完成对应修复后需做开关 A/B、RT SkyLight 路径对照以及固定场景多帧 GPU 性能验证。当前结论是 review 已交付、实现仍有待修问题，不是实现验收通过。
