#include "Modules/ModuleManager.h"
#include "Hotel/ElenaHotelSettings.h"
#include "ISequencerModule.h"
#include "MovieSceneTrackEditor.h"

class FElenaHotelSettingsTrackEditor : public FMovieSceneTrackEditor
{
public:
    explicit FElenaHotelSettingsTrackEditor(TSharedRef<ISequencer> Sequencer)
        : FMovieSceneTrackEditor(Sequencer) {}

    virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const override
    {
        return TrackClass == UElenaHotelSettingsTrack::StaticClass();
    }

    static TSharedRef<ISequencerTrackEditor> Create(TSharedRef<ISequencer> Sequencer)
    {
        return MakeShared<FElenaHotelSettingsTrackEditor>(Sequencer);
    }
};

class FUnrealSampleEditorModule : public FDefaultModuleImpl
{
public:
    virtual void StartupModule() override
    {
        ISequencerModule& Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>(TEXT("Sequencer"));
        TrackEditorHandle = Sequencer.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FElenaHotelSettingsTrackEditor::Create));
    }

    virtual void ShutdownModule() override
    {
        if (FModuleManager::Get().IsModuleLoaded(TEXT("Sequencer")))
        {
            FModuleManager::GetModuleChecked<ISequencerModule>(TEXT("Sequencer")).UnRegisterTrackEditor(TrackEditorHandle);
        }
    }

private:
    FDelegateHandle TrackEditorHandle;
};

IMPLEMENT_MODULE(FUnrealSampleEditorModule, UnrealSampleEditor)
