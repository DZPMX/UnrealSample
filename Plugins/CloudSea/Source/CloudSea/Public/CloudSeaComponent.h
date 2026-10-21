// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com

#pragma once
#include "Components/SceneComponent.h"
#include "CloudSeaComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
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
#endif

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool TryGetView(FSceneViewProjectionData& OutProjectionData) const;
	void InitializeMaterialDriver();
	void ApplyStaticMaterialParameters();
	void UpdateProxyVisibility(bool bHideForFade);
	void UpdateCloudSeaTransform();
	void UpdateViewMaterialParameters(
		const FVector& ViewLocation,
		bool bUseFullScreenProxy,
		double SignedCameraDepthBelowCloudTopMaterialUnits,
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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

public:
	/** Fixed world-space height of the cloud layer base, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (DisplayName = "Cloud Layer Base World Height (cm)", Units = "cm", ForceUnits = "cm"))
	double CloudLayerBaseWorldHeight = 15000;

	/** Positive thickness in material units. World thickness in cm = CloudLayerHeight * 100. */
	UPROPERTY(BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (ClampMin = "0.001", DisplayName = "Cloud Layer Thickness"))
	float CloudLayerHeight = 50.0f;

	/** Camera XY, in world centimeters, at which the authored UV offsets apply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (DisplayName = "Volume Anchor Reference Camera XY (cm)", Units = "cm", ForceUnits = "cm"))
	FVector2D VolumeAnchorReferenceCameraXY = FVector2D::ZeroVector;

	/** Maximum camera-centered quadratic sampling cells (16-64) across RayMarchActiveDistance. Clipped or opaque rays use fewer samples. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Cloud Sea|Ray March",
		meta = (ClampMin = "16", ClampMax = "64", UIMin = "16", UIMax = "64", DisplayName = "Ray March Step Count"))
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

	/** Density UV change per material unit along each axis; not a world distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density", meta = (DisplayName = "Density UV Scale"))
	FVector DensityVolumeUVScale = FVector(0.0015f, 0.0015f, 0.02f);

	/** Density XY offset in material units, before applying DensityVolumeUVScale. Multiply by 100 for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density",meta=(ClampMin = "0.0",ClampMax = "10000.0",UIMin = "0.0", UIMax = "10000.0", DisplayName = "Density Offset"))
	FVector2D DensityUVOffset = FVector2D(0.0f, 0.0f);

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

	/** RGB tint applied to atmosphere light 0 and both sky-light contributions. Alpha is unused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting", meta = (HideAlphaChannel, DisplayName = "Cloud Scattering Tint"))
	FLinearColor CloudScatteringTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Sky-light scale for the density texture's G channel. Does not directly affect prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting",meta=(ClampMin = "0.0",ClampMax = "1.0",UIMin = "0.0", UIMax = "1.0", DisplayName = "Ambient Light Scale"))
	float CloudDiffuseWeight = 1.0f;

	/** Sky-fill scale for the density texture's B channel, independent of the HG phase. Also affects prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting",meta=(ClampMin="0.0",UIMin="0.0", DisplayName = "Fill Light Scale"))
	float UnphasedLightWeight = 1.0f;

	/** Sun-light scale for the density texture's B channel, multiplied by the dual HG phase. Also affects prefill color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Lighting",meta=(ClampMin="0.0",UIMin="0.0", DisplayName = "Sun Light Scale"))
	float PhaseLightWeight = 1.0f;

	/** First HG lobe: positive G peaks toward the light; negative G peaks away from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "-0.99", ClampMax = "0.99", DisplayName = "Phase G"))
	float PhaseG = 0.6f;

	/** Second HG lobe, using the same atmosphere light direction as the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "-0.99", ClampMax = "0.99", DisplayName = "Phase G2"))
	float PhaseG2 = -0.3f;

	/** Blend between the two HG lobes: zero selects G; one selects G2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Phase Function", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Phase Blend"))
	float PhaseBlend = 0.2f;

	/** Selects mirrored rendering with prefill or single-layer rendering without prefill. Below-cloud fading applies to both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition")
	ECloudSeaVerticalRenderingMode VerticalRenderingMode =
		ECloudSeaVerticalRenderingMode::ReferenceMirrorWithPrefill;

	/** Vertical distance in cm below CloudLayerBaseWorldHeight at which the complete cloud starts fading. Applies to both rendering modes; scales final premultiplied color and opacity. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Cloud Sea|Transition",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", ForceUnits = "cm",
			DisplayName = "Below Cloud Fade Start Distance (cm)"))
	float BelowCloudFadeStartWorldDistance = 20000.0f;

	/** Vertical distance in cm below CloudLayerBaseWorldHeight at which the complete cloud is hidden. Fades linearly from the start distance. Zero disables fading; an end at or before the start hides immediately at the end distance. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Cloud Sea|Transition",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", ForceUnits = "cm",
			DisplayName = "Below Cloud Fade End Distance (cm)"))
	float BelowCloudFadeEndWorldDistance = 30000.0f;

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
