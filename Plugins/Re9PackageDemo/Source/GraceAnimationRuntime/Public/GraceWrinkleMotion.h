#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "GraceWrinkleMotion.generated.h"

USTRUCT(BlueprintType)
struct GRACEANIMATIONRUNTIME_API FGraceWrinkleTrack
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Original Track")
    int32 MaskIndex = 0;

    UPROPERTY(EditAnywhere, Category="Original Track")
    int32 Compression = 0;

    UPROPERTY(EditAnywhere, Category="Original Track")
    TArray<int32> Frames;

    UPROPERTY(EditAnywhere, Category="Original Track")
    TArray<uint8> Packed;

    UPROPERTY(EditAnywhere, Category="Original Track")
    TArray<float> Unpack;

    float Single(int32 Index) const;
    float Pair(int32 Index, float Fraction) const;
    float Sample(float Frame, float ResourceLastFrame, int32& CachedIndex) const;
};

UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Grace Original Wrinkle Tracks"))
class GRACEANIMATIONRUNTIME_API UGraceWrinkleMotion : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Source", meta=(ToolTip="Original MOT/MOTLIST resource path."))
    FString SourceResource;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Source")
    FString SourceSHA256;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ToolTip="Original MOT frame rate in frames per second."))
    float SourceFrameRate = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ToolTip="Last source frame supplied to the original sparse-key search."))
    float SourceLastFrame = 0.0f;

    UPROPERTY(EditAnywhere, Category="Original Tracks")
    TArray<FGraceWrinkleTrack> Tracks;

    UFUNCTION(BlueprintCallable, Category="Grace|Wrinkles")
    TArray<float> SampleAtSeconds(float Seconds) const;
};
