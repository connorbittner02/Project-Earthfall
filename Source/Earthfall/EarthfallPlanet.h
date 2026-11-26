#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallPlanet.generated.h"

UCLASS()
class EARTHFALL_API AEarthfallPlanet : public AActor
{
    GENERATED_BODY()

public:
    AEarthfallPlanet();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="Earthfall|Impact")
    void ApplyImpact(FVector WorldLocation, float Radius, float Energy);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Earthfall|Planet")
    float PlanetRadius = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Earthfall|Planet")
    float RotationDegreesPerSecond = 1.5f;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> PlanetMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;
};
