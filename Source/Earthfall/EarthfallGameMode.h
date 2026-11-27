#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EarthfallGameMode.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Builds the whole scene at runtime on the empty engine map: the planet, Moon,
 * the Milky Way backdrop, the Sun disc and corona, and the fixed-exposure
 * post-process the unlit materials are authored against.
 */
UCLASS()
class EARTHFALL_API AEarthfallGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEarthfallGameMode();
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> CelestialSphereMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SpaceBackdropMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SunDiscMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SunCoronaMaterial;

	class UStaticMeshComponent* SpawnCelestialSphere(const TCHAR* Name, UMaterialInterface* Material,
		const FVector& Location, float Radius, bool bSeenFromInside, int32 SortPriority);
};
