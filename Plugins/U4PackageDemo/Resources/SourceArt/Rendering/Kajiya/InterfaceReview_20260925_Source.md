# Kajiya 接口 review：当前源代码快照

2026-09-25。本附录是完整文件快照，不是简化算法。外围 renderer 的完整本轮 diff 见证据目录 Final.diff。

## Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionKajiyaHairOutput.h

SHA-256: `b9864d356d0b72d88bb55ebd087a908e8c004a73b8f3694506e10d5255bb69bf`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionKajiyaHairOutput.generated.h"

/** Explicit surface and independent primary/secondary lobe inputs for Kajiya hair. */
UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionKajiyaHairOutput : public UMaterialExpressionCustomOutput
{
	GENERATED_UCLASS_BODY()

	/** World-space surface normal. Default: (0, 0, 1). No implicit normalization. */
	UPROPERTY()
	FExpressionInput SurfaceNormal;

	/** World-space strand direction. Default: (0, 1, 0). */
	UPROPERTY()
	FExpressionInput StrandDirection;

	/** Linear RGB base color. Default: black. */
	UPROPERTY()
	FExpressionInput LinearBaseColor;

	/** Primary lobe strength in [0, 1]. Default: 0. */
	UPROPERTY()
	FExpressionInput PrimarySpecular;

	/** Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4. */
	UPROPERTY()
	FExpressionInput PrimaryRoughness;

	/** Primary strand shift in [-0.5, 0.5]. Default: 0. */
	UPROPERTY()
	FExpressionInput PrimaryShift;

	/** Secondary lobe strength in [0, 1]. Default: 0. */
	UPROPERTY()
	FExpressionInput SecondarySpecular;

	/** Secondary lobe roughness in [0, 1]. Independent default: 0.4. */
	UPROPERTY()
	FExpressionInput SecondaryRoughness;

	/** Secondary strand shift in [-0.5, 0.5]. Default: 0. */
	UPROPERTY()
	FExpressionInput SecondaryShift;

	/** Material ambient occlusion in [0, 1]. Default: 1 (unoccluded). */
	UPROPERTY()
	FExpressionInput AmbientOcclusion;

	/** Backlighting strength in [0, 1]. Default: 0. */
	UPROPERTY()
	FExpressionInput Backlit;

public:
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;
#endif
	virtual int32 GetNumOutputs() const override { return 11; }
	virtual FString GetFunctionName() const override { return TEXT("GetKajiyaHairOutput"); }
	virtual FString GetDisplayName() const override { return TEXT("Kajiya Hair Forward"); }
};

```

## Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionKajiyaHairOutput.cpp

SHA-256: `71e613c52e9a3c74189d10c365499b13ae1b103a3674ac66122573fc6245cd12`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialExpressionKajiyaHairOutput.h"
#include "MaterialCompiler.h"

UMaterialExpressionKajiyaHairOutput::UMaterialExpressionKajiyaHairOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	Outputs.Reset();
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionKajiyaHairOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// The inherited material compiler API supplies a required, non-null borrow.
	FMaterialCompiler& MaterialCompiler = *Compiler;
	int32 CodeInput = INDEX_NONE;
	switch (OutputIndex)
	{
	case 0:
		CodeInput = SurfaceNormal.IsConnected() ? SurfaceNormal.Compile(Compiler) : MaterialCompiler.Constant3(0.0f, 0.0f, 1.0f);
		break;
	case 1:
		CodeInput = StrandDirection.IsConnected() ? StrandDirection.Compile(Compiler) : MaterialCompiler.Constant3(0.0f, 1.0f, 0.0f);
		break;
	case 2:
		CodeInput = LinearBaseColor.IsConnected() ? LinearBaseColor.Compile(Compiler) : MaterialCompiler.Constant3(0.0f, 0.0f, 0.0f);
		break;
	case 3:
		CodeInput = PrimarySpecular.IsConnected() ? PrimarySpecular.Compile(Compiler) : MaterialCompiler.Constant(0.0f);
		break;
	case 4:
		CodeInput = PrimaryRoughness.IsConnected() ? PrimaryRoughness.Compile(Compiler) : MaterialCompiler.Constant(0.4f);
		break;
	case 5:
		CodeInput = PrimaryShift.IsConnected() ? PrimaryShift.Compile(Compiler) : MaterialCompiler.Constant(0.0f);
		break;
	case 6:
		CodeInput = SecondarySpecular.IsConnected() ? SecondarySpecular.Compile(Compiler) : MaterialCompiler.Constant(0.0f);
		break;
	case 7:
		CodeInput = SecondaryRoughness.IsConnected() ? SecondaryRoughness.Compile(Compiler) : MaterialCompiler.Constant(0.4f);
		break;
	case 8:
		CodeInput = SecondaryShift.IsConnected() ? SecondaryShift.Compile(Compiler) : MaterialCompiler.Constant(0.0f);
		break;
	case 9:
		CodeInput = AmbientOcclusion.IsConnected() ? AmbientOcclusion.Compile(Compiler) : MaterialCompiler.Constant(1.0f);
		break;
	case 10:
		CodeInput = Backlit.IsConnected() ? Backlit.Compile(Compiler) : MaterialCompiler.Constant(0.0f);
		break;
	}
	return MaterialCompiler.CustomOutput(this, OutputIndex, CodeInput);
}

void UMaterialExpressionKajiyaHairOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Kajiya Hair Forward"));
}

EMaterialValueType UMaterialExpressionKajiyaHairOutput::GetInputValueType(int32 InputIndex)
{
	return InputIndex < 3 ? MCT_Float3 : MCT_Float1;
}
#endif

```

## Engine/Shaders/Private/KajiyaHair.ush

SHA-256: `974dc99f9d2e225bcd0b91f92cb210b739d0ab9e5a224ab81224f01c49dbd697`

```cpp
// Kajiya hair lighting for opaque forward hair cards.
// All directions are world-space. SurfaceNormal controls diffuse/backlighting;
// StrandDirection controls the longitudinal specular lobes.
#pragma once

struct FKajiyaHairData
{
	float3 SurfaceNormal;
	float3 StrandDirection;
	float3 BaseColor;
	float PrimarySpecular;
	float PrimaryRoughness;
	float PrimaryShift;
	float SecondarySpecular;
	float SecondaryRoughness;
	float SecondaryShift;
	float AmbientOcclusion;
	float Backlit;
};

float KajiyaHairLuminance(float3 Color)
{
	return dot(Color, float3(0.30f, 0.59f, 0.11f));
}

// Continuous source-forward environment arithmetic (35716), with UE-provided
// SH/probe inputs and visibility. This is not the byte-encoded deferred path.
struct FKajiyaHairEnvironment
{
	float3 Direction;
	float3 Base;
	float3 Directional;
	float3 Diffuse;
	float3 PrimarySpecular;
	float3 SecondarySpecular;
	float3 Specular;
	float SpecularOcclusion;
	float Gate;
};

float KajiyaHairEnvironmentLobe(float3 T, float3 N, float Shift, float Roughness, float3 H)
{
	float3 Offset = Shift * N;
	Offset *= 2.0f;
	float3 Shifted = T + Offset;
	float3 Strand = Shifted * rsqrt(dot(Shifted, Shifted));
	float Exponent = exp2(9.0f - Roughness * 10.0f);
	float StrandDotHalf = dot(Strand, H);
	float SinThetaSquared = saturate(1.0f - StrandDotHalf * StrandDotHalf);
	float DirectionAttenuation = min(max(StrandDotHalf + 1.0f, 0.0f), 1.0f);
	return DirectionAttenuation * exp2(Exponent * log2(SinThetaSquared));
}

FKajiyaHairEnvironment GetKajiyaHairEnvironment(
	FKajiyaHairData Hair, float3 V, float3 VertexSH, float3 D, float3 ProbeColor,
	bool bFrontFace, float DirectionVisibility)
{
	FKajiyaHairEnvironment Result = (FKajiyaHairEnvironment)0;
	Result.Direction = D;
	// The Sky adapter uses instance/directional/specular scales 1 and base fill 0.
	Result.Directional = min(ProbeColor, VertexSH);
	float Projection = saturate(dot(D, Hair.SurfaceNormal));
	float3 Removed = Result.Directional * Projection;
	Result.Base = VertexSH - Removed;
	if (!bFrontFace)
	{
		float BackProjection = saturate(dot(Hair.SurfaceNormal, D));
		Result.Base = Result.Base + Result.Directional * BackProjection;
		Result.Directional = 0;
	}

	float NormalWeight = saturate(dot(Hair.SurfaceNormal, Hair.SurfaceNormal));
	float BaseWeight = Hair.AmbientOcclusion * NormalWeight;
	float NoD = dot(Hair.SurfaceNormal, D);
	float DirectionWeight = Hair.AmbientOcclusion * saturate(NoD);
	Result.Diffuse = Hair.BaseColor * (Result.Base * BaseWeight + Result.Directional * DirectionWeight);

	float NoV = dot(Hair.SurfaceNormal, V);
	float LogVisibility = log2(Hair.AmbientOcclusion + NoV);
	float ViewVisibility = exp2(Hair.PrimaryRoughness * LogVisibility);
	float MinusOne = ViewVisibility + (-1.0f);
	Result.SpecularOcclusion = saturate(Hair.AmbientOcclusion + MinusOne);
	// UE indirect occlusion is an adapter, not the original directional shadow G.
	// No SSR RGB/alpha or local bounce is supplied by this first-version adapter.
	Result.Gate = DirectionVisibility * Result.SpecularOcclusion;
	if (Result.Gate > 0.001f && any(Result.Directional > 0.0f))
	{
		// A zero-color UE probe has no meaningful direction, so it cannot produce a lobe.
		float3 HalfSum = D + V;
		float3 H = HalfSum * rsqrt(dot(HalfSum, HalfSum));
		float3 BaseMinusOne = Hair.BaseColor + (-1.0f);
		float3 PrimaryTint = BaseMinusOne * 0.2f + 1.0f;
		float3 PrimaryAmplitude = Hair.PrimarySpecular * PrimaryTint;
		PrimaryAmplitude *= 0.8f;
		float SecondaryWeight = KajiyaHairLuminance(Hair.BaseColor) * 0.3f + 0.6f;
		float3 SecondaryTint = BaseMinusOne * SecondaryWeight + 1.0f;
		float3 SecondaryAmplitude = Hair.SecondarySpecular * SecondaryTint;
		float PrimaryLobe = KajiyaHairEnvironmentLobe(Hair.StrandDirection, Hair.SurfaceNormal, Hair.PrimaryShift, Hair.PrimaryRoughness, H);
		float SecondaryLobe = KajiyaHairEnvironmentLobe(Hair.StrandDirection, Hair.SurfaceNormal, Hair.SecondaryShift, Hair.SecondaryRoughness, H);
		float3 Primary = PrimaryAmplitude * PrimaryLobe;
		float3 Secondary = SecondaryAmplitude * SecondaryLobe;
		float Angle = saturate(NoD * 0.75f + 0.25f);
		float AOAngle = Hair.AmbientOcclusion * Angle;
		float HalfVisibility = 0.5f * AOAngle;
		float Visibility = Result.Gate * HalfVisibility;
		Result.Specular = Result.Directional * ((Primary + Secondary) * Visibility);
		Result.PrimarySpecular = Result.Directional * (Primary * Visibility);
		Result.SecondarySpecular = Result.Directional * (Secondary * Visibility);
	}
	return Result;
}

float KajiyaHairPow10(float Value)
{
	float Squared = Value * Value;
	float Fourth = Squared * Squared;
	return Fourth * Fourth * Squared;
}

float3 KajiyaHairBacklit(FKajiyaHairData Hair, float3 LightDirection, float3 ViewDirection, float Attenuation)
{
	float NoV = max(1e-7f, dot(Hair.SurfaceNormal, ViewDirection));
	float NoL = saturate(dot(Hair.SurfaceNormal, LightDirection));
	float3 BacklightDirection = -(LightDirection + Hair.SurfaceNormal * 0.3f);
	float Alignment = saturate(dot(ViewDirection, BacklightDirection));
	float ViewGrazing = 1.0f - NoV;

	// Forward transmission plus a view-dependent rim, both with a tenth power.
	float Transmission = 3.0f * ViewGrazing * (1.0f - NoL) * KajiyaHairPow10(Alignment);
	float Rim = 0.5f * KajiyaHairPow10(ViewGrazing);
	float ColorWeight = KajiyaHairLuminance(Hair.BaseColor) * 0.7f + 0.3f;
	float3 BacklightColor = lerp(float3(0.992f, 0.808f, 0.518f), Hair.BaseColor, 0.5f);
	return Attenuation * Hair.Backlit * ColorWeight * (Transmission + Rim) * BacklightColor;
}

float KajiyaHairSpecularLobe(float3 StrandDirection, float3 HalfVector, float Exponent)
{
	float StrandDotHalf = dot(normalize(StrandDirection), HalfVector);
	float SinThetaSquared = saturate(1.0f - StrandDotHalf * StrandDotHalf);
	float DirectionalAttenuation = saturate(StrandDotHalf + 1.0f);
	return pow(SinThetaSquared, Exponent) * DirectionalAttenuation;
}

float3 KajiyaHairSpecular(FKajiyaHairData Hair, float3 LightDirection, float3 ViewDirection, float Attenuation)
{
	float3 HalfVector = normalize(LightDirection + ViewDirection);
	float3 PrimaryTint = lerp(1.0f, Hair.BaseColor, 0.2f);
	float SecondaryColorWeight = KajiyaHairLuminance(Hair.BaseColor) * 0.3f + 0.6f;
	float3 SecondaryTint = lerp(1.0f, Hair.BaseColor, SecondaryColorWeight);
	float PrimaryExponent = exp2(9.0f - 10.0f * Hair.PrimaryRoughness);
	float SecondaryExponent = exp2(9.0f - 10.0f * Hair.SecondaryRoughness);

	// Shifts retain the reference's decoded half-range [-0.5, 0.5].
	float3 PrimaryStrand = Hair.StrandDirection + Hair.SurfaceNormal * (2.0f * Hair.PrimaryShift);
	float3 SecondaryStrand = Hair.StrandDirection + Hair.SurfaceNormal * (2.0f * Hair.SecondaryShift);
	float PrimaryLobe = KajiyaHairSpecularLobe(PrimaryStrand, HalfVector, PrimaryExponent);
	float SecondaryLobe = KajiyaHairSpecularLobe(SecondaryStrand, HalfVector, SecondaryExponent);
	float3 PrimarySpecular = Hair.PrimarySpecular * 0.8f * PrimaryTint * PrimaryLobe;
	float3 SecondarySpecular = Hair.SecondarySpecular * SecondaryTint * SecondaryLobe;

	float FrontLighting = saturate(dot(Hair.SurfaceNormal, LightDirection) * 0.75f + 0.25f);
	float Visibility = 0.5f * Hair.AmbientOcclusion * Attenuation * FrontLighting;
	return Visibility * (SecondarySpecular + PrimarySpecular);
}

// Unshadowed local lights bypass this tint/occlusion term in the reference.
float3 KajiyaHairLocalVisibility(FKajiyaHairData Hair, float3 LightAxis, float Shadow)
{
	float AxisAlignment = abs(dot(LightAxis, Hair.SurfaceNormal));
	float Occlusion = saturate(2.0f * Hair.AmbientOcclusion * Hair.AmbientOcclusion - 1.0f + AxisAlignment);
	float Visibility = min(sqrt(Shadow), Occlusion);
	float3 ShadowTint = lerp(float3(217.0f, 117.0f, 63.0f) / 255.0f, 1.0f, Visibility);
	return ShadowTint * Visibility;
}

float KajiyaHairSunVisibility(FKajiyaHairData Hair, float3 LightDirection, float Shadow)
{
	float ColorWeight = 1.5f + 0.5f * Hair.AmbientOcclusion * KajiyaHairLuminance(Hair.BaseColor);
	float Occlusion = Hair.AmbientOcclusion * Hair.AmbientOcclusion * ColorWeight;
	float AxisAlignment = abs(dot(LightDirection, Hair.SurfaceNormal));
	float AngularVisibility = (AxisAlignment + 0.5f) * (4.0f / 9.0f);
	return saturate(Occlusion - 0.7f + AngularVisibility) * Shadow;
}

```

## Engine/Shaders/Private/KajiyaHairMaterial.ush

SHA-256: `8d6c2eae24039f62aba26475aa9065bcee5df609993af24dc06945a1a3cf49ec`

```cpp
// Material logic for the two Elena hair draws, expressed in authoring units.
// Texture channels and quantization follow the reference. Render-target bit
// packing is unnecessary in this forward path; its quantization is retained.
#pragma once
#include "KajiyaHair.ush"

struct FKajiyaHairMaterialSettings
{
	float3 Tint;
	float ColorIntensity;
	float NormalStrength;
	float SpecularIntensity;
	float Roughness;
	float AOStrength;
	float BacklitStrength;
	float ShiftVariation;
	float PrimaryShift;
	float SecondaryShift;
	float ColorMultiplier;
	float GlobalColorScale;
};

float KajiyaHairQuantize(float Value, float Levels)
{
	return floor(saturate(Value) * Levels) / Levels;
}

// Surface normals use rounded 12/11-bit octahedral coordinates. The strand
// direction uses truncated 8/8-bit coordinates. Keep both precisions.
float3 KajiyaHairQuantizeDirection(float3 Direction, float2 Levels, float RoundingBias)
{
	float3 Octahedron = Direction / (abs(Direction.x) + abs(Direction.y) + abs(Direction.z));
	float2 Sign = float2(Octahedron.x >= 0.0f ? 1.0f : -1.0f, Octahedron.y >= 0.0f ? 1.0f : -1.0f);
	float2 Folded = (1.0f - abs(Octahedron.yx)) * Sign;
	float2 Encoded = (Octahedron.z >= 0.0f ? Octahedron.xy : Folded) * 0.5f + 0.5f;
	float2 Quantized = floor(saturate(Encoded) * Levels + RoundingBias);
	float2 Decoded = Quantized * (2.0f / Levels) - 1.0f;

	float Z = 1.0f - abs(Decoded.x) - abs(Decoded.y);
	float2 DecodedSign = float2(Decoded.x >= 0.0f ? 1.0f : -1.0f, Decoded.y >= 0.0f ? 1.0f : -1.0f);
	float2 Unfolded = (1.0f - abs(Decoded.yx)) * DecodedSign;
	return normalize(float3(Z >= 0.0f ? Decoded : Unfolded, Z));
}

FKajiyaHairData EvaluateKajiyaHairMaterial(
	float3 ColorSample, float3 NormalAOSample, float AOSample, float3 ShiftSample,
	float3 MeshNormal, float3 MeshTangent, float3 MeshBinormal, FKajiyaHairMaterialSettings Settings)
{
	FKajiyaHairData Hair;
	float3 EncodedColor = ColorSample * Settings.Tint * Settings.ColorIntensity;

	// NR.GB stores tangent-space normal XY. NR.R modulates UV1 occlusion
	// through an overlay blend; it is not the roughness input.
	float2 NormalXY = (NormalAOSample.gb * 2.0f - 1.0f) * Settings.NormalStrength;
	float NormalZ = sqrt(saturate(1.0f - dot(NormalXY, NormalXY)));
	float3 SurfaceNormal = normalize(MeshTangent) * NormalXY.x + normalize(MeshBinormal) * NormalXY.y;
	SurfaceNormal += normalize(MeshNormal) * NormalZ;
	Hair.SurfaceNormal = KajiyaHairQuantizeDirection(SurfaceNormal, float2(4095.0f, 2047.0f), 0.5f);
	Hair.StrandDirection = KajiyaHairQuantizeDirection(normalize(MeshBinormal), float2(255.0f, 255.0f), 0.0f);

	float TextureAO = lerp(1.0f, AOSample, Settings.AOStrength);
	float AOContrast = NormalAOSample.r;
	float CombinedAO = AOContrast > 0.5f
		? 1.0f - (2.0f - 2.0f * AOContrast) * (1.0f - TextureAO)
		: 2.0f * AOContrast * TextureAO;
	CombinedAO = saturate(CombinedAO);

	// Shift.R varies both shifts; Shift.G/B independently scale the two lobes.
	float ShiftOffset = (ShiftSample.r - 0.5f) * 0.2f * Settings.ShiftVariation;
	Hair.PrimaryShift = KajiyaHairQuantize((Settings.PrimaryShift + ShiftOffset) * 0.5f + 0.5f, 63.0f) - 0.5f;
	Hair.SecondaryShift = KajiyaHairQuantize((Settings.SecondaryShift + ShiftOffset) * 0.5f + 0.5f, 63.0f) - 0.5f;
	Hair.PrimarySpecular = KajiyaHairQuantize(Settings.SpecularIntensity * ShiftSample.g, 255.0f);
	Hair.SecondarySpecular = KajiyaHairQuantize(Settings.SpecularIntensity * ShiftSample.b, 255.0f);
	Hair.PrimaryRoughness = KajiyaHairQuantize(Settings.Roughness, 255.0f);
	Hair.SecondaryRoughness = Hair.PrimaryRoughness;
	Hair.AmbientOcclusion = KajiyaHairQuantize(CombinedAO, 63.0f);

	float BacklightMask = CombinedAO * (ShiftSample.g * 3.0f);
	BacklightMask *= saturate(KajiyaHairLuminance(EncodedColor) * 1.2f);
	Hair.Backlit = KajiyaHairQuantize(Settings.BacklitStrength * BacklightMask, 63.0f);

	// UNorm sampling, color grading, 8-bit storage, then gamma-2.2 decode.
	EncodedColor = saturate(EncodedColor * Settings.GlobalColorScale * Settings.ColorMultiplier);
	float3 Gray = KajiyaHairLuminance(EncodedColor);
	EncodedColor += (Gray - EncodedColor) * (-0.5f * (1.0f - CombinedAO));
	float3 QuantizedColor = floor(saturate(EncodedColor) * 255.0f) / 255.0f;
	Hair.BaseColor = pow(QuantizedColor, 2.2f);
	return Hair;
}

```

## Engine/Shaders/Private/KajiyaHairForwardLighting.ush

SHA-256: `0df364535f88f300cf5f9acb3a4dd36ec92bb124a8af1ce2a6db23de05370c59`

```cpp
#pragma once

#if MATERIAL_SHADINGMODEL_KAJIYA_HAIR
FDeferredLightingSplit GetKajiyaHairForwardLightingSplit(
	float3 TranslatedWorldPosition, float3 ViewDirection, FGBufferData GBuffer,
	FDeferredLightData LightData, float4 LightAttenuation, float Dither, float3 IndirectLight, bool bApplyLocalVisibility, inout float SurfaceShadow)
{
	FDeferredLightingSplit Result = (FDeferredLightingSplit)0;
	FKajiyaHairData Hair = GBuffer.KajiyaHair;
	float3 LightDirection = LightData.Direction;
	float3 ToLight = LightDirection;
	float Attenuation = 1.0f;
	float3 LightColor = LightData.Color;
	if (LightData.bRadialLight)
	{
		Attenuation = GetLocalLightAttenuation(TranslatedWorldPosition, LightData, ToLight, LightDirection);
		if (LightData.bRectLight)
		{
			FRect Rect = GetRect(ToLight, LightData);
			Attenuation *= IntegrateLight(Rect);
			LightColor *= SampleSourceTexture(LightDirection, Rect, ConvertToRectTexture(LightData));
		}
		else
		{
			Attenuation *= IntegrateLight(GetCapsule(ToLight, LightData), LightData.bInverseSquared);
		}
	}

	if (Attenuation > 0.0f)
	{
		FShadowTerms Shadow = (FShadowTerms)0;
		Shadow.SurfaceShadow = 1.0f;
		Shadow.TransmissionShadow = 1.0f;
		Shadow.TransmissionThickness = 1.0f;
		Shadow.HairTransmittance.OpaqueVisibility = 1.0f;
		GetShadowTerms(GBuffer.Depth, GBuffer.PrecomputedShadowFactors, GBuffer.ShadingModelID, 1.0f,
			LightData, TranslatedWorldPosition, LightDirection, LightAttenuation, Dither, Shadow);
		SurfaceShadow = Shadow.SurfaceShadow;

		float3 Visibility = 1.0f;
		if (LightData.bRadialLight)
		{
			// The reference AO/direction term belongs to shadow-casting lights even
			// when UE has no packed shadow-mask channel for the forward pass.
			if (bApplyLocalVisibility)
			{
				Visibility = KajiyaHairLocalVisibility(Hair, -LightData.Direction, Shadow.SurfaceShadow);
			}
		}
		else
		{
			// Original cloud, visibility and cutscene factors are supplied by UE.
			Attenuation = saturate(KajiyaHairSunVisibility(Hair, LightDirection, Shadow.SurfaceShadow));
		}

		float3 Diffuse = DiffuseScatterInUncharted4(GBuffer, LightDirection, Hair.SurfaceNormal, Attenuation) * LightData.DiffuseScale;
		float3 Specular = KajiyaHairSpecular(Hair, LightDirection, ViewDirection, Attenuation) * LightData.SpecularScale;
		float3 Backlit = KajiyaHairBacklit(Hair, LightDirection, ViewDirection, Attenuation);
		if (!LightData.bRadialLight)
		{
			// Ambient-base luminance modulates sun backlit in the reference.
			// Dedicated environment mode supplies AmbientBase before this call.
			// Legacy mode retains UE's available base-pass indirect input.
			float AmbientLuminance = KajiyaHairLuminance(IndirectLight);
			Backlit = AmbientLuminance * Backlit;
		}
		float3 Multiplier = Visibility * LightColor;
		// RDC accumulates (specular + backlit) before diffuse. UE keeps these
		// components split for its scene-color/reflection composite.
		Result.DiffuseLighting = float4(Diffuse * Multiplier, 0.0f);
		Result.SpecularLighting = float4((Specular + Backlit) * Multiplier, 0.0f);
		Result.LightingLuminance = Luminance(((Specular + Backlit) + Diffuse) * Multiplier);
	}
	return Result;
}
#endif

```

## 11 输入来源映射

```json
[
  {
    "graph": "Game__Demo__Character__Elena__Material__M_Hair_Masked_KajiyaForward.json",
    "material": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward",
    "output": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionKajiyaHairOutput_1",
    "custom": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
    "inputs": [
      {
        "index": 0,
        "name": "SurfaceNormal",
        "description": "World-space surface normal. Default: (0, 0, 1). No implicit normalization.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 1.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 1
      },
      {
        "index": 1,
        "name": "StrandDirection",
        "description": "World-space strand direction. Default: (0, 1, 0).",
        "type": "float3",
        "default": "Constant3(0.0f, 1.0f, 0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 2
      },
      {
        "index": 2,
        "name": "LinearBaseColor",
        "description": "Linear RGB base color. Default: black.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 0
      },
      {
        "index": 3,
        "name": "PrimarySpecular",
        "description": "Primary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 3
      },
      {
        "index": 4,
        "name": "PrimaryRoughness",
        "description": "Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 4
      },
      {
        "index": 5,
        "name": "PrimaryShift",
        "description": "Primary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 5
      },
      {
        "index": 6,
        "name": "SecondarySpecular",
        "description": "Secondary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 6
      },
      {
        "index": 7,
        "name": "SecondaryRoughness",
        "description": "Secondary lobe roughness in [0, 1]. Independent default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 7
      },
      {
        "index": 8,
        "name": "SecondaryShift",
        "description": "Secondary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 8
      },
      {
        "index": 9,
        "name": "AmbientOcclusion",
        "description": "Material ambient occlusion in [0, 1]. Default: 1 (unoccluded).",
        "type": "float",
        "default": "Constant(1.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 9
      },
      {
        "index": 10,
        "name": "Backlit",
        "description": "Backlighting strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Elena/Material/M_Hair_Masked_KajiyaForward.M_Hair_Masked_KajiyaForward:MaterialExpressionCustom_1",
        "source_index": 10
      }
    ],
    "obsolete_candidates": []
  },
  {
    "graph": "Game__Demo__Character__Nathan__Material__M_Nathan_HairKajiyaFine.json",
    "material": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine",
    "output": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionKajiyaHairOutput_0",
    "custom": null,
    "inputs": [
      {
        "index": 0,
        "name": "SurfaceNormal",
        "description": "World-space surface normal. Default: (0, 0, 1). No implicit normalization.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionTransform_2",
        "source_index": 0
      },
      {
        "index": 1,
        "name": "StrandDirection",
        "description": "World-space strand direction. Default: (0, 1, 0).",
        "type": "float3",
        "default": "Constant3(0.0f, 1.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionTransform_3",
        "source_index": 0
      },
      {
        "index": 2,
        "name": "LinearBaseColor",
        "description": "Linear RGB base color. Default: black.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_29",
        "source_index": 0
      },
      {
        "index": 3,
        "name": "PrimarySpecular",
        "description": "Primary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_21",
        "source_index": 0
      },
      {
        "index": 4,
        "name": "PrimaryRoughness",
        "description": "Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_20",
        "source_index": 0
      },
      {
        "index": 5,
        "name": "PrimaryShift",
        "description": "Primary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSubtract_42",
        "source_index": 0
      },
      {
        "index": 6,
        "name": "SecondarySpecular",
        "description": "Secondary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_23",
        "source_index": 0
      },
      {
        "index": 7,
        "name": "SecondaryRoughness",
        "description": "Secondary lobe roughness in [0, 1]. Independent default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_22",
        "source_index": 0
      },
      {
        "index": 8,
        "name": "SecondaryShift",
        "description": "Secondary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSubtract_43",
        "source_index": 0
      },
      {
        "index": 9,
        "name": "AmbientOcclusion",
        "description": "Material ambient occlusion in [0, 1]. Default: 1 (unoccluded).",
        "type": "float",
        "default": "Constant(1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionSaturate_18",
        "source_index": 0
      },
      {
        "index": 10,
        "name": "Backlit",
        "description": "Backlighting strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionConstant_16",
        "source_index": 0
      }
    ],
    "obsolete_candidates": [
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_21",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionNamedRerouteDeclaration_5",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_22",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_23",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionNamedRerouteDeclaration_6",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_25",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_24",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionNamedRerouteUsage_6",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionAppendVector_20",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaFine.M_Nathan_HairKajiyaFine:MaterialExpressionNamedRerouteUsage_5"
    ]
  },
  {
    "graph": "Game__Demo__Character__Nathan__Material__M_Nathan_HairKajiyaLayered.json",
    "material": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered",
    "output": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionKajiyaHairOutput_1",
    "custom": null,
    "inputs": [
      {
        "index": 0,
        "name": "SurfaceNormal",
        "description": "World-space surface normal. Default: (0, 0, 1). No implicit normalization.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionTransform_3",
        "source_index": 0
      },
      {
        "index": 1,
        "name": "StrandDirection",
        "description": "World-space strand direction. Default: (0, 1, 0).",
        "type": "float3",
        "default": "Constant3(0.0f, 1.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionTransform_2",
        "source_index": 0
      },
      {
        "index": 2,
        "name": "LinearBaseColor",
        "description": "Linear RGB base color. Default: black.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionSaturate_5",
        "source_index": 0
      },
      {
        "index": 3,
        "name": "PrimarySpecular",
        "description": "Primary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionSaturate_60",
        "source_index": 0
      },
      {
        "index": 4,
        "name": "PrimaryRoughness",
        "description": "Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionSaturate_59",
        "source_index": 0
      },
      {
        "index": 5,
        "name": "PrimaryShift",
        "description": "Primary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionStaticSwitchParameter_5",
        "source_index": 0
      },
      {
        "index": 6,
        "name": "SecondarySpecular",
        "description": "Secondary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionStaticSwitchParameter_4",
        "source_index": 0
      },
      {
        "index": 7,
        "name": "SecondaryRoughness",
        "description": "Secondary lobe roughness in [0, 1]. Independent default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionStaticSwitchParameter_7",
        "source_index": 0
      },
      {
        "index": 8,
        "name": "SecondaryShift",
        "description": "Secondary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionStaticSwitchParameter_6",
        "source_index": 0
      },
      {
        "index": 9,
        "name": "AmbientOcclusion",
        "description": "Material ambient occlusion in [0, 1]. Default: 1 (unoccluded).",
        "type": "float",
        "default": "Constant(1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionSaturate_43",
        "source_index": 0
      },
      {
        "index": 10,
        "name": "Backlit",
        "description": "Backlighting strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionSaturate_64",
        "source_index": 0
      }
    ],
    "obsolete_candidates": [
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_55",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteDeclaration_123",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteUsage_163",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_58",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_6",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_5",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_57",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionAppendVector_4",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteDeclaration_124",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteDeclaration_11",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteUsage_162",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaLayered.M_Nathan_HairKajiyaLayered:MaterialExpressionNamedRerouteUsage_11"
    ]
  },
  {
    "graph": "Game__Demo__Character__Nathan__Material__M_Nathan_HairKajiyaMapped.json",
    "material": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped",
    "output": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionKajiyaHairOutput_1",
    "custom": null,
    "inputs": [
      {
        "index": 0,
        "name": "SurfaceNormal",
        "description": "World-space surface normal. Default: (0, 0, 1). No implicit normalization.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionTransform_3",
        "source_index": 0
      },
      {
        "index": 1,
        "name": "StrandDirection",
        "description": "World-space strand direction. Default: (0, 1, 0).",
        "type": "float3",
        "default": "Constant3(0.0f, 1.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionTransform_2",
        "source_index": 0
      },
      {
        "index": 2,
        "name": "LinearBaseColor",
        "description": "Linear RGB base color. Default: black.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionSaturate_2",
        "source_index": 0
      },
      {
        "index": 3,
        "name": "PrimarySpecular",
        "description": "Primary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionSaturate_31",
        "source_index": 0
      },
      {
        "index": 4,
        "name": "PrimaryRoughness",
        "description": "Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionSaturate_30",
        "source_index": 0
      },
      {
        "index": 5,
        "name": "PrimaryShift",
        "description": "Primary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionStaticSwitchParameter_1",
        "source_index": 0
      },
      {
        "index": 6,
        "name": "SecondarySpecular",
        "description": "Secondary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionStaticSwitchParameter_0",
        "source_index": 0
      },
      {
        "index": 7,
        "name": "SecondaryRoughness",
        "description": "Secondary lobe roughness in [0, 1]. Independent default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionStaticSwitchParameter_3",
        "source_index": 0
      },
      {
        "index": 8,
        "name": "SecondaryShift",
        "description": "Secondary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionStaticSwitchParameter_2",
        "source_index": 0
      },
      {
        "index": 9,
        "name": "AmbientOcclusion",
        "description": "Material ambient occlusion in [0, 1]. Default: 1 (unoccluded).",
        "type": "float",
        "default": "Constant(1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionSaturate_22",
        "source_index": 0
      },
      {
        "index": 10,
        "name": "Backlit",
        "description": "Backlighting strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionSaturate_34",
        "source_index": 0
      }
    ],
    "obsolete_candidates": [
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_73",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteDeclaration_64",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_2",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteUsage_2",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteUsage_87",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_59",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteDeclaration_63",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_1",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_3",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteDeclaration_2",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionNamedRerouteUsage_88",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaMapped.M_Nathan_HairKajiyaMapped:MaterialExpressionAppendVector_75"
    ]
  },
  {
    "graph": "Game__Demo__Character__Nathan__Material__M_Nathan_HairKajiyaPacked.json",
    "material": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked",
    "output": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionKajiyaHairOutput_1",
    "custom": null,
    "inputs": [
      {
        "index": 0,
        "name": "SurfaceNormal",
        "description": "World-space surface normal. Default: (0, 0, 1). No implicit normalization.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionTransform_4",
        "source_index": 0
      },
      {
        "index": 1,
        "name": "StrandDirection",
        "description": "World-space strand direction. Default: (0, 1, 0).",
        "type": "float3",
        "default": "Constant3(0.0f, 1.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionTransform_2",
        "source_index": 0
      },
      {
        "index": 2,
        "name": "LinearBaseColor",
        "description": "Linear RGB base color. Default: black.",
        "type": "float3",
        "default": "Constant3(0.0f, 0.0f, 0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionSaturate_2",
        "source_index": 0
      },
      {
        "index": 3,
        "name": "PrimarySpecular",
        "description": "Primary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionSaturate_20",
        "source_index": 0
      },
      {
        "index": 4,
        "name": "PrimaryRoughness",
        "description": "Primary lobe roughness in [0, 1]; also controls environment specular occlusion. Default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionSaturate_19",
        "source_index": 0
      },
      {
        "index": 5,
        "name": "PrimaryShift",
        "description": "Primary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionStaticSwitchParameter_1",
        "source_index": 0
      },
      {
        "index": 6,
        "name": "SecondarySpecular",
        "description": "Secondary lobe strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionStaticSwitchParameter_0",
        "source_index": 0
      },
      {
        "index": 7,
        "name": "SecondaryRoughness",
        "description": "Secondary lobe roughness in [0, 1]. Independent default: 0.4.",
        "type": "float",
        "default": "Constant(0.4f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionStaticSwitchParameter_6",
        "source_index": 0
      },
      {
        "index": 8,
        "name": "SecondaryShift",
        "description": "Secondary strand shift in [-0.5, 0.5]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionStaticSwitchParameter_2",
        "source_index": 0
      },
      {
        "index": 9,
        "name": "AmbientOcclusion",
        "description": "Material ambient occlusion in [0, 1]. Default: 1 (unoccluded).",
        "type": "float",
        "default": "Constant(1.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionSaturate_13",
        "source_index": 0
      },
      {
        "index": 10,
        "name": "Backlit",
        "description": "Backlighting strength in [0, 1]. Default: 0.",
        "type": "float",
        "default": "Constant(0.0f)",
        "source": "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionSaturate_22",
        "source_index": 0
      }
    ],
    "obsolete_candidates": [
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_1",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteUsage_63",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_23",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteDeclaration_45",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_22",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_24",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteUsage_62",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_2",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteDeclaration_46",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteUsage_2",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionAppendVector_3",
      "/Game/Demo/Character/Nathan/Material/M_Nathan_HairKajiyaPacked.M_Nathan_HairKajiyaPacked:MaterialExpressionNamedRerouteDeclaration_2"
    ]
  }
]
```
