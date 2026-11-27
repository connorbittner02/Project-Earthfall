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
	UPROPERTY()
	float GridBlackoutPercent = 0.0f;
	UPROPERTY()
	float AtmosphereHealth = 100.0f;
	UPROPERTY()
	float MeanTemperatureC = 15.0f;
	UPROPERTY()
	float DebrisMass = 0.0f;
	UPROPERTY()
	FString LatestDispatch = TEXT("GLOBAL NETWORKS NOMINAL  //  AWAITING FIRST STRIKE");
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
};

UCLASS()
class EARTHFALL_API UEarthfallPopulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ApplyStrike(const FVector& LocalDirection, float RadiusRadians, float Energy, int32 WeaponIndex);
	void ResetSimulation();
	FEarthfallTelemetry GetTelemetry() const { return Telemetry; }
	float GetCityLightAtDirection(const FVector& LocalDirection) const;

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

	TArray<FEarthfallPopulationCell> Cells;
	TArray<FCityAnchor> Cities;
	TArray<FEarthfallNewsEvent> NewsQueue;
	FEarthfallTelemetry Telemetry;
	float GridPowerSum = 0.0f;
	uint64 NewsSequence = 0;

	void BuildCityAnchors();
	void SeedPopulation();
	int32 GetCellIndex(const FVector& LocalDirection) const;
	FString DescribeImpactRegion(const FVector& LocalDirection, bool& bNearCity) const;
	void EnqueueNews(const FString& Text, int32 Priority);
};
