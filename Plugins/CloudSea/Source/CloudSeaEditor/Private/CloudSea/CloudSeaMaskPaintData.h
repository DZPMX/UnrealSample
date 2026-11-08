// Copyright KuaiShou Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CloudSeaMaskPaintData.generated.h"

/** World-space brush path, retained so moving/resizing the canvas does not repeatedly filter old strokes. */
USTRUCT()
struct FCloudSeaMaskPaintStroke
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector2D> Points;

	UPROPERTY()
	float RadiusMeters = 50.0f;

	UPROPERTY()
	float Strength = 1.0f;

	UPROPERTY()
	float Hardness = 0.5f;

	UPROPERTY()
	bool bPaintWhite = false;
};

/** Transactional CPU canvas. Instances live in the transient package, never in a texture asset. */
UCLASS(Transient)
class UCloudSeaMaskPaintData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<uint8> Pixels;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	UPROPERTY()
	FVector2D CenterWorldXY = FVector2D::ZeroVector;

	/** Full world extent in meters; texture dimensions remain fixed when this grows. */
	UPROPERTY()
	FVector2D WorldSize = FVector2D(500.0, 500.0);

	UPROPERTY()
	FBox2D PaintedWorldBounds = FBox2D(ForceInit);

	UPROPERTY()
	bool bUseOriginalPixels = true;

	UPROPERTY()
	TArray<FCloudSeaMaskPaintStroke> Strokes;
};
