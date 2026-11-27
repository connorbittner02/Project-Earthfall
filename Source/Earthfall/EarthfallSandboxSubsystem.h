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
	constexpr uint8 AirTraffic = 4;  // airliners between hub cities
	constexpr uint8 Shipping = 5;    // freighters on the sea lanes
	constexpr uint8 Combat = 6;      // fighting in a rage or insurgency zone
	constexpr uint8 AirWing = 7;     // air strike wings crossing land
	constexpr uint8 Shield = 8;      // fusion shield over a tier-4 capital
	constexpr uint8 Army = 9;        // ground armies and garrisons
	constexpr uint8 Count = 10;
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
	/** 0 conventional, 1 hypersonic, 2 orbital defense, 3 fusion shielded. */
	int32 TechTier = 0;
	int32 NuclearStockpile = 0;
	TArray<FString> AtWarWith;
	TArray<FString> Allies;
	/** People currently infected inside the nation's outbreaks. */
	double Infected = 0.0;
	/** The standing order from Command mode: NONE, ATTACK <name>, MOBILIZED or EVACUATING. */
	FString Orders = TEXT("NONE");
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
	/** Pairs of factions currently at war. */
	int32 GetActiveWarCount() const;
	static const TCHAR* TechTierLabel(int32 Tier);
	int32 GetFleetCount() const;
	int32 GetArmyCount() const;
	int32 GetUnitCount() const { return Units.Num(); }
	int32 GetCommandedCountry() const;
	int32 GetBorderCrisisCount() const { return BorderCrises.Num(); }
	double GetInfected() const { return InfectedNow; }
	double GetCured() const { return CuredTotal; }
	double GetPlagueDead() const { return PlagueDead; }
	/** People infected in outbreaks centred on this nation. */
	double GetCountryInfected(int32 CountryId) const;

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
		int32 TechTier = 0;
		int32 Resources = 0;
		bool bFortified = false;
		bool bHoldFire = false;
		FString Orders = TEXT("NONE");
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
		int32 Volleys = 4;
		float FireTimer = 0.0f;
		/** A naval fleet keeps to water along Path; an air wing flies the great circle. */
		bool bNaval = false;
		/** A ground army: marches overland and stops at the sea. */
		bool bArmy = false;
		TArray<FVector> Path;
		int32 PathIndex = 0;
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
	/** A region of disease (0), rage (1) or insurgency (2). */
	struct FBioZone
	{
		FVector Direction = FVector::ForwardVector;
		float Radius = 0.05f;
		float Strength = 0.2f;
		float Age = 0.0f;
		float SpreadTimer = 6.0f;
		int32 Kind = 0;
		int32 CountryId = 0;
		int32 Generation = 0;
		double Infected = 0.0;
	};
	struct FRoute
	{
		FVector From = FVector::ForwardVector;
		FVector To = FVector::ForwardVector;
		int32 FromCountry = 0;
		int32 ToCountry = 0;
		float Phase = 0.0f;
		float Speed = 0.02f;
		bool bSea = false;
		bool bOpen = true;
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
	TArray<FBioZone> BioZones;
	/** 240x120 grid, 1.5 degrees a cell: 1 where the cell centre is open water. */
	TArray<uint8> WaterGrid;
	/** Country ID -> the countries it shares a border with. */
	TMap<int32, TArray<int32>> Neighbours;
	/** Countries already reported as overwhelmed by refugees. */
	TSet<int32> BorderCrises;
	/** Where an EMP has blinded ground batteries: XYZ direction, W cosine of the radius, and when it wears off. */
	TArray<TPair<FVector4, float>> EmpZones;
	/** Country ID -> a direction near the middle of it. */
	TMap<int32, FVector> CountryCentres;
	float BorderTimer = 0.0f;
	TArray<FRoute> Routes;
	float BioTimer = 0.0f;
	float BioNewsTimer = 0.0f;
	float RouteTimer = 0.0f;
	double InfectedNow = 0.0;
	double CuredTotal = 0.0;
	double PlagueDead = 0.0;
	bool bBioMaskDirty = false;
	/** Country ID -> world time its interceptor battery has reloaded. */
	TMap<int32, float> BatteryReadyAt;
	TWeakObjectPtr<AEarthOrbitPawn> MeteorPawn;
	float WorldTime = 0.0f;
	float DebrisLevel = 0.0f;
	float DebrisNewsLevel = 0.0f;
	float DiplomacyTimer = 0.0f;
	int32 PendingWarCountry = 0;
	/** The faction Command mode is giving orders to, or -1. */
	int32 CommandedFaction = -1;
	float SkirmishTimer = 0.0f;
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
	/** Mode 0 picks a fleet when the target can be reached by sea, 1 forces a fleet, 2 an air wing. */
	bool SpawnUnit(int32 Owner, int32 Target, const FVector& From, int32 Mode = 0);
	/** Mode 3 forces a ground army. */
	void FireMissile(const FVector& FromDirection, float FromAltitudeScale, const FVector& AimDirection, int32 AttackerCountry, bool bNuclear, int32 Munition = 0);
	/** The shell a faction of this tech tier loads for one shot. */
	static int32 PickMunition(int32 TechTier);
	bool IsDefenseBlinded(const FVector& LocalDirection) const;
	void BuildWaterGrid();
	/** A water-only route from near From to the reachable water nearest To. Empty if there is none. */
	bool FindSeaRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const;
	void BuildNeighbours();
	void TickBorders(float DeltaTime);

	FString UseVolcano(const FVector& LocalDirection);
	FString UseTsunami(const FVector& LocalDirection);
	FString UseMeteorShower(const FVector& LocalDirection, AEarthOrbitPawn* Pawn);
	FString UseWhisperOfWar(const FVector& LocalDirection);
	FString UseForcePeace(const FVector& LocalDirection);
	FString UseSatellite(const FVector& LocalDirection);
	FString UseStrikeGroup(const FVector& LocalDirection);
	FString UsePlague(const FVector& LocalDirection);
	FString UseRage(const FVector& LocalDirection);
	FString UseCure(const FVector& LocalDirection);
	FString UseInsurgency(const FVector& LocalDirection);
	FString UseCivTool(int32 Slot, const FVector& LocalDirection, AEarthOrbitPawn* Pawn);
	void SetTechTier(int32 Faction, int32 Tier);
	/** True if the great circle between two points never leaves land. */
	bool HasLandRoute(const FVector& From, const FVector& To) const;
	void TickSkirmishes(float DeltaTime);
	void AddBioZone(const FVector& Direction, float Radius, float Strength, int32 Kind, int32 Generation);
	void TickBio(float DeltaTime);
	void BuildRoutes();
	void TickTraffic(float DeltaTime);
};
