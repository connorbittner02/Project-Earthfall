#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallTypes.h"
#include "EarthfallProjectile.generated.h"

class AEarthOrbitPawn;
class AEarthfallPlanet;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A visible munition flying from the firing platform at the player's orbital
 * point of view to its target: a hull, a glowing head and a camera-facing ribbon
 * trail that brightens into a re-entry streak as it meets the atmosphere. The
 * detonation is only resolved when it arrives.
 */
UCLASS()
class EARTHFALL_API AEarthfallProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallProjectile();
	virtual void Tick(float DeltaSeconds) override;

	void Launch(AEarthfallPlanet* InPlanet, AEarthOrbitPawn* InOwnerPawn, const FVector& InStart, const FEarthfallStrike& InStrike);

private:
	static constexpr int32 TrailPoints = 48;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HeadGlow;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Trail;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> HullMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TrailMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HullMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrailMID;

	TWeakObjectPtr<AEarthfallPlanet> Planet;
	TWeakObjectPtr<AEarthOrbitPawn> OwnerPawn;
	FEarthfallStrike Strike;
	EEarthfallProjectile Style = EEarthfallProjectile::Rod;
	FLinearColor Accent = FLinearColor::White;
	FVector Start = FVector::ZeroVector;
	FVector Control = FVector::ZeroVector;
	FVector PlanetCenter = FVector::ZeroVector;
	float PlanetRadius = 100.0f;
	float TravelSeconds = 1.0f;
	float TrailSeconds = 1.2f;
	float Age = 0.0f;
	float AgeAtImpact = 0.0f;
	bool bLaunched = false;
	bool bDetonated = false;

	struct FTrailPoint
	{
		FVector Position = FVector::ZeroVector;
		float BirthAge = 0.0f;
	};
	TArray<FTrailPoint> History;

	FVector PositionAt(float Alpha) const;
	FVector CameraLocation() const;
	void BuildTrailTopology();
	void UpdateTrail();
	void Detonate();
};
