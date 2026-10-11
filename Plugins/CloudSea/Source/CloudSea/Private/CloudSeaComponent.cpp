// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com
// Adapted from DzPmx/UnrealSample, commit 2f065f5f72ee0e06099ea2a855f329070fcdf0cb.

#include "CloudSeaComponent.h"

#include "CoreGlobals.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

namespace
{
	constexpr double DiskHeightCompressionFactor = 10.0;
	constexpr double DiskHeightRemainderFactor = DiskHeightCompressionFactor - 1.0;
	constexpr double DiskProxyNearClipSafetyFactor = 1.5;
	constexpr double DiskProxyNearClipSafetyPadding = 10.0;
}

UCloudSeaComponent::UCloudSeaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.EndTickGroup = TG_PostUpdateWork;
	bTickInEditor = true;
	SetMobility(EComponentMobility::Movable);
}

void UCloudSeaComponent::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	FEditorDelegates::OnEditorCameraMoved.AddUObject(
		this,
		&UCloudSeaComponent::HandleEditorCameraMoved);
#endif
	// Rebind to the mesh's current material and reapply authored settings on registration.
	DynamicMaterial = nullptr;
	UpdateCloudSeaTransform();
}

void UCloudSeaComponent::OnUnregister()
{
	if (DynamicMaterial != nullptr)
	{
		DynamicMaterial->SetScalarParameterValue(TEXT("CloudOpacity"), 1.0f);
	}
	UpdateProxyVisibility(false);
	if (ProxyMeshComponent.IsValid() && ProxyMeshComponent->GetMaterial(0) == DynamicMaterial.Get())
	{
		ProxyMeshComponent->SetMaterial(0, OriginalProxyMaterial);
	}
	DynamicMaterial = nullptr;
	OriginalProxyMaterial = nullptr;
#if WITH_EDITOR
	FEditorDelegates::OnEditorCameraMoved.RemoveAll(this);
#endif
	Super::OnUnregister();
}

void UCloudSeaComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateCloudSeaTransform();
}

#if WITH_EDITOR
void UCloudSeaComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (IsRegistered())
	{
		ApplyStaticMaterialParameters();
		UpdateCloudSeaTransform();
	}

	// Super can rerun construction scripts and replace this component.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UCloudSeaComponent::HandleEditorCameraMoved(
	const FVector&,
	const FRotator&,
	ELevelViewportType,
	int32)
{
	UpdateCloudSeaTransform();
}
#endif

bool UCloudSeaComponent::TryGetView(
	FVector& OutViewLocation,
	FRotator& OutViewRotation,
	float& OutNearClipPlane) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const UWorld& World = *GetWorld();
	OutNearClipPlane = GNearClippingPlane;

#if WITH_EDITOR
	// SIE and ejected PIE render through the level viewport while the player camera remains elsewhere.
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		const FLevelEditorViewportClient& ViewportClient = *GCurrentLevelEditingViewportClient;
		if (ViewportClient.GetWorld() == &World && ViewportClient.IsPerspective())
		{
			OutViewLocation = ViewportClient.GetViewLocation();
			OutViewRotation = ViewportClient.GetViewRotation();
			OutNearClipPlane = ViewportClient.GetNearClipPlane();
			return true;
		}
	}
#endif

	if (World.IsGameWorld())
	{
		const TWeakObjectPtr<APlayerCameraManager> CameraManager =
			UGameplayStatics::GetPlayerCameraManager(this, CameraPlayerIndex);
		if (CameraManager.IsValid())
		{
			OutViewLocation = CameraManager->GetCameraLocation();
			OutViewRotation = CameraManager->GetCameraRotation();
			return true;
		}

		return false;
	}

	return false;
}

void UCloudSeaComponent::InitializeMaterialDriver()
{
	if (!ProxyMeshComponent.IsValid())
	{
		DynamicMaterial = nullptr;
		OriginalProxyMaterial = nullptr;
		const TWeakObjectPtr<AActor> Owner = GetOwner();
		if (Owner.IsValid())
		{
			ProxyMeshComponent = Owner->FindComponentByClass<UStaticMeshComponent>();
		}
	}

	if (ProxyMeshComponent.IsValid() && ProxyMeshComponent->bSelectable)
	{
		// The cloud proxy is never selectable, including after this driver is removed.
		ProxyMeshComponent->bSelectable = false;
		ProxyMeshComponent->MarkRenderStateDirty();
	}

	if (ProxyMeshComponent.IsValid())
	{
		const TObjectPtr<UMaterialInterface> MeshMaterial = ProxyMeshComponent->GetMaterial(0);
		if (MeshMaterial != DynamicMaterial.Get())
		{
			OriginalProxyMaterial = MeshMaterial;
			DynamicMaterial = nullptr;
		}

		TObjectPtr<UMaterialInterface> SourceMaterial = bUseRaymarchDither
			? MaterialWithDither.Get()
			: MaterialWithoutDither.Get();
		if (SourceMaterial == nullptr)
		{
			SourceMaterial = OriginalProxyMaterial;
		}
		if (DynamicMaterial == nullptr
			|| (DynamicMaterial.Get() != SourceMaterial && DynamicMaterial->Parent != SourceMaterial))
		{
			DynamicMaterial = ProxyMeshComponent->CreateDynamicMaterialInstance(0, SourceMaterial.Get());
			ApplyStaticMaterialParameters();
		}
	}
}

void UCloudSeaComponent::ApplyStaticMaterialParameters()
{
	if (DynamicMaterial == nullptr)
	{
		return;
	}

	DynamicMaterial->SetScalarParameterValue(
		TEXT("RayMarchStepCount"),
		static_cast<float>(FMath::Clamp(RayMarchStepCount, 16, 64)));
	DynamicMaterial->SetScalarParameterValue(TEXT("NoiseUVScale"), NoiseVolumeUVScale);
	DynamicMaterial->SetScalarParameterValue(TEXT("NoiseWarpAmplitude"), NoiseWarpAmplitude);
	DynamicMaterial->SetScalarParameterValue(TEXT("ExtinctionScale"), ExtinctionDensityScale);
	DynamicMaterial->SetScalarParameterValue(
		TEXT("SceneDepthFadeInvDistance"),
		SceneDepthFadeReciprocalDistance);
	DynamicMaterial->SetVectorParameterValue(TEXT("CloudScatteringTint"), CloudScatteringTint);
	DynamicMaterial->SetScalarParameterValue(TEXT("AmbientLightScale"), CloudDiffuseWeight);
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseG"), PhaseG);
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseG2"), PhaseG2);
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseBlend"), PhaseBlend);
	DynamicMaterial->SetVectorParameterValue(TEXT("DensityUVScale"), FLinearColor(DensityVolumeUVScale));
	DynamicMaterial->SetScalarParameterValue(TEXT("FillLightScale"), UnphasedLightWeight);
	DynamicMaterial->SetScalarParameterValue(TEXT("SunLightScale"), PhaseLightWeight);
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudLayerThickness"), CloudLayerHeight);
	DynamicMaterial->SetScalarParameterValue(TEXT("WorldUnitScale"), WorldUnitsPerMaterialUnit);
}

void UCloudSeaComponent::UpdateProxyVisibility(bool bHideForFade)
{
	if (!bHideForFade || FadeHiddenProxyMeshComponent != ProxyMeshComponent)
	{
		if (FadeHiddenProxyMeshComponent.IsValid())
		{
#if WITH_EDITOR
			const TObjectPtr<UWorld> ProxyWorld = FadeHiddenProxyMeshComponent->GetWorld();
			if (ProxyWorld != nullptr && !ProxyWorld->IsGameWorld())
			{
				FadeHiddenProxyMeshComponent->SetIsTemporarilyHiddenInEditor(false);
			}
			else
#endif
			{
				FadeHiddenProxyMeshComponent->SetVisibility(true);
			}
		}
		FadeHiddenProxyMeshComponent.Reset();
	}

	if (bHideForFade && ProxyMeshComponent.IsValid())
	{
#if WITH_EDITOR
		const TObjectPtr<UWorld> ProxyWorld = ProxyMeshComponent->GetWorld();
		if (ProxyWorld != nullptr && !ProxyWorld->IsGameWorld())
		{
			// Camera-driven editor hiding must not be serialized into the level.
			if (ProxyMeshComponent->ShouldRender())
			{
				ProxyMeshComponent->SetIsTemporarilyHiddenInEditor(true);
				FadeHiddenProxyMeshComponent = ProxyMeshComponent;
			}
			return;
		}
#endif
		if (ProxyMeshComponent->GetVisibleFlag())
		{
			ProxyMeshComponent->SetVisibility(false);
			FadeHiddenProxyMeshComponent = ProxyMeshComponent;
		}
	}
}

void UCloudSeaComponent::UpdateCloudSeaTransform()
{
	InitializeMaterialDriver();

	FVector ViewLocation;
	FRotator ViewRotation;
	float NearClipPlane;
	if (!TryGetView(ViewLocation, ViewRotation, NearClipPlane))
	{
		return;
	}

	const float CloudOpacity = BelowCloudFadeWorldDistance > 0.0f
		? static_cast<float>(1.0 - FMath::Clamp(
			(CloudLayerBaseWorldHeight - ViewLocation.Z)
				/ static_cast<double>(BelowCloudFadeWorldDistance),
			0.0,
			1.0))
		: 1.0f;
	if (DynamicMaterial != nullptr)
	{
		DynamicMaterial->SetScalarParameterValue(TEXT("CloudOpacity"), CloudOpacity);
	}
	if (CloudOpacity <= 0.0f)
	{
		UpdateProxyVisibility(true);
		return;
	}

	const double CloudLayerTopWorldHeight =
		CloudLayerBaseWorldHeight
		+ CloudLayerHeight * WorldUnitsPerMaterialUnit;
	const double SignedCameraDepthBelowCloudTopMaterialUnits =
		(CloudLayerTopWorldHeight - ViewLocation.Z) / WorldUnitsPerMaterialUnit;
	const double CameraHeightAboveCloudTopWorldUnits =
		FMath::Max(ViewLocation.Z - CloudLayerTopWorldHeight, 0.0);
	const double DiskRayMarchTransitionAlpha = FMath::SmoothStep(
		0.0,
		static_cast<double>(DiskRayMarchTransitionWorldDistance),
		CameraHeightAboveCloudTopWorldUnits);
	const bool bUseReferenceMirrorTransition =
		VerticalRenderingMode == ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill;
	const float DiskTransitionStartMarchDistanceMaterialUnits =
		bUseReferenceMirrorTransition
		? FullScreenTransitionMarchDistance
		: RayMarchActiveDistance;
	const float ActiveDiskRayMarchDistanceMaterialUnits = FMath::Lerp(
		DiskTransitionStartMarchDistanceMaterialUnits,
		RayMarchActiveDistance,
		static_cast<float>(DiskRayMarchTransitionAlpha));
	const bool bUseFullScreenProxy =
		ViewLocation.Z <= CloudLayerTopWorldHeight;

	FVector CloudSeaLocation;
	FQuat CloudSeaRotation;
	double CloudSeaWorldRadius = ProxyWorldRadius;
	if (bUseFullScreenProxy)
	{
		const FVector ViewForward = ViewRotation.Vector();
		CloudSeaLocation = ViewLocation + ViewForward * FullScreenProxyCameraDistance;
		// The plane is in front of the camera; point its +Z normal back toward the camera.
		CloudSeaRotation = FQuat::FindBetweenNormals(FVector::UpVector, -ViewForward);
	}
	else
	{
		const double CompressedCameraDepthBelowCloudTopMaterialUnits =
			SignedCameraDepthBelowCloudTopMaterialUnits / DiskHeightCompressionFactor;
		CloudSeaWorldRadius =
			DiskHeightCompressionFactor
			* FMath::Sqrt(
				FMath::Square(ActiveDiskRayMarchDistanceMaterialUnits)
				- FMath::Square(CompressedCameraDepthBelowCloudTopMaterialUnits))
			* WorldUnitsPerMaterialUnit;
		const double ReconstructedDiskProxyWorldOffset =
			CompressedCameraDepthBelowCloudTopMaterialUnits
			* WorldUnitsPerMaterialUnit
			* (DiskMarchFarDistance / ActiveDiskRayMarchDistanceMaterialUnits);
		const double DiskProxyWorldOffset = FMath::Min(
			ReconstructedDiskProxyWorldOffset,
			-(static_cast<double>(NearClipPlane) * DiskProxyNearClipSafetyFactor
				+ DiskProxyNearClipSafetyPadding));
		const double DiskProxyWorldZ = ViewLocation.Z + DiskProxyWorldOffset;
		CloudSeaLocation = FVector(
			ViewLocation.X,
			ViewLocation.Y,
			DiskProxyWorldZ);
		CloudSeaRotation = FQuat::Identity;
	}

	const FVector CloudSeaScale(CloudSeaWorldRadius, CloudSeaWorldRadius, 1.0);
	const FTransform CloudSeaTransform(CloudSeaRotation, CloudSeaLocation, CloudSeaScale);
	SetWorldTransform(CloudSeaTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UpdateViewMaterialParameters(
		ViewLocation,
		bUseFullScreenProxy,
		SignedCameraDepthBelowCloudTopMaterialUnits,
		ActiveDiskRayMarchDistanceMaterialUnits);
	UpdateProxyVisibility(false);
}

void UCloudSeaComponent::UpdateViewMaterialParameters(
	const FVector& ViewLocation,
	bool bUseFullScreenProxy,
	double SignedCameraDepthBelowCloudTopMaterialUnits,
	float ActiveDiskRayMarchDistanceMaterialUnits)
{
	if (DynamicMaterial == nullptr)
	{
		return;
	}

	const float CameraDepthBelowCloudTopMaterialUnits =
		static_cast<float>(SignedCameraDepthBelowCloudTopMaterialUnits);
	float CameraCloudTopDepth = CameraDepthBelowCloudTopMaterialUnits;
	float CloudPrefillOpacity = 0.0f;
	float CloudDepthReconstructionOffset = 0.0f;
	float ActiveRayMarchDistance = RayMarchActiveDistance;
	const bool bUseReferenceMirrorTransition =
		VerticalRenderingMode == ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill;
	const float CameraDepthBelowCloudBaseMaterialUnits =
		-CloudLayerHeight
		+ CameraDepthBelowCloudTopMaterialUnits;
	if (bUseFullScreenProxy)
	{
		// Keep the mirrored layer and its prefill transition intact before compressing the gap below it.
		const float FullScreenCompressionAnchorDepthBelowCloudBaseMaterialUnits =
			bUseReferenceMirrorTransition ? CloudLayerHeight : 0.0f;
		const float CameraDistanceBelowFullScreenCompressionAnchorMaterialUnits =
			CameraDepthBelowCloudBaseMaterialUnits
			- FullScreenCompressionAnchorDepthBelowCloudBaseMaterialUnits;
		if (CameraDistanceBelowFullScreenCompressionAnchorMaterialUnits > 0.0f)
		{
			const float CompressedCameraDistanceBelowAnchorMaterialUnits =
				CameraDistanceBelowFullScreenCompressionAnchorMaterialUnits
				/ static_cast<float>(DiskHeightCompressionFactor);
			CameraCloudTopDepth =
				CloudLayerHeight
				+ FullScreenCompressionAnchorDepthBelowCloudBaseMaterialUnits
				+ CompressedCameraDistanceBelowAnchorMaterialUnits;
			CloudDepthReconstructionOffset =
				CameraDistanceBelowFullScreenCompressionAnchorMaterialUnits
				- CompressedCameraDistanceBelowAnchorMaterialUnits;
		}
	}
	else
	{
		CameraCloudTopDepth =
			SignedCameraDepthBelowCloudTopMaterialUnits / DiskHeightCompressionFactor;
		CloudDepthReconstructionOffset =
			CameraCloudTopDepth * DiskHeightRemainderFactor;
		ActiveRayMarchDistance =
			ActiveDiskRayMarchDistanceMaterialUnits;
	}

	float DensityVolumeZSign = 1.0f;
	if (bUseFullScreenProxy && bUseReferenceMirrorTransition)
	{
		const bool bCameraBelowLayer = CameraDepthBelowCloudBaseMaterialUnits > 0.0f;

		float PrefillCoordinate;
		if (bCameraBelowLayer)
		{
			const float NormalizedDepthBelowBase = FMath::Clamp(
				CameraDepthBelowCloudBaseMaterialUnits / CloudLayerHeight,
				0.0f,
				1.0f);
			ActiveRayMarchDistance =
				FullScreenTransitionMarchDistance * NormalizedDepthBelowBase;
			PrefillCoordinate = 1.0f - NormalizedDepthBelowBase;
			DensityVolumeZSign = -1.0f;
		}
		else
		{
			const float NormalizedDepthBelowTop = FMath::Clamp(
				CameraDepthBelowCloudTopMaterialUnits / CloudLayerHeight,
				0.0f,
				1.0f);
			ActiveRayMarchDistance =
				FullScreenTransitionMarchDistance * (1.0f - NormalizedDepthBelowTop);
			PrefillCoordinate = NormalizedDepthBelowTop;
		}

		const float PrefillBase = FMath::Clamp(
			1.5f * PrefillCoordinate,
			0.0f,
			1.0f);
		CloudPrefillOpacity =
			FMath::Square(FMath::Square(PrefillBase));
	}

	DynamicMaterial->SetScalarParameterValue(TEXT("CloudTopOffsetZ"), CameraCloudTopDepth);
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudPrefillOpacity"), CloudPrefillOpacity);
	DynamicMaterial->SetScalarParameterValue(TEXT("DepthReconstructionOffsetZ"), CloudDepthReconstructionOffset);
	DynamicMaterial->SetScalarParameterValue(TEXT("RayMarchMaxDistance"), ActiveRayMarchDistance);

	const FVector2D CameraDeltaMaterialUnits(
		(ViewLocation.X - VolumeAnchorReferenceCameraXY.X) / WorldUnitsPerMaterialUnit,
		(ViewLocation.Y - VolumeAnchorReferenceCameraXY.Y) / WorldUnitsPerMaterialUnit);
	// Camera-relative ray coordinates require UV offsets to follow the camera delta.
	const FVector2D CameraRelativeDensityUVOffset = DensityUVOffset + CameraDeltaMaterialUnits;
	const FVector2D CameraRelativeNoiseUVOffset = NoiseUVOffset + CameraDeltaMaterialUnits;
	DynamicMaterial->SetVectorParameterValue(
		TEXT("DensityOffset"),
		FLinearColor(CameraRelativeDensityUVOffset.X, CameraRelativeDensityUVOffset.Y, 0.0f, 0.0f));
	DynamicMaterial->SetVectorParameterValue(
		TEXT("NoiseOffset"),
		FLinearColor(CameraRelativeNoiseUVOffset.X, CameraRelativeNoiseUVOffset.Y, 0.0f, 0.0f));

	DynamicMaterial->SetScalarParameterValue(
		TEXT("UseFullScreenProxy"),
		bUseFullScreenProxy ? 1.0f : 0.0f);
	DynamicMaterial->SetScalarParameterValue(
		TEXT("DensityZSign"),
		DensityVolumeZSign);
}
