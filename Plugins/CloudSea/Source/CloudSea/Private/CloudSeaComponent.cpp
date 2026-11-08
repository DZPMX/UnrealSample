// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com

#include "CloudSeaComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneInterface.h"
#include "SceneView.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

namespace
{
	constexpr double WorldUnitsPerMaterialUnit = 100.0;
	constexpr float ReferenceCloudLayerThickness = 50.0f;
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
	LastUpdateInputs.Reset();
	DynamicMaterial = nullptr;
	UpdateCloudSeaTransform();
}

void UCloudSeaComponent::OnUnregister()
{
	LastUpdateInputs.Reset();
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

void UCloudSeaComponent::SetCloudMaskPaintPreview(UTexture2D* Texture,
	const FVector2D& CenterWorldXY, const FVector2D& WorldSizeMeters)
{
	if (CloudMaskPaintPreview == Texture && CloudMaskPaintPreviewCenter == CenterWorldXY
		&& CloudMaskPaintPreviewSize == WorldSizeMeters) return;
	CloudMaskPaintPreview = Texture;
	CloudMaskPaintPreviewCenter = CenterWorldXY;
	CloudMaskPaintPreviewSize = WorldSizeMeters;
	ApplyStaticMaterialParameters();
	UpdateCloudSeaTransform();
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
			OutProjectionData = View->SceneViewInitOptions;
			return true;
		}
	}
#endif

	if (World.IsGameWorld())
	{
		const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, CameraPlayerIndex);
		const ULocalPlayer* LocalPlayer = PlayerController != nullptr ? PlayerController->GetLocalPlayer() : nullptr;
		return LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr
			&& LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, OutProjectionData);
	}

	return false;
}

float UCloudSeaComponent::GetEffectiveThicknessScale() const
{
	return FMath::Clamp(CloudThicknessScale, 0.0f, 2.0f);
}

float UCloudSeaComponent::GetUnscaledCloudProfileHeight() const
{
	return FMath::Max(CloudLayerHeight, 0.001f);
}

float UCloudSeaComponent::GetCloudProfileHeight() const
{
	// Zero scale is hidden before rendering. Keep material normalization finite even
	// while initializing its parameters or using an extremely small positive scale.
	return FMath::Max(GetUnscaledCloudProfileHeight() * GetEffectiveThicknessScale(), 0.001f);
}

float UCloudSeaComponent::GetEffectiveBottomExtension() const
{
	return FMath::Clamp(CloudBottomExtension, 0.0f, 500.0f) * GetEffectiveThicknessScale();
}

float UCloudSeaComponent::GetEffectiveBottomTransitionHeight() const
{
	return FMath::Clamp(CloudBottomTransitionHeight * GetEffectiveThicknessScale(), 0.001f, GetCloudProfileHeight());
}

float UCloudSeaComponent::GetEffectiveCloudLayerHeight() const
{
	return GetCloudProfileHeight() + GetEffectiveBottomExtension();
}

float UCloudSeaComponent::MapToCloudProfileHeight(float HeightAboveExtendedBase) const
{
	// Prefill follows the original top, not the repeated texture phase. The caller
	// clamps below-base heights so camera coverage never pulses at a loop seam.
	return HeightAboveExtendedBase - GetEffectiveBottomExtension();
}

bool UCloudSeaComponent::IsCloudMaskActive() const
{
	// Mirror/prefill retains its original behavior, even if a mask is assigned.
	bool bHasMask = CloudMaskTexture != nullptr;
#if WITH_EDITOR
	// A new mask can be painted before a persistent texture asset is created.
	bHasMask |= CloudMaskPaintPreview != nullptr;
#endif
	return bEnableCloudMask && bHasMask
		&& VerticalRenderingMode == ECloudSeaVerticalRenderingMode::SingleLayerWithoutPrefill;
}

void UCloudSeaComponent::InitializeMaterialDriver()
{
	if (!ProxyMeshComponent.IsValid())
	{
		LastUpdateInputs.Reset();
		bProxyHiddenForFade = false;
		DynamicMaterial = nullptr;
		if (AActor* Owner = GetOwner())
		{
			ProxyMeshComponent = Owner->FindComponentByClass<UStaticMeshComponent>();
		}
	}

	if (!ProxyMeshComponent.IsValid())
	{
		return;
	}

#if WITH_EDITOR
	if (ProxyMeshComponent->bSelectable)
	{
		// The cloud proxy is never selectable, including after this driver is removed.
		ProxyMeshComponent->bSelectable = false;
		ProxyMeshComponent->MarkRenderStateDirty();
	}
#endif

	UMaterialInterface* SourceMaterial = bUseRaymarchDither
		? MaterialWithDither.Get()
		: MaterialWithoutDither.Get();
	if (SourceMaterial == nullptr)
	{
		return;
	}

	if (DynamicMaterial == nullptr || DynamicMaterial->Parent.Get() != SourceMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
		DynamicMaterial->SetFlags(RF_Transient);
		ApplyStaticMaterialParameters();
	}
	ProxyMeshComponent->SetMaterial(0, DynamicMaterial.Get());
}

void UCloudSeaComponent::ApplyStaticMaterialParameters()
{
	// Property edits, preview changes and a new MID must refresh the view parameters too.
	LastUpdateInputs.Reset();
	if (DynamicMaterial == nullptr)
	{
		return;
	}

	UTexture2D* MaskTexture = CloudMaskTexture.Get();
#if WITH_EDITOR
	if (CloudMaskPaintPreview != nullptr)
	{
		MaskTexture = CloudMaskPaintPreview.Get();
	}
#endif
	if (MaskTexture != nullptr)
	{
		DynamicMaterial->SetTextureParameterValue(TEXT("CloudMaskTexture"), MaskTexture);
	}
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudMaskEnabled"), IsCloudMaskActive() ? 1.0f : 0.0f);

	const float LayerThickness = GetEffectiveCloudLayerHeight();
	FVector EffectiveDensityUVScale = DensityVolumeUVScale;
	// Keep the authored profile scale independent of additional thickness below it.
	EffectiveDensityUVScale.Z *= ReferenceCloudLayerThickness / GetCloudProfileHeight();

	DynamicMaterial->SetScalarParameterValue(
		TEXT("RayMarchStepCount"),
		static_cast<float>(FMath::Clamp(RayMarchStepCount, 16, 128)));
	DynamicMaterial->SetScalarParameterValue(TEXT("NoiseUVScale"), NoiseVolumeUVScale);
	DynamicMaterial->SetScalarParameterValue(TEXT("NoiseWarpAmplitude"), NoiseWarpAmplitude);
	DynamicMaterial->SetScalarParameterValue(TEXT("ExtinctionScale"), ExtinctionDensityScale);
	DynamicMaterial->SetScalarParameterValue(
		TEXT("SceneDepthFadeInvDistance"),
		SceneDepthFadeReciprocalDistance);
	DynamicMaterial->SetVectorParameterValue(TEXT("CloudScatteringTint"), CloudScatteringTint);
	DynamicMaterial->SetScalarParameterValue(TEXT("AmbientLightScale"), FMath::Clamp(CloudDiffuseWeight, 0.0f, 2.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseG"), PhaseG);
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseG2"), PhaseG2);
	DynamicMaterial->SetScalarParameterValue(TEXT("PhaseBlend"), PhaseBlend);
	DynamicMaterial->SetVectorParameterValue(TEXT("DensityUVScale"), FLinearColor(EffectiveDensityUVScale));
	DynamicMaterial->SetScalarParameterValue(TEXT("FillLightScale"), FMath::Clamp(UnphasedLightWeight, 0.0f, 2.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("SunLightScale"), FMath::Clamp(PhaseLightWeight, 0.0f, 2.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("MoonLightScale"), FMath::Clamp(MoonLightScale, 0.0f, 2.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudLayerThickness"), LayerThickness);
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudBottomExtension"), GetEffectiveBottomExtension());
	DynamicMaterial->SetScalarParameterValue(TEXT("CloudBottomTransitionHeight"), GetEffectiveBottomTransitionHeight());
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
	const UWorld* ProxyWorld = ProxyMeshComponent->GetWorld();
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

bool UCloudSeaComponent::FCloudSeaUpdateInputs::operator==(const FCloudSeaUpdateInputs& Other) const
{
	return ViewOrigin == Other.ViewOrigin
		&& ViewRotationMatrix == Other.ViewRotationMatrix
		&& ProjectionMatrix == Other.ProjectionMatrix
		&& ViewRect == Other.ViewRect
		&& ProxyComponent == Other.ProxyComponent
		&& ProxyMesh == Other.ProxyMesh
		&& Material == Other.Material
		&& MeshBoundsOrigin == Other.MeshBoundsOrigin
		&& MeshBoundsExtent == Other.MeshBoundsExtent
		&& CloudLayerBaseWorldHeight == Other.CloudLayerBaseWorldHeight
		&& CloudLayerHeight == Other.CloudLayerHeight
		&& CloudThicknessScale == Other.CloudThicknessScale
		&& CloudBottomExtension == Other.CloudBottomExtension
		&& bEnableCloudFade == Other.bEnableCloudFade
		&& CloudFadeStartWorldDistance == Other.CloudFadeStartWorldDistance
		&& CloudFadeRangeWorldDistance == Other.CloudFadeRangeWorldDistance
		&& RayMarchActiveDistance == Other.RayMarchActiveDistance
		&& bMobileRendering == Other.bMobileRendering
		&& bEnableMobileHeightCompression == Other.bEnableMobileHeightCompression
		&& MobileHeightCompressionFactor == Other.MobileHeightCompressionFactor
		&& bEnableMobileHeightLimit == Other.bEnableMobileHeightLimit
		&& MobileMaxCameraHeightWorldDistance == Other.MobileMaxCameraHeightWorldDistance
		&& FullScreenProxyCameraDistance == Other.FullScreenProxyCameraDistance
		&& DiskMarchFarDistance == Other.DiskMarchFarDistance
		&& VerticalRenderingMode == Other.VerticalRenderingMode
		&& VolumeAnchorReferenceCameraXY == Other.VolumeAnchorReferenceCameraXY
		&& DensityUVOffset == Other.DensityUVOffset
		&& NoiseUVOffset == Other.NoiseUVOffset
		&& MaskCenter == Other.MaskCenter
		&& MaskSize == Other.MaskSize;
}

UCloudSeaComponent::FCloudSeaUpdateInputs UCloudSeaComponent::BuildUpdateInputs(
	const FSceneViewProjectionData& ProjectionData) const
{
	UStaticMesh* ProxyMesh = ProxyMeshComponent.IsValid() ? ProxyMeshComponent->GetStaticMesh() : nullptr;
	const FBoxSphereBounds MeshBounds = ProxyMesh ? ProxyMesh->GetBounds() : FBoxSphereBounds(ForceInit);
	const UWorld* World = GetWorld();
	FCloudSeaUpdateInputs Inputs;
	Inputs.ViewOrigin = ProjectionData.ViewOrigin;
	Inputs.ViewRotationMatrix = ProjectionData.ViewRotationMatrix;
	Inputs.ProjectionMatrix = ProjectionData.ProjectionMatrix;
	Inputs.ViewRect = ProjectionData.GetConstrainedViewRect();
	Inputs.ProxyComponent = ProxyMeshComponent;
	Inputs.ProxyMesh = ProxyMesh;
	Inputs.Material = DynamicMaterial.Get();
	Inputs.MeshBoundsOrigin = MeshBounds.Origin;
	Inputs.MeshBoundsExtent = MeshBounds.BoxExtent;
	Inputs.CloudLayerBaseWorldHeight = CloudLayerBaseWorldHeight;
	Inputs.CloudLayerHeight = CloudLayerHeight;
	Inputs.CloudThicknessScale = CloudThicknessScale;
	Inputs.CloudBottomExtension = CloudBottomExtension;
	Inputs.bEnableCloudFade = bEnableCloudFade;
	Inputs.CloudFadeStartWorldDistance = CloudFadeStartWorldDistance;
	Inputs.CloudFadeRangeWorldDistance = BelowCloudFadeWorldDistance;
	Inputs.RayMarchActiveDistance = RayMarchActiveDistance;
	Inputs.bMobileRendering = World != nullptr && World->Scene != nullptr
		&& IsMobilePlatform(World->Scene->GetShaderPlatform());
	Inputs.bEnableMobileHeightCompression = bEnableMobileHeightCompression;
	Inputs.MobileHeightCompressionFactor = MobileHeightCompressionFactor;
	Inputs.bEnableMobileHeightLimit = bEnableMobileHeightLimit;
	Inputs.MobileMaxCameraHeightWorldDistance = MobileMaxCameraHeightWorldDistance;
	Inputs.FullScreenProxyCameraDistance = FullScreenProxyCameraDistance;
	Inputs.DiskMarchFarDistance = DiskMarchFarDistance;
	Inputs.VerticalRenderingMode = VerticalRenderingMode;
	Inputs.VolumeAnchorReferenceCameraXY = VolumeAnchorReferenceCameraXY;
	Inputs.DensityUVOffset = DensityUVOffset;
	Inputs.NoiseUVOffset = NoiseUVOffset;
	Inputs.MaskCenter = CloudMaskCenterWorldXY;
	Inputs.MaskSize = CloudMaskWorldSize;
#if WITH_EDITOR
	if (CloudMaskPaintPreview != nullptr)
	{
		Inputs.MaskCenter = CloudMaskPaintPreviewCenter;
		Inputs.MaskSize = CloudMaskPaintPreviewSize;
	}
#endif
	return Inputs;
}

void UCloudSeaComponent::UpdateCloudSeaTransform()
{
	InitializeMaterialDriver();

	if (GetEffectiveThicknessScale() <= 0.0f)
	{
		LastUpdateInputs.Reset();
		if (DynamicMaterial != nullptr)
		{
			DynamicMaterial->SetScalarParameterValue(TEXT("CloudOpacity"), 0.0f);
		}
		UpdateProxyVisibility(true);
		return;
	}

	FSceneViewProjectionData ProjectionData;
	if (!TryGetView(ProjectionData))
	{
		LastUpdateInputs.Reset();
		return;
	}

	// Read the actual projection before comparing: FOV, viewport and view modifiers can
	// change without camera movement. Never discard a later update just by frame number.
	const FCloudSeaUpdateInputs UpdateInputs = BuildUpdateInputs(ProjectionData);
	if (LastUpdateInputs.IsSet() && UpdateInputs == LastUpdateInputs.GetValue()
		&& GetComponentTransform().Equals(LastUpdatedComponentTransform, 0.0))
	{
		return;
	}
	// Only a completed visible update can be reused. Hidden/invalid paths must retry,
	// including when the camera returns to the last visible position.
	LastUpdateInputs.Reset();
	const FVector ViewLocation = ProjectionData.ViewOrigin;
	const float NearClipPlane = ProjectionData.GetNearPlaneFromProjectionMatrix();
	const float LayerThickness = GetEffectiveCloudLayerHeight();
	const double CloudLayerTopWorldHeight = CloudLayerBaseWorldHeight
		+ GetUnscaledCloudProfileHeight() * WorldUnitsPerMaterialUnit;
	// Use the real camera and fixed cloud top, before mobile sampling height adjustments.
	const double SignedCameraDepthBelowCloudTopWorldUnits = CloudLayerTopWorldHeight - ViewLocation.Z;
	// A positive start offset places the fade start above the top; fading proceeds downward.
	const double FadeStartHeightOffset = static_cast<double>(CloudFadeStartWorldDistance);
	const double FadeRange = static_cast<double>(BelowCloudFadeWorldDistance);
	const float CloudOpacity = bEnableCloudFade && FadeRange > 0.0
		? static_cast<float>(1.0 - FMath::Clamp(
			(SignedCameraDepthBelowCloudTopWorldUnits + FadeStartHeightOffset)
				/ FadeRange,
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

	const double SignedCameraDepthBelowCloudTopMaterialUnits =
		SignedCameraDepthBelowCloudTopWorldUnits / WorldUnitsPerMaterialUnit;
	const float RayMarchDistanceMaterialUnits = FMath::Clamp(RayMarchActiveDistance, 1500.0f, 8000.0f);
	const bool bUseFullScreenProxy =
		ViewLocation.Z <= CloudLayerTopWorldHeight;
	const UWorld* World = GetWorld();
	const bool bMobileRendering = World != nullptr && World->Scene != nullptr
		&& IsMobilePlatform(World->Scene->GetShaderPlatform());
	const double HeightCompressionFactor = bMobileRendering && bEnableMobileHeightCompression
		? FMath::Max(static_cast<double>(MobileHeightCompressionFactor), 1.0)
		: 1.0;

	// Share the same rendering height between the proxy and material. Desktop uses the real height.
	double RenderCameraDepthBelowCloudTopMaterialUnits = SignedCameraDepthBelowCloudTopMaterialUnits;
	if (bUseFullScreenProxy)
	{
		// Preserve the mirrored layer and prefill transition before compressing the gap below them.
		const double CompressionAnchorDepthBelowCloudTopMaterialUnits = LayerThickness
			* (VerticalRenderingMode == ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill ? 2.0 : 1.0);
		if (HeightCompressionFactor > 1.0
			&& RenderCameraDepthBelowCloudTopMaterialUnits > CompressionAnchorDepthBelowCloudTopMaterialUnits)
		{
			RenderCameraDepthBelowCloudTopMaterialUnits = CompressionAnchorDepthBelowCloudTopMaterialUnits
				+ (RenderCameraDepthBelowCloudTopMaterialUnits - CompressionAnchorDepthBelowCloudTopMaterialUnits)
					/ HeightCompressionFactor;
		}
	}
	else
	{
		if (bMobileRendering && bEnableMobileHeightLimit)
		{
			RenderCameraDepthBelowCloudTopMaterialUnits = FMath::Max(
				RenderCameraDepthBelowCloudTopMaterialUnits,
				-FMath::Max(static_cast<double>(MobileMaxCameraHeightWorldDistance), 1.0) / WorldUnitsPerMaterialUnit);
		}
		RenderCameraDepthBelowCloudTopMaterialUnits /= HeightCompressionFactor;
	}

	const double MinimumProxyDistance =
		static_cast<double>(NearClipPlane) * ProxyNearClipSafetyFactor + ProxyNearClipSafetyPadding;

	FVector CloudSeaLocation;
	FQuat CloudSeaRotation;
	FVector CloudSeaScale;
	if (bUseFullScreenProxy)
	{
		const UStaticMesh* ProxyMesh = ProxyMeshComponent.IsValid() ? ProxyMeshComponent->GetStaticMesh() : nullptr;
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
		const double DiskRadiusSquaredMaterialUnits = FMath::Square(MaxDiskDistanceMaterialUnits)
			- FMath::Square(RenderCameraDepthBelowCloudTopMaterialUnits);
		if (DiskRadiusSquaredMaterialUnits <= 0.0)
		{
			// No ray can reach the layer within the march limit. Avoid an invalid square root
			// when desktop heights or configurable mobile heights exceed that limit.
			UpdateProxyVisibility(true);
			return;
		}
		const double CloudSeaWorldRadius =
			HeightCompressionFactor
			* FMath::Sqrt(DiskRadiusSquaredMaterialUnits)
			* WorldUnitsPerMaterialUnit;
		const double ReconstructedDiskProxyWorldOffset =
			RenderCameraDepthBelowCloudTopMaterialUnits
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
		RenderCameraDepthBelowCloudTopMaterialUnits,
		RayMarchDistanceMaterialUnits);
	UpdateProxyVisibility(false);
	LastUpdateInputs = UpdateInputs;
	LastUpdatedComponentTransform = GetComponentTransform();
}

void UCloudSeaComponent::UpdateViewMaterialParameters(
	const FVector& ViewLocation,
	bool bUseFullScreenProxy,
	double SignedCameraDepthBelowCloudTopMaterialUnits,
	double RenderCameraDepthBelowCloudTopMaterialUnits,
	float RayMarchDistanceMaterialUnits)
{
	if (DynamicMaterial == nullptr)
	{
		return;
	}

	const float CameraDepthBelowCloudTopMaterialUnits =
		static_cast<float>(SignedCameraDepthBelowCloudTopMaterialUnits);
	const float LayerThickness = GetEffectiveCloudLayerHeight();
	const float CameraCloudTopDepth = static_cast<float>(RenderCameraDepthBelowCloudTopMaterialUnits);
	float CloudPrefillOpacity = 0.0f;
	// Restore the real depth for scene intersections and fog; desktop needs no correction.
	const float CloudDepthReconstructionOffset = static_cast<float>(
		SignedCameraDepthBelowCloudTopMaterialUnits - RenderCameraDepthBelowCloudTopMaterialUnits);
	const bool bUseReferenceMirrorTransition =
		VerticalRenderingMode == ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill;
	const float CameraDepthBelowCloudBaseMaterialUnits =
		-LayerThickness
		+ CameraDepthBelowCloudTopMaterialUnits;

	float DensityVolumeZSign = 1.0f;
	if (bUseFullScreenProxy && bUseReferenceMirrorTransition)
	{
		const bool bCameraBelowLayer = CameraDepthBelowCloudBaseMaterialUnits > 0.0f;

		float PrefillCoordinate;
		if (bCameraBelowLayer)
		{
			const float NormalizedDepthBelowBase = FMath::Clamp(
				CameraDepthBelowCloudBaseMaterialUnits / LayerThickness,
				0.0f,
				1.0f);
			PrefillCoordinate = 1.0f - NormalizedDepthBelowBase;
			DensityVolumeZSign = -1.0f;
		}
		else
		{
			const float NormalizedDepthBelowTop = FMath::Clamp(
				CameraDepthBelowCloudTopMaterialUnits / LayerThickness,
				0.0f,
				1.0f);
			PrefillCoordinate = NormalizedDepthBelowTop;
		}

		if (GetEffectiveBottomExtension() > 0.0f)
		{
			// Keep the prefill transition anchored to the original top in either orientation.
			// Repeating density below it must not repeat the camera's coverage transition.
			const float OrientedHeightAboveExtendedBase = bCameraBelowLayer
				? CameraDepthBelowCloudBaseMaterialUnits
				: LayerThickness - CameraDepthBelowCloudTopMaterialUnits;
			PrefillCoordinate = 1.0f - FMath::Clamp(
				MapToCloudProfileHeight(OrientedHeightAboveExtendedBase) / GetCloudProfileHeight(), 0.0f, 1.0f);
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

	// Subtract in double precision before sending camera-relative UVs to the shader.
	// RayDepthOffset reconstructs the real distance for mobile height compression too.
	FVector2D MaskCenter = CloudMaskCenterWorldXY;
	FVector2D MaskSize = CloudMaskWorldSize;
#if WITH_EDITOR
	if (CloudMaskPaintPreview != nullptr)
	{
		MaskCenter = CloudMaskPaintPreviewCenter;
		MaskSize = CloudMaskPaintPreviewSize;
	}
#endif
	const double InvMaskWidth = 1.0 / FMath::Max(MaskSize.X, 1.0);
	const double InvMaskHeight = 1.0 / FMath::Max(MaskSize.Y, 1.0);
	DynamicMaterial->SetVectorParameterValue(TEXT("CloudMaskMapping"), FLinearColor(
		(ViewLocation.X - MaskCenter.X) / WorldUnitsPerMaterialUnit * InvMaskWidth + 0.5,
		(ViewLocation.Y - MaskCenter.Y) / WorldUnitsPerMaterialUnit * InvMaskHeight + 0.5,
		InvMaskWidth, InvMaskHeight));

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
