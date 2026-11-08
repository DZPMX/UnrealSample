// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com

#pragma once
#include "Components/SceneComponent.h"
#include "Math/IntRect.h"
#include "Math/Matrix.h"
#include "Misc/Optional.h"
#include "CloudSeaComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UStaticMesh;
class UTexture2D;
struct FSceneViewProjectionData;

#if WITH_EDITOR
enum ELevelViewportType : int;
#endif

UENUM(BlueprintType)
enum class ECloudSeaVerticalRenderingMode : uint8
{
	ReferenceMirrorWithPrefill UMETA(DisplayName = "Reference Mirror with Prefill"),
	SingleLayerWithoutPrefill UMETA(DisplayName = "Single Layer without Prefill")
};


UCLASS(ClassGroup = (Rendering), BlueprintType, meta = (BlueprintSpawnableComponent))
class CLOUDSEA_API UCloudSeaComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCloudSeaComponent();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	/** Editor brush preview only; never serialized or used by cooked builds. */
	void SetCloudMaskPaintPreview(UTexture2D* Texture,
		const FVector2D& CenterWorldXY = FVector2D::ZeroVector,
		const FVector2D& WorldSizeMeters = FVector2D::ZeroVector);
#endif

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Exact inputs to the camera-driven update; not serialized with the component.
	struct FCloudSeaUpdateInputs
	{
		FVector ViewOrigin;
		FMatrix ViewRotationMatrix;
		FMatrix ProjectionMatrix;
		FIntRect ViewRect;
		TWeakObjectPtr<UStaticMeshComponent> ProxyComponent;
		TWeakObjectPtr<UStaticMesh> ProxyMesh;
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		FVector MeshBoundsOrigin;
		FVector MeshBoundsExtent;
		double CloudLayerBaseWorldHeight;
		float CloudLayerHeight;
		float CloudThicknessScale;
		float CloudBottomExtension;
		bool bEnableCloudFade;
		float CloudFadeStartWorldDistance;
		float CloudFadeRangeWorldDistance;
		float RayMarchActiveDistance;
		bool bMobileRendering;
		bool bEnableMobileHeightCompression;
		float MobileHeightCompressionFactor;
		bool bEnableMobileHeightLimit;
		float MobileMaxCameraHeightWorldDistance;
		float FullScreenProxyCameraDistance;
		float DiskMarchFarDistance;
		ECloudSeaVerticalRenderingMode VerticalRenderingMode;
		FVector2D VolumeAnchorReferenceCameraXY;
		FVector2D DensityUVOffset;
		FVector2D NoiseUVOffset;
		FVector2D MaskCenter;
		FVector2D MaskSize;

		bool operator==(const FCloudSeaUpdateInputs& Other) const;
	};

	FCloudSeaUpdateInputs BuildUpdateInputs(const FSceneViewProjectionData& ProjectionData) const;
	bool TryGetView(FSceneViewProjectionData& OutProjectionData) const;
	float GetEffectiveThicknessScale() const;
	float GetUnscaledCloudProfileHeight() const;
	float GetCloudProfileHeight() const;
	float GetEffectiveBottomExtension() const;
	float GetEffectiveBottomTransitionHeight() const;
	float GetEffectiveCloudLayerHeight() const;
	float MapToCloudProfileHeight(float HeightAboveExtendedBase) const;
	bool IsCloudMaskActive() const;
	void InitializeMaterialDriver();
	void ApplyStaticMaterialParameters();
	void UpdateProxyVisibility(bool bHideForFade);
	void UpdateCloudSeaTransform();
	void UpdateViewMaterialParameters(
		const FVector& ViewLocation,
		bool bUseFullScreenProxy,
		double SignedCameraDepthBelowCloudTopMaterialUnits,
		double RenderCameraDepthBelowCloudTopMaterialUnits,
		float RayMarchDistanceMaterialUnits);

#if WITH_EDITOR
	void HandleEditorCameraMoved(
		const FVector& ViewLocation,
		const FRotator& ViewRotation,
		ELevelViewportType ViewportType,
		int32 ViewIndex);
#endif

	TWeakObjectPtr<UStaticMeshComponent> ProxyMeshComponent;
	bool bProxyHiddenForFade = false;
	TOptional<FCloudSeaUpdateInputs> LastUpdateInputs;
	FTransform LastUpdatedComponentTransform;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	TObjectPtr<UTexture2D> CloudMaskPaintPreview;

	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FVector2D CloudMaskPaintPreviewCenter = FVector2D::ZeroVector;

	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FVector2D CloudMaskPaintPreviewSize = FVector2D::ZeroVector;
#endif

public:
	/** Base of the unscaled cloud profile, in cm. Cloud top stays at this height + CloudLayerHeight * 100. Thickness Scale moves the bottom, including the extension, relative to that fixed top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (DisplayName = "Cloud Layer Reference Base World Height (cm)", Units = "cm", ForceUnits = "cm"))
	double CloudLayerBaseWorldHeight = 15000;

	/** Original profile thickness in meters, before Thickness Scale. Retained for asset compatibility. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer",
		meta = (Units = "m", ForceUnits = "m", DisplayName = "Cloud Profile Thickness (m)"))
	float CloudLayerHeight = 50.0f;

	/** Scales the full layer thickness, including Bottom Extension, around the fixed cloud top. One preserves authored thickness; zero hides the entire cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer",
		meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Thickness Scale"))
	float CloudThicknessScale = 1.0f;

	/** Additional thickness (0-500 meters before Thickness Scale), filled by overlapping repeats of the density texture's lower half. Zero preserves the original profile. Changing extension alone keeps the cloud top and texture scales fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer",
		meta = (ClampMin = "0.0", ClampMax = "500.0", UIMin = "0.0", UIMax = "500.0", Units = "m", ForceUnits = "m", DisplayName = "Bottom Extension (m)"))
	float CloudBottomExtension = 0.0f;

	/** Cross-fade height in meters before Thickness Scale, at loop seams and the final bottom cap. Internally limited to one quarter of the repeated texture interval. The property name is retained for asset compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "Cloud Sea|Layer",
		meta = (ClampMin = "0.001", UIMin = "0.1", Units = "m", ForceUnits = "m", EditCondition = "CloudBottomExtension > 0.0",
			DisplayName = "Bottom Loop Blend Height (m)"))
	float CloudBottomTransitionHeight = 10.0f;

	/** Camera XY, in world centimeters, at which the authored UV offsets apply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (DisplayName = "Volume Anchor Reference Camera XY (cm)", Units = "cm", ForceUnits = "cm"))
	FVector2D VolumeAnchorReferenceCameraXY = FVector2D::ZeroVector;

	/** Compress the camera-to-layer gap only on mobile rendering platforms, including editor mobile preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mobile Height", meta = (DisplayName = "Enable Mobile Height Compression"))
	bool bEnableMobileHeightCompression = true;

	/** Divisor for the camera-to-layer gap on mobile. One uses the real gap. Does not change the cloud layer's world height or thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mobile Height",
		meta = (ClampMin = "1.0", UIMin = "1.0", EditCondition = "bEnableMobileHeightCompression", DisplayName = "Mobile Height Compression Factor"))
	float MobileHeightCompressionFactor = 10.0f;

	/** Limit the camera height used by the above-cloud disk on mobile, before compression. Real depth is still reconstructed for scene intersections and fog. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mobile Height", meta = (DisplayName = "Enable Mobile Height Limit"))
	bool bEnableMobileHeightLimit = true;

	/** Maximum real camera distance above the cloud top used for mobile disk sampling, in centimeters. Does not limit cameras inside or below the layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mobile Height",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm", ForceUnits = "cm", EditCondition = "bEnableMobileHeightLimit",
			DisplayName = "Mobile Max Camera Height Above Cloud Top (cm)"))
	float MobileMaxCameraHeightWorldDistance = 100000.0f;

	/** Maximum camera-centered quadratic sampling cells (16-128) across RayMarchActiveDistance. Clipped or opaque rays use fewer samples. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Cloud Sea|Ray March",
		meta = (ClampMin = "16", ClampMax = "128", UIMin = "16", UIMax = "128", DisplayName = "Ray March Step Count"))
	int32 RayMarchStepCount = 32;

	/** PC only. Selects the assigned material; does not change the material's static UseRayMarchJitter parameter. Mobile shaders use a fixed sample offset regardless of this setting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Use Ray March Jitter (PC Only)"))
	bool bUseRaymarchDither = false;

	/** Source material or MI with UseRayMarchJitter disabled. Creates a transient component-owned MID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Material Without Jitter"))
	TObjectPtr<UMaterialInterface> MaterialWithoutDither;

	/** Source material or MI with UseRayMarchJitter enabled. Creates a transient component-owned MID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Material With Jitter"))
	TObjectPtr<UMaterialInterface> MaterialWithDither;

	/** Fixed march limit shared by both proxy paths, in material units, with a runtime range of 1500-8000. Multiply by 100 for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (ClampMin = "1500",ClampMax="8000",UIMin="1500", UIMax="8000", DisplayName = "Ray March Max Distance"))
	float RayMarchActiveDistance = 2500.0f;

	/** Inverse material-unit fade length at scene intersections. For positive values, fade length in cm = 100 / value. Zero disables this fade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (ClampMin = "0.0", DisplayName = "Scene Depth Fade Inverse Distance"))
	float SceneDepthFadeReciprocalDistance = 0.05f;

	/** Extinction per material unit, multiplied by sampled density and ray length to obtain optical depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density", meta = (ClampMin = "0.0",ClampMax="1.0",UIMin = "0.0", UIMax = "1.0", DisplayName = "Extinction Scale"))
	float ExtinctionDensityScale = 0.05f;

	/** Density UV change per material unit. Z uses 50 / (CloudLayerHeight * Thickness Scale); changing Bottom Extension alone never changes the texture scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density", meta = (DisplayName = "Density UV Scale"))
	FVector DensityVolumeUVScale = FVector(0.0015f, 0.0015f, 0.02f);

	/** Density XY offset in material units, before applying DensityVolumeUVScale. Multiply by 100 for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density",meta=(ClampMin = "0.0",ClampMax = "10000.0",UIMin = "0.0", UIMax = "10000.0", DisplayName = "Density Offset"))
	FVector2D DensityUVOffset = FVector2D(0.0f, 0.0f);

	/** World XY density mask, supported only in Single Layer without Prefill. White retains cloud, black removes it through the entire layer. Outside the rectangle, cloud is retained. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mask", meta = (DisplayName = "Enable Cloud Mask"))
	bool bEnableCloudMask = false;

	/** Linear grayscale texture authored with the component's viewport painting tool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mask", meta = (EditCondition = "bEnableCloudMask", DisplayName = "Cloud Mask Texture"))
	TObjectPtr<UTexture2D> CloudMaskTexture;

	/** Center of the mask rectangle in world centimeters. Independent of cloud flow, actor position and camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mask", meta = (EditCondition = "bEnableCloudMask", Units = "cm", ForceUnits = "cm", DisplayName = "Mask Center World XY (cm)"))
	FVector2D CloudMaskCenterWorldXY = FVector2D::ZeroVector;

	/** Full width along world X and Y, in meters. The image U axis follows +X and V follows +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Mask", meta = (EditCondition = "bEnableCloudMask", ClampMin = "1.0", Units = "m", ForceUnits = "m", DisplayName = "Mask World Size (m)"))
	FVector2D CloudMaskWorldSize = FVector2D(500.0, 500.0);

	/** Noise UV change per material unit; not a world distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise", meta = (DisplayName = "Noise UV Scale"))
	float NoiseVolumeUVScale = 0.01f;

	/** Noise displacement amplitude in material units, before applying the sampled noise and height factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise", meta = (DisplayName = "Noise Warp Amplitude"))
	float NoiseWarpAmplitude = 14.0f;

	/** Noise XY offset in material units, before applying NoiseVolumeUVScale. Multiply by 100 for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise",meta=(ClampMin = "0.0",ClampMax = "10000.0",UIMin = "0.0", UIMax = "10000.0", DisplayName = "Noise Offset"))
	FVector2D NoiseUVOffset = FVector2D(0.0f, 0.0f);

	/** Visible cloud motion in world XY, shared by density and noise. Dimensionless and not normalized: each component sets material units per second. (1, 0) moves along +X; (0, 0) stops. Default (-1, -1) preserves the original motion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Flow", meta = (DisplayName = "Flow Direction (World XY)"))
	FVector2D FlowDirection = FVector2D(-1.0f, -1.0f);

	/** RGB tint applied to both atmosphere lights and both sky-light contributions. Alpha is unused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (HideAlphaChannel, DisplayName = "Cloud Scattering Tint"))
	FLinearColor CloudScatteringTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Sky-light scale for the density texture's G channel. Does not directly affect prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Ambient Light Scale"))
	float CloudDiffuseWeight = 1.0f;

	/** Sky-fill scale for the density texture's B channel, independent of the HG phase. Also affects prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Fill Light Scale"))
	float UnphasedLightWeight = 1.0f;

	/** Shared sun and moon scale for the density texture's B channel, using each light's dual HG phase. Also affects prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Sun Light Scale"))
	float PhaseLightWeight = 1.0f;

	/** Additional multiplier for atmosphere light 1 (moon) before the shared sun/moon scale. Also affects prefill color. One preserves the original moon contribution. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", DisplayName = "Moon Light Scale"))
	float MoonLightScale = 1.0f;

	/** First HG lobe: positive G peaks toward the light; negative G peaks away from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "-0.99", ClampMax = "0.99", DisplayName = "Phase G"))
	float PhaseG = 0.6f;

	/** Second HG lobe, using the same atmosphere light direction as the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "-0.99", ClampMax = "0.99", DisplayName = "Phase G2"))
	float PhaseG2 = -0.3f;

	/** Blend between the two HG lobes: zero selects G; one selects G2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Phase Blend"))
	float PhaseBlend = 0.2f;

	/** Selects mirrored rendering with prefill or single-layer rendering without prefill. Cloud-top-relative fading applies to both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition")
	ECloudSeaVerticalRenderingMode VerticalRenderingMode =
		ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill;

	/** Enables camera-height fading from Cloud Fade Start Distance downward over Cloud Fade Range. Disabling restores full fade opacity while preserving the configured distances. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition",
		meta = (DisplayName = "Enable Cloud Fade"))
	bool bEnableCloudFade = true;

	/** Signed vertical offset from the cloud top where fading starts, in cm. Positive is above the top; negative is below it. Full opacity at and above this height; fades only downward over Cloud Fade Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition",
		meta = (EditCondition = "bEnableCloudFade", Units = "cm", ForceUnits = "cm", DisplayName = "Cloud Fade Start Distance (cm)"))
	float CloudFadeStartWorldDistance = 0.0f;

	/** Downward fade interval length in cm, measured from Cloud Fade Start Distance. Fully hidden this far below the start. Zero disables fading; negative legacy values also disable it. The property name is retained for serialized asset and Blueprint compatibility; existing stored values are interpreted as range lengths. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition",
		meta = (EditCondition = "bEnableCloudFade", ClampMin = "0.0", UIMin = "0.0", Units = "cm", ForceUnits = "cm", DisplayName = "Cloud Fade Range (cm)"))
	float BelowCloudFadeWorldDistance = 5000.0f;

	/** Local player whose camera drives this component in a game world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Camera Player Index"))
	int32 CameraPlayerIndex = 0;

	/** Requested distance in front of the camera for the full-screen plane, in centimeters. Increased to at least 1.5 times the effective near clip distance plus 10 cm when needed. Plane dimensions fit the current view projection automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Full Screen Proxy Camera Distance (cm)", Units = "cm", ForceUnits = "cm"))
	float FullScreenProxyCameraDistance = 300.0f;

	/** Reference distance used only to position the disk proxy, in material units. Multiply by 100 for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Disk March Far Distance"))
	float DiskMarchFarDistance = 2050.0f;
};
