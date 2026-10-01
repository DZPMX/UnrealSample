# Kajiya 环境 Mask 替代方案核查

日期：2026-09-25。对应请求：查找独立 `KajiyaEnvironmentMask` 的替代方案。
状态：只读源码分析与方案记录；未修改引擎、材质或关卡，未编译。以下推荐是设计方案，不是已通过像素等价或 GPU 性能验证的实现。

## 结论

优先复用现有 GBuffer 的 Kajiya 材质身份，在已经读取这些数据的环境合成中作判断，删除独立 R8 Mask、清除 pass、OpaqueForward 的附加 MRT 和三个合成器中的 Mask SRV。

必须同步处理 GBuffer 身份和通用屏幕空间代理材质的区别。不能仅删除 `ShadingModelsMaterial.ush` 中的 Default Lit 转换，然后认为所有消费者自然支持 Kajiya。

设计目标是不增加纹理、Buffer、UAV/SRV 绑定、新的纹理消费者或全屏分类 pass。复用已有采样结果，不单独再调用一次 GBuffer 采样。对照当前实现，头发像素读取的资源和读取覆盖会改变；实施前仍需按 UAV/SRV 审批规则列清实际访问变化，不能以“资源已经存在”代替审批。

## 已核实的当前行为

1. `RenderOpaqueForwardPass` 仅在上游确认存在相关 draw 时调用。进入该函数且不是全局 Forward 时，会创建 Depth Target 尺寸的 `PF_R8_UINT`、ShaderResource + RenderTargetable 纹理，清黑。Create2D 未指定额外样本数；该资源为单采样。无论环境 CVar 是否为 1，都存在这个分配与清除。
2. 同一帧的各 view 复用这个纹理，并在绘制时用各自 viewport。它不是每盏灯或每种材质各一张。OpaqueForward 以 MRT1 写入；非专用环境像素也写 0。深度与材质 discard/coverage 决定实际写入。
3. CPU 启用条件是：非全局 Forward、存在非静态 SkyLight、有 ProcessedTexture 或开启实时捕获、`r.KajiyaHair.Environment == 1`。另外，整个 OpaqueForward 支持/调度条件仍适用，包括 Substrate 不支持、有效 draw、非 RayTracedOverlay 等。
4. Shader 的实际 ownership 是上述 Enabled 且当前像素的 ShadingModelID 为 Kajiya；不能用“材质编译集合包含 Kajiya”代替逐像素判定。当前 Shader 在 OpaqueForward、非全局 Forward、编译包含 Kajiya、非 Unlit 材质代码路径内执行该判断。
5. 专用环境已接管的像素在 forward 阶段跳过通用 diffuse/IBL；后续 DiffuseIndirect、ReflectionEnvironment、AmbientCubemap 再读取 Mask，避免追加 UE 环境光。
6. DiffuseIndirect 的跳过结果不是统一输出黑色：双源混合为 Add=0、Multiply=1；非双源 GI 输出原 SceneColor；AO-only 为 Multiply=1。ReflectionEnvironment 的加法输出保持 0；AmbientCubemap 输出 0。这些语义必须保留。
7. DiffuseIndirect 在主流程中存在 OpaqueForward 前后两次调用。前一次 Mask 尚未创建，不能在替代方案中只根据全局 CVar 提前开启后续阶段的跳过逻辑。

对应源码：

- [资源创建、启用与绑定](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/OpaqueForwardPassRendering.cpp:214)
- [像素 ownership 与环境求值](G:/UnrealSourceCode/Engine/Shaders/Private/BasePassPixelShader.usf:1397)
- [MRT 写入](G:/UnrealSourceCode/Engine/Shaders/Private/BasePassPixelShader.usf:2500)
- [DiffuseIndirect 混合恒等结果](G:/UnrealSourceCode/Engine/Shaders/Private/DiffuseIndirectComposite.usf:392)
- [ReflectionEnvironment 跳过](G:/UnrealSourceCode/Engine/Shaders/Private/ReflectionEnvironmentPixelShader.usf:446)
- [AmbientCubemap 跳过](G:/UnrealSourceCode/Engine/Shaders/Private/AmbientCubemapComposite.usf:481)
- [OpaqueForward 和两个间接光阶段的顺序](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3287)

## 推荐：复用 GBuffer 的材质 ID

已有证据：

- `SHADINGMODELID_KAJIYA_HAIR` 已定义为 13，ShadingModelID 占现有 GBufferB alpha 的低 4 位；不需要新 ID、不扩展格式、不占用 SelectiveOutputMask 高位。
- 当前 Kajiya 在普通 Deferred BasePass 中被改成 Default Lit，随后仍写标准 GBuffer。ShaderGenerationUtil 已把 Kajiya 放入标准 GBuffer slot 使用集合。
- 当前项目 SM6 实际生成的 `AutogenShaderHeaders.ush`，两种 layout 均在既有 MRT2.w 编码/解码此低 4 位。不能仅根据已弃用的手写 EncodeGBuffer 推断当前行为，也不能手改 Intermediate 生成文件。
- DiffuseIndirect 的 `ReadMaterialData` 内部读取 GBuffer；另两个合成器已经调用 `GetGBufferDataFromSceneTextures`。均存在复用已读材质身份的入口。

拟定的数据契约：

1. 在普通 BasePass 的已有 GBuffer 输出中保留实际 Kajiya 像素身份。写入必须与有效 OpaqueForward 路由一致，不能把 Nanite、被排除的混合模型、非主 pass 等未被专用路径接管的像素误标。可在既有 draw/参数路径传递数值状态；无需为此引入 GPU 纹理或 Buffer。每 draw 路由和每像素模型是不同条件。
2. 每 view 使用与专用路径一致的数值状态，区分“功能具备条件”和“本阶段之前已提交接管绘制”。模式关闭、没有有效 SkyLight、没有相应 draw、专用 pass 未执行时，不跳过通用环境。
3. 三个合成器复用各自的 GBuffer 读取结果判断身份和启用状态，并保留上述全部混合恒等结果。不能整次跳过包含其他角色/材质的 pass。
4. 对 Lumen/SSGI/SSR 等仍使用标准代理数据的算法，显式保留当前 Default Lit 代理的计算语义。GBuffer 材质身份应可辨认；屏幕空间算法的代理 BRDF 应在其适配位置明确表达。不能把只存在于 forward 材质寄存器中的 11 个 Kajiya 输入当成已经从 GBuffer 解码出的数据。
5. 一并删除旧 Mask 的 SceneTextures 成员、参数、dummy 绑定、创建/清除、MRT 声明与写入、PSO 目标格式和 blend write mask。运行时 CVar 切换不再改变附加 MRT 是否存在，因为该 MRT 完全移除。

具体消费端风险已确认：

- `LumenMaterial.ush` 的 `bIsSimple`、`HasDefaultShading` 明确比较 Default Lit。
- 通用 `IntegrateBxDF` 没有 Kajiya case，未知 ID 返回零照明。
- `SampleBxDF` 对未知 ID 返回零采样，`SupportsSampleBxDF` 返回 false。
- GBuffer 自动解码和 `bForceUnlitOrDefaultLit` 调用必须同步检查；部分调用只读原始 ID，不经过完整材质适配。

因此“直接保留 ID13，其余不改”不满足等价要求。完整消费者迁移仍是实施工作，当前没有宣称已经完成。

对应源码：

- [现有 ID 与位布局](G:/UnrealSourceCode/Engine/Shaders/Private/ShadingCommon.ush:20)
- [当前身份被改成 Default Lit 的位置](G:/UnrealSourceCode/Engine/Shaders/Private/ShadingModelsMaterial.ush:172)
- [标准 GBuffer slot](G:/UnrealSourceCode/Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp:1833)
- [当前生成的 GBuffer 编码](G:/UnrealSample/Intermediate/ShaderAutogen/PCD3D_SM6/AutogenShaderHeaders.ush:35)
- [有效 BasePass 路由判断](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp:218)
- [Lumen 材质读取](G:/UnrealSourceCode/Engine/Shaders/Private/Lumen/LumenMaterial.ush:619)
- [通用 BxDF 分派](G:/UnrealSourceCode/Engine/Shaders/Private/ShadingModels.ush:1075)
- [通用 BxDF 采样分派](G:/UnrealSourceCode/Engine/Shaders/Private/ShadingModelsSampling.ush:208)

## 其他方案

| 方案 | 当前核查结果 | 取舍 |
| --- | --- | --- |
| 现有 Stencil 位 + 硬件 stencil test | 现有 bit1 标记整个 OpaqueForward draw，不区分混合材质中的 Kajiya 像素；不同阶段 Stencil 有复用/清理。当前编辑器启动日志明确报告像素 Shader Stencil Ref 不支持。 | 不能直接作为保留混合模型能力的完整替代。按纯 Kajiya draw 分类只是受限版本；另画分类 pass 又增加绘制与深度/stencil 访问。 |
| 现有 LightingChannelsTexture 的高位 | 这张 R8 texture 已在 OpaqueForward 存在时生成；当前 bits0–2 为 lighting channels，bit3 为 ray tracing representation，bit4 为 forward 标记。高位可作为存储候选。 | 空位不等于数据已经存在。CopyStencil 阶段当前只读 Stencil/NaniteShadingMask，没有逐像素 Kajiya 身份；引入 GBuffer 读取将新增 SRV 消费。AmbientCubemap 目前也没有此纹理绑定。需额外审批，收益不如直接复用其已有 GBuffer 读取。 |
| 把 Kajiya 绘制移到环境合成之后覆盖 SceneColor | 当前 OpaqueForward 在后一次 DiffuseIndirect 和反射合成之前。普通 forward 材质仍需要后续 Lumen 合成。 | 不能把整个 pass 后移。另分 Kajiya pass 需要处理混合模型拆分/重复绘制，并重新核对 SSR 读取 SceneColor 的时机。不是保持既有渲染顺序的局部替换。 |
| GBuffer CustomData 或 SceneColor alpha 塞标记 | Kajiya 当前走标准 slot，CustomData 不是已定义的 Kajiya mask 存储契约；SceneColor alpha 已有 SSS/捕获用途。 | 不优先采用。可能新增通道写入或损害已有语义，且没有利用现成的模型 ID。 |
| Mask 降分辨率/只在 CVar=1 时分配 | 可缩减部分资源开销；低分辨率不能保证 masked 发丝边缘逐像素一致。CVar 条件分配还需解决当前缓存 PSO 的 MRT 布局。 | 保留专用纹理及绑定，未解决本次关注的结构问题。 |

证据：

- [Stencil 生命周期/位用途](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/PostProcess/SceneRenderTargets.h:5)
- [当前机器 Stencil Ref 能力日志](G:/UnrealSample/Saved/Codex/Kajiya_InterfaceReview_20260925/FinalEditor.log:863)
- [LightingChannels 生成与既有绑定](G:/UnrealSourceCode/Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:3662)
- [复制的具体位运算](G:/UnrealSourceCode/Engine/Shaders/Private/DownsampleDepthPixelShader.usf:129)

## 性能预算与验证

以下为格式/分辨率计算出的逻辑数据量，不是实测 VRAM 峰值、DRAM 带宽或 GPU 耗时。

| 渲染目标尺寸 | 当前单张 R8 Mask 有效 texel 数据量 |
| --- | ---: |
| 1920 × 1080 | 1.98 MiB |
| 2560 × 1440 | 3.52 MiB |
| 3840 × 2160 | 7.91 MiB |

当前一次帧内的逻辑 Mask 数据量可按 `P + W + ΣRᵢ` 字节建立预算：P 是全目标 clear 像素数，W 是所有 view 的实际 forward 写入样本数，Rᵢ 是每次合成调用实际读取的像素数。DiffuseIndirect 可有多阶段，AmbientCubemap 可有多个贡献，不能固定假定正好三次全屏读取。缓存、fast clear、压缩、对齐、overdraw 和 RDG aliasing 会改变实际硬件成本。

若仅举例假设一次全屏 clear、一次全屏 write、三次全屏 read，4K 对应 39.55 MiB/帧的逻辑数据，60Hz 为约 2.49 GB/s。此例不是当前场景的带宽测量。

推荐方案移除上述 Mask 数据、clear pass、额外 MRT 和 SRV。GBuffer 格式/尺寸/样本数/数量保持现状，增加的是数值判定和所需代理材质分派。关闭功能或没有 Kajiya 时，也不再保留空 Mask。

不能承诺零额外读取：当前头发像素可在读取 GBuffer 前通过 R8 Mask 提前退出；替代后需要从 GBuffer 得到身份。读取范围和顺序会改变。若把完整 GBuffer 解码搬到 early-out 前，可能引入不必要加载；若先单独读 ID 再调用完整解码，也可能重复采样 GBufferB。必须检查最终 Shader 指令及资源表，而不是依赖“编译器应该优化”。

实施验收应同时满足：

1. 固定镜头/姿态/曝光/分辨率且资源预热，用旧实现已保存或实施前采集的基线比较，避免冷启动和历史变化。
2. CVar 0/1、无 SkyLight、非静态 SkyLight 的已有 cubemap 与实时捕获、无 Kajiya、普通 forward 材质、同一个材质中的 Kajiya/Default Lit 像素、masked 边缘与遮挡均按预期。
3. Lumen on/off、SSR/反射、AmbientCubemap、AO-only、GI 双源和 copy 组合保持原混合行为；保留 forward 前后阶段区别。其他模型像素不变，CVar0 保留现有代理结果。
4. 多 view、分辨率变化和不支持的 Substrate/全局 Forward 范围不被误接管；不宣称由本方案新增这些平台支持。
5. 抓帧确认 Mask allocation/clear/MRT/SRV 已完全移除；核对 GBufferB 采样次数及读取覆盖，无偷偷新增资源消费者。
6. 同设置多帧测量 BasePass、OpaqueForward、受影响 composite 和全帧 GPU 时间；记录波动范围。不能用静态显存估算或像素一致代替性能验收。

## 请求复核

- Mask 的实际用途、生产/消费和必须保留的行为：已核查。
- 现有 GBuffer、Stencil、LightingChannels 候选及限制：已核查并比较。
- 推荐替代方向及资源/性能预算：已给出。
- 实施、完整消费端迁移、像素等价和 GPU 性能：本轮未执行、未验证。
