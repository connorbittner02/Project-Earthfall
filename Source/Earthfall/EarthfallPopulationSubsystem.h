#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EarthfallPopulationSubsystem.generated.h"

USTRUCT()
struct FEarthfallTelemetry
{
	GENERATED_BODY()

	UPROPERTY()
	double LivingPopulation = 8100000000.0;
	UPROPERTY()
	double TotalCasualties = 0.0;
	UPROPERTY()
	double DisplacedPopulation = 0.0;
	/** Subset of TotalCasualties drowned by tsunami landfall. */
	UPROPERTY()
	double TsunamiCasualties = 0.0;
	/** Subset of TotalCasualties killed by drifting radioactive fallout rather than blast. */
	UPROPERTY()
	double FalloutCasualties = 0.0;
	UPROPERTY()
	float GridBlackoutPercent = 0.0f;
	UPROPERTY()
	float AtmosphereHealth = 100.0f;
	UPROPERTY()
	float MeanTemperatureC = 15.0f;
	UPROPERTY()
	float DebrisMass = 0.0f;
	/** Soot currently suspended in the atmosphere, teragrams. */
	UPROPERTY()
	float SootTeragrams = 0.0f;
	/** Airborne radioactivity, 0..100. */
	UPROPERTY()
	float FalloutIndex = 0.0f;
	/** Sunlight still reaching the surface through the soot, percent. */
	UPROPERTY()
	float SunlightPercent = 100.0f;
	UPROPERTY()
	FString LatestDispatch = TEXT("GLOBAL NETWORKS NOMINAL  //  AWAITING FIRST STRIKE");
};

/** Government condition of one nation, worst last. */
enum class EEarthfallStability : uint8
{
	Stable,
	Emergency,
	MartialLaw,
	Collapsed
};

/** Live figures for one nation, as shown in the HUD's Country Dossier. */
struct FEarthfallCountryStats
{
	int32 Id = 0;
	FString Name;
	double InitialPopulation = 0.0;
	double LivingPopulation = 0.0;
	double Casualties = 0.0;
	double Displaced = 0.0;
	float GridPercent = 100.0f;
	float GridCapacityGW = 0.0f;
	float DefenseReadiness = 0.0f;
	EEarthfallStability Stability = EEarthfallStability::Stable;
};

struct FEarthfallPopulationCell
{
	double LivingPopulation = 0.0;
	double DisplacedPopulation = 0.0;
	float PowerGridHealth = 1.0f;
	float HazardHeat = 0.0f;
	float CityLightPotential = 0.0f;
};

struct FEarthfallNewsEvent
{
	int32 Priority = 0;
	uint64 Sequence = 0;
	FString Text;
	/** Remaining on-air seconds; dispatches retire so later strikes are never permanently masked. */
	float TimeToLive = 0.0f;
};

/** One drifting cloud of soot and fallout. The GPU AshPlumeRT draws it; this copy kills people. */
struct FEarthfallAshPlume
{
	double Latitude = 0.0;
	double Longitude = 0.0;
	float RadiusRadians = 0.1f;
	float Soot = 0.0f;
	float Fallout = 0.0f;
};

UCLASS()
class EARTHFALL_API UEarthfallPopulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/**
	 * Resolve one detonation against the population grid and loft its ash plume.
	 * Energy already includes the yield multiplier; the multipliers are passed too
	 * so soot, fallout and climate forcing can scale with them.
	 */
	void ApplyStrike(const FVector& LocalDirection, float RadiusRadians, float Energy, int32 WeaponIndex,
		float YieldMultiplier = 1.0f, float FalloutMultiplier = 1.0f);
	void ResetSimulation();
	FEarthfallTelemetry GetTelemetry() const { return Telemetry; }
	float GetCityLightAtDirection(const FVector& LocalDirection) const;

	/** Every dispatch still on air, joined for the HUD ticker. Highest priority first. */
	FString GetDispatchTicker() const;

	/** Angular speed of a tsunami front in radians per second. Shared with M_TsunamiStep. */
	static constexpr float TsunamiSpeed = 0.16f;

	/** Start a tsunami at an ocean impact; it trips coastal grids as its front arrives. */
	void AddTsunami(const FVector& LocalDirection, float Amplitude);

	/** Country ID (0 = open ocean) under a planet-local direction, from the Natural Earth mask. */
	int32 GetCountryAtDirection(const FVector& LocalDirection) const;
	int32 GetCountryCount() const { return CountryStates.Num(); }
	bool GetCountryStats(int32 CountryId, FEarthfallCountryStats& OutStats) const;
	/** Push a headline summarising one nation onto the news bar. */
	void AnnounceCountry(int32 CountryId);
	/** Put a headline on the news bar. Priority 1 (routine) to 9 (extinction-level). */
	void PostDispatch(const FString& Text, int32 Priority) { EnqueueNews(Text, Priority); }
	/** Kill this fraction of everyone alive, everywhere (famine, exposure). Returns the dead. */
	double ApplyGlobalAttrition(double Fraction);
	static const TCHAR* StabilityLabel(EEarthfallStability Stability);

	/** Simulation clock shared with the ash advection shader so both use the same wind. */
	double GetWindTime() const { return WindTime; }

private:
	struct FCityAnchor
	{
		FString Name;
		FVector Direction = FVector::ForwardVector;
		float Weight = 1.0f;
	};

	static constexpr int32 GridWidth = 512;
	static constexpr int32 GridHeight = 256;
	static constexpr double InitialPopulation = 8100000000.0;

	/** Dispatch queue tuning. */
	static constexpr int32 MaxDispatches = 8;
	static constexpr float DispatchRotationSeconds = 4.5f;
	static constexpr float DispatchBaseLifetimeSeconds = 18.0f;
	static constexpr float DispatchLifetimePerPriority = 4.0f;

	/** Environmental relaxation toward the soot-forced equilibrium. */
	static constexpr float BaselineAtmosphereHealth = 100.0f;
	static constexpr float BaselineTemperatureC = 15.0f;
	static constexpr float AtmosphereRecoveryPerSecond = 0.18f;
	static constexpr float HazardCoolPerSecond = 0.25f;
	static constexpr int32 HazardCellsPerTick = 4096;

	/** Plume tuning. The decay times mirror ash_advection.hlsl. */
	static constexpr int32 MaxPlumes = 16;
	static constexpr float SootDecaySeconds = 170.0f;
	static constexpr float FalloutDecaySeconds = 110.0f;
	static constexpr float PlumeSpreadRadiansPerSecond = 0.004f;
	static constexpr float FalloutDoseInterval = 0.5f;

	static constexpr int32 CountryGridWidth = 1024;
	static constexpr int32 CountryGridHeight = 512;

	struct FCountryState
	{
		double InitialPopulation = 0.0;
		double LivingPopulation = 0.0;
		double Displaced = 0.0;
		float GridSum = 0.0f;
		int32 CellCount = 0;
		EEarthfallStability Stability = EEarthfallStability::Stable;
	};

	/** 1024x512 country IDs loaded from Content/Earth/Data/CountryID.bin. */
	TArray<uint8> CountryGrid;
	/** Country ID of each population cell. */
	TArray<uint8> CellCountry;
	TArray<FCountryState> CountryStates;
	struct FTsunami
	{
		FVector Direction = FVector::ForwardVector;
		float Radius = 0.0f;
		float Amplitude = 1.0f;
		TArray<uint8> LandfallCountries;
	};
	struct FCoastalCell
	{
		int32 Index = 0;
		FVector Direction = FVector::ForwardVector;
	};
	TArray<FTsunami> Tsunamis;
	/** Land cells within two cells of open water: the only ones a tsunami can reach. */
	TArray<FCoastalCell> CoastalCells;
	TArray<FEarthfallPopulationCell> Cells;
	TArray<FCityAnchor> Cities;
	TArray<FEarthfallNewsEvent> NewsQueue;
	TArray<FEarthfallAshPlume> Plumes;
	FEarthfallTelemetry Telemetry;
	float GridPowerSum = 0.0f;
	uint64 NewsSequence = 0;
	float DispatchRotationTimer = 0.0f;
	int32 DispatchCursor = 0;
	int32 HazardSweepCursor = 0;
	double WindTime = 0.0;
	float FalloutDoseTimer = 0.0f;
	float FalloutNewsTimer = 0.0f;
	double FalloutCasualtiesSinceNews = 0.0;

	void LoadCountryGrid();
	void BuildCityAnchors();
	void SeedPopulation();
	int32 GetCellIndex(const FVector& LocalDirection) const;
	FString DescribeImpactRegion(const FVector& LocalDirection, bool& bNearCity) const;
	void EnqueueNews(const FString& Text, int32 Priority);
	void AdvanceDispatches(float DeltaTime);
	void AdvancePlumes(float DeltaTime);
	void AdvanceTsunamis(float DeltaTime);
	void ApplyFalloutDose(float DeltaTime);
	void RecoverEnvironment(float DeltaTime);
	void RefreshPopulationTotals();
	static float DispatchLifetimeForPriority(int32 Priority);
};
