#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElenaRigData.generated.h"

UCLASS(BlueprintType)
class UNREALSAMPLE_API UElenaRigData : public UDataAsset
{
    GENERATED_BODY()

public:
    // Original group order, command blocks and reference SQT; no game executable is loaded.
    UPROPERTY(EditAnywhere, Category="Source Rig")
    TArray<uint8> ProgramBytes;

    UPROPERTY(EditAnywhere, Category="Source Rig")
    TArray<FName> BoneNames;

    UPROPERTY(EditAnywhere, Category="Source Rig")
    TArray<int32> DirectBones;

    UPROPERTY(EditAnywhere, Category="Source Rig")
    TArray<int32> DerivedBones;
};
