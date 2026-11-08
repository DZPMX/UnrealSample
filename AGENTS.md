# Project Engineering Rules

## Maintainable Implementation of Reconstructed Algorithms

Unpacked code, decompiled pseudocode, disassembly, and extracted shader
instructions must be used to understand and verify behavior. Do not copy them
directly or mechanically translate them into the formal implementation.

Understand the full algorithm and data semantics first, then implement them as
clear, maintainable project code with meaningful names, explicit types and
interfaces, cohesive functions, and normal engine architecture. This applies to
C++, HLSL, plugin code, and all other production code, including LocalWind.

Register-style code, instruction emulation, unexplained offsets or bit patterns,
and reproduced general-purpose game math routines are not acceptable shortcuts.
Renaming temporary registers, adding comments, or wrapping such code in helper
functions is not a sufficient conversion. Prefer suitable UE or standard
facilities after verifying their semantics. Necessary encodings and layouts
must have named contracts and documented purposes.

Keep detailed reverse-engineering evidence separate from production code.
Preserve the established algorithmic behavior; identify and verify numerical or
engine adaptation differences instead of claiming unproven equivalence. The
source-fidelity rules require complete analysis and do not authorize mechanical
transcription. Review maintainability and behavioral correspondence separately
before declaring an implementation complete.

## Mandatory Post-Implementation Review

After completing any implementation, perform a separate review pass before
declaring the work complete.

The review must:

1. Re-read the user's current request and the request checklist.
2. Review the complete diff produced for the request, not only the last edit.
3. Trace every changed line to an explicit requirement. Review the final
   implementation for functionality or behavior added without a user request or
   applicable approval. Remove those additions and their solely supporting
   changes before completion; reporting or disabling them is not sufficient.
   Necessary implementation details must have a demonstrated dependency on a
   requested outcome. Preserve unrelated pre-existing work and requested behavior.
4. Check the resulting control flow, data flow, lifecycle, ownership, and edge
   cases for semantic regressions or contradictions with previously satisfied
   requirements.
5. Re-check all applicable project rules, including pointer and lambda capture
   rules.
6. Run the permitted verification steps appropriate to the change. Do not run
   compilation unless the user explicitly requested compilation in the current
   conversation.
7. Fix issues found by the review, then repeat the review on the complete updated
   diff and resulting implementation. Verify that unauthorized additions are
   removed and all requested outcomes remain satisfied.
8. Report each requested outcome as satisfied, partially satisfied, not satisfied,
   or not verified, including any remaining limitations. Distinguish confirmed
   unmet requirements from results that lack sufficient verification evidence.

Implementation and review are distinct passes. Do not treat reasoning performed
while writing the implementation as the required post-implementation review.
Do not declare completion until the review has been performed and reported.

## Verify Parameter Semantics Before Implementation

Before wiring or reusing an engine property, flag, helper, material input, or
shader parameter, inspect its declaration, editor display name and tooltip,
default value, producers, transport, and actual consumers. Establish its units,
coordinate space, supported rendering paths, enable conditions, and intended
scope where applicable. Do not infer its contract from an internal name alone.

State the relevant source-backed mapping before implementation. If metadata,
comments, and current consumers disagree, identify the discrepancy and resolve
the intended behavior before choosing a connection. Do not silently broaden a
setting's scope merely because its underlying field is available. In particular,
ray-tracing-specific controls must not gate ordinary raster lighting without
explicit approval of that semantic change.

The separate post-implementation review must trace the resulting connections
back to the user-facing controls and their intended scope, including enabled,
disabled, and unsupported rendering configurations. Distinguish source checks
from runtime verification; successful compilation does not establish semantic
correctness. Use the actual editor display names when explaining controls.

## Rebuild for the Current Interface; No Legacy Compatibility

For every new or replacement implementation within the request's scope, rebuild
the affected implementation directly around the current, explicitly defined
interface. Do not wrap or extend the old interface to keep old callers working.
Remove superseded interface support from the affected production path.

Do not add or retain backward-compatibility branches, old fields or aliases,
adapters for old callers, version/count-based layout detection, or implicit
parameter substitutions. The new implementation must expose and consume one
current interface.

Migrate declarations, producers, consumers, material graphs/assets, and parameter
writers together. Preserve the values and behavior required by the current
request through explicit migration; do not keep old consumers working through
fallbacks or multiple layouts. If a consumer cannot be migrated, report the
specific dependency and unresolved work instead of adding a compatibility layer
or claiming the migration is complete.

During the separate review, verify that every affected producer and consumer uses
the current interface and that superseded interface support has been removed.
Keep this work within the requested scope; do not rewrite unrelated code.

Keep inputs explicit, meaningfully named, and consistently organized. Any
necessary internal packing must not obscure the public input contract. Do not
silently substitute one distinct parameter for another to accommodate an old
layout, such as using primary roughness as secondary roughness solely because
the old node did not expose a separate input.

For example, do not preserve both five-output and six-output Kajiya node
layouts with successive `NUM_MATERIAL_OUTPUTS_GETKAJIYAHAIROUTPUT >= 5` and
`>= 6` branches after migrating to one current layout. This rule does not ban
conditions required by actual shader permutations, optional features, or
supported rendering configurations. Do not replace removed compatibility code
with new assertions or failure gates without explicit authorization.

## Explicit Approval for UE Render-Pipeline UAV/SRV Changes

Before adding or expanding any UAV/SRV resource binding or shader access in the
UE rendering pipeline, explain the concrete proposal to the user and obtain
explicit approval for those resource and access changes. Wait for approval
before implementing them.

This applies to textures and buffers, new resources and additional bindings or
consumers of existing resources, reads and UAV writes, and bindings introduced
through RDG, shader parameter structs, uniform buffers, or other indirect paths.
It also covers new render targets/MRT attachments that will subsequently be
consumed as SRVs. A declaration such as Texture2D or an RDG texture parameter
does not avoid this rule merely because there is no explicit CreateSRV call.

The approval request must identify:

- The purpose of the resource/access and the existing resources or mechanisms
  considered for reuse, including why they do or do not satisfy the requirement.
- Resource type, format, dimensions, sample count, allocation/reuse behavior,
  lifetime, and whether its cost scales per frame, view, light, or material.
- All affected producer/consumer passes, added bindings/MRTs, clears, copies,
  reads/writes, and relevant synchronization or resource-transition costs.
- Estimated memory and bandwidth costs, available GPU timing evidence, and a
  plan to measure unverified costs. Clearly distinguish estimates from measured
  results; do not invent timings or claim negligible overhead without evidence.
- Behavior when the feature is disabled or unused, including whether resource
  allocation, clearing, attachments, or reads still occur.

Approval to restore an effect, modify renderer code, compile, or work unattended
does not replace this specific approval. A prior explicit approval remains valid
only for the resource/access scope actually presented and approved; expanding
that scope requires approval before implementation.

Performance is part of the design and approval, not a follow-up after visual
matching. RDG ownership, transient lifetime, a small texel format, or a disabled
shading branch alone does not establish that a resource has no meaningful cost.
Do not report functional or visual verification as performance acceptance.

## UE Asset Reconstruction Skill

For model or skeleton conversion/import/reimport, texture import, and material
reconstruction or organization associated with source-game/capture migration,
read and follow `ue-asset-reconstruction` before substantive work. The installed
entrypoint is `C:/Users/Dz/.codex/skills/ue-asset-reconstruction/SKILL.md`.
If it has moved, locate it in the active skills catalog or Codex skills directory;
do not silently omit the workflow if it cannot be found.

The skill contains the naming, descriptions, grouping, graph connections,
import rules and verification criteria directly. It must not require Elena or
another character as a runtime, asset or documentation dependency. Confirm the
semantics of every relevant parameter, including internal/fixed parameters and
parameters proposed for deletion or merging. Hiding an unknown parameter does
not satisfy this requirement. Existing explicit session authorization remains
applicable; this skill grants no new compilation or restart permission.

Reuse semantically identical inputs and computations through a single producing
node/subgraph and distribute its outputs. Do not duplicate calculation chains
per output, layout section or draw. Verify sampling, coordinate, precision and
branch-context equivalence before merging; repeated MF calls alone do not prove
that duplicate computation has been eliminated.

Prefer existing UE material nodes, built-in Material Functions and algorithms.
Inspect the current engine implementation and map its full semantics before
writing custom equivalents. Use built-ins wherever they satisfy the requested
source behavior or explicitly approved adaptation; document concrete gaps and
implement only the missing parts. This preference does not authorize reducing
source fidelity or duplicating shared computation.

## 调试可视化与小范围迭代规则

以下规则适用于调试可视化及类似的小范围迭代。

1. **先明确最终行为，再修改代码。**
   综合当前要求和已经确认的上下文，列清相关状态及对应行为，例如“未选择、选择单个、
   选择多个、取消选择”。不得只根据最后一句话修改，也不得自行补充用户未要求的行为。
   明确的信息不重复询问；确有影响实现的歧义时，集中澄清。

2. **用户纠正一处要求时，更新完整规则。**
   不能把纠正理解成一个孤立补丁。必须检查它对默认状态、选中状态、其他对象及恢复行为
   的影响，并同步所有相关消费者和说明，避免出现局部正确、整体矛盾。

3. **选择满足需求的最简单实现。**
   优先复用现有绘制方式、共享函数和引擎机制。不得为了主观上的“更好看”“更灵活”增加
   未要求的调色板、缓存、配置、辅助类或生命周期管理。方案获批也不免除设计是否过度
   复杂的责任。

4. **视觉比例必须有统一、明确的依据。**
   修改前确认线宽、箭头和范围的单位、坐标空间及缩放关系。相同用途的元素共用尺寸规则，
   并检查小范围、大范围和重叠场景。不得为不同类型随意设置不一致的比例，也不得未经
   要求通过加粗、放大来表达选中状态。

5. **源码正确与视觉合格必须分别验收。**
   代码检查、数值检查和示意图只能证明各自覆盖的内容，不能替代实际引擎画面。
   未获授权编译或无法运行验证时，明确标记“视觉未验证”。不得把源码检查通过描述为
   效果已合格，也不得在缺少新证据时反复猜测、调参。

6. **小改动保持小范围，替换时清理旧实现。**
   修改仅限于当前要求及其直接依赖。已有明确批准的实现形式继续复用，不重复请求同一
   批准。新要求使旧逻辑失效时，应删除对应实现、缓存、依赖和说明，不保留无用兼容路径，
   也不顺带重构其他功能。

7. **控制重复读取、工具输出和沟通成本。**
   已完整理解且没有变化的代码不反复全文读取。后续优先检查相关函数、调用点和新增差异；
   只有发现新依赖或外部修改时才扩大检查范围。工具输出只保留判断所需内容，进度更新只
   报告新发现、决定和待验证事项，不反复讲述整套方案。

8. **完成后进行一次独立的完整范围复查，并准确报告结果。**
   对照最终行为检查全部相关路径，确认无遗漏、无未要求的新增功能、无失效旧逻辑。
   利用已读源码和完整差异完成复查，不机械重复所有检查。最终说明具体改动、已验证内容
   和未验证内容；有新修改或新证据时，才重复受影响的检查。
