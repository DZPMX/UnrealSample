#include "GraceRunStopMatching.h"

namespace Grace::Locomotion
{
    namespace
    {
        // Original node's MaxStartFrame=1, untrimmed clips, bank stride=4.
        // Each clip's fragments begin at frame 1. Only these entries qualify.
        // The full bank and eligibility derivation remain in the research data.
        constexpr float RunStopStartFrame = 1.0f;
        constexpr float PositionWeight = 1.0f;
        constexpr float VelocityWeight = 0.5f;

        const FRunPoseQuery LeftStopEntry
        {
            FVector3f(0.16240298748016357f, 0.054618775844573975f, 0.4327794313430786f),
            FVector3f(-0.037640199065208435f, 0.45414674282073975f, -0.5586367845535278f),
            FVector2f(-0.2685105800628662f, -1.2073016166687012f),
            FVector2f(0.15721768140792847f, 0.47037720680236816f)
        };

        const FRunPoseQuery RightStopEntry
        {
            FVector3f(0.039789147675037384f, 0.04532827436923981f, 0.06761530786752701f),
            FVector3f(-0.07404571771621704f, 0.16473236680030823f, -0.20412257313728333f),
            FVector2f(0.007516443729400635f, -1.8996057510375977f),
            FVector2f(0.010919719934463501f, 3.0167198181152344f)
        };

        float AccumulateWeightedError(float Sum, float QueryValue, float ReferenceValue, float Weight)
        {
            // Source rounds the weighted residual, then rounds each accumulation.
            // Weight multiplies the residual before squaring. Module uses precise FP.
            const double Difference = static_cast<double>(QueryValue) - static_cast<double>(ReferenceValue);
            const float WeightedResidual = static_cast<float>(Difference * static_cast<double>(Weight));
            const double SquaredResidual = static_cast<double>(WeightedResidual) * static_cast<double>(WeightedResidual);
            return static_cast<float>(static_cast<double>(Sum) + SquaredResidual);
        }

        float PositionDistance(const FRunPoseQuery& Query, const FRunPoseQuery& Reference)
        {
            float Distance = 0.0f;
            Distance = AccumulateWeightedError(Distance, Query.LeftFootPosition.X, Reference.LeftFootPosition.X, PositionWeight);
            Distance = AccumulateWeightedError(Distance, Query.LeftFootPosition.Y, Reference.LeftFootPosition.Y, PositionWeight);
            Distance = AccumulateWeightedError(Distance, Query.LeftFootPosition.Z, Reference.LeftFootPosition.Z, PositionWeight);
            Distance = AccumulateWeightedError(Distance, Query.RightFootPosition.X, Reference.RightFootPosition.X, PositionWeight);
            Distance = AccumulateWeightedError(Distance, Query.RightFootPosition.Y, Reference.RightFootPosition.Y, PositionWeight);
            return AccumulateWeightedError(Distance, Query.RightFootPosition.Z, Reference.RightFootPosition.Z, PositionWeight);
        }

        float VelocityDistance(const FRunPoseQuery& Query, const FRunPoseQuery& Reference)
        {
            float Distance = 0.0f;
            Distance = AccumulateWeightedError(Distance, Query.LeftFootVelocityXZ.X, Reference.LeftFootVelocityXZ.X, VelocityWeight);
            Distance = AccumulateWeightedError(Distance, Query.LeftFootVelocityXZ.Y, Reference.LeftFootVelocityXZ.Y, VelocityWeight);
            Distance = AccumulateWeightedError(Distance, Query.RightFootVelocityXZ.X, Reference.RightFootVelocityXZ.X, VelocityWeight);
            return AccumulateWeightedError(Distance, Query.RightFootVelocityXZ.Y, Reference.RightFootVelocityXZ.Y, VelocityWeight);
        }
    }

    FRunStopSelection MatchRunStop(const FRunPoseQuery& Query)
    {
        const float LeftPositionDistance = PositionDistance(Query, LeftStopEntry);
        const float RightPositionDistance = PositionDistance(Query, RightStopEntry);
        const float LeftVelocityDistance = VelocityDistance(Query, LeftStopEntry);
        const float RightVelocityDistance = VelocityDistance(Query, RightStopEntry);

        // Default solver: position-sort/keep 6, velocity-sort/keep 4,
        // position-sort/keep 2, then strictly smallest velocity distance.
        // Here only two candidates qualify. No pruning stage removes either.
        // Position breaks a velocity tie; an exact tie retains node order (left).
        // Hip Euler-Y is serialized in the bank but excluded by both type masks.
        const bool bRightHasSmallerVelocityError = RightVelocityDistance < LeftVelocityDistance;
        const bool bEqualVelocityAndCloserRightPose = RightVelocityDistance == LeftVelocityDistance
            && RightPositionDistance < LeftPositionDistance;
        const ERunStopFoot Foot = bRightHasSmallerVelocityError || bEqualVelocityAndCloserRightPose
            ? ERunStopFoot::Right : ERunStopFoot::Left;
        return {Foot, RunStopStartFrame};
    }
    float MatchRunSideTurnCancel(const FRunPoseQuery& Query, bool IsMirrored)
    {
        // Bank 0 deliberately shares this data between motions 5370 and 5375.
        // With one clip and MaxStartFrame=10, only these three fragments qualify.
        const FRunPoseQuery Entries[]
        {
            {FVector3f(-0.029390159994363785f, 0.04551105946302414f, 0.07223951816558838f), FVector3f(-0.013012509793043137f, 0.2527245283126831f, -0.4208556115627289f),
                FVector2f(0.5439311861991882f, -2.1036949157714844f), FVector2f(-1.5101954936981201f, 1.9491398334503174f)},
            {FVector3f(0.0071194288320839405f, 0.05568768084049225f, -0.0676538497209549f), FVector3f(-0.12718844413757324f, 0.1869676262140274f, -0.25225892663002014f),
                FVector2f(0.6739068031311035f, -2.0287065505981445f), FVector2f(-1.7789541482925415f, 2.872572422027588f)},
            {FVector3f(0.06946409493684769f, 0.10034340620040894f, -0.17593231797218323f), FVector3f(-0.26985564827919006f, 0.10080181062221527f, -0.024676144123077393f),
                FVector2f(0.9521589279174805f, -1.3064485788345337f), FVector2f(-2.3760805130004883f, 3.6301965713500977f)}
        };
        const FRunPoseQuery MirroredQuery
        {
            FVector3f(-Query.RightFootPosition.X, Query.RightFootPosition.Y, Query.RightFootPosition.Z),
            FVector3f(-Query.LeftFootPosition.X, Query.LeftFootPosition.Y, Query.LeftFootPosition.Z),
            FVector2f(-Query.RightFootVelocityXZ.X, Query.RightFootVelocityXZ.Y),
            FVector2f(-Query.LeftFootVelocityXZ.X, Query.LeftFootVelocityXZ.Y)
        };
        const FRunPoseQuery& ActiveQuery = IsMirrored ? MirroredQuery : Query;
        int32 BestIndex = 0;
        float BestPosition = PositionDistance(ActiveQuery, Entries[0]);
        float BestVelocity = VelocityDistance(ActiveQuery, Entries[0]);
        for (int32 Index = 1; Index < UE_ARRAY_COUNT(Entries); ++Index)
        {
            const float Position = PositionDistance(ActiveQuery, Entries[Index]);
            const float Velocity = VelocityDistance(ActiveQuery, Entries[Index]);
            // Default solver keeps 3 by position, 2 by velocity, 2 by position,
            // then the strict minimum velocity. For these three finite candidates
            // this preserves the same minimum, position tie-break and frame order.
            if (Velocity < BestVelocity || (Velocity == BestVelocity && Position < BestPosition))
            {
                BestIndex = Index;
                BestPosition = Position;
                BestVelocity = Velocity;
            }
        }
        return 1.0f + 4.0f * BestIndex;
    }

}
