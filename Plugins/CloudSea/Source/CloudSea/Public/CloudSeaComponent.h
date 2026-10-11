// Copyright KuaiShou Games, Inc. All Rights Reserved.
// Author: lizhicheng@kuaishou.com

#pragma once
#include "Components/SceneComponent.h"
#include "CloudSeaComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UStaticMeshComponent;

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
	bool TryGetView(
		FVector& OutViewLocation,
		FRotator& OutViewRotation,
		float& OutNearClipPlane) const;
	void InitializeMaterialDriver();
	void ApplyStaticMaterialParameters();
	void UpdateProxyVisibility(bool bHideForFade);
	void UpdateCloudSeaTransform();
	void UpdateViewMaterialParameters(
		const FVector& ViewLocation,
		bool bUseFullScreenProxy,
		double SignedCameraDepthBelowCloudTopMaterialUnits,
		float ActiveDiskRayMarchDistanceMaterialUnits);

#if WITH_EDITOR
	void HandleEditorCameraMoved(
		const FVector& ViewLocation,
		const FRotator& ViewRotation,
		ELevelViewportType ViewportType,
		int32 ViewIndex);
#endif

	TWeakObjectPtr<UStaticMeshComponent> ProxyMeshComponent;
	TWeakObjectPtr<UStaticMeshComponent> FadeHiddenProxyMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalProxyMaterial;

public:
	/** Fixed world-space height of the cloud layer base, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (DisplayName = "Cloud Layer Base World Height (cm)", Units = "cm", ForceUnits = "cm"))
	double CloudLayerBaseWorldHeight = 15000;

	/** Positive thickness in material units. World thickness in cm = CloudLayerHeight * WorldUnitsPerMaterialUnit. */
	UPROPERTY(BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (ClampMin = "0.001", DisplayName = "Cloud Layer Thickness"))
	float CloudLayerHeight = 50.0f;

	/** Conversion from material distance to world cm. At 100, one material unit is 100 cm (1 meter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Layer", meta = (ClampMin = "0.001", DisplayName = "World Unit Scale"))
	float WorldUnitsPerMaterialUnit = 100.0;

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

	/** Selects a prepared MI; does not change the material's static UseRayMarchJitter parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Use Ray March Jitter"))
	bool bUseRaymarchDither = false;

	/** MI with UseRayMarchJitter disabled. If unset, uses the proxy's original material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Material Without Jitter"))
	TObjectPtr<UMaterialInstanceConstant> MaterialWithoutDither;

	/** MI with UseRayMarchJitter enabled. If unset, uses the proxy's original material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (DisplayName = "Material With Jitter"))
	TObjectPtr<UMaterialInstanceConstant> MaterialWithDither;

	/** March limit before camera-dependent transitions, in material units. Multiply by WorldUnitsPerMaterialUnit for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (ClampMin = "100",ClampMax="3000",UIMin="100", UIMax="3000", DisplayName = "Ray March Max Distance"))
	float RayMarchActiveDistance = 2500.0f;

	/** Inverse material-unit fade length at scene intersections. For positive values, fade length in cm = WorldUnitsPerMaterialUnit / value. Zero disables this fade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Ray March", meta = (ClampMin = "0.0", DisplayName = "Scene Depth Fade Inverse Distance"))
	float SceneDepthFadeReciprocalDistance = 0.05f;

	/** Extinction per material unit, multiplied by sampled density and ray length to obtain optical depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density", meta = (ClampMin = "0.0",ClampMax="1.0",UIMin = "0.0", UIMax = "1.0", DisplayName = "Extinction Scale"))
	float ExtinctionDensityScale = 0.05f;

	/** Density UV change per material unit along each axis; not a world distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density", meta = (DisplayName = "Density UV Scale"))
	FVector DensityVolumeUVScale = FVector(0.0015f, 0.0015f, 0.02f);

	/** Density XY offset in material units, before applying DensityVolumeUVScale. Multiply by WorldUnitsPerMaterialUnit for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Density",meta=(ClampMin = "0.0",ClampMax = "10000.0",UIMin = "0.0", UIMax = "10000.0", DisplayName = "Density Offset"))
	FVector2D DensityUVOffset = FVector2D(0.0f, 0.0f);

	/** Noise UV change per material unit; not a world distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise", meta = (DisplayName = "Noise UV Scale"))
	float NoiseVolumeUVScale = 0.01f;

	/** Noise displacement amplitude in material units, before applying the sampled noise and height factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise", meta = (DisplayName = "Noise Warp Amplitude"))
	float NoiseWarpAmplitude = 14.0f;

	/** Noise XY offset in material units, before applying NoiseVolumeUVScale. Multiply by WorldUnitsPerMaterialUnit for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Noise",meta=(ClampMin = "0.0",ClampMax = "10000.0",UIMin = "0.0", UIMax = "10000.0", DisplayName = "Noise Offset"))
	FVector2D NoiseUVOffset = FVector2D(0.0f, 0.0f);

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

	/** Vertical distance in cm below CloudLayerBaseWorldHeight over which the complete cloud fades linearly to hidden in either rendering mode. Zero disables fading. Only scales final opacity; does not change cloud geometry, prefill or raymarch settings. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Cloud Sea|Transition",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", ForceUnits = "cm",
			DisplayName = "Below Cloud Fade World Distance (cm)"))
	float BelowCloudFadeWorldDistance = 5000.0f;

	/** March distance at either edge of the mirrored full-screen transition, in material units. Multiply by WorldUnitsPerMaterialUnit for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition", meta = (DisplayName = "Full Screen Transition March Distance"))
	float FullScreenTransitionMarchDistance = 750.0f;

	/** Height above the cloud top over which the disk march distance reaches its limit, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Transition", meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "Disk Ray March Transition World Distance (cm)", Units = "cm", ForceUnits = "cm"))
	float DiskRayMarchTransitionWorldDistance = 1000.0f;

	/** Local player whose camera drives this component in a game world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Camera Player Index"))
	int32 CameraPlayerIndex = 0;

	/** Full-screen proxy radius in centimeters; the disk radius is derived per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Proxy World Radius (cm)", Units = "cm", ForceUnits = "cm"))
	double ProxyWorldRadius = 95000.0f;

	/** Distance in front of the camera used by the full-screen plane, in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Full Screen Proxy Camera Distance (cm)", Units = "cm", ForceUnits = "cm"))
	float FullScreenProxyCameraDistance = 300.0f;

	/** Reference distance used only to position the disk proxy, in material units. Multiply by WorldUnitsPerMaterialUnit for cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Sea|Camera and Proxy", meta = (DisplayName = "Disk March Far Distance"))
	float DiskMarchFarDistance = 2050.0f;
};
