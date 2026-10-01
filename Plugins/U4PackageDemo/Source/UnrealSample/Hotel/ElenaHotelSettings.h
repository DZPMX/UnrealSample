#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sections/MovieSceneHookSection.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "ElenaHotelSettings.generated.h"

class APostProcessVolume;
class ULightComponent;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;

// These are the eight zero-fade, unit-weight submissions in hotel 060-100.
// This data does not define a general replacement for RenderSettingsMgr.
USTRUCT(BlueprintType)
struct FElenaHotelSettingsMap
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Source") FName Name;
    UPROPERTY(EditAnywhere, Category="Source") int64 MapId = 0;
    UPROPERTY(EditAnywhere, Category="Source") int64 OwnerId = 0;
    UPROPERTY(EditAnywhere, Category="Source") int32 Priority = 0;
    UPROPERTY(EditAnywhere, Category="Source") bool bResetDefaults = false;
    UPROPERTY(EditAnywhere, Category="Source") TArray<FVector2D> CurrentFrameRanges;
    UPROPERTY(EditAnywhere, Category="Source") TArray<FVector2D> MayaFrameRanges;
    UPROPERTY(EditAnywhere, Category="Source") TMap<FName, double> Values;
    // Unresolved nested descriptors are retained verbatim; they are not guessed.
    UPROPERTY(EditAnywhere, Category="Source") FString UnresolvedFields;
};

USTRUCT(BlueprintType)
struct FElenaHotelLightCurve
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Source") TObjectPtr<ULightComponent> Light;
    UPROPERTY(EditAnywhere, Category="Source") TArray<FVector2D> Keys;
    UPROPERTY(EditAnywhere, Category="Source") TArray<int32> ConstantSegments;
    UPROPERTY(EditAnywhere, Category="Source") float DefaultValue = 0.0f;
};

UCLASS()
class UNREALSAMPLE_API AElenaHotelSettingsController : public AActor
{
    GENERATED_BODY()
public:
    AElenaHotelSettingsController();
    UPROPERTY(EditAnywhere, Category="Hotel|Source") TMap<FName, double> Defaults;
    UPROPERTY(EditAnywhere, Category="Hotel|Source") TArray<FElenaHotelSettingsMap> Maps;
    UPROPERTY(EditAnywhere, Category="Hotel|Source") TArray<FElenaHotelLightCurve> LightCurves;
    UPROPERTY(EditAnywhere, Category="Hotel|Source") float BakedLightGain = 1.0f;
    UPROPERTY(EditAnywhere, Category="Hotel|Targets") TObjectPtr<APostProcessVolume> PostProcessVolume;
    UPROPERTY(EditAnywhere, Category="Hotel|Targets") TObjectPtr<UMaterialParameterCollection> Collection;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") bool bSettingsActive = false;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") double EvaluatedFrame = -1.0;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") TArray<FName> ActiveMaps;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") TArray<FName> SubmittedMaps;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") TArray<FName> WithdrawnMaps;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") TMap<FName, double> ResolvedSettings;
    UPROPERTY(VisibleAnywhere, Transient, Category="Hotel|Runtime") TMap<FName, FName> WinningMaps;

    UFUNCTION(BlueprintCallable, Category="Hotel") void EvaluateSettings(double Frame);
    UFUNCTION(BlueprintCallable, Category="Hotel") void RestoreSettings();
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Destroyed() override;

private:
    float Setting(FName Name) const;
    void ApplyOutputs();
    TWeakObjectPtr<UMaterialParameterCollectionInstance> ActiveCollection;
    TWeakObjectPtr<APostProcessVolume> ActiveVolume;
    TMap<FName, FLinearColor> PreviousParameters;
    TMap<TWeakObjectPtr<ULightComponent>, float> PreviousLights;
    float PreviousAO = 0.0f;
    float PreviousSSR = 0.0f;
    bool bPreviousAOOverride = false;
    bool bPreviousSSROverride = false;
};

UCLASS()
class UNREALSAMPLE_API UElenaHotelSettingsSection : public UMovieSceneHookSection
{
    GENERATED_BODY()
public:
    UElenaHotelSettingsSection(const FObjectInitializer& Initializer);
    virtual void Begin(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const override;
    virtual void Update(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const override;
    virtual void End(TSharedRef<FSharedPlaybackState> State, const UE::MovieScene::FEvaluationHookParams& Params) const override;
};

// The inherited engine track owns its section array and its required raw-pointer API.
UCLASS()
class UNREALSAMPLE_API UElenaHotelSettingsTrack : public UMovieScenePropertyTrack
{
    GENERATED_BODY()
public:
    UElenaHotelSettingsTrack(const FObjectInitializer& Initializer);
    virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
    virtual UMovieSceneSection* CreateNewSection() override;
};
