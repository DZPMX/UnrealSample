// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com

#include "CloudSeaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneView.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

namespace
{
	constexpr double WorldUnitsPerMaterialUnit = 100.0;
	constexpr double DiskHeightCompressionFactor = 10.0;
	constexpr double MaxDiskCameraHeightWorldDistance = 100000.0; // 1000 meters above the cloud top, in cm.
	constexpr double ProxyNearClipSafetyFactor = 1.5;
	constexpr double ProxyNearClipSafetyPadding = 10.0;
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
	// Recreate the transient MID and reapply component settings on registration.
	DynamicMaterial = nullptr;
	UpdateCloudSeaTransform();
}

void UCloudSeaComponent::OnUnregister()
{
	UpdateProxyVisibility(false);
	if (ProxyMeshComponent.IsValid() && ProxyMeshComponent->GetMaterial(0) == DynamicMaterial.Get())
	{
		ProxyMeshComponent->SetMaterial(0, nullptr);
	}
	DynamicMaterial = nullptr;
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

bool UCloudSeaComponent::TryGetView(FSceneViewProjectionData& OutProjectionData) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const UWorld& World = *GetWorld();

#if WITH_EDITOR
	// SIE and ejected PIE render through the level viewport while the player camera remains elsewhere.
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		FLevelEditorViewportClient& ViewportClient = *GCurrentLevelEditingViewportClient;
		if (ViewportClient.GetWorld() == &World && ViewportClient.IsPerspective())
		{
			if (ViewportClient.Viewport == nullptr
				|| ViewportClient.Viewport->GetSizeXY().X <= 0
				|| ViewportClient.Viewport->GetSizeXY().Y <= 0)
			{
				return false;
			}
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				ViewportClient.Viewport, ViewportClient.GetScene(), ViewportClient.EngineShowFlags)
				.SetRealtimeUpdate(ViewportClient.IsRealtime()));
			const FSceneView* View = ViewportClient.CalcSceneView(&ViewFamily);
			if (View == nullptr)
			{
				return false;
			}
			const FSceneView& SceneView = *View;
			OutProjectionData = SceneView.SceneViewInitOptions;
			return true;
		}
	}
#endif

	if (World.IsGameWorld())
	{
		const TObjectPtr<APlayerController> PlayerController = UGameplayStatics::GetPlayerController(this, CameraPlayerIndex);
		const TObjectPtr<ULocalPlayer> LocalPlayer = PlayerController != nullptr ? PlayerController->GetLocalPlayer() : nullptr;
		return LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr
			&& LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, OutProjectionData);
	}

	return false;
}

void UCloudSeaComponent::InitializeMaterialDriver()
{
	if (!ProxyMeshComponent.IsValid())
	{
		bProxyHiddenForFade = false;
		DynamicMaterial = nullptr;
		if (const TObjectPtr<AActor> Owner = GetOwner(); Owner != nullptr)
		{
			ProxyMeshComponent = Owner->FindComponentByClass<UStaticMeshComponent>();
		}
	}

	if (!ProxyMeshComponent.IsValid())
	{
		return;
	}

	if (ProxyMeshComponent->bSelectable)
	{
		// The cloud proxy is never selectable, including after this driver is removed.
		ProxyMeshComponent->bSelectable = false;
		ProxyMeshComponent->MarkRenderStateDirty();
	}

	const TObjectPtr<UMaterialInterface> SourceMaterial = bUseRaymarchDither
		? MaterialWithDither.Get()
		: MaterialWithoutDither.Get();
	if (SourceMaterial == nullptr)
	{
		return;
	}

	if (DynamicMaterial == nullptr || DynamicMaterial->Parent.Get() != SourceMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(SourceMaterial.Get(), this);
		DynamicMaterial->SetFlags(RF_Transient);
		ApplyStaticMaterialParameters();
	}
	ProxyMeshComponent->SetMaterial(0, DynamicMaterial.Get());
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
	DynamicMaterial->SetVectorParameterValue(
		TEXT("FlowDirection"),
		FLinearColor(FlowDirection.X, FlowDirection.Y, 0.0f, 0.0f));
}

void UCloudSeaComponent::UpdateProxyVisibility(bool bHideForFade)
{
	if (!ProxyMeshComponent.IsValid())
	{
		bProxyHiddenForFade = false;
		return;
	}
	if (bHideForFade == bProxyHiddenForFade)
	{
		return;
	}

#if WITH_EDITOR
	const TObjectPtr<UWorld> ProxyWorld = ProxyMeshComponent->GetWorld();
	if (ProxyWorld != nullptr && !ProxyWorld->IsGameWorld())
	{
		if (bHideForFade && !ProxyMeshComponent->ShouldRender())
		{
			return;
		}
		// Camera-driven editor hiding must not be serialized into the level.
		ProxyMeshComponent->SetIsTemporarilyHiddenInEditor(bHideForFade);
	}
	else
#endif
	{
		if (bHideForFade && !ProxyMeshComponent->GetVisibleFlag())
		{
			return;
		}
		ProxyMeshComponent->SetVisibility(!bHideForFade);
	}
	bProxyHiddenForFade = bHideForFade;
}

void UCloudSeaComponent::UpdateCloudSeaTransform()
{
	InitializeMaterialDriver();

	FSceneViewProjectionData ProjectionData;
	if (!TryGetView(ProjectionData))
	{
		return;
	}
	const FVector ViewLocation = ProjectionData.ViewOrigin;
	const float NearClipPlane = ProjectionData.GetNearPlaneFromProjectionMatrix();

	float CloudOpacity = 1.0f;
	if (BelowCloudFadeEndWorldDistance > 0.0f)
	{
		const double CameraDistanceBelowCloudBase = CloudLayerBaseWorldHeight - ViewLocation.Z;
		if (BelowCloudFadeEndWorldDistance > BelowCloudFadeStartWorldDistance)
		{
			const double FadeWorldDistance =
				static_cast<double>(BelowCloudFadeEndWorldDistance) - BelowCloudFadeStartWorldDistance;
			CloudOpacity = static_cast<float>(1.0 - FMath::Clamp(
				(CameraDistanceBelowCloudBase - BelowCloudFadeStartWorldDistance) / FadeWorldDistance,
				0.0,
				1.0));
		}
		else
		{
			CloudOpacity = CameraDistanceBelowCloudBase < BelowCloudFadeEndWorldDistance ? 1.0f : 0.0f;
		}
	}
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
	const float RayMarchDistanceMaterialUnits = FMath::Clamp(RayMarchActiveDistance, 1500.0f, 8000.0f);
	const bool bUseFullScreenProxy =
		ViewLocation.Z <= CloudLayerTopWorldHeight;
	const double MinimumProxyDistance =
		static_cast<double>(NearClipPlane) * ProxyNearClipSafetyFactor + ProxyNearClipSafetyPadding;

	FVector CloudSeaLocation;
	FQuat CloudSeaRotation;
	FVector CloudSeaScale;
	if (bUseFullScreenProxy)
	{
		const TObjectPtr<UStaticMesh> ProxyMesh = ProxyMeshComponent.IsValid() ? ProxyMeshComponent->GetStaticMesh() : nullptr;
		if (ProxyMesh == nullptr)
		{
			return;
		}
		const FBoxSphereBounds MeshBounds = ProxyMesh->GetBounds();
		const FIntRect& ViewRect = ProjectionData.GetConstrainedViewRect();
		const FMatrix& Projection = ProjectionData.ProjectionMatrix;
		if (MeshBounds.BoxExtent.X <= 0.0 || MeshBounds.BoxExtent.Y <= 0.0
			|| ViewRect.Width() <= 0 || ViewRect.Height() <= 0
			|| Projection.M[0][0] <= 0.0 || Projection.M[1][1] <= 0.0)
		{
			return;
		}
		const FVector ViewRight = ProjectionData.ViewRotationMatrix.GetColumn(0);
		const FVector ViewUp = ProjectionData.ViewRotationMatrix.GetColumn(1);
		const FVector ViewForward = ProjectionData.ViewRotationMatrix.GetColumn(2);
		const double FullScreenProxyDistance = FMath::Max(
			static_cast<double>(FullScreenProxyCameraDistance), MinimumProxyDistance);
		const FVector4 ProjectedCenter = Projection.TransformFVector4(FVector4(0.0, 0.0, FullScreenProxyDistance, 1.0));
		const double HalfWidth = ProjectedCenter.W / Projection.M[0][0];
		const double HalfHeight = ProjectedCenter.W / Projection.M[1][1];
		// Fit the view rectangle, with one pixel per edge for rounding and projection jitter.
		CloudSeaScale = FVector(
			HalfWidth * (1.0 + 2.0 / ViewRect.Width()) / MeshBounds.BoxExtent.X,
			HalfHeight * (1.0 + 2.0 / ViewRect.Height()) / MeshBounds.BoxExtent.Y,
			1.0);
		// Align both axes with the view (including roll), with the plane's +Z facing the camera.
		CloudSeaRotation = FRotationMatrix::MakeFromXZ(ViewRight, -ViewForward).ToQuat();
		CloudSeaLocation = ViewLocation + ViewForward * FullScreenProxyDistance
			- ViewRight * (ProjectedCenter.X / Projection.M[0][0])
			- ViewUp * (ProjectedCenter.Y / Projection.M[1][1])
			- CloudSeaRotation.RotateVector(CloudSeaScale * MeshBounds.Origin);
	}
	else
	{
		const double MaxDiskDistanceMaterialUnits = RayMarchDistanceMaterialUnits;
		// Clamp the real height before compression so the disk keeps its size above the limit.
		const double CompressedCameraDepthBelowCloudTopMaterialUnits = FMath::Max(
			SignedCameraDepthBelowCloudTopMaterialUnits,
			-MaxDiskCameraHeightWorldDistance / WorldUnitsPerMaterialUnit) / DiskHeightCompressionFactor;
		const double CloudSeaWorldRadius =
			DiskHeightCompressionFactor
			* FMath::Sqrt(
				FMath::Square(MaxDiskDistanceMaterialUnits)
				- FMath::Square(CompressedCameraDepthBelowCloudTopMaterialUnits))
			* WorldUnitsPerMaterialUnit;
		const double ReconstructedDiskProxyWorldOffset =
			CompressedCameraDepthBelowCloudTopMaterialUnits
			* WorldUnitsPerMaterialUnit
			* (DiskMarchFarDistance / RayMarchDistanceMaterialUnits);
		const double DiskProxyWorldOffset = FMath::Min(
			ReconstructedDiskProxyWorldOffset,
			-MinimumProxyDistance);
		const double DiskProxyWorldZ = ViewLocation.Z + DiskProxyWorldOffset;
		CloudSeaLocation = FVector(
			ViewLocation.X,
			ViewLocation.Y,
			DiskProxyWorldZ);
		CloudSeaRotation = FQuat::Identity;
		CloudSeaScale = FVector(CloudSeaWorldRadius, CloudSeaWorldRadius, 1.0);
	}

	const FTransform CloudSeaTransform(CloudSeaRotation, CloudSeaLocation, CloudSeaScale);
	{
		// Propagate location, rotation and scale together before updating material parameters.
		FScopedMovementUpdate ScopedMovementUpdate(this, EScopedUpdate::DeferredUpdates);
		SetWorldTransform(CloudSeaTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	UpdateViewMaterialParameters(
		ViewLocation,
		bUseFullScreenProxy,
		SignedCameraDepthBelowCloudTopMaterialUnits,
		RayMarchDistanceMaterialUnits);
	UpdateProxyVisibility(false);
}

void UCloudSeaComponent::UpdateViewMaterialParameters(
	const FVector& ViewLocation,
	bool bUseFullScreenProxy,
	double SignedCameraDepthBelowCloudTopMaterialUnits,
	float RayMarchDistanceMaterialUnits)
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
		CameraCloudTopDepth = FMath::Max(
			SignedCameraDepthBelowCloudTopMaterialUnits,
			-MaxDiskCameraHeightWorldDistance / WorldUnitsPerMaterialUnit) / DiskHeightCompressionFactor;
		// Restore the real camera-to-layer distance after compression and clamping.
		CloudDepthReconstructionOffset =
			SignedCameraDepthBelowCloudTopMaterialUnits - CameraCloudTopDepth;
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
			PrefillCoordinate = 1.0f - NormalizedDepthBelowBase;
			DensityVolumeZSign = -1.0f;
		}
		else
		{
			const float NormalizedDepthBelowTop = FMath::Clamp(
				CameraDepthBelowCloudTopMaterialUnits / CloudLayerHeight,
				0.0f,
				1.0f);
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
	DynamicMaterial->SetScalarParameterValue(TEXT("RayMarchMaxDistance"), RayMarchDistanceMaterialUnits);

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
