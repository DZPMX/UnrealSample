#pragma once

#include "CoreMinimal.h"

namespace Grace::Locomotion
{
    // RE model space: X lateral, Y up, Z forward; metres and metres/second.
    struct FRunPoseQuery
    {
        FVector3f LeftFootPosition;
        FVector3f RightFootPosition;
        FVector2f LeftFootVelocityXZ;
        FVector2f RightFootVelocityXZ;
    };

    enum class ERunStopFoot : uint8
    {
        Left,
        Right
    };

    struct FRunStopSelection
    {
        ERunStopFoot Foot;
        float StartFrame;
    };

    // The current ordinary GeneralM RunEnd node, with its original two clips.
    // This is deliberately not an interface for arbitrary TRAM nodes/banks.
    // Domain: finite skeletal queries with non-overflowing float distances.
    FRunStopSelection MatchRunStop(const FRunPoseQuery& Query);

    // GeneralM side-turn cancellation: frame 1/5/9, shared 5375 data.
    // The original ClipTag mirror XOR selects the reflected opposite-foot query.
    float MatchRunSideTurnCancel(const FRunPoseQuery& Query, bool IsMirrored);
}
