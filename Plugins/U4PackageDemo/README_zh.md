# U4PackageDemo

本插件集中保存 Character 渲染资产、Elena 和 Nathan 角色、酒店场景，以及 UnrealSample 客户端和编辑器模块。

## 入口和目录

| 内容 | 位置 |
|---|---|
| UE 资产根目录 | `/U4PackageDemo/Demo/Character` |
| 酒店场景 | `/U4PackageDemo/Demo/Character/Elena/Cinematics/L_Elena_Hotel_Preview` |
| 独立面部序列 | `/U4PackageDemo/CR_Elena_Face_Take1` |
| 客户端模块 | `Source/UnrealSample` |
| 编辑器模块 | `Source/UnrealSampleEditor` |
| 原始资料和材质记录 | `Resources/SourceArt` |
| Rig 接入脚本 | `Scripts/ElenaRuntimeRig` |
| 迁移复查结果 | `Resources/Migration/ReferenceAudit_20260926.json` |
| 原始重导入文件缺失清单 | `Resources/Migration/MissingImportSources_20260926.json` |

在内容浏览器中启用“显示插件内容”后，可从 `Plugins / U4PackageDemo` 进入。当前本机虚拟目录为 `/All/Plugins/U4PackageDemo/Demo/Character`。

## 2026-09-26 迁移与引用修复

从迁移前备份恢复完整资产后，通过 UE 的整批包迁移建立统一的包名映射，再由 UE 加载和保存。项目内旧的 Character 内容目录及根目录面部序列已移除；备份保留在项目 `Saved/Codex/U4PackageDemo_MigrationBackup_20260926`。

- 插件中共有 1,258 个资产包，包含 718 个动画序列、12 个 SkeletalMesh、12 个 Skeleton、54 个材质、55 个材质实例、2 个 Level Sequence 和酒店关卡。
- 恢复了前次迁移丢失的动画骨架、材质、纹理、动画蓝图和 Rig 数据引用；17 个酒店道具动画的网格引用已迁移。
- `CR_Elena_Face_Take1` 按用户选择保留为独立序列。清除了失效的临时世界演员绑定，保留角色绑定槽、三条轨道及全部关键帧。
- 清除了 `M_Caruncle` 指向已不存在 StaticMesh 的编辑器预览网格引用。
- 当前角色清单、纹理源文件清单、脚本路径和本机编辑器资源路径已改为插件路径。
- 共享默认法线在插件中保留独立副本；原工程中的共享默认法线仍供其他内容使用。
- 两个 C++ 模块及随附数据的 18 个文件与迁移前备份逐字节一致。模块名保持 `UnrealSample` 和 `UnrealSampleEditor`，资产中的 `/Script/` 类型名不变。项目 Target 文件继续作为工程构建入口。
- 插件明确声明 `EnhancedInput`、`ACLPlugin`、`AnimationData` 和 `ControlRig` 依赖。

## 独立复查结果

移除旧内容目录后，以最终 `.uproject` 和 `.uplugin` 配置启动现有 UE 可执行文件，执行无渲染的加载及引用检查：

| 检查项 | 结果 |
|---|---|
| 逐包实际加载 | 1,258 / 1,258 成功 |
| 缺失的资产包及硬、软依赖 | 0 |
| 插件指向旧 `/Game` 内容的依赖 | 0 |
| 无 Skeleton 的动画序列 | 0 |
| 插件中的重定向器 | 0 |
| 旧 Character 目录残留资产 | 0 |
| C++ 模块实际加载位置 | 均为本插件 `Binaries/Win64` |
| 酒店关卡对象数据 | 统一包路径和四个自动生成的 RenderTarget 对象名称后，完整文本差异为 0 |
| 面部序列数据 | 仅移除失效演员绑定并更新序列签名；其余完整对象文本一致 |

关卡原资产注册表中有 18 条已失效的旧 Bloom 材质记录。这些文件在迁移前已不存在，加载后的原关卡对象也未引用它们；UE 重存后清除了这些陈旧记录，关卡对象数据保持一致。

检查完成时 UE 报告 0 个错误。日志仍有原模型颈部辅助骨骼的零缩放 / NIL matrix 警告，这些源数据在迁移前已存在并按原样保留。本轮未执行编译命令，未进行 PIE、打包或 GPU 画面对比；实际加载使用本机已有模块二进制。

## 重导入来源记录

469 条因目录变化而失效的重导入文件路径已通过 UE 的 `AssetImportData` 接口修正，并核对实际解析文件路径。

该接口同时刷新文件时间戳和 MD5。其中 91 个现有源文件在本轮修改路径前就已经与资产记录的原导入 MD5 不同；本轮只更新导入元数据，没有重导入动画、网格或纹理。修改前的完整记录保存在 `ImportMetadataBeforePathUpdate_20260926.json`，这些既有差异另见 `ExistingSourceHashDifferences_20260926.json`。

最终还保留 44 条迁移前即缺失的外部原始导入文件记录：角色资产 43 条、共享默认法线副本 1 条。已按原文件名及 MD5 检索已知源目录，未找到相同原件。缺失清单保存原路径、时间戳和 MD5，未用其他版本或 UE 导出文件替代原件。它们不属于运行时资产依赖；对应 UE 资产均已迁入插件并成功加载。恢复这些原始文件前，相关资产无法直接从其历史来源重导入。

`Resources/SourceArt` 和 Rig 脚本的说明中保留了各历史实现阶段的记录；当前迁移入口及复查状态以本文件和迁移审计文件为准。
