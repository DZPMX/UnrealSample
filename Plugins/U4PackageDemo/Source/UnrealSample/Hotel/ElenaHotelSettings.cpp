#include "Hotel/ElenaHotelSettings.h"

#include "Components/LightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneSharedPlaybackState.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

AElenaHotelSettingsController::AElenaHotelSettingsController()
{
    PrimaryActorTick.bCanEverTick = false;
}

float AElenaHotelSettingsController::Setting(FName Name) const
{
    return static_cast<float>(ResolvedSettings.FindRef(Name));
}

void AElenaHotelSettingsController::EvaluateSettings(double Frame)
{
    // All source comparisons for these maps are >= and < on the authoring frame.
    // Intervals retain subframes and the approved 63-frame splice at the 070 cut.
    const float CurrentFrame = static_cast<float>(Frame + 3079.0);
    const float MayaFrame = static_cast<float>(Frame + (Frame < 287.0 ? 3079.0 : 3142.0));
    TArray<int32> ActiveIndices;
    for (int32 Index = 0; Index < Maps.Num(); ++Index)
    {
        bool bSubmitted = false;
        for (const FVector2D& Range : Maps[Index].CurrentFrameRanges)
        {
            if (CurrentFrame >= Range.X && CurrentFrame < Range.Y)
            {
                bSubmitted = true;
            }
        }
        for (const FVector2D& Range : Maps[Index].MayaFrameRanges)
        {
            if (MayaFrame >= Range.X && MayaFrame < Range.Y) bSubmitted = true;
        }
        if (bSubmitted) ActiveIndices.Add(Index);
    }
    ActiveIndices.Sort([this](int32 A, int32 B)
    {
        const FElenaHotelSettingsMap& Left = Maps[A];
        const FElenaHotelSettingsMap& Right = Maps[B];
        if (Left.Priority != Right.Priority) return Left.Priority < Right.Priority;
        if (Left.MapId != Right.MapId) return Left.MapId < Right.MapId;
        return Left.OwnerId < Right.OwnerId;
    });

    const TArray<FName> PreviousMaps = ActiveMaps;
    ActiveMaps.Reset();
    SubmittedMaps.Reset();
    WithdrawnMaps.Reset();
    ResolvedSettings = Defaults;
    WinningMaps.Reset();
    for (int32 Index : ActiveIndices)
    {
        const FElenaHotelSettingsMap& Map = Maps[Index];
        ActiveMaps.Add(Map.Name);
        if (!PreviousMaps.Contains(Map.Name)) SubmittedMaps.Add(Map.Name);
        if (Map.bResetDefaults)
        {
            ResolvedSettings = Defaults;
            WinningMaps.Reset();
            for (const auto& Pair : Defaults) WinningMaps.Add(Pair.Key, Map.Name);
        }
        for (const auto& Pair : Map.Values)
        {
            ResolvedSettings.Add(Pair.Key, Pair.Value);
            WinningMaps.Add(Pair.Key, Map.Name);
        }
    }
    for (FName Name : PreviousMaps)
    {
        if (!ActiveMaps.Contains(Name)) WithdrawnMaps.Add(Name);
    }
    EvaluatedFrame = Frame;
    ApplyOutputs();
}

void AElenaHotelSettingsController::ApplyOutputs()
{
    TMap<FName, FLinearColor> Parameters;
    Parameters.Add(TEXT("Hotel_ExposureSaturation"), FLinearColor(
        FMath::Pow(2.0f, Setting(TEXT("tonemap:tonemap-manual-exposure-value"))),
        Setting(TEXT("post:saturation")), 1.0f, 1.0f));
    FLinearColor Scale(0, 0, 0, 0);
    FLinearColor Bias(0, 0, 0, 0);
    FLinearColor Tint(0, 0, 0, 0);
    const FString Channels(TEXT("rgb"));
    const float Density = Setting(TEXT("post:vignette-density"));
    for (int32 Channel = 0; Channel < 3; ++Channel)
    {
        const FString Suffix = FString::Chr(Channels[Channel]);
        const float Maximum = Setting(FName(TEXT("post:tint-rgb-max-adjust-") + Suffix)) + Setting(FName(TEXT("post:tint-rgb-max-vector-") + Suffix));
        const float Minimum = Setting(FName(TEXT("post:tint-rgb-min-adjust-") + Suffix)) + Setting(FName(TEXT("post:tint-rgb-min-vector-") + Suffix));
        const float Multiplier = Setting(FName(TEXT("post:tint-rgb-multiplier-") + Suffix));
        const float Adjust = Maximum == Minimum ? 0.0f : Multiplier / (Maximum - Minimum);
        Scale.Component(Channel) = Adjust * Setting(FName(TEXT("post:tint-rgb-final-multiply-") + Suffix));
        Bias.Component(Channel) = Setting(FName(TEXT("post:tint-rgb-final-add-") + Suffix)) - (Minimum * Adjust) * Multiplier;
        Tint.Component(Channel) = FMath::Pow((Setting(FName(TEXT("post:vignette-tint-") + Suffix)) - 1.0f) * Density + 1.0f, 2.2f);
    }
    Parameters.Add(TEXT("Hotel_FinalScale"), Scale);
    Parameters.Add(TEXT("Hotel_FinalBias"), Bias);
    Parameters.Add(TEXT("Hotel_VignetteTint"), Tint);
    const float InverseScale = 1.0f / (Setting(TEXT("post:vignette-scale")) * 1.17f);
    Parameters.Add(TEXT("Hotel_VignetteShape"), FLinearColor(Density > 0.0f ? 1.0f : 0.0f,
        1.0f / (Setting(TEXT("post:vignette-softness")) + 0.01f),
        (1.0f / Setting(TEXT("post:vignette-stretch"))) * InverseScale,
        (InverseScale * 9.0f) * 0.0625f));
    Parameters.Add(TEXT("Hotel_Sharpen"), FLinearColor(Setting(TEXT("post:dof-enable-bokeh")),
        Setting(TEXT("misc:sharpen-amount")), Setting(TEXT("misc:sharpen-threshold")), 0.0f));

    if (!bSettingsActive)
    {
        // World/asset references can be absent during actor creation or world teardown.
        const TObjectPtr<UWorld> World = GetWorld();
        if (World && Collection) ActiveCollection = World->GetParameterCollectionInstance(Collection);
        ActiveVolume = PostProcessVolume;
        if (ActiveCollection.IsValid())
        {
            for (const auto& Pair : Parameters)
            {
                FLinearColor Value;
                ActiveCollection->GetVectorParameterValue(Pair.Key, Value);
                PreviousParameters.Add(Pair.Key, Value);
            }
        }
        if (ActiveVolume.IsValid())
        {
            const FPostProcessSettings& Settings = ActiveVolume->Settings;
            PreviousAO = Settings.AmbientOcclusionIntensity;
            PreviousSSR = Settings.ScreenSpaceReflectionMaxRoughness;
            bPreviousAOOverride = Settings.bOverride_AmbientOcclusionIntensity;
            bPreviousSSROverride = Settings.bOverride_ScreenSpaceReflectionMaxRoughness;
        }
        for (const FElenaHotelLightCurve& Curve : LightCurves)
        {
            if (Curve.Light) PreviousLights.Add(Curve.Light, Curve.Light->Intensity);
        }
        bSettingsActive = true;
    }
    if (ActiveCollection.IsValid())
    {
        for (const auto& Pair : Parameters) ActiveCollection->SetVectorParameterValue(Pair.Key, Pair.Value);
    }
    if (ActiveVolume.IsValid())
    {
        FPostProcessSettings& Settings = ActiveVolume->Settings;
        // Source disables SSAO; UE AO is an adapter, not the RGB ambient-shadow input.
        Settings.bOverride_AmbientOcclusionIntensity = true;
        Settings.AmbientOcclusionIntensity = Setting(TEXT("lighting:ssao-enable")) != 0.0f ? PreviousAO : 0.0f;
        Settings.bOverride_ScreenSpaceReflectionMaxRoughness = true;
        Settings.ScreenSpaceReflectionMaxRoughness = Setting(TEXT("lighting:ssr-max-reflected-roughness"));
    }

    const float GainRatio = FMath::Pow(Setting(TEXT("misc:cutscene-lights-multiplier")), 2.2f) / BakedLightGain;
    for (const FElenaHotelLightCurve& Curve : LightCurves)
    {
        float Value = Curve.DefaultValue;
        if (!Curve.Keys.IsEmpty())
        {
            int32 Lower = 0;
            int32 Upper = Curve.Keys.Num();
            while (Lower < Upper)
            {
                const int32 Middle = (Lower + Upper) / 2;
                if (Curve.Keys[Middle].X <= EvaluatedFrame) Lower = Middle + 1;
                else Upper = Middle;
            }
            const int32 Index = FMath::Max(0, Lower - 1);
            Value = static_cast<float>(Curve.Keys[Index].Y);
            if (Lower > 0 && Lower < Curve.Keys.Num() && !Curve.ConstantSegments.Contains(Index))
            {
                const float Alpha = static_cast<float>((EvaluatedFrame - Curve.Keys[Index].X) / (Curve.Keys[Lower].X - Curve.Keys[Index].X));
                Value = FMath::Lerp(Value, static_cast<float>(Curve.Keys[Lower].Y), Alpha);
            }
        }
        // Curves retain original UE photometric adaptation. Remove the old baked
        // global factor algebraically once, never multiply the previous frame.
        if (Curve.Light) Curve.Light->SetIntensity(Value * GainRatio);
    }
}

void AElenaHotelSettingsController::RestoreSettings()
{
    if (bSettingsActive)
    {
        if (ActiveCollection.IsValid())
        {
            for (const auto& Pair : PreviousParameters) ActiveCollection->SetVectorParameterValue(Pair.Key, Pair.Value);
        }
        if (ActiveVolume.IsValid())
        {
            FPostProcessSettings& Settings = ActiveVolume->Settings;
            Settings.AmbientOcclusionIntensity = PreviousAO;
            Settings.ScreenSpaceReflectionMaxRoughness = PreviousSSR;
            Settings.bOverride_AmbientOcclusionIntensity = bPreviousAOOverride;
            Settings.bOverride_ScreenSpaceReflectionMaxRoughness = bPreviousSSROverride;
        }
        for (const auto& Pair : PreviousLights)
        {
            if (Pair.Key.IsValid()) Pair.Key->SetIntensity(Pair.Value);
        }
    }
    PreviousParameters.Reset();
    PreviousLights.Reset();
    ActiveCollection.Reset();
    ActiveVolume.Reset();
    bSettingsActive = false;
    WithdrawnMaps = ActiveMaps;
    SubmittedMaps.Reset();
    ActiveMaps.Reset();
    ResolvedSettings.Reset();
    WinningMaps.Reset();
    EvaluatedFrame = -1.0;
}

void AElenaHotelSettingsController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreSettings();
    Super::EndPlay(EndPlayReason);
}

void AElenaHotelSettingsController::Destroyed()
{
    RestoreSettings();
    Super::Destroyed();
}

UElenaHotelSettingsSection::UElenaHotelSettingsSection(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    bRequiresRangedHook = true;
    SetCompletionMode(EMovieSceneCompletionMode::RestoreState);
}

void UElenaHotelSettingsSection::Begin(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const
{
    Update(State, Params);
}

void UElenaHotelSettingsSection::Update(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const
{
    const double Frame = FFrameRate::TransformTime(Params.Context.GetTime(), Params.Context.GetFrameRate(), FFrameRate(30, 1)).AsDecimal();
    for (const TWeakObjectPtr<>& Object : State->FindBoundObjects(Params.ObjectBindingID, Params.SequenceID))
    {
        const TObjectPtr<AElenaHotelSettingsController> Controller = Cast<AElenaHotelSettingsController>(Object.Get());
        if (Controller) Controller->EvaluateSettings(Frame);
    }
}

void UElenaHotelSettingsSection::End(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const
{
    for (const TWeakObjectPtr<>& Object : State->FindBoundObjects(Params.ObjectBindingID, Params.SequenceID))
    {
        const TObjectPtr<AElenaHotelSettingsController> Controller = Cast<AElenaHotelSettingsController>(Object.Get());
        if (Controller) Controller->RestoreSettings();
    }
}

UElenaHotelSettingsTrack::UElenaHotelSettingsTrack(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    EvalOptions.bCanEvaluateNearestSection = false;
    EvalOptions.bEvalNearestSection = false;
}

bool UElenaHotelSettingsTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
    return SectionClass == UElenaHotelSettingsSection::StaticClass();
}

UMovieSceneSection* UElenaHotelSettingsTrack::CreateNewSection()
{
    const TObjectPtr<UElenaHotelSettingsSection> Section = NewObject<UElenaHotelSettingsSection>(this, NAME_None, RF_Transactional);
    return Section.Get();
}
