#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EarthfallSandboxSubsystem.generated.h"

class AEarthOrbitPawn;
class AEarthfallPlanet;
class UEarthfallPopulationSubsystem;

/**
 * The god-game layer over the strike simulator: everything on the BIO & NATURE
 * and DIPLOMACY & SPAWNERS tabs of the command dock. The orbital weapons on the
 * first tab still go through the pawn's launcher.
 *
 * A tool is used at a planet-local direction and answers with one status line
 * for the HUD.
 */
UCLASS()
class EARTHFALL_API UEarthfallSandboxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Use the tool in (Tab, Slot) at a planet-local direction. Returns the HUD status line. */
	FString UseTool(int32 Tab, int32 Slot, const FVector& LocalDirection, AEarthOrbitPawn* Pawn);
	void ResetSandbox();

private:
	AEarthfallPlanet* FindPlanet() const;
	UEarthfallPopulationSubsystem* Population() const;

	FString UseVolcano(const FVector& LocalDirection);
	FString UseTsunami(const FVector& LocalDirection);
	FString UseMeteorShower(const FVector& LocalDirection, AEarthOrbitPawn* Pawn);

	/** Meteors still to fall from a shower, staggered over a few seconds. */
	struct FPendingMeteor
	{
		FVector Direction = FVector::ForwardVector;
		float Delay = 0.0f;
	};
	TArray<FPendingMeteor> PendingMeteors;
	TWeakObjectPtr<AEarthOrbitPawn> MeteorPawn;
};
