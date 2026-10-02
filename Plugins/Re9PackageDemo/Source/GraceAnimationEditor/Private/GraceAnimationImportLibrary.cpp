#include "GraceAnimationImportLibrary.h"
#include "GraceWrinkleMotion.h"
#include "Animation/AnimSequence.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"

void UGraceAnimationImportLibrary::AttachOriginalWrinkleTracks(const FString& Filename, UAnimSequence* Animation)
{
    // UHT requires a UObject pointer at this reflected API boundary.
    const TObjectPtr<UAnimSequence> Sequence = Animation;
    if (IsValid(Sequence))
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Filename))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to read wrinkle tracks file '%s'."), *Filename);
            return;
        }
        TSharedPtr<FJsonObject> Parsed;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to parse wrinkle tracks JSON file '%s'."), *Filename);
            return;
        }
        const FJsonObject& Data = *Parsed;
        const FJsonObject& Source = *Data.GetObjectField(TEXT("source"));
        Sequence->Modify();
        Sequence->RemoveUserDataOfClass(UGraceWrinkleMotion::StaticClass());
        if (!Data.GetArrayField(TEXT("wrinkle_tracks")).IsEmpty())
        {
            const TObjectPtr<UGraceWrinkleMotion> Wrinkle = NewObject<UGraceWrinkleMotion>(Sequence, NAME_None, RF_Transactional);
            Wrinkle->SourceResource = Source.GetStringField(TEXT("path"));
            Wrinkle->SourceSHA256 = Source.GetStringField(TEXT("sha256"));
            Wrinkle->SourceFrameRate = Data.GetNumberField(TEXT("frame_rate"));
            Wrinkle->SourceLastFrame = Data.GetNumberField(TEXT("frame_count"));
            for (const TSharedPtr<FJsonValue>& Entry : Data.GetArrayField(TEXT("wrinkle_tracks")))
            {
                const FJsonObject& Track = *Entry->AsObject();
                FGraceWrinkleTrack& Target = Wrinkle->Tracks.AddDefaulted_GetRef();
                Target.MaskIndex = Track.GetNumberField(TEXT("mask_index"));
                Target.Compression = Track.GetNumberField(TEXT("compression"));
                for (const TSharedPtr<FJsonValue>& Value : Track.GetArrayField(TEXT("frames"))) Target.Frames.Add(Value->AsNumber());
                for (const TSharedPtr<FJsonValue>& Value : Track.GetArrayField(TEXT("packed"))) Target.Packed.Add(Value->AsNumber());
                for (const TSharedPtr<FJsonValue>& Value : Track.GetArrayField(TEXT("unpack"))) Target.Unpack.Add(Value->AsNumber());
            }
            Sequence->AddAssetUserData(Wrinkle);
        }
        const TObjectPtr<UPackage> Package = Sequence->GetPackage();
        Package->GetMetaData().SetValue(Sequence, TEXT("RE9Source"), *Source.GetStringField(TEXT("path")));
        Package->GetMetaData().SetValue(Sequence, TEXT("RE9SourceSHA256"), *Source.GetStringField(TEXT("sha256")));
        Package->GetMetaData().SetValue(Sequence, TEXT("RE9PreparedData"), *Filename);
        Sequence->MarkPackageDirty();
    }
}
