#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EarthfallSandboxSubsystem.generated.h"

class AEarthOrbitPawn;
class AEarthfallPlanet;
class AEarthfallProjectile;
class UEarthfallPopulationSubsystem;
struct FEarthfallStrike;

/** Kinds of small moving light the sandbox draws over the globe. */
namespace EarthfallMarkers
{
	constexpr uint8 Unit = 0;        // strike groups under way
	constexpr uint8 Interceptor = 1; // defensive missiles climbing to a kill
	constexpr uint8 Debris = 2;      // Kessler debris in low orbit
	constexpr uint8 Satellite = 3;   // defense satellites
	constexpr uint8 Count = 4;
}

/** One light for AEarthfallSandboxView to draw this frame. World space. */
struct FEarthfallMarker
{
	FVector Position = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float Size = 0.1f;
	/** Length along Forward as a multiple of Size. */
	float Stretch = 1.0f;
	uint8 Kind = 0;
};

/** What the Country Dossier shows about a nation as a military power. */
struct FEarthfallFactionInfo
{
	FString Code;
	float MilitaryStrength = 0.0f;
	int32 NuclearStockpile = 0;
	TArray<FString> AtWarWith;
	TArray<FString> Allies;
};

/**
 * The god-game layer over the strike simulator: everything on the BIO & NATURE
 * and DIPLOMACY & SPAWNERS tabs of the command dock, and the world that reacts.
 *
 *   Factions   Fourteen major powers start with capitals, alliances, militaries
 *              and stockpiles; any other nation becomes a faction when it is
 *              drawn into a war. Power grid, stability and defense readiness
 *              are read live from the population simulation.
 *   Wars       Declared by the player (Whisper of War) or by the AI: a nation
 *              struck from orbit may blame a rival, and allies follow each
 *              other in. Warring factions launch real missiles at each other's
 *              capitals and send strike groups toward enemy shores.
 *   Defense    A struck nation with power and readiness launches interceptors
 *              at inbound rods, meteors and warheads. Each battery reloads, so
 *              a salvo gets through where a single shot would not. Satellites
 *              cover whatever is below them.
 *   Debris     Launches and kills litter low orbit. A thick enough field
 *              starts destroying munitions on its own.
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

	/** The planet reports every resolved strike, so nations can react to being hit. */
	void NotifyStrike(const FVector& LocalDirection, const FEarthfallStrike& Strike);
	/** Every munition that leaves a launcher adds a little to the debris field. */
	void NotifyLaunch() { AddDebris(0.012f); }

	bool GetFactionInfo(int32 CountryId, FEarthfallFactionInfo& OutInfo) const;
	const TArray<FEarthfallMarker>& GetMarkers() const { return Markers; }
	/** 0 = clean sky, 1 = a shell of debris that destroys most launches. */
	float GetDebrisLevel() const { return DebrisLevel; }
	void AddDebris(float Amount);
	int32 GetInterceptCount() const { return InterceptCount; }

private:
	struct FFaction
	{
		int32 CountryId = 0;
		FString Code;
		FString Name;
		/** Planet-local direction of the capital: where its missiles leave from and where its enemies aim. */
		FVector Capital = FVector::ForwardVector;
		float MilitaryStrength = 50.0f;
		int32 NuclearStockpile = 0;
		int32 Bloc = 0;
		TArray<int32> AtWar;
		TArray<int32> Allies;
		float LaunchTimer = 2.0f;
		float GroupTimer = 6.0f;
		int32 Exchanges = 0;
		bool bFallen = false;
	};
	struct FPendingJoin
	{
		int32 Ally = 0;
		int32 Enemy = 0;
		float Delay = 0.0f;
	};
	struct FUnit
	{
		FVector Direction = FVector::ForwardVector;
		int32 Owner = 0;
		int32 Target = 0;
		int32 Volleys = 3;
		float FireTimer = 0.0f;
	};
	struct FInterceptor
	{
		FVector Start = FVector::ZeroVector;
		TWeakObjectPtr<AEarthfallProjectile> Target;
		FString Defender;
		float Age = 0.0f;
		float Duration = 0.55f;
	};
	struct FSatellite
	{
		FVector Direction = FVector::ForwardVector;
		FVector Axis = FVector::UpVector;
		float ReadyAt = 0.0f;
	};
	struct FDebris
	{
		FVector Axis = FVector::UpVector;
		FVector Start = FVector::ForwardVector;
		float Altitude = 1.12f;
		float Speed = 0.1f;
	};
	struct FPendingMeteor
	{
		FVector Direction = FVector::ForwardVector;
		float Delay = 0.0f;
	};

	TArray<FFaction> Factions;
	TMap<int32, int32> FactionByCountry;
	TArray<FPendingJoin> PendingJoins;
	TArray<FUnit> Units;
	TArray<FInterceptor> Interceptors;
	TArray<FSatellite> Satellites;
	TArray<FDebris> Debris;
	TArray<FPendingMeteor> PendingMeteors;
	TArray<FEarthfallMarker> Markers;
	/** Country ID -> world time its interceptor battery has reloaded. */
	TMap<int32, float> BatteryReadyAt;
	TWeakObjectPtr<AEarthOrbitPawn> MeteorPawn;
	float WorldTime = 0.0f;
	float DebrisLevel = 0.0f;
	float DebrisNewsLevel = 0.0f;
	float DiplomacyTimer = 0.0f;
	int32 PendingWarCountry = 0;
	int32 InterceptCount = 0;
	bool bFactionsBuilt = false;

	AEarthfallPlanet* FindPlanet() const;
	UEarthfallPopulationSubsystem* Population() const;
	void BuildFactions();
	/** The faction for a country, created on demand; Hint is where it was clicked or struck. -1 for open ocean. */
	int32 FactionFor(int32 CountryId, const FVector& Hint);
	bool AreAtWar(int32 A, int32 B) const;
	void DeclareWar(int32 A, int32 B, const TCHAR* Reason);
	void MakePeace(int32 Faction);
	void TickWars(float DeltaTime);
	void TickUnits(float DeltaTime);
	void TickDefense(float DeltaTime);
	void TickDebrisField(float DeltaTime);
	void BuildMarkers();
	void LaunchMissile(FFaction& From, const FFaction& At, bool bNuclear);
	void SpawnUnit(int32 Owner, int32 Target, const FVector& From);

	FString UseVolcano(const FVector& LocalDirection);
	FString UseTsunami(const FVector& LocalDirection);
	FString UseMeteorShower(const FVector& LocalDirection, AEarthOrbitPawn* Pawn);
	FString UseWhisperOfWar(const FVector& LocalDirection);
	FString UseForcePeace(const FVector& LocalDirection);
	FString UseSatellite(const FVector& LocalDirection);
	FString UseStrikeGroup(const FVector& LocalDirection);
};
