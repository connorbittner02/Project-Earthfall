#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallMoon.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/** A procedurally cratered Moon on a slow inclined orbit, lit by the same sun vector as Earth. */
UCLASS()
class EARTHFALL_API AEarthfallMoon : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallMoon();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Earthfall|Moon")
	float OrbitRadius = 360.0f;

	UPROPERTY(EditAnywhere, Category = "Earthfall|Moon")
	float MoonRadius = 18.0f;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MoonMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MoonMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoonMID;
	float OrbitPhase = 2.2f;
	float SunUpdateAccumulator = 0.0f;

	void UpdateSun();
};
