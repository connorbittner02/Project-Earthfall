#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallSandboxView.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Draws the sandbox's small moving lights: strike groups, interceptors, the
 * debris ring, satellites and traffic. UEarthfallSandboxSubsystem decides where
 * they are each frame; this actor only turns that list into one GPU-instanced
 * batch of glowing, stretched spheres per kind.
 */
UCLASS()
class EARTHFALL_API AEarthfallSandboxView : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallSandboxView();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BatchMIDs;

	TArray<TArray<FTransform>> Transforms;
	TArray<int32> InstanceCounts;
};
