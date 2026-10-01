> 2026-09-25 材质状态更新：当前材质已赋予模型，目录内已有 14 M、34 MI、226 纹理、4 Profile、1 SK、1 Skeleton，共 280 个资产。最新参数接口、修复和未完成事项见 [Materials/README_zh.md](Materials/README_zh.md)。下文保留的是初始模型／纹理导入记录；其中 219 张纹理、尚未赋材质等陈述只适用于初始导入时点。

# Nathan Drake：hero-island UE 导入记录

本次范围：按 Elena 的目录组织方式，将已转换的 Nathan 岛屿探险装模型和原游戏贴图导入 UE 并保存。

## UE 资产

- 主目录：`/U4PackageDemo/Demo/Character/Nathan`
- 模型：`/U4PackageDemo/Demo/Character/Nathan/Mesh/SK_Nathan_Unified`
- 骨架：`/U4PackageDemo/Demo/Character/Nathan/Mesh/SK_Nathan_Unified_Skeleton`
- 贴图：`/U4PackageDemo/Demo/Character/Nathan/Textures`，219 个 Texture2D。
- 共 221 个角色独立资产，均保存在 Nathan 目录；当前路径以 `TextureManifest.json` 为准。

贴图子目录：`Beard`、`Belt`、`Body`、`Brow`、`Caruncle`、`Eye`、`EyeLash`、`Face`、`Hair`、`Holster`、`Pants`、`Pouch`、`Ring`、`Shared`、`Shirt`、`Shoes`、`Tear`、`Teeth`、`Tongue`、`Watch`、`Wrinkles/Body`、`Wrinkles/Face`。

## 模型及骨架

导入源：`Mesh/NathanDrake_Island_Unified_SkeletalMesh.fbx`，与前轮最终 FBX 字节一致，SHA256 `d8d239c58e765c04aa2c9ff488d5938b3458da98fe29d9d4b029fcc31151b012`。

沿用 Elena 的导入法线、MikkTSpace 重建切线、单位及坐标设置；使用独立 Nathan Skeleton。临时关闭 Interchange FBX、通过 FbxFactory 导入，结束后将会话开关恢复为原值 1。

- 702 根原始骨骼全部保留，无缺失、额外或错误父关系。父关系核查以先前已交付 FBX 的单根适配为目标；源 PAK 的 16 个额外根组在 FBX 阶段已挂接到原 root，保持世界绑定变换。
- 41 个 LOD0 部件、91,088 个三角形；UE 导出回读为 73,248 个拆分顶点。顶点数量变化没有改变三角形及其绕序。
- 41 个部件材质槽对应 34 个不同原始材质，另有 46 个 UE 渲染 section。保留部件槽位，显示名改为原材质名；重复材质加部件后缀，ImportedMaterialSlotName 保留 UE 导入名以供重导入。
- 原始槽位、参数和贴图哈希记录在 `MaterialSlotMapping.json`；各贴图的 UE 路径、原路径、哈希与使用者记录在 `TextureManifest.json`。

UE 回读与原 PAK 解码值比较：顶点最大分量差 `0.000031472 cm`，已有 UV 通道最大差 0，法线最大分量差 `1.78814e-7`，归一化蒙皮权重最大差 `3.77297e-5`；Mesh/Skeleton 的参考骨骼位置最大差 `0.000006847 cm`。这些数据来自编辑器参考骨架及 FBX 回读，不等于对 GPU 运行时缓冲所有量化阶段的逐字节检查。

## 贴图转换与 UE 设置

从实际使用的 `hero-island`、`hero-island-head`、`hero-island-shirt`、`hero-island-holster` PAK 收集引用与配套褶皱资源，以实际纹理哈希在原游戏 `texturedict2` 中获取高分辨率数据。219 张均命中，未使用缺失替代图片补齐。原包 `/missingtexture/` 记录属于占位输入，列在清单的 `excluded` 中；它们没有被伪造为已还原的动态纹理。

读取纹理数据时按每级 512 字节对齐、每块行 256 字节对齐剥离填充，所有被剥离字节均为 0。共 2,179 个原 mip；可与角色 PAK 常驻贴图对照的 1,875 个 mip，其压缩块与纹理字典逐字节一致。

每一级 mip 解码为 BGRA8 并垂直翻转，沿用 Elena 的 UV/贴图配套方向；不重建 mip、不重新归一化打包法线、不翻转绿色通道。源 sRGB 格式在离线解码时按同位布局 UNORM 解码以保留存储值，sRGB 采样由 UE 纹理设置承担。独立解码器对所有 mip 的有效源通道核查，最大差 1/255。

UE 设置：`TC_BC7`、`LeaveExistingMips`、`Character` 纹理组，保留 Alpha。原本标记为 sRGB 的纹理，以及明确作为 BaseColor、BloodColor、MudColor、SandColor 或眼睛/泪线环境颜色输入的纹理启用 sRGB，共 34 张；其他 185 张保留线性采样。包含 AO/法线/遮罩打包数据的图没有套用会改写通道的 Normalmap 压缩。具体原 Shader 的解码与 UE 材质外观适配仍属于后续材质工作。

219 张 UE 纹理全部再次导出 DDS：尺寸、mip 数量、每一级源像素与对应导入 DDS 完全一致。BC7 运行时压缩会由 UE 重新编码，此处不声称 GPU 压缩块与原游戏压缩块完全一致。

## 原数据特例和交付边界

- 原数据的 6 根 `bone0063_neck_pose_jnt` 至 `bone0068_neck_pose_jnt5` 缩放约为 `1e-12`，UE 导入时输出零缩放/NIL matrix 提示。它们均无蒙皮权重、无子骨骼，已按原数据保留，没有擅自删除或改为单位缩放。当前参考骨架及可见网格数值检查通过；以后使用这些辅助节点做动态驱动时需单独处理其语义。
- 原始手表镜片 `Drake_12` 没有 UV，UE 为统一网格补了默认 UV。核查的“UV 差异为 0”指原数据实际存在的 UV 通道。
- FBX 无 smoothing group 信息，UE 给出提示。本次使用导入法线，回读法线核查通过。
- 此次导入模型和贴图，材质槽尚未赋予 Nathan 的着色材质，当前显示默认材质。没有新建 Material/MI、动画、PhysicsAsset、Control Rig 或褶皱驱动；褶皱贴图已归档不代表运行时生成系统已接通。
- 原游戏 PAK、Elena 资产和引擎源码没有在本轮被写入。没有执行项目/蓝图/Shader 编译命令；导入时正常生成了 SkeletalMesh 渲染数据与纹理压缩数据。
- 本轮结束时没有未保存内容资产。用户原有 `/Temp/Untitled_1` 关卡仍保持未保存状态，没有替用户保存关卡。

## 单独复查及请求验收

实现后另行复读本轮全部 Python 脚本、资产设置、映射清单及用户请求，检查路径作用范围、回调注销、异常记录、导入选项、数据方向、源通道及保存结果。复查补充了三角形绕序检查，并再次验证通过；全部脚本通过 AST 语法解析，未运行构建命令。

| 用户请求/检查项 | 状态 |
|---|---|
| 沿用 Elena 的模型、贴图目录安排 | 已满足：独立 Nathan/Mesh 和 Textures/部位 |
| 从原数据提取 hero-island 相关贴图 | 已满足：219 张，原 mip 保留，完整来源映射 |
| 模型、骨架、贴图导入 UE 并保存 | 已满足：221 个 uasset，保存成功 |
| 骨骼、材质槽、UV、纹理设置复查 | 已满足本文所列回读检查；原数据特例已记录 |
| 原游戏着色外观与动画表现 | 本轮未实施、未验证；当前请求为资产导入 |

完整证据目录：`G:/UnrealSample/Saved/Codex/Nathan_UE_Import_20260924`。主要文件为 `MeshNumericReview.json`、`SourcePreparation.json`、`SourceTextureVerification.json`、`TextureReadbackComparison.json`、`TextureUEVerification.json`、`FinalUEState.json`、`ImportLogReview.json`。
