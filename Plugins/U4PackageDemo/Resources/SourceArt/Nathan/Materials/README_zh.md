# Nathan 材质：当前接口与修复记录

2026-09-25。此目录记录按 `ue-asset-reconstruction` 规范修复后实际保存的资产接口。材质图、参数和实例迁移已实施；头发严格 Pass 还原仍未完成，不能把本目录理解为全角色严格还原验收通过。

当前酒店截帧修正和待完成工作见 [酒店材质对齐报告](Evidence/HotelAlignment/Report_zh.md)。[此前接口整理报告](Evidence/RepairReport_zh.md) 保留为历史证据；涉及眉毛父材质、颜色域及皮肤第二瓣的旧结论以酒店报告为准。

## 使用

- 在 `/U4PackageDemo/Demo/Character/Nathan/Material` 调整对应 `MI_Nathan_*`。不需要查原反汇编编号才能调整材质。
- 参数按贴图、基础色、法线、镜面、覆盖、遮蔽、分层和 UE 适配分组；Tooltip 说明用途、影响、输入、范围／默认和启用条件。
- 功能开关只用于确认过的输入算法。当前 34 个源实例组合已经检查；它们不构成任意组合都可用的万能材质接口。
- 普通颜色、粗糙度、Shift 等调整使用实例值。`CoverageNormalization` 是源覆盖除数，不等于 UE 的硬裁剪阈值。
- 三个头发家族分别对应普通映射、GBA 颜色／R AO 打包、泥／雪分层输入，继续共用公共 Kajiya。它们不是三个角色专用 Shading Model。
- `MaterialInterface.json` 是此次保存后的参数／有效实例值／41 个槽位／纹理／Profile 快照。未来编辑应以当前资产为准，并更新快照；勿直接重跑历史自动构建脚本。

## 已实施

- 六张伤口／破损遮罩恢复源 sRGB 视图语义，36 个采样同步为 Color；纹理导入映射同步。
- 五张头发颜色数据改为线性采样，颜色变换恢复到源计算指定的位置；保留 saturate → log2 → ×2.2 → exp2 → saturate 的顺序。它不是对所有 UE 颜色输入再次统一校正。
- 同源别名和分量颜色合并；混用的天气／伤口／汗膜输入拆成有实际语义的参数。静态开关按实际输入功能整理。
- 已核实适用的 Lerp 与正 Z 法线重建使用 UE 节点；不以 SafeNormalize、FlattenNormal 或 DitherTemporalAA 替换未经证明等价的源算法。
- 公共 UV、采样和子计算保留单一生产者，通过 Named Reroute 分发。15 个表面材质图有中文编号分区和最终输出区。
- 保存后的清单：15 个表面母材质、34 个实例、41 个槽位；553 项母材质内唯一参数。本轮五图没有失效命名路由、重复生产者或缺失五项说明的参数。
- 酒店截帧修正新增眉毛 Kajiya 输入家族，修正三个皮肤 Profile 的干燥第二瓣粗糙度、四个材质家族的颜色解码阶段及 17 张纹理导入设置。酒店 Sequence 接入 Nathan 的 26 个剧情材质状态通道，并验证越界复位。

## 家族与来源

| 母材质（M_Nathan_） | 实例（MI_Nathan_）与源 EID | 当前参数数 |
|---|---|---:|
| Skin | Face 29596、Chest 29625、Arms 29502 | 122 |
| Cloth | Shirt 29483、Pants 29615、Boots 29586 | 134 |
| SurfaceLayered | Watch 29435、Belt 29454、Holster 29464、Pouch 29539 | 90 |
| SurfaceAnatomy | Teeth 29529、Tongue 29634、Caruncle 29492 | 14 |
| FineHair | Stubble 32306、Eyelashes 32335、ArmHair 32392 | 23 |
| HairKajiyaFine | Eyebrows 32344（酒店当前 EID 51910） | 16 |
| HairKajiyaMapped | HairAlphaThinDithered 29644、HairThin 29664、HairThickCutoff 29674、HairThinNoShadow 29684、HairThick 29704、HairAlphaThin 32354 | 27 |
| HairKajiyaPacked | HairThinAlphaDithered 29694、HairThinAlpha 32383 | 14 |
| HairKajiyaLayered | HairThinMudDithered 29654、HairThinMud 32326、HairThinMudSnow 32364 | 50 |
| Eye | Eye 29425 | 15 |
| HolsterBorder | HolsterBorder 29473 | 8 |
| HolsterMetal | HolsterMetal 29444 | 9 |
| Ring | Ring 29549 | 16 |
| Tears | Tears 32316 | 10 |
| WatchLens | WatchLens 32401 | 5 |

## 验证与限制

- 改名／分支调整代入各实际 MI 的前后表达式比较通过；图布局展开命名路由后输出签名一致。此证明覆盖已记录的实例组合，不是全 GPU 浮点逐位证明。
- 独立展开全部路由再检查，14 图未发现输入、属性和递归上游完全相同的重复生产节点。图级复用不等于已测得 GPU 加速。
- 此前接口整理轮的 14 M／34 MI 均取得 Shader 统计；该轮修改 59 个资产。酒店轮另更新 27 个 uasset（含 Sequence，新增一个眉毛母材质），三个 Profile 仅修改 Roughness1。两轮统计不能混为一次改动。
- 实际查看了 HolsterMetal 图的输出／公共输入区域以及 HairThick 实例分组／高光参数。未完成所有大图逐区的视觉可读性验收。球形预览不构成角色头发画面验收。
- 四个源连续透明头发主绘制仍由现有 Masked 路径承载。透明深度、颜色／阴影／速度各自的覆盖条件和随机相位尚未恢复；具体 Renderer 扩展方案见 Evidence/HairPassImplementationProposal_zh.md，待本轮新形式及编译授权。
- 源各级 mip 与 UE GPU 重压缩、sampler 过滤、全部部件 TBN，以及同机位／同曝光画面对照尚未完成。
- Face／Arms／Shirt 已由现有 ABP 动态生成法线并绑定到 MID；默认捕获纹理用于回退，本轮验证五个槽位持续绑定 RT。部分顶点输入及湿润／黏液等动态生产者仍有适配边界，不能由 RT 绑定正常推断全部原算法恢复。
- 酒店轮保存既有酒店 Sequence 的 Nathan 材质轨道；保留其原有相机、灯光和动画轨道。未改公共引擎 Shader。用户原先的编辑器帧和相机锁定状态在诊断后恢复。

## 证据

- `Graphs/`：当前完整连接、精确浮点属性、路由声明／引用和输出；节点 ID 使用对象路径。
- `Evidence/ShaderBindings.json`：34／34 完整 DXBC 与原 PAK 精确匹配、841 项反射组件映射。
- `Evidence/CurrentParameterSourceMap.json`：迁移前全部参数到源操作和作者符号的台账，保留当时语义状态；它不是当前接口。
- `Evidence/ReadableParameterMigration.json` 与特殊迁移 `.py.txt`／读回记录：旧名到当前语义接口的转换证据，仅供追溯，不能直接运行重放。当前说明以 MaterialInterface 为准。
- `Evidence/FinalIndependentReview.json`：全部资产变更、引用／Profile／槽位、接口、路由、共享生产者和脚本语法检查。
- 原始完整 Shader、CB、管线快照和前后图证据在 `G:/UnrealSample/Saved/Codex/Nathan_SkillReview_20260925`、`G:/UnrealSample/Saved/Codex/Nathan_SkillFix_20260925`。
