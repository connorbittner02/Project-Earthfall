#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "EarthfallHUD.generated.h"

UCLASS()
class EARTHFALL_API AEarthfallHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void DrawTextLine(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCenter = false);
};
