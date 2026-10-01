#include "AnimNode_ElenaRig.h"
#include "Animation/AnimInstanceProxy.h"

namespace
{
// FBX import: source Y-up to UE Z-up swaps global Y/Z; local bone axes reflect Y.
FTransform SourceWorldToUE(const ElenaRig::Matrix& Source)
{
    constexpr int32 Rows[3] = {0, 2, 1};
    constexpr double Signs[3] = {1, -1, 1};
    FMatrix Matrix = FMatrix::Identity;
    for (int32 Axis = 0; Axis != 3; ++Axis)
    for (int32 Component = 0; Component != 3; ++Component)
        Matrix.M[Axis][Component] = Source[Rows[Component]][Axis] * Source[3][Axis] * Signs[Axis];
    for (int32 Component = 0; Component != 3; ++Component)
        Matrix.M[3][Component] = Source[Rows[Component]][3] * 100.0;
    return FTransform(Matrix);
}

void UELocalToSource(const FTransform& Local, bool bRoot, ElenaRig::SQT& SQT)
{
    FQuat Rotation = Local.GetRotation();
    FVector Translation = Local.GetTranslation();
    const FVector Scale = Local.GetScale3D();
    if (bRoot)
    {
        // Source R = C^-1 * UE R * D; transpose indices for UE row-vector matrices.
        const FMatrix UE = Local.ToMatrixNoScale();
        FMatrix Source = FMatrix::Identity;
        constexpr int32 Rows[3] = {0, 2, 1};
        constexpr double Signs[3] = {1, -1, 1};
        for (int32 Axis = 0; Axis != 3; ++Axis)
        for (int32 Component = 0; Component != 3; ++Component)
            Source.M[Axis][Component] = UE.M[Axis][Rows[Component]] * Signs[Axis];
        Rotation = FQuat(Source).GetNormalized();
        Translation = FVector(Translation.X, Translation.Z, Translation.Y);
    }
    else
    {
        Rotation = FQuat(-Rotation.X, Rotation.Y, -Rotation.Z, Rotation.W);
        Translation.Y = -Translation.Y;
    }
    SQT[0][0] = float(Scale.X); SQT[0][1] = float(Scale.Y); SQT[0][2] = float(Scale.Z);
    SQT[1] = {float(Rotation.X),float(Rotation.Y),float(Rotation.Z),float(Rotation.W)};
    SQT[2][0] = float(Translation.X / 100.0);
    SQT[2][1] = float(Translation.Y / 100.0);
    SQT[2][2] = float(Translation.Z / 100.0);
}
}

void FAnimNode_ElenaRig::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);
    InputPose.Initialize(Context);
    Program = {};
    DirectBones.Reset(); DerivedBones.Reset();
    if (RigData && !RigData->ProgramBytes.IsEmpty())
    {
        Program = ElenaRig::ReadProgram(std::as_bytes(std::span(RigData->ProgramBytes.GetData(), RigData->ProgramBytes.Num())));
        DirectBones = RigData->DirectBones;
        DerivedBones = RigData->DerivedBones;
    }
}

void FAnimNode_ElenaRig::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    InputPose.CacheBones(Context);
    const FBoneContainer& Bones = Context.AnimInstanceProxy->GetRequiredBones();
    BoneMap.Reset();
    DerivedByCompact.Init(INDEX_NONE, Bones.GetCompactPoseNumBones());
    if (RigData)
    {
        for (const FName& Name : RigData->BoneNames)
            BoneMap.Add(Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bones.GetReferenceSkeleton().FindBoneIndex(Name))));
        for (int32 Source : DerivedBones)
            if (BoneMap[Source].GetInt() != INDEX_NONE) DerivedByCompact[BoneMap[Source].GetInt()] = Source;
    }
}

void FAnimNode_ElenaRig::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    GetEvaluateGraphExposedInputs().Execute(Context);
    InputPose.Update(Context);
}

void FAnimNode_ElenaRig::Evaluate_AnyThread(FPoseContext& Output)
{
    InputPose.Evaluate(Output);
    if (bEnabled && !Program.ReferencePose.empty())
    {
        Inputs = Program.ReferencePose;
        for (int32 Source : DirectBones)
            if (BoneMap[Source].GetInt() != INDEX_NONE)
                UELocalToSource(Output.Pose[BoneMap[Source]], Source == 0, Inputs[Source]);
        Machine.Evaluate(Program, Inputs);
        ComponentPose.SetNum(Output.Pose.GetNumBones());
        for (FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
        {
            const auto Parent = Output.Pose.GetParentBoneIndex(Bone);
            const int32 Source = DerivedByCompact[Bone.GetInt()];
            if (Source != INDEX_NONE)
            {
                ComponentPose[Bone.GetInt()] = SourceWorldToUE(Machine.Worlds[Source]);
                Output.Pose[Bone] = Parent.GetInt() == INDEX_NONE ? ComponentPose[Bone.GetInt()] :
                    ComponentPose[Bone.GetInt()].GetRelativeTransform(ComponentPose[Parent.GetInt()]);
            }
            else
                ComponentPose[Bone.GetInt()] = Parent.GetInt() == INDEX_NONE ? Output.Pose[Bone] :
                    Output.Pose[Bone] * ComponentPose[Parent.GetInt()];
        }
    }
}

void FAnimNode_ElenaRig::GatherDebugData(FNodeDebugData& DebugData)
{
    DebugData.AddDebugItem(FString::Printf(TEXT("Elena Source Rig: %s, %d auxiliary bones"),
        bEnabled ? TEXT("Realtime") : TEXT("Input pose fallback"), DerivedBones.Num()));
    InputPose.GatherDebugData(DebugData.BranchFlow(1.0f));
}
