# Kajiya 阴影、输入与材质诊断崩溃修复

本轮范围：OpaqueForward 改用原生 CSM／VSM 阴影查询，修正普通环境光误用 Ray Tracing 专用开关，补齐独立 Ray Traced SkyLight 合成排除，移除 Elena 材质输入中的量化／手动 Gamma。Light Function 保持原实现。随后处理两次实际崩溃。

## 两次崩溃的区别

### BlueNoise 参数块未绑定

第一份日志报错：像素着色器需要 `BlueNoise`，slot 1 却收到 `FViewUniformShaderParameters`。启用原生 VSM SMRT 后，采样函数读取 BlueNoise；OpaqueForward 没有原生半透明 BasePass 中对应的嵌套参数。

经用户明确批准，新增 `FOpaqueForwardShadowParameters`，嵌入原生 `FBlueNoiseParameters`。当前帧 RDG 参数绑定该 UB，`BasePassPixelShader.usf` 的 OpaqueForward 排列将 `BlueNoise` 映射到 `OpaqueForwardShadows.BlueNoise`。复用引擎已有 Scalar／Vec2 纹理；VSM 关闭时使用原生 dummy 参数。未创建纹理、UAV、逐灯噪声资源或拷贝。RayCount=0 时走原生硬阴影采样，避免进入以射线数量作除数的 SMRT 分支。

### 材质诊断导致 Material UB 释放后仍被绘制缓存使用

第二份日志位于 `FD3D12ResourceBinder::SetTexture`。Breadcrumb 是 Nathan 头发的 `VelocityParallel`，不是 OpaqueForward。初始小型转储不足以读取完整资源表，随后用 LLDB 在异常发生当下捕获现场，并通过原来的 `after_start.py` 稳定重现。

现场证据：

- UB 名称为 `Material`；复现中 slot 1、资源表第 10 项被作为纹理读取。
- UB `AtomicFlags.Packed = 0xc0000000`：引用数为 0，删除标志已置位。
- 旧资源表内存被复用为 UTF-16 字符串 `ClearBuffer(Lumen.HashTableElementCounts Size=8192bytes)`，已经不是纹理对象指针数组。
- 调用入口 `VibeUE::UMaterialNodeService::GetMaterialDiagnostics()` 原来直接执行 `Material->ForceRecompileForRendering()`。
- 引擎调用链：`ForceRecompileForRendering → CacheResourceShadersForRendering → RecacheUniformExpressions(true) → InvalidateUniformExpressionCache(true)`。最后一步清空材质 UB。源码明确要求调用者使用 `FMaterialUpdateContext` 更新图元渲染状态，因为 cached mesh commands 缓存了 UB 地址；该诊断接口没有使用此流程。

修复仅移除诊断接口的强制重编译，并在接口注释中说明读取当前状态。真正重编译继续使用 UE 的 `MaterialEditingLibrary::RecompileMaterial()`，其内部已有 `FMaterialUpdateContext` 和渲染状态同步。本轮没有改 D3D12RHI 的空指针行为，没有新增断言、忽略错误或跳过绘制。

修复后，在恢复的 Nathan 场景连续执行 500 次诊断，随后通过正规 API 重编译 5 个头发主材质并继续渲染至第 240 个测试 tick。全部材质编译状态正常，未复现崩溃。证据：`Saved/Codex/Kajiya_ShadowAndInputFix_20260925/DiagnosticsRegression.json`、`EditorVerified.log`、`BuildDiagnosticFix.log`。

## 阴影路径

- 删除 `OpaqueForward.Shadows` 全分辨率 RGBA16F 逐灯数组、ShadowSlices 映射、保存／拷贝函数以及 MissingVSMShadows 的额外全屏投影。
- Deferred renderer 下的 OpaqueForward 从现有 ForwardLight 参数读取方向光 CSM，VSM 则使用原生 `FVirtualShadowMapSamplingParameters`、光源 VSM ID、页表与物理页池。
- 方向光／点光／聚光的 VSM 软阴影调用原生 SMRT；硬阴影使用原生 SampleVirtualShadowMap。保留原 Kajiya 对阴影可见度的响应。
- OpaqueForward 的局部灯光接收阴影不受 `Affect Translucent Lighting` 或半透明专用质量开关限制。
- Light Function 的分配、逐灯掩码、采样与颜色相乘流程没有改变。
- 当前方向光根据用户批准切换为非硬件光追阴影，以使用 VSM。移除旧 shadow mask 后，不声称这条新路径支持硬件 Ray Traced Shadows 传递。全局 Forward／MSAA、非 VSM 局部灯光不应从当前 Deferred 测试结果推断为已验证。

## 环境光与材质输入

- 普通 Kajiya 专用环境光与 Debug 输出不再读取 SkyLight 的 `Affect Ray Tracing Reflections`／`Affect Ray Tracing Global Illumination`。
- 独立 Ray Traced SkyLight Composite 通过已有 GBuffer 的 `StoredShadingModelID` 排除已拥有专用环境响应的 Kajiya 像素；使用 Stored ID 是因为普通解码路径会将延迟代理解释为 Default Lit。没有新增 GBuffer／遮罩读取资源。
- Elena 材质保留原纹理通道、UV、mip bias、AO overlay、shift variation、双瓣强度和背光计算顺序，移除模拟原 GBuffer 精度的量化。
- BaseColor 改用原 sRGB 纹理的标准采样；Tint 离线转换为线性数值；移除运行时 `pow(...,2.2)` 和颜色 255 级取整。颜色修正和背光亮度门控现在在线性空间执行，这是用户要求的 UE 输入适配，不宣称与原编码空间运算数值等价。
- 已保存主材质及 m0／m3／M4 三个实例。图内节点数维持 54，节点连线不变；参数命名、描述和分组同步整理。

## 验证边界与独立复查

已重新阅读本轮完整 `Current.diff`，逐项核对新增参数的生产、传递、消费与生命周期；未引入额外兼容布局、断言、纹理分配、UAV 或超出批准范围的资源读取。预先存在的原始指针和 lambda 未顺带重构。本次诊断修复只有移除副作用及对应接口说明。

构建通过。第一批单 tick 截图没有测试几何，全部为黑色，因此已判定无效，不作为效果验证；对应大型 RDC 已删除，文本分析保留。独立测试进程显式设置 actor transform、等待组件更新并避免 Slate 回调重入，完成有效对照。未改动用户后来新建的场景。

### 有效运行与 GPU 证据

证据根目录：`Saved/Codex/Kajiya_ShadowAndInputFix_20260925/`。

- `Isolated/Result.json`：19 次有效线性浮点截图，方向光／点光／聚光 VSM 软阴影、RayCount=0 硬阴影、方向光 CSM 及各自关闭阴影的对照均完成，无崩溃／非有限输出。测试灯光关闭 Affect Translucent Lighting，仍能影响 OpaqueForward 的阴影。
- `Isolated/RuntimeAnalysis.json`：各灯的阴影图与关闭阴影图存在实际差异。这里只验证阴影分支与可见度响应，没有将这组人工场景强度当作原作灯光标定。
- `Isolated/GpuEvidence/Directional_Soft_Shadow/summary.json`：OpaqueForward 绑定原生 `Shadow.Virtual.ProjectionData`、`Shadow.Virtual.PageTable`、`Shadow.Virtual.PhysicalPagePool`、`FastBlueNoise_scalar_128x128x64`、`FastBlueNoise_vec2_128x128x64`。未发现旧 `OpaqueForward.Shadows` 全屏数组或 CopyShadow／MissingVSMShadows Pass。
- 两张既有 BlueNoise 纹理均为 128×8192、单层；Scalar 为 R8_UNORM，Vec2 资源为 B8G8R8A8_TYPELESS。对应引擎按 128×128×64 个样本组织的原生资源，不是新建的逐灯纹理。
- `IsolatedEnvironment/KajiyaPixelComparisons.json`：42684 个 Kajiya 内部像素，在 SkyLight 两个 Ray Tracing 专用开关的四种组合下 RGB 完全一致。
- `IsolatedEnvironment/GpuEvidence/CompositePixelEvidence.json`：SkyLight 使用 Use Project Setting，故本次 CVar 开关真正控制了是否执行独立光追 SkyLight。关闭时 0 个 RT SkyLight Pass；开启时两次捕获的 SceneCapture 均有 RT SkyLight 和 `GlobalIlluminationComposite`。读取 GPU 中的 GBufferB 按 ID 13／1 区分像素，合成前后 42684 个 Kajiya 像素的最大／平均 RGB 差均为 0；4032 个 Default Lit 像素正常增加照明，最大增量约 0.01011／0.00904。不是整个合成 Pass 被跳过。
- 对应 `8313_PS.txt` 的真实 SM6.6 反汇编有 `GBufferB.a * 255 + 0.5`、低 4 位模型 ID、ID==13 且启用参数非零则输出 0 的完整分支；非 Kajiya 分支仍执行原 Albedo × 去噪光照。这里 GBufferB 的硬件编码／解码属于原生 GBuffer 读回，不是重新在材质输出端添加量化。

注意：第一次环境对照将 SkyLight 的 Cast Ray Traced Shadows 强制为 Enabled，会覆盖 CVar 的关闭意图；该组仅作为已有光追合成下的稳定性观察。结论使用后续 `IsolatedEnvironment` 中 Use Project Setting 的真实开关对照。

最终逐项审查：本次两类崩溃修复、原生阴影接入、错误开关移除、独立光追天空合成排除、Elena 材质及三个实例迁移、Light Function 保持不变均有对应代码／资产／运行证据。全局 Forward／MSAA、非 VSM 局部阴影未在本轮验证，不从 Deferred CSM／VSM 结果外推；原硬件光追直接光阴影传递不属于本轮 CSM／VSM 实现。

最终只读检查时，用户主编辑器已切换到新建的 `/Temp/Untitled_2`，`r.KajiyaHair.Environment=1`、Debug=0、VSM=1，但新方向光的 Cast Ray Traced Shadows 为 Use Project Setting，且 `r.RayTracing.Shadows=1`。此前改为 Disabled 的是旧场景里的方向光；没有自动修改新场景。测试本轮 VSM 接收路径需要该灯选择 Disabled。详见 `FinalLiveState.json`。

性能方面，源码已确认删除逐灯全屏阴影副本及复制／补投影工作。新增采样使用既有 VSM 和 BlueNoise 资源；实际 GPU 时间仍需要独立测量，不把功能验证作为性能验收。
