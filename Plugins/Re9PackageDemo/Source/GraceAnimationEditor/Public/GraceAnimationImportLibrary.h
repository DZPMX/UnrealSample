#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GraceAnimationImportLibrary.generated.h"

class UAnimSequence;

UCLASS()
class GRACEANIMATIONEDITOR_API UGraceAnimationImportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Grace|Import")
    static void AttachOriginalWrinkleTracks(const FString& Filename, UAnimSequence* Animation);
};
