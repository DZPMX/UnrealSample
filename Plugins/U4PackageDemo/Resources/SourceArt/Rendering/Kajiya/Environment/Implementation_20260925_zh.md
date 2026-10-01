# Kajiya 专用环境路径：实施与验收记录

日期：2026-09-25。引擎：当前本地 UE 5.7.4 分支。范围：桌面 Deferred 渲染器内现有 Opaque/Masked Kajiya OpaqueForward 绘制与动态 SkyLight。

本版已经实现、编译、加载，并在 Nathan、Elena 上验证。生产算法是**UE SkyLight 输入适配 + 源 Forward 连续域环境响应**。它不是原作 CPU 探针系统，不等同于原作 Deferred 字节编码消费端，也不代表酒店最终画面已经完全一致。

## 授权与交付状态

用户已批准公共渲染器 C++/Shader 实施、保留旧模式、专用头发不接收 Lumen 局部间接光；已有本对话编译授权。最后一次重启另获“保存当前酒店关卡并重启”授权。

| 请求结果 | 状态与证据 |
|---|---|
| Kajiya 专用环境 diffuse、双瓣 specular、AO、sun backlit 输入 | 已实现；两角色 GPU 输出和非均匀 SkyLight 测试通过 |
| 阻止同一 Kajiya 像素重复合成通用环境光 | 已实现前向内部排除及后续三个合成端的归属判断；mask GPU 绑定、取值和被接管像素保持不变已验证；完整选择性 Lumen 贡献对照仍有限制，见下文 |
| 保留旧模式、无动态天空回退、其他材质原路径 | 已实现；模式往返、无天空回退、两角色测试中非接管像素不变已验证 |
| 保存并重启、恢复预览 | 已保存；新进程恢复酒店 060–100、第 1122 帧、相机锁定；当前模式 1、Debug 0。恢复与截图检查时无脏包；最后只读检查时酒店关卡再次 dirty，保留内存状态，未再次保存 |
| Global Forward/MSAA 不添加不匹配 MRT | 已加编译和分配条件；完整构建及桌面 Deferred 重启回归通过；未切项目至 Forward/MSAA 做实际运行验证 |
| 与原作环境完全一致 | 部分满足：响应有源依据，输入生产仍为批准的 UE 适配；缺原作非零方向环境样本的完整重放以及 CPU 探针恢复 |

## 使用方式

控制台 `r.KajiyaHair.Environment 0` 为旧 UE 路径，`r.KajiyaHair.Environment 1` 为专用路径。源码默认仍为 0；本次已在当前编辑器开启 1，没有写项目持久配置。以后启动编辑器若要使用新路径，需要重新设置 1。

`r.KajiyaHair.Environment.Debug 0` 返回正常渲染。其余值只替换被接管 Kajiya 像素的输出；正常曝光、Bloom、后处理仍会处理调试颜色，因此视口白色区域不能当作原始整数 mask 读数。

| Debug 值 | 输出 |
|---|---|
| 1 | 被接管像素白色 |
| 2 / 3 / 4 | 世界 N / 发丝 T / 环境 D，按 `v*0.5+0.5` 显示 |
| 5 / 6 / 7 | 顶点插值 SH / AmbientBase / AmbientDirectional |
| 8 | 环境 diffuse，遵守 SkyLight GI 开关 |
| 9 / 10 | 主瓣 / 次瓣环境 specular，遵守 SkyLight reflection 开关 |
| 11 | RGB 分别为材质 AO / UE IndirectOcclusion / specOcc |
| 12 | 高光门控值 Gate |
| 13 | 送入 sun backlit 的环境基础亮度，GI 关闭时为 0；不是最终 backlit 结果 |
| 14 | 专用环境 diffuse + specular |

## 源材料与完整行为依据

已阅读的设计为 [Design_zh.md](G:/UnrealSample/Saved/Codex/Kajiya_AmbientDesign_20260925/Design_zh.md)，原完整生产者、消费者、量化和分支分析为 [Review_zh.md](G:/UnrealSample/Saved/Codex/Kajiya_AmbientAudit_20260925/Review_zh.md)。本记录不替代这份完整源分析。

源文件与最终引擎文件的 SHA-256 记录在本次证据目录 `Manifest.json`。核心源文件：

- `Saved/Codex/Elena_Hair_ShaderAudit_20260923/30952_Pixel.txt`：环境基础/方向拆分 `_347..366`，背面重分配 `_377..395`。
- `Saved/Codex/Elena_Hair_AppearanceFix_20260923/30952_Vertex.txt`：源顶点 SH 生产；UE SH 布局不与源布局混同。
- `Saved/Codex/Elena_Hair_ShaderAudit_20260923/35716_Pixel.txt`：连续 Forward；`_780..787` 的 specOcc、`_788..806` 的 diffuse、`_813..814` 的门控及随后 H、tint、Shift、双瓣计算。
- 同目录 `35423_Compute.txt`：Deferred hair 消费端；byte 常量和求值顺序不同，未冒称本版与其位级一致。
- RiderDuck 当前源捕获 `H:/RDCFiles/U4/5-frame18212.rdc`。本次检查候选 draw 的方向 RGB 未找到可用于证明非零双瓣响应的完整原作样本；`SourceProbeColors.json` 只记录检查值，不把所有候选 draw 假定成同一 hair 变体。

## 输入、内部参数与固定常量台账

源美术默认值和作者滑杆范围未由材料给出；本版未虚构这些信息。现有 KajiyaHairOutput 六输入、所有 MI 值及直接光函数保持原接口。本次不新增角色依赖、材质图参数、MPC、捕获常量硬编码或量化。

| 输入/常量 | 实际语义、空间、生命周期与映射 |
|---|---|
| `Environment` | 全局 RenderThreadSafe int；仅值 1 请求接管，其他值走旧模式；默认 0。需已有 OpaqueForward 支持及可用非静态 SkyLight |
| `Debug` | 全局 RenderThreadSafe int；默认 0，1–14 如上表；不改 MI，只在有效接管时替换输出 |
| `Enabled` / `Debug` uniform | 每次渲染设置，绑定当前 OpaqueForward pass；Enabled 还要求非 Global Forward、Sky 非静态、已有 ProcessedTexture 或开启实时捕获 |
| UE Sky SH、`SkyLightColor` | 复用当前视图 UE 已过滤/卷积 SH 与已解析线性颜色强度，避免再次乘 Intensity；每视图输入；实时捕获实际运行尚未验收 |
| 顶点 N、`KajiyaSkyIrradiance` | 世界顶点法线归一化后用完整 `GetSkySHDiffuse` 求值，乘 SkyLightColor，再随三角形插值；不以像素 N 替代 |
| 一阶亮度权重 `[0.30,0.59,0.11]` | 与现有 Kajiya 亮度函数一致。UE SH 第 0/1/2 组 xyz 各乘对应 SkyLightColor 分量和权重，相加得到一阶向量；这是批准的方向估计适配，不是 U4 CPU 算法 |
| 世界 D、ProbeColor | 一阶向量长度平方 >0 才归一化；颜色为 `max(E(D)-E(-D),0)`；零向量时 D=(0,0,1)、颜色 0，不保留历史方向；每像素共用一次结果 |
| 实例倍率、方向倍率、高光倍率 | UE 桥接中性约定均为 1；不是原作默认值 |
| 基础补光 | UE 桥接约定 0；没有把未知源补光当作已恢复 |
| 表面 N、发丝 T、底色 | 复用当前 Kajiya 数据：世界方向、线性底色；原材质采样、解码、扰动未在此重写 |
| AO | 现有材质 AO；用于基础/方向 diffuse、specOcc、双瓣可见性；不额外添加一次 AOMultiBounce 或最终通用 AO |
| 主/次 specular、roughness、Shift | 现有双瓣独立输入。Shift 已由材质解码；本函数先 Shift*N 再乘 2、加 T、归一化；粗糙度指数 `exp2(9-roughness*10)` |
| 主瓣 tint/强度常量 `.2,1,.8` | `(BaseColor-1)*.2+1`；先乘 PrimarySpecular，再乘 .8 |
| 次瓣 tint 常量 `.3,.6,1` | 权重 `Luminance(BaseColor)*.3+.6`；tint `(BaseColor-1)*weight+1`；再乘 SecondarySpecular |
| V、NoV、specOcc | UE 世界相机向量；保留 `log2(AO+dot(N,V))`、乘主粗糙度、exp2、先加 -1 再加 AO 并 saturate；不添加 NoV clamp/epsilon。源 log/归一化退化边界不承诺全域有限 |
| DirectionVisibility / Gate | UE 当前 IndirectOcclusion 为遮挡适配 G，Gate=G*specOcc。它不是源 Forward 本来具有的 G 条件；无源 SSR alpha、本地 bounce 输入，本版不生产它们 |
| `.001` / 非零颜色条件 | 保留 `Gate>.001` 阈值；另有 UE 适配有效性条件 `any(Directional>0)`，零颜色没有有意义的方向，不求可能退化的 H；不能把此额外条件描述成原作原样 |
| H、瓣方向衰减 | H=(D+V)*rsqrt(dot(D+V,D+V))；每瓣 `sin²=saturate(1-dot(Tshift,H)^2)`；衰减 min(max(dot+1,0),1)，乘 exp2(exponent*log2(sin²))；无新 epsilon |
| `.75,.25,.5` | 双瓣角度 `saturate(NoD*.75+.25)`，乘 AO，再乘 .5，再乘 Gate；生产 specular 保留两瓣先相加的顺序 |
| FrontFace | 使用本次 raster 的正反面；背面保留先减方向投影、再加回、方向归零的操作，不做代数消去 |
| SkyLight GI / reflection 开关 | GI 管环境 diffuse 和送入 sun backlit 的基础值；reflection 管环境双瓣；已做分别关闭验证 |
| PreExposure | 所有新增照明在现有 BasePass 预曝光之前合成，沿原路径仅乘一次；调试输出同样如此 |
| Mask 0 / 1 | PF_R8_UINT 每帧 RDG 资源；0 仍由通用 UE 合成，1 已由 Kajiya 合成；与同一次颜色写入共用深度、裁剪、dither 和覆盖 |

## 实现对应与内置复用

共修改 14 个现有引擎文件。相对于本轮开始备份的完整 diff 为证据目录 `Implementation.diff`，避免把分支原有修改算成本轮成果。

- `KajiyaHair.ush`：新增环境结果结构与连续环境计算，保留旧直接光函数。基础/方向拆分只生产一次；同一 AmbientBase 供 diffuse、调试、sun backlit 使用。H、tint、可见性共享；两瓣不同 roughness/Shift 分开计算。
- `KajiyaHairForwardLighting.ush`：仅更新环境输入说明；sun backlit 已有消费者复用新 Base 输入。
- `BasePassCommon.ush`、`BasePassVertexShader.usf`：新增受条件限制的顶点 SH 插值，复用完整 UE `GetSkySHDiffuse`，不自写 SH 解码或重做 Cubemap 过滤。
- `BasePassPixelShader.usf`：UE 主方向估计、环境响应接线、跳过被接管像素的通用前向 diffuse/IBL、调试输出、归属写入。
- `PixelShaderOutputCommon.ush`：匹配整数 MRT1 输出。仅 OpaqueForward 且非 Global Forward 时使用。
- `OpaqueForwardPassRendering.cpp`：两个控制台变量、pass uniform、每帧 mask 分配/清零及附件绑定。为运行时切换匹配缓存 PSO，在支持路径的旧/新模式都绑定同一个格式，旧模式写 0。
- `OpaqueForwardPassShaderTypes.h`、`BasePassRendering.cpp`：编译输出格式与 PSO 预缓存同步；Global Forward/MSAA 不新增此附件。
- `Internal/SceneTextures.h`：`TOptional<std::reference_wrapper<FRDGTexture>>` 保存本帧 RDG 借用引用，RDG 持有生命周期，没有增加自定义 raw pointer 字段。
- `DiffuseIndirectComposite.usf`、`ReflectionEnvironmentPixelShader.usf`、`AmbientCubemapComposite.usf` 与 `IndirectLightRendering.cpp`：绑定并消费归属。加法输出为 0，乘法输出为 1；非 dual-source 合成回写输入 SceneColor 保留 RGBA；分离 SSS 维持零加法。没有资源时使用现有 UE ZeroUIntDummy，并以 Enabled 限定读取。

没有用 UE `GetSpecularOcclusion` 替代源 specOcc：顺序与输入 clamp 不等同。也没有用普通各向同性 IBL 或已有直接光函数代替环境双瓣；只有缺失的环境响应另写，已能满足适配的 Sky SH、天空资源、渲染图、曝光和后续处理继续复用。

## 验证证据与解释

全部证据根目录：`G:/UnrealSample/Saved/Codex/Kajiya_Environment_Implementation_20260925`。

1. **编译与加载**：初轮完整构建 `Build3.log` 成功；最后 Forward 排除修改 `FinalCompileOnly.log` 成功，退出后 `FinalBuild.log` 链接成功。新编辑器 `FinalEditor.log` 已实际绘制三种模式，未出现新 Shader 编译错误或渲染致命错误。
2. **Nathan 线性采样**：`CaptureModes.json` / `CaptureAnalysis.json`，第 1157 帧，同一回调内 960×540 RGBA16F 捕获。接管 9193 像素；所有输出有限；非接管像素最大差 0；旧模式切回最大差 0。均匀酒店 Sky 下，接管区域平均 RGB 从 [0.0172871,0.0104420,0.00451049] 变为 [0.0121498,0.00734357,0.00350618]。这只说明这组受控数据，不承诺所有后处理和屏幕追踪下其他像素永远不变。
3. **当前均匀天空**：SH/Base 约 [0.0473328,0.0473328,0.0583496]，方向 RGB 和两瓣环境 specular 为 0。这是当前输入及估计器的结果，不通过额外 gain 伪造方向高光。
4. **非均匀 Cubemap**：`SkyTests/Analysis.json`。临时使用已有 SpecCube_02 作为适配器测试输入，测试后还原原 Sky。两瓣均有非零输出；Sky 强度翻倍时环境项翻倍，最大偏差 5.96e-8（半浮点量化量级）；旋转 90 度改变 D 和瓣响应；GI 关只移除对应 diffuse/sun 输入；reflection 关只移除双瓣。它不是原游戏同帧非零方向输入重放。
5. **Lumen**：同组 capture 请求 Lumen 后专用头发输出不变。另用真实视口 GPU dump 确认执行 `DiffuseIndirectComposite(DiffuseIndirect=ScreenProbeGather)`，输入 Lumen 缓冲非零，mask 为 R8_UINT，含 15948 接管像素，接管 RGBA 前后差 0。但 dump 中非接管区域前后也全部相同，故该 dump 单独不能证明“其他像素仍获得非零 Lumen 增量”；未把原因猜作已知。对应 `GPUCompositeProof.json`、`gpu_lumen`。选择性隔离的完整 GPU 对照仍标部分验证。
6. **Elena 与无天空**：`ElenaTests/Analysis.json`，10778 接管像素，有限输出，非接管最大差 0；关闭原 Sky 后新/旧模式最大差 0，证明回退。未动角色材质资产。
7. **重启后实际视口**：`FinalSmoke/Legacy.png`、`Ownership.png`、`Dedicated.png` 已查看。第 1122 帧正常切换；接管显示在头发、眉毛、鬓角；正常模式头发不再叠旧环境结果。视口截图有后处理、时间噪声和编辑器图标，仅作加载/外观检查，不当作严格线性误差测试。
8. **状态恢复**：`AuthorizedSave.json`、`RestoredFinal.json`、`FinalSmoke/Result.json`：第 1122 帧，相机锁定，mode=1/debug=0，在这些检查时 dirty map/content 均空。最后只读 `LiveVerification.json` 显示酒店关卡后来再次被标记 dirty，content 仍空；原因未确认，保留当前内存状态，未再次保存或清除 dirty 标记。临时测试 Sky/后处理/SceneCapture Actor 已销毁、原 Sky 恢复。

## 已知限制与退出事件

- 原作 CPU 探针选择/插值、逐实例环境、原 ambientShadow.G 与 SSR alpha、源 bounce、Deferred 字节编码响应没有完整移植。此版只能称批准的 SkyLight 适配，不能称原引擎环境完全一致。
- 一阶估计对相反方向等强亮区可能得到零方向，即使 Cubemap 仍有高频结构。没有偷偷增加阈值、方向补偿或上帧缓存。
- 原作非零方向样本的逐项响应重放、AO/门控全边界、独立非 Kajiya Forward 测试材质、实际 Global Forward/MSAA、实时 Sky 捕获、多视图/动态分辨率、性能均未全面 GPU 验收。Substrate、移动端和全局 Forward 不是本版新增支持范围。
- 本版接管可见头发的环境合成；没有让 Lumen Surface Cache、其他物体中的头发反射或所有二次射线改用原作积分。
- 关闭旧编辑器时，在关卡保存之后发生访问异常：`UBrowseToAssetOverrideSubsystem::GetBrowseToAssetOverride -> DetailsView -> Sequencer release -> Slate shutdown`，见 `Editor.log` 2535 起。保存结果成功且无脏包，新进程成功打开。异常根因未定位，不能断言与本轮完全无关，也没有扩大范围修改编辑器生命周期代码。

## 独立复核

实施后另行重新读取请求、设计、14 文件完整 diff，并检查控制流、输入空间、复用、资源生命周期、mask 所有消费者、旧模式、Global Forward 条件与预缓存格式。逐行均对应环境响应、归属、调试或兼容要求；未改既有直接光、角色输入、灯光曲线或后处理算法。

`DiffReview.json` 未发现新增 assert/check/ensure 或隐式 lambda 捕获；新增 C++ 借用使用引用/optional reference wrapper，raw API 结果只留在 UE API 边界。指定文件 `git diff --check` 通过。所有验收结论以上表和实际证据为准，源画面完全对齐仍为部分完成。

备份：证据目录 `Before/`、`Baseline.json`、`Before.diff`，以及获批保存前的 `HotelBeforeAuthorizedSave.umap`。恢复资产须通过关闭加载/UE 资产流程处理，不应在编辑器加载期间直接覆盖。
