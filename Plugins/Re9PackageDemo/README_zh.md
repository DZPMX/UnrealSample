# Re9PackageDemo

本插件集中保存工程内的 Grace 角色资产、预览关卡、动画播放与皱纹采样代码，以及编辑器导入模块。

## 入口与目录

| 内容 | 位置 |
|---|---|
| UE 资产根目录 | `/Re9PackageDemo/Character/Grace` |
| 预览关卡 | `/Re9PackageDemo/Character/Grace/Preview/L_Grace_Default` |
| 角色蓝图 | `/Re9PackageDemo/Character/Grace/Control/BP_GraceCharacter` |
| 合并角色网格 | `/Re9PackageDemo/Character/Grace/Mesh/SK_Grace_Default` |
| 客户端动画模块 | `Source/GraceAnimationRuntime` |
| 编辑器导入模块 | `Source/GraceAnimationEditor` |
| 专用纹理组配置 | `Config/DeviceProfiles.ini` |
| 迁移验证记录 | `Resources/Migration/ReferenceAudit_20261001.json` |

在内容浏览器中启用“显示插件内容”，从 `Plugins / Re9PackageDemo` 进入。插件包含 533 个资产包，包括 133 个动画序列、3 个动画蓝图、3 个普通蓝图、3 个骨骼网格、10 个骨架、178 个材质函数、18 个材质、39 个材质实例、118 个纹理及纹理数组，以及 Groom、输入、Niagara 和预览关卡等资产。

插件显式依赖 `EnhancedInput`、`ACLPlugin`、`AnimationData`、`ControlRig`、`HairStrands`、`AlembicHairImporter` 和 `Niagara`。Grace 的资产依赖不指向其他角色插件或项目 `/Game` 内容。

## 2026-10-01 迁移范围

- 通过 UE 原生整批包迁移，将 `/Game/Character/Grace/...` 映射到 `/Re9PackageDemo/Character/Grace/...`，同步硬引用、软引用、说明文字和贴图来源元数据。
- 原 `Plugins/GraceAnimation` 的两个模块迁入本插件。模块名和 `/Script/GraceAnimationRuntime`、`/Script/GraceAnimationEditor` 类型路径保持不变；10 个源文件与迁移前备份逐字节一致，先前的皱纹数组初始化及导入 JSON 错误处理修复保留。
- 原工程 `DefaultDeviceProfiles.ini` 的 Grace 专用设置整体迁入插件的 `Config/DeviceProfiles.ini`，沿用 UE 插件配置层格式。实际加载的 Windows、WindowsEditor 配置均为 `TEXTUREGROUP_Project48, MaxAniso=16`。
- 根据本次明确选择，只迁移工程内内容。`H:\Grace_RE9` 等工程外的 FBX、纹理、解包资料及历史脚本保持原位置；249 个解析后的导入源文件路径与迁移前一致。
- 原资产目录已移出活动 Content 目录。原件、备份、迁移脚本、完整对象导出及日志保留在工程 `Saved/Codex/Re9PackageDemo_Migration_20261001`。

## 独立复核

复核在旧资产目录移出后，以现有 UE 可执行文件重新启动进程进行。使用 `-NullRHI -NoShaderCompile`；本次已获准资产加载、保存及验证需要的蓝图等资产编译，没有执行 C++ 构建或 Shader 编译。

逐包加载、硬软引用、重定向器、导入路径、模块加载位置和对象文本比较的最终统计见随附 JSON。对象比较覆盖完整导出，而非只抽查资产名或材质参数。

- 六个蓝图的差异为 GUID 重编号：逐一验证一一映射及全部重复引用，其他完整对象文本在替换包路径后一致。
- 一处 Named Reroute 的未固定默认节点颜色在不同进程中发生变化，已按迁移前的完整对象导出固定为原值；材质说明文字中的 Profile 路径同步更新，UE 随之更新的材质缓存 GUID 单独核对。
- 预览关卡经 UE 迁移保存后，补入现有角色蓝图的手电筒组件和动态材质实例。对原关卡和新关卡执行相同的角色蓝图编译及实例重建后，两份完整关卡对象文本在替换包路径后完全一致。原关卡的直接 Lighter 网格依赖由角色蓝图依赖覆盖。
- 六个手电筒纹理原有的空文件名导入记录被 UE 原生保存清理，未移除有效导入源路径。
- `SK_Grace_Default` 的 `RE9MergeSources` 字符串原样保留；其中七个旧部件标识在迁移前已无对应资产，也不在硬软依赖中。未将这些字符串改写成并不存在的新资产地址，原值在审计记录中可查。

## 首次迁移验证的限制

迁移前后加载原角色蓝图均会触发引擎已有的 `SimpleConstructionScript.cpp:474` handled ensure 及组件重挂接警告，导致命令行进程返回 1；该返回码不作为全部检查通过的证据，资产级结果与日志分别保存。

首次迁移检查时，工程已启用的 `LocalWind` 缺少可加载模块，检查进程使用 `-DisablePlugins=LocalWind` 启动；工程的 LocalWind 设置保留。当时没有验证所有工程插件同时启用时的编辑器启动。后续编译验证结果见下节。

首次迁移验证覆盖资产迁移、UE 读回、蓝图和关卡对象一致性，使用原有模块二进制。当时未执行 C++ 构建、PIE 操作、最终渲染画面对照、材质编辑器视觉验收、GPU 性能测量或打包验证。

## 2026-10-01 编译与测试

收到明确编译指令后，已完成 `UnrealSampleEditor Win64 Development` 构建，最终 UBT 退出码为 0。Re9PackageDemo 的两个模块均从当前源码重新编译、链接，并在新 UE 进程中从插件目录成功加载。

完整目标首次构建发现引擎 `LocalWind` 插件两处编译错误，已做最小修正：`GetPlayerController` 传入子系统的当前 World，视图扩展补充 `SceneView.h`。重试成功，后续测试保留工程启用的全部插件，未禁用 LocalWind。修正位于引擎仓库，不在本插件源码内。

| 检查 | 结果 |
|---|---|
| Grace 资产加载 | 533 / 533 |
| 缺失依赖、旧 Grace 包引用、无骨架动画 | 均为 0 |
| 蓝图及动画蓝图编译 | 6 / 6 执行完成，生成类存在，无蓝图编译错误；仍有下述组件层级警告 |
| 预览关卡 | 加载成功，角色及头发、打火机、手电筒组件存在 |
| 缺失文件／损坏 JSON 导入 | 2 / 2 通过；完整动画对象导出与元数据均未改变 |
| 资产与 Re9 源码写入检查 | 533 个资产及 10 个源文件哈希均未变化 |

资产检查仍记录迁移前已有的 `SimpleConstructionScript.cpp:474` handled ensure 和组件重挂接警告，进程退出码为 1；不能称为零错误的测试运行。导入异常测试的退出码 1 来自两个预期的错误日志，两项资产状态比较均通过。测试脚本未保存资产。

测试使用 `-NullRHI -NoShaderCompile`。本轮没有验证 Shader 编译、渲染画面、PIE 交互、强制参考姿势首帧回归、风场 GPU 行为、性能或打包。构建时另有 VibeUE 的 `StructUtils`、`GameplayTagsEditor` 依赖声明警告。

结构化结果见 `Resources/Migration/BuildTest_20261001.json`，原始构建与测试日志位于工程 `Saved/Codex/Re9PackageDemo_BuildTest_20261001`。
