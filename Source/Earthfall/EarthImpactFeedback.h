#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "EarthImpactFeedback.generated.h"

UCLASS()
class EARTHFALL_API AEarthImpactFeedback : public AActor
{
    GENERATED_BODY()
public:
    AEarthImpactFeedback();
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UPointLightComponent> Flash;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UProceduralMeshComponent> Ring;
    float Age = 0.0f;
    virtual void Tick(float DeltaSeconds) override;
private:
    void UpdateRing(float Alpha);
};
