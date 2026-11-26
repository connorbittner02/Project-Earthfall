#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EarthfallGameMode.generated.h"

UCLASS()
class EARTHFALL_API AEarthfallGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AEarthfallGameMode();
    virtual void BeginPlay() override;
};
