#include "EarthfallPopulationSubsystem.h"
#include "EarthfallTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"

namespace
{
constexpr double EarthRadiusKm = 6371.0;

struct FCountryRecord
{
	const TCHAR* Name;
	double Population;
	float GridCapacityGW;
	float DefenseReadiness;
};

// Natural Earth 1:50m admin-0 countries; index is the ID stored in the mask.
const FCountryRecord GCountryTable[] = {
#include "EarthfallCountryTable.inl"
};

FVector DirectionFromRadians(double Latitude, double Longitude)
{
	return FVector(FMath::Cos(Latitude) * FMath::Cos(Longitude),
		-FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
}

FString FormatPeople(double Count)
{
	if (Count >= 1.e9) return FString::Printf(TEXT("%.2fB"), Count / 1.e9);
	if (Count >= 1.e6) return FString::Printf(TEXT("%.2fM"), Count / 1.e6);
	if (Count >= 1.e3) return FString::Printf(TEXT("%.1fK"), Count / 1.e3);
	return FString::Printf(TEXT("%.0f"), Count);
}
}

void UEarthfallPopulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadCountryGrid();
	BuildCityAnchors();
	ResetSimulation();
}

void UEarthfallPopulationSubsystem::Deinitialize()
{
	Cells.Reset();
	Cities.Reset();
	NewsQueue.Reset();
	Plumes.Reset();
	Super::Deinitialize();
}

TStatId UEarthfallPopulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEarthfallPopulationSubsystem, STATGROUP_Tickables);
}

void UEarthfallPopulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UWorld* World = GetWorld();
	// Editor preview worlds share the subsystem lifetime but must not age the sim.
	if (!World || !World->IsGameWorld() || DeltaTime <= 0.0f) return;
	WindTime += DeltaTime;
	AdvanceDispatches(DeltaTime);
	AdvancePlumes(DeltaTime);
	AdvanceTsunamis(DeltaTime);
	RecoverEnvironment(DeltaTime);
}

float UEarthfallPopulationSubsystem::DispatchLifetimeForPriority(int32 Priority)
{
	// Larger events stay on air longer, but nothing is permanent.
	return DispatchBaseLifetimeSeconds + FMath::Clamp(Priority, 0, 10) * DispatchLifetimePerPriority;
}

void UEarthfallPopulationSubsystem::AdvanceDispatches(float DeltaTime)
{
	if (NewsQueue.IsEmpty()) return;

	uint64 NewestSequence = 0;
	for (const FEarthfallNewsEvent& Event : NewsQueue)
	{
		NewestSequence = FMath::Max(NewestSequence, Event.Sequence);
	}

	for (FEarthfallNewsEvent& Event : NewsQueue) Event.TimeToLive -= DeltaTime;

	// Retire stale dispatches, but always keep the newest one so the ticker
	// never goes blank mid-session.
	const int32 Retired = NewsQueue.RemoveAll([NewestSequence](const FEarthfallNewsEvent& Event)
	{
		return Event.TimeToLive <= 0.0f && Event.Sequence != NewestSequence;
	});
	if (Retired > 0) DispatchCursor = 0;

	// Rotate the headline. Without this, one priority-10 dispatch holds slot 0
	// forever and every later strike is reported to nobody.
	DispatchRotationTimer += DeltaTime;
	if (DispatchRotationTimer >= DispatchRotationSeconds && NewsQueue.Num() > 1)
	{
		DispatchRotationTimer = 0.0f;
		DispatchCursor = (DispatchCursor + 1) % NewsQueue.Num();
	}
	if (!NewsQueue.IsValidIndex(DispatchCursor)) DispatchCursor = 0;
	Telemetry.LatestDispatch = NewsQueue[DispatchCursor].Text;
}

void UEarthfallPopulationSubsystem::AdvancePlumes(float DeltaTime)
{
	float TotalSoot = 0.0f;
	float TotalFallout = 0.0f;
	const float FalloutKeep = FMath::Exp(-DeltaTime / FalloutDecaySeconds);
	for (FEarthfallAshPlume& Plume : Plumes)
	{
		// Same wind field the GPU uses to move the visible plume.
		// As in ash_advection.hlsl: lofted carbon rides the faster upper wind and outlasts low smoke.
		const FVector2D Wind = EarthfallWind::Velocity(Plume.Latitude, Plume.Longitude, WindTime) * (0.55 + 1.75 * Plume.Loft);
		Plume.Longitude += Wind.X / FMath::Max(FMath::Cos(Plume.Latitude), 0.08) * DeltaTime;
		Plume.Latitude = FMath::Clamp(Plume.Latitude + Wind.Y * DeltaTime, -1.50, 1.50);
		if (Plume.Longitude > PI) Plume.Longitude -= 2.0 * PI;
		if (Plume.Longitude < -PI) Plume.Longitude += 2.0 * PI;
		Plume.RadiusRadians = FMath::Min(Plume.RadiusRadians + PlumeSpreadRadiansPerSecond * DeltaTime, 0.95f);
		Plume.Soot *= FMath::Exp(-DeltaTime / (SootDecaySeconds * (1.0f + 2.0f * Plume.Loft)));
		Plume.Fallout *= FalloutKeep;
		TotalSoot += Plume.Soot;
		TotalFallout += Plume.Fallout;
	}
	Plumes.RemoveAll([](const FEarthfallAshPlume& Plume) { return Plume.Soot < 0.02f && Plume.Fallout < 0.01f; });

	Telemetry.SootTeragrams = TotalSoot;
	Telemetry.FalloutIndex = 100.0f * (1.0f - FMath::Exp(-TotalFallout / 8.0f));
	Telemetry.SunlightPercent = 100.0f * FMath::Exp(-TotalSoot / 600.0f);

	FalloutDoseTimer += DeltaTime;
	GrowthTimer += DeltaTime;
	if (GrowthTimer >= 1.0f)
	{
		GrowCities(GrowthTimer);
		GrowthTimer = 0.0f;
	}
	if (FalloutDoseTimer >= FalloutDoseInterval)
	{
		ApplyFalloutDose(FalloutDoseTimer);
		FalloutDoseTimer = 0.0f;
	}

	FalloutNewsTimer += DeltaTime;
	if (FalloutNewsTimer >= 12.0f && FalloutCasualtiesSinceNews >= 250000.0)
	{
		const FEarthfallAshPlume* Worst = nullptr;
		for (const FEarthfallAshPlume& Plume : Plumes)
		{
			if (!Worst || Plume.Fallout > Worst->Fallout) Worst = &Plume;
		}
		if (Worst)
		{
			bool bNearCity = false;
			const FString Region = DescribeImpactRegion(DirectionFromRadians(Worst->Latitude, Worst->Longitude), bNearCity);
			EnqueueNews(FString::Printf(TEXT("[FALLOUT · %s] Radioactive plume drifting downwind  //  +%s fallout casualties  //  %.0f%% sunlight reaching surface"),
				*Region, *FormatPeople(FalloutCasualtiesSinceNews), Telemetry.SunlightPercent), 4);
		}
		FalloutCasualtiesSinceNews = 0.0;
		FalloutNewsTimer = 0.0f;
	}
}

void UEarthfallPopulationSubsystem::ApplyFalloutDose(float DeltaTime)
{
	if (Cells.IsEmpty()) return;
	double NewCasualties = 0.0;
	for (const FEarthfallAshPlume& Plume : Plumes)
	{
		if (Plume.Fallout < 0.02f) continue;
		// A plume thins as it spreads, so dose falls with the square of its radius.
		const float Intensity = FMath::Min(Plume.Fallout * FMath::Square(0.25f / FMath::Max(Plume.RadiusRadians, 0.1f)), 6.0f);
		const double PeakFraction = 1.0 - FMath::Exp(-0.0035 * Intensity * DeltaTime);
		if (PeakFraction < 1.e-6) continue;

		const FVector Center = DirectionFromRadians(Plume.Latitude, Plume.Longitude);
		const float CosRadius = FMath::Cos(Plume.RadiusRadians);
		const int32 RowMin = FMath::Clamp(FMath::FloorToInt((0.5 - (Plume.Latitude + Plume.RadiusRadians) / PI) * GridHeight), 0, GridHeight - 1);
		const int32 RowMax = FMath::Clamp(FMath::CeilToInt((0.5 - (Plume.Latitude - Plume.RadiusRadians) / PI) * GridHeight), 0, GridHeight - 1);
		for (int32 Y = RowMin; Y <= RowMax; ++Y)
		{
			const double Latitude = PI * (0.5 - (static_cast<double>(Y) + 0.5) / GridHeight);
			for (int32 X = 0; X < GridWidth; ++X)
			{
				FEarthfallPopulationCell& Cell = Cells[Y * GridWidth + X];
				if (Cell.LivingPopulation <= 0.0) continue;
				const double Longitude = ((static_cast<double>(X) + 0.5) / GridWidth - 0.5) * 2.0 * PI;
				const float Dot = FVector::DotProduct(DirectionFromRadians(Latitude, Longitude), Center);
				if (Dot < CosRadius) continue;
				const float Profile = 1.0f - FMath::SmoothStep(0.0f, 1.0f, FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)) / Plume.RadiusRadians);
				const double Before = Cell.LivingPopulation;
				const double Casualties = Before * PeakFraction * Profile;
				Cell.DisplacedPopulation = FMath::Max(0.0, Cell.DisplacedPopulation - Casualties * (Cell.DisplacedPopulation / Before));
				Cell.LivingPopulation = Before - Casualties;
				// Survivors flee the plume three times faster than it kills them.
				const double NotYetDisplaced = FMath::Max(0.0, Cell.LivingPopulation - Cell.DisplacedPopulation);
				Cell.DisplacedPopulation += NotYetDisplaced * FMath::Min(PeakFraction * Profile * 3.0, 1.0);
				NewCasualties += Casualties;
			}
		}
	}
	if (NewCasualties > 0.0)
	{
		Telemetry.TotalCasualties += NewCasualties;
		Telemetry.FalloutCasualties += NewCasualties;
		FalloutCasualtiesSinceNews += NewCasualties;
		RefreshPopulationTotals();
	}
}

void UEarthfallPopulationSubsystem::RecoverEnvironment(float DeltaTime)
{
	// Soot shades the surface: the climate chases a colder equilibrium while it
	// hangs in the air and drifts back to baseline as it rains out.
	const float SootCoolingC = -22.0f * (1.0f - FMath::Exp(-Telemetry.SootTeragrams / 700.0f));
	const float TargetTemperature = BaselineTemperatureC + SootCoolingC;
	Telemetry.MeanTemperatureC += (TargetTemperature - Telemetry.MeanTemperatureC) * (1.0f - FMath::Exp(-DeltaTime / 20.0f));

	const float TargetAtmosphere = BaselineAtmosphereHealth * FMath::Exp(-Telemetry.SootTeragrams / 900.0f)
		* (1.0f - 0.25f * Telemetry.FalloutIndex / 100.0f);
	if (Telemetry.AtmosphereHealth > TargetAtmosphere)
	{
		Telemetry.AtmosphereHealth += (TargetAtmosphere - Telemetry.AtmosphereHealth) * (1.0f - FMath::Exp(-DeltaTime / 8.0f));
	}
	else
	{
		Telemetry.AtmosphereHealth = FMath::FInterpConstantTo(Telemetry.AtmosphereHealth,
			TargetAtmosphere, DeltaTime, AtmosphereRecoveryPerSecond);
	}

	if (Cells.IsEmpty()) return;

	// Amortized sweep: cooling all 131072 cells every frame would be wasteful, so
	// each tick advances one slice and the per-visit step is scaled by how many
	// ticks it takes to come back around. Average cooling rate is unchanged.
	const int32 SliceSize = FMath::Min(HazardCellsPerTick, Cells.Num());
	const float SweepScale = static_cast<float>(Cells.Num()) / static_cast<float>(FMath::Max(1, SliceSize));
	const float CoolStep = HazardCoolPerSecond * DeltaTime * SweepScale;
	for (int32 Step = 0; Step < SliceSize; ++Step)
	{
		FEarthfallPopulationCell& Cell = Cells[HazardSweepCursor];
		// Cryo strikes drive HazardHeat negative, so relax toward zero from both sides.
		Cell.HazardHeat = FMath::FInterpConstantTo(Cell.HazardHeat, 0.0f, 1.0f, CoolStep);
		HazardSweepCursor = (HazardSweepCursor + 1) % Cells.Num();
	}
}

void UEarthfallPopulationSubsystem::RefreshPopulationTotals()
{
	double LivingTotal = 0.0;
	double DisplacedTotal = 0.0;
	GridPowerSum = 0.0f;
	for (FCountryState& State : CountryStates)
	{
		State.LivingPopulation = 0.0;
		State.Displaced = 0.0;
		State.GridSum = 0.0f;
	}
	const bool bTallyCountries = CellCountry.Num() == Cells.Num() && !CountryStates.IsEmpty();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FEarthfallPopulationCell& Cell = Cells[Index];
		// A cell can never report more displaced survivors than it has living ones.
		const double Displaced = FMath::Min(Cell.DisplacedPopulation, Cell.LivingPopulation);
		LivingTotal += Cell.LivingPopulation;
		DisplacedTotal += Displaced;
		GridPowerSum += Cell.PowerGridHealth;
		if (bTallyCountries)
		{
			FCountryState& State = CountryStates[CellCountry[Index]];
			State.LivingPopulation += Cell.LivingPopulation;
			State.Displaced += Displaced;
			State.GridSum += Cell.PowerGridHealth;
		}
	}

	// Governments degrade with deaths, refugees and blackout; each step down is news.
	for (int32 Id = 1; Id < CountryStates.Num(); ++Id)
	{
		FCountryState& State = CountryStates[Id];
		if (State.InitialPopulation <= 0.0 || State.CellCount == 0) continue;
		const double CasualtyFraction = 1.0 - State.LivingPopulation / State.InitialPopulation;
		const double RefugeeFraction = State.Displaced / State.InitialPopulation;
		const double Blackout = 1.0 - State.GridSum / State.CellCount;
		const double Stress = CasualtyFraction + 0.35 * RefugeeFraction + 0.25 * Blackout + State.BorderStress;
		const EEarthfallStability Next = Stress < 0.03 ? EEarthfallStability::Stable
			: (Stress < 0.15 ? EEarthfallStability::Emergency
			: (Stress < 0.40 ? EEarthfallStability::MartialLaw : EEarthfallStability::Collapsed));
		if (Next > State.Stability)
		{
			State.Stability = Next;
			// Micro-states would flood the ticker, so only nations over a million report.
			if (State.InitialPopulation >= 1.0e6)
			{
				EnqueueNews(FString::Printf(TEXT("[%s \u00B7 %s] %s casualties  //  %s refugees  //  %.0f%% of grid online"),
					StabilityLabel(Next), *FString(GCountryTable[Id].Name).ToUpper(),
					*FormatPeople(State.InitialPopulation - State.LivingPopulation), *FormatPeople(State.Displaced),
					(1.0 - Blackout) * 100.0), 3 + static_cast<int32>(Next));
			}
		}
	}
	Telemetry.LivingPopulation = FMath::Max(0.0, LivingTotal);
	Telemetry.DisplacedPopulation = FMath::Max(0.0, DisplacedTotal);
	Telemetry.GridBlackoutPercent = Cells.IsEmpty() ? 0.0f : (1.0f - GridPowerSum / Cells.Num()) * 100.0f;
}

FString UEarthfallPopulationSubsystem::GetDispatchTicker() const
{
	if (NewsQueue.IsEmpty()) return Telemetry.LatestDispatch;
	TArray<FString> Parts;
	Parts.Reserve(NewsQueue.Num());
	for (const FEarthfallNewsEvent& Event : NewsQueue) Parts.Add(Event.Text);
	return FString::Join(Parts, TEXT("     ///     "));
}

void UEarthfallPopulationSubsystem::BuildCityAnchors()
{
	Cities.Reset();
	auto AddCity = [this](const TCHAR* Name, double Lat, double Lon, float Weight)
	{
		FCityAnchor& City = Cities.AddDefaulted_GetRef();
		City.Name = Name;
		City.Direction = EarthfallSolar::DirectionFromLatLon(Lat, Lon);
		City.Weight = Weight;
	};

	AddCity(TEXT("Tokyo"), 35.68, 139.69, 12.0f);
	AddCity(TEXT("New York"), 40.71, -74.01, 9.0f);
	AddCity(TEXT("London"), 51.51, -0.13, 7.0f);
	AddCity(TEXT("Cairo"), 30.04, 31.24, 6.0f);
	AddCity(TEXT("Mumbai"), 19.08, 72.88, 10.0f);
	AddCity(TEXT("Sao Paulo"), -23.55, -46.63, 8.0f);
	AddCity(TEXT("Sydney"), -33.87, 151.21, 4.0f);
	AddCity(TEXT("Paris"), 48.86, 2.35, 5.0f);
	AddCity(TEXT("Lagos"), 6.52, 3.38, 6.0f);
	AddCity(TEXT("Jakarta"), -6.21, 106.85, 8.0f);
	AddCity(TEXT("Manila"), 14.60, 120.98, 7.0f);
	AddCity(TEXT("Dhaka"), 23.81, 90.41, 9.0f);
	AddCity(TEXT("Karachi"), 24.86, 67.01, 6.0f);
	AddCity(TEXT("Beijing"), 39.90, 116.41, 8.0f);
	AddCity(TEXT("Shanghai"), 31.23, 121.47, 9.0f);
	AddCity(TEXT("Delhi"), 28.61, 77.21, 10.0f);
	AddCity(TEXT("Mexico City"), 19.43, -99.13, 8.0f);
	AddCity(TEXT("Los Angeles"), 34.05, -118.24, 5.0f);
	AddCity(TEXT("Chicago"), 41.88, -87.63, 4.0f);
	AddCity(TEXT("Toronto"), 43.65, -79.38, 4.0f);
	AddCity(TEXT("Buenos Aires"), -34.60, -58.38, 5.0f);
	AddCity(TEXT("Lima"), -12.05, -77.04, 5.0f);
	AddCity(TEXT("Santiago"), -33.45, -70.67, 4.0f);
	AddCity(TEXT("Istanbul"), 41.01, 28.98, 6.0f);
	AddCity(TEXT("Moscow"), 55.76, 37.62, 5.0f);
	AddCity(TEXT("Johannesburg"), -26.20, 28.04, 4.0f);
	AddCity(TEXT("Nairobi"), -1.29, 36.82, 3.0f);
	AddCity(TEXT("Kinshasa"), -4.32, 15.31, 4.0f);
	AddCity(TEXT("Bangkok"), 13.76, 100.50, 5.0f);
	AddCity(TEXT("Seoul"), 37.57, 126.98, 6.0f);
	AddCity(TEXT("Singapore"), 1.35, 103.82, 3.0f);
	AddCity(TEXT("Riyadh"), 24.71, 46.68, 3.0f);
	AddCity(TEXT("Tehran"), 35.69, 51.39, 5.0f);
	AddCity(TEXT("Madrid"), 40.42, -3.70, 4.0f);
	AddCity(TEXT("Berlin"), 52.52, 13.40, 4.0f);
	AddCity(TEXT("Rome"), 41.90, 12.50, 4.0f);
	AddCity(TEXT("Bogota"), 4.71, -74.07, 4.0f);
	AddCity(TEXT("Melbourne"), -37.81, 144.96, 3.0f);
	AddCity(TEXT("Hong Kong"), 22.32, 114.17, 5.0f);
	AddCity(TEXT("Osaka"), 34.69, 135.50, 5.0f);
	AddCity(TEXT("Yellowstone"), 44.43, -110.59, 0.2f);
	AddCity(TEXT("Reykjavik"), 64.15, -21.94, 0.4f);
	AddCity(TEXT("Cape Town"), -33.92, 18.42, 3.0f);
	AddCity(TEXT("Dubai"), 25.20, 55.27, 3.0f);
	AddCity(TEXT("Honolulu"), 21.31, -157.86, 1.0f);
	AddCity(TEXT("Anchorage"), 61.22, -149.90, 0.8f);
}

void UEarthfallPopulationSubsystem::SeedPopulation()
{
	Cells.SetNum(GridWidth * GridHeight);
	TArray<double> Weights;
	Weights.SetNumZeroed(Cells.Num());
	double TotalWeight = 0.0;

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		const double V = (static_cast<double>(Y) + 0.5) / GridHeight;
		const double Latitude = PI * (0.5 - V);
		const double LatitudeDegrees = FMath::RadiansToDegrees(Latitude);
		const double AreaWeight = FMath::Max(0.06, FMath::Cos(Latitude));

		for (int32 X = 0; X < GridWidth; ++X)
		{
			const double U = (static_cast<double>(X) + 0.5) / GridWidth;
			const double Longitude = (U - 0.5) * 2.0 * PI;
			const FVector Direction = DirectionFromRadians(Latitude, Longitude);

			const double BroadNoise = FMath::PerlinNoise2D(FVector2D(Longitude * 0.65, Latitude * 0.65));
			const double LandWeight = FMath::SmoothStep(-0.28, 0.30, BroadNoise);
			double Density = 0.00012 + LandWeight * 0.00030;
			for (const FCityAnchor& City : Cities)
			{
				const double ChordSquared = FMath::Max(0.0, 2.0 * (1.0 - FVector::DotProduct(Direction, City.Direction)));
				const double Sigma = City.Name == TEXT("Yellowstone") ? 0.045 : 0.12;
				Density += City.Weight * FMath::Exp(-ChordSquared / (2.0 * Sigma * Sigma));
			}

			// Broad inhabited belts fill the gaps between the named city anchors.
			const double Eurasia = FMath::Exp(-FMath::Square((LatitudeDegrees - 28.0) / 24.0))
				* FMath::Exp(-FMath::Square((FMath::RadiansToDegrees(Longitude) - 72.0) / 72.0));
			const double Africa = FMath::Exp(-FMath::Square((LatitudeDegrees - 3.0) / 25.0))
				* FMath::Exp(-FMath::Square((FMath::RadiansToDegrees(Longitude) - 22.0) / 38.0));
			const double Americas = FMath::Exp(-FMath::Square((LatitudeDegrees - 20.0) / 33.0))
				* FMath::Exp(-FMath::Square((FMath::RadiansToDegrees(Longitude) + 88.0) / 40.0));
			Density += Eurasia * 0.9 + Africa * 0.38 + Americas * 0.36;

			const int32 Index = Y * GridWidth + X;
			Weights[Index] = Density * AreaWeight;
			TotalWeight += Weights[Index];
		}
	}

	// Each nation's Natural Earth population is spread over that nation's own
	// cells by the density weights above, so nobody lives in the open ocean and
	// per-country tallies add up. The world total is scaled to InitialPopulation.
	const int32 CountryCount = UE_ARRAY_COUNT(GCountryTable);
	TArray<double> CountryWeight;
	CountryWeight.SetNumZeroed(CountryCount);
	for (int32 Index = 0; Index < Cells.Num(); ++Index) CountryWeight[CellCountry[Index]] += Weights[Index];
	double PlacedPopulation = 0.0;
	for (int32 Id = 1; Id < CountryCount; ++Id)
	{
		if (CountryWeight[Id] > 0.0) PlacedPopulation += GCountryTable[Id].Population;
	}
	const bool bHaveCountries = PlacedPopulation > 0.0;
	const double WorldScale = bHaveCountries ? InitialPopulation / PlacedPopulation : 1.0;

	GridPowerSum = 0.0f;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		FEarthfallPopulationCell& Cell = Cells[Index];
		const int32 Id = CellCountry[Index];
		if (bHaveCountries)
		{
			Cell.LivingPopulation = Id > 0 && CountryWeight[Id] > 0.0
				? GCountryTable[Id].Population * WorldScale * Weights[Index] / CountryWeight[Id] : 0.0;
		}
		else
		{
			Cell.LivingPopulation = TotalWeight > 0.0 ? InitialPopulation * Weights[Index] / TotalWeight : InitialPopulation / Cells.Num();
		}
		Cell.BaselinePopulation = Cell.LivingPopulation;
		Cell.DisplacedPopulation = 0.0;
		Cell.PowerGridHealth = 1.0f;
		Cell.HazardHeat = 0.0f;
		// Keep the emissive city-light mask localized to dense urban cells instead
		// of washing the entire globe with the low-density population background.
		Cell.CityLightPotential = FMath::Clamp((FMath::Loge(1.0 + Cell.LivingPopulation) - 12.0) / 3.2, 0.0, 1.0);
		GridPowerSum += 1.0f;
	}

	CountryStates.Reset();
	CountryStates.SetNum(CountryCount);
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		FCountryState& State = CountryStates[CellCountry[Index]];
		State.InitialPopulation += Cells[Index].LivingPopulation;
		State.LivingPopulation += Cells[Index].LivingPopulation;
		State.GridSum += 1.0f;
		++State.CellCount;
	}
}

int32 UEarthfallPopulationSubsystem::GetCellIndex(const FVector& LocalDirection) const
{
	const FVector Direction = LocalDirection.GetSafeNormal();
	const double Longitude = FMath::Atan2(-Direction.Y, Direction.X);
	const double Latitude = FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0));
	const double U = (Longitude + PI) / (2.0 * PI);
	const double V = (PI * 0.5 - Latitude) / PI;
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * GridWidth), 0, GridWidth - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * GridHeight), 0, GridHeight - 1);
	return Y * GridWidth + X;
}

FString UEarthfallPopulationSubsystem::DescribeImpactRegion(const FVector& LocalDirection, bool& bNearCity) const
{
	const FVector Direction = LocalDirection.GetSafeNormal();
	const FCityAnchor* NearestCity = nullptr;
	float NearestDot = -1.0f;
	for (const FCityAnchor& City : Cities)
	{
		const float Dot = FVector::DotProduct(Direction, City.Direction);
		if (Dot > NearestDot)
		{
			NearestDot = Dot;
			NearestCity = &City;
		}
	}

	bNearCity = NearestCity && NearestDot > FMath::Cos(FMath::DegreesToRadians(9.0));
	if (bNearCity) return NearestCity->Name.ToUpper();

	const double Lon = FMath::RadiansToDegrees(FMath::Atan2(-Direction.Y, Direction.X));
	const double Lat = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0)));
	auto In = [Lat, Lon](double LonMin, double LonMax, double LatMin, double LatMax)
	{
		return Lon >= LonMin && Lon <= LonMax && Lat >= LatMin && Lat <= LatMax;
	};
	if (Lat > 72.0) return TEXT("THE ARCTIC");
	if (Lat < -62.0) return TEXT("ANTARCTICA");
	if (In(-170.0, -52.0, 14.0, 72.0)) return TEXT("NORTH AMERICA");
	if (In(-82.0, -34.0, -56.0, 14.0)) return TEXT("SOUTH AMERICA");
	if (In(34.0, 63.0, 12.0, 42.0)) return TEXT("THE MIDDLE EAST");
	if (In(-11.0, 40.0, 36.0, 71.0)) return TEXT("EUROPE");
	if (In(-18.0, 52.0, -35.0, 37.0)) return TEXT("AFRICA");
	if (In(40.0, 180.0, 50.0, 72.0)) return TEXT("SIBERIA");
	if (In(60.0, 97.0, 5.0, 37.0)) return TEXT("SOUTH ASIA");
	if (In(97.0, 146.0, 18.0, 54.0)) return TEXT("EAST ASIA");
	if (In(92.0, 141.0, -11.0, 18.0)) return TEXT("SOUTHEAST ASIA");
	if (In(112.0, 155.0, -44.0, -10.0)) return TEXT("AUSTRALIA");
	if (Lat < -45.0) return TEXT("THE SOUTHERN OCEAN");
	if (Lon > 146.0 || Lon < -82.0) return TEXT("THE PACIFIC");
	if (Lon > -70.0 && Lon < 20.0) return TEXT("THE ATLANTIC");
	return TEXT("THE INDIAN OCEAN");
}

void UEarthfallPopulationSubsystem::ApplyStrike(const FVector& LocalDirection, float RadiusRadians, float Energy,
	int32 WeaponIndex, float YieldMultiplier, float FalloutMultiplier)
{
	if (Cells.IsEmpty()) SeedPopulation();
	const FVector Center = LocalDirection.GetSafeNormal();
	const float StrikeAngle = FMath::Clamp(RadiusRadians, 0.002f, PI * 0.85f);
	static const float PeakCasualtyFractions[] = {0.38f, 0.24f, 0.62f, 0.85f, 0.42f, 0.76f};
	static const float PeakDisplacementFractions[] = {0.56f, 0.64f, 0.82f, 0.38f, 0.48f, 0.88f};
	const int32 SafeWeapon = FMath::Clamp(WeaponIndex, 0, EarthfallWeapons::Count - 1);
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(SafeWeapon);
	const bool bNuclear = SafeWeapon == EarthfallWeapons::Nuclear;
	// Lethality compounds with energy: survival is (1 - peak)^scale, so a 100x
	// yield sterilises the blast zone instead of saturating at a clamped 1.15.
	const float EnergyScale = FMath::Max(Energy / 180.0f, 0.05f);
	const float YieldRoot = FMath::Sqrt(FMath::Max(YieldMultiplier, 0.01f));
	const float FalloutAngle = FMath::Min(StrikeAngle * (bNuclear ? 3.5f : (SafeWeapon == 2 ? 2.1f : 1.7f)), PI);
	double NewCasualties = 0.0;
	double NewDisplaced = 0.0;

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		const double Latitude = PI * (0.5 - (static_cast<double>(Y) + 0.5) / GridHeight);
		for (int32 X = 0; X < GridWidth; ++X)
		{
			FEarthfallPopulationCell& Cell = Cells[Y * GridWidth + X];
			if (Cell.LivingPopulation <= 0.0 && Cell.DisplacedPopulation <= 0.0) continue;
			const double Longitude = ((static_cast<double>(X) + 0.5) / GridWidth - 0.5) * 2.0 * PI;
			const float Dot = FVector::DotProduct(DirectionFromRadians(Latitude, Longitude), Center);
			if (Dot < FMath::Cos(FalloutAngle)) continue;
			const float AngularDistance = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
			const float CoreT = FMath::Clamp(AngularDistance / StrikeAngle, 0.0f, 1.0f);
			const float CoreProfile = 1.0f - FMath::SmoothStep(0.0f, 1.0f, CoreT);
			const double PopulationBeforeImpact = Cell.LivingPopulation;
			const double KillFraction = 1.0 - FMath::Pow(1.0 - PeakCasualtyFractions[SafeWeapon] * CoreProfile, EnergyScale);
			const double Casualties = PopulationBeforeImpact * KillFraction;
			if (PopulationBeforeImpact > 0.0)
			{
				Cell.DisplacedPopulation = FMath::Max(0.0, Cell.DisplacedPopulation
					- Casualties * (Cell.DisplacedPopulation / PopulationBeforeImpact));
			}
			Cell.LivingPopulation = FMath::Max(0.0, Cell.LivingPopulation - Casualties);
			NewCasualties += Casualties;

			const float FalloutT = FMath::Clamp(AngularDistance / FalloutAngle, 0.0f, 1.0f);
			const float FalloutProfile = 1.0f - FMath::SmoothStep(0.0f, 1.0f, FalloutT);
			const double NotYetDisplaced = FMath::Max(0.0, Cell.LivingPopulation - Cell.DisplacedPopulation);
			const double DisplaceFraction = 1.0 - FMath::Pow(1.0 - PeakDisplacementFractions[SafeWeapon] * FalloutProfile, EnergyScale);
			const double Displacement = NotYetDisplaced * DisplaceFraction;
			Cell.DisplacedPopulation += Displacement;
			NewDisplaced += Displacement;
			Cell.PowerGridHealth = FMath::Clamp(Cell.PowerGridHealth - FalloutProfile * (SafeWeapon == 3 ? 0.62f : 1.0f), 0.0f, 1.0f);
			Cell.HazardHeat = SafeWeapon == 4
				? FMath::Min(Cell.HazardHeat, -0.8f * CoreProfile)
				: FMath::Max(Cell.HazardHeat, CoreProfile * FMath::Min(EnergyScale, 2.0f));
			if (bNuclear) Cell.HazardHeat = FMath::Max(Cell.HazardHeat, 1.5f * FalloutProfile);
		}
	}

	Telemetry.TotalCasualties += NewCasualties;
	const float AtmosphereHit = (SafeWeapon == 4 ? 0.06f : bNuclear ? 1.2f : 0.22f + FMath::Min(EnergyScale, 1.15f) * 0.55f) * YieldRoot;
	Telemetry.AtmosphereHealth = FMath::Max(0.0f, Telemetry.AtmosphereHealth - AtmosphereHit);
	Telemetry.MeanTemperatureC += (SafeWeapon == 4 ? -0.55f : (SafeWeapon == 2 ? 0.22f : SafeWeapon == 3 ? -0.08f : bNuclear ? 0.38f : 0.04f)) * YieldRoot;
	Telemetry.DebrisMass += Energy * (SafeWeapon == 3 ? 0.012f : 0.028f);

	// Loft the ash plume. Soot shades the planet; fallout kills downwind.
	const float PlumeScale = FMath::Pow(FMath::Max(YieldMultiplier, 0.01f), 0.75f) * FalloutMultiplier;
	if (Spec.Soot * PlumeScale > 0.01f || Spec.Fallout * PlumeScale > 0.01f)
	{
		if (Plumes.Num() >= MaxPlumes) Plumes.RemoveAt(0);
		FEarthfallAshPlume& Plume = Plumes.AddDefaulted_GetRef();
		Plume.Latitude = FMath::Asin(FMath::Clamp(Center.Z, -1.0, 1.0));
		Plume.Longitude = FMath::Atan2(-Center.Y, Center.X);
		Plume.RadiusRadians = FMath::Clamp(StrikeAngle * 1.6f, 0.06f, 0.95f);
		Plume.Soot = Spec.Soot * 6.0f * PlumeScale;
		Plume.Fallout = Spec.Fallout * PlumeScale;
		Plume.Loft = FMath::Clamp(0.30f + 0.06f * FMath::Log2(FMath::Max(YieldMultiplier, 0.01f)) + (WeaponIndex == EarthfallWeapons::Nuclear ? 0.15f : 0.0f), 0.08f, 1.0f);
	}

	// Living, displaced and grid totals come from the authoritative grid. Running
	// them up strike by strike let the displaced figure drift above the living
	// population: casualties among the already-displaced were subtracted from the
	// cells but never from the global running total.
	RefreshPopulationTotals();

	bool bNearCity = false;
	const FString Region = DescribeImpactRegion(Center, bNearCity);
	const double ZoneKm = StrikeAngle * EarthRadiusKm;
	const FString PowerText = FString::Printf(TEXT("%.0f%% grid blackout"), Telemetry.GridBlackoutPercent);
	const FString YieldText = FMath::IsNearlyEqual(YieldMultiplier, 1.0f) ? FString() : FString::Printf(TEXT(" x%g YIELD"), YieldMultiplier);
	const TCHAR* Alert = bNuclear ? TEXT("fallout plume + regional EMP blackout")
		: (SafeWeapon == 2 ? TEXT("seismic and coastal wave watch")
		: (SafeWeapon == 1 ? TEXT("thermal plume advisory")
		: (SafeWeapon == 4 ? TEXT("regional freeze warning") : TEXT("impact response"))));
	const FString Dispatch = bNuclear
		? FString::Printf(TEXT("[NUCLEAR DETONATION%s · %s] %s casualties  //  %s displaced  //  %s  //  %s  //  %.0f KM ZONE"),
			*YieldText, *Region, *FormatPeople(NewCasualties), *FormatPeople(NewDisplaced), *PowerText, Alert, ZoneKm)
		: FString::Printf(TEXT("[BREAKING · %s] Orbital %s%s  //  %s casualties  //  %s displaced  //  %s  //  %s  //  %.0f KM ZONE"),
			*Region, Spec.ReportName, *YieldText, *FormatPeople(NewCasualties), *FormatPeople(NewDisplaced), *PowerText, Alert, ZoneKm);
	const int32 Priority = bNuclear ? 10
		: (Telemetry.AtmosphereHealth < 70.0f ? 5
		: (NewCasualties >= 100000000.0 ? 4
		: (NewCasualties >= 1000000.0 ? 3 : (bNearCity ? 2 : 1))));
	EnqueueNews(Dispatch, Priority);
}

void UEarthfallPopulationSubsystem::EnqueueNews(const FString& Text, int32 Priority)
{
	FEarthfallNewsEvent& Event = NewsQueue.AddDefaulted_GetRef();
	Event.Priority = Priority;
	Event.Sequence = ++NewsSequence;
	Event.Text = Text;
	Event.TimeToLive = DispatchLifetimeForPriority(Priority);
	const uint64 IncomingSequence = Event.Sequence;
	NewsQueue.StableSort([](const FEarthfallNewsEvent& A, const FEarthfallNewsEvent& B)
	{
		return A.Priority == B.Priority ? A.Sequence > B.Sequence : A.Priority > B.Priority;
	});
	if (NewsQueue.Num() > MaxDispatches)
	{
		// A low-priority dispatch must still reach air, so keep it over the oldest
		// entry that would otherwise survive the trim.
		const int32 IncomingIndex = NewsQueue.IndexOfByPredicate([IncomingSequence](const FEarthfallNewsEvent& E)
		{
			return E.Sequence == IncomingSequence;
		});
		if (IncomingIndex >= MaxDispatches) NewsQueue.Swap(MaxDispatches - 1, IncomingIndex);
		NewsQueue.SetNum(MaxDispatches);
	}
	// Point the ticker at the incoming dispatch; rotation takes over from here.
	const int32 CursorIndex = NewsQueue.IndexOfByPredicate([IncomingSequence](const FEarthfallNewsEvent& E)
	{
		return E.Sequence == IncomingSequence;
	});
	DispatchCursor = CursorIndex == INDEX_NONE ? 0 : CursorIndex;
	DispatchRotationTimer = 0.0f;
	Telemetry.LatestDispatch = NewsQueue.IsEmpty() ? Text : NewsQueue[DispatchCursor].Text;
}

void UEarthfallPopulationSubsystem::ResetSimulation()
{
	SeedPopulation();
	MeasureCities(true);
	Telemetry = FEarthfallTelemetry();
	Telemetry.LivingPopulation = InitialPopulation;
	GridPowerSum = static_cast<float>(Cells.Num());
	NewsQueue.Reset();
	Plumes.Reset();
	Tsunamis.Reset();
	NewsSequence = 0;
	DispatchCursor = 0;
	DispatchRotationTimer = 0.0f;
	HazardSweepCursor = 0;
	FalloutDoseTimer = 0.0f;
	FalloutNewsTimer = 0.0f;
	FalloutCasualtiesSinceNews = 0.0;
	EnqueueNews(Telemetry.LatestDispatch, 1);
}

float UEarthfallPopulationSubsystem::GetCityLightAtDirection(const FVector& LocalDirection) const
{
	if (Cells.IsEmpty()) return 0.0f;
	const FEarthfallPopulationCell& Cell = Cells[GetCellIndex(LocalDirection)];
	return FMath::Clamp(Cell.CityLightPotential * Cell.PowerGridHealth, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Countries
// ---------------------------------------------------------------------------
void UEarthfallPopulationSubsystem::LoadCountryGrid()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Earth/Data/CountryID.bin");
	if (!FFileHelper::LoadFileToArray(CountryGrid, *Path) || CountryGrid.Num() != CountryGridWidth * CountryGridHeight)
	{
		UE_LOG(LogTemp, Warning, TEXT("Earthfall: %s missing or malformed; countries disabled. Run Scripts/prepare_country_data.py."), *Path);
		CountryGrid.Init(0, CountryGridWidth * CountryGridHeight);
	}
	// Each population cell takes the ID at its centre in the finer mask.
	const int32 ScaleX = CountryGridWidth / GridWidth;
	const int32 ScaleY = CountryGridHeight / GridHeight;
	const int32 CountryCount = UE_ARRAY_COUNT(GCountryTable);
	CellCountry.SetNumUninitialized(GridWidth * GridHeight);
	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const uint8 Id = CountryGrid[(Y * ScaleY + ScaleY / 2) * CountryGridWidth + X * ScaleX + ScaleX / 2];
			CellCountry[Y * GridWidth + X] = Id < CountryCount ? Id : 0;
		}
	}

	CoastalCells.Reset();
	for (int32 Y = 2; Y < GridHeight - 2; ++Y)
	{
		const double Latitude = PI * (0.5 - (static_cast<double>(Y) + 0.5) / GridHeight);
		for (int32 X = 0; X < GridWidth; ++X)
		{
			if (CellCountry[Y * GridWidth + X] == 0) continue;
			bool bNearWater = false;
			for (int32 DY = -2; DY <= 2 && !bNearWater; ++DY)
			{
				for (int32 DX = -2; DX <= 2; ++DX)
				{
					if (CellCountry[(Y + DY) * GridWidth + (X + DX + GridWidth) % GridWidth] == 0)
					{
						bNearWater = true;
						break;
					}
				}
			}
			if (!bNearWater) continue;
			FCoastalCell& Coastal = CoastalCells.AddDefaulted_GetRef();
			Coastal.Index = Y * GridWidth + X;
			Coastal.Direction = DirectionFromRadians(Latitude, ((static_cast<double>(X) + 0.5) / GridWidth - 0.5) * 2.0 * PI);
		}
	}
}

int32 UEarthfallPopulationSubsystem::GetCountryAtDirection(const FVector& LocalDirection) const
{
	if (CountryGrid.IsEmpty()) return 0;
	const FVector Direction = LocalDirection.GetSafeNormal();
	const double U = (FMath::Atan2(-Direction.Y, Direction.X) + PI) / (2.0 * PI);
	const double V = (PI * 0.5 - FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0))) / PI;
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * CountryGridWidth), 0, CountryGridWidth - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * CountryGridHeight), 0, CountryGridHeight - 1);
	const int32 Id = CountryGrid[Y * CountryGridWidth + X];
	return Id < CountryStates.Num() ? Id : 0;
}

bool UEarthfallPopulationSubsystem::GetCountryStats(int32 CountryId, FEarthfallCountryStats& OutStats) const
{
	if (CountryId <= 0 || !CountryStates.IsValidIndex(CountryId)) return false;
	const FCountryState& State = CountryStates[CountryId];
	const FCountryRecord& Record = GCountryTable[CountryId];
	OutStats.Id = CountryId;
	OutStats.Name = Record.Name;
	OutStats.InitialPopulation = State.InitialPopulation;
	OutStats.LivingPopulation = State.LivingPopulation;
	OutStats.Casualties = FMath::Max(0.0, State.InitialPopulation - State.LivingPopulation);
	OutStats.Displaced = State.Displaced;
	OutStats.GridPercent = State.CellCount > 0 ? State.GridSum / State.CellCount * 100.0f : 100.0f;
	OutStats.GridCapacityGW = Record.GridCapacityGW;
	OutStats.DefenseReadiness = Record.DefenseReadiness;
	OutStats.Stability = State.Stability;
	return true;
}

const TCHAR* UEarthfallPopulationSubsystem::StabilityLabel(EEarthfallStability Stability)
{
	switch (Stability)
	{
	case EEarthfallStability::Emergency: return TEXT("EMERGENCY");
	case EEarthfallStability::MartialLaw: return TEXT("MARTIAL LAW");
	case EEarthfallStability::Collapsed: return TEXT("COLLAPSED");
	default: return TEXT("STABLE");
	}
}

void UEarthfallPopulationSubsystem::AnnounceCountry(int32 CountryId)
{
	FEarthfallCountryStats Stats;
	if (!GetCountryStats(CountryId, Stats)) return;
	EnqueueNews(FString::Printf(TEXT("[DOSSIER \u00B7 %s] Population %s  //  %s casualties  //  %s refugees  //  grid %.0f%%  //  government %s"),
		*Stats.Name.ToUpper(), *FormatPeople(Stats.LivingPopulation), *FormatPeople(Stats.Casualties),
		*FormatPeople(Stats.Displaced), Stats.GridPercent, StabilityLabel(Stats.Stability)), 6);
}

// ---------------------------------------------------------------------------
// Tsunamis. The GPU wave in TsunamiRT is what the player sees; these analytic
// fronts expand at the same speed and decide when each stretch of coast is hit.
// A coast only counts as hit if the great-circle path to it is open water, so a
// continent shields the seas behind it (diffraction around it is not modelled).
// ---------------------------------------------------------------------------
void UEarthfallPopulationSubsystem::AddTsunami(const FVector& LocalDirection, float Amplitude)
{
	if (Tsunamis.Num() >= 6) Tsunamis.RemoveAt(0);
	FTsunami& Tsunami = Tsunamis.AddDefaulted_GetRef();
	Tsunami.Direction = LocalDirection.GetSafeNormal();
	Tsunami.Amplitude = FMath::Clamp(Amplitude, 0.1f, 4.0f);
	bool bNearCity = false;
	EnqueueNews(FString::Printf(TEXT("[TSUNAMI WARNING \u00B7 %s] Ocean impact has displaced the sea surface  //  all coasts on alert"),
		*DescribeImpactRegion(Tsunami.Direction, bNearCity)), 6);
}

void UEarthfallPopulationSubsystem::AdvanceTsunamis(float DeltaTime)
{
	if (Tsunamis.IsEmpty() || Cells.IsEmpty()) return;
	double NewCasualties = 0.0;
	bool bAnyLandfall = false;
	for (FTsunami& Tsunami : Tsunamis)
	{
		const float Previous = Tsunami.Radius;
		Tsunami.Radius += TsunamiSpeed * DeltaTime;
		const float CosNear = FMath::Cos(Previous);
		const float CosFar = FMath::Cos(FMath::Min(Tsunami.Radius, PI));
		// A ring wave thins as it spreads and loses energy with range.
		const float Strength = Tsunami.Amplitude / FMath::Sqrt(1.0f + Tsunami.Radius * 10.0f) * FMath::Exp(-Tsunami.Radius / 1.1f);
		if (Strength < 0.10f) continue;
		for (const FCoastalCell& Coastal : CoastalCells)
		{
			const float Dot = FVector::DotProduct(Coastal.Direction, Tsunami.Direction);
			if (Dot > CosNear || Dot <= CosFar) continue;
			// Walk the path from the impact: two land samples before the shore block it.
			int32 LandSamples = 0;
			for (int32 Step = 1; Step <= 12 && LandSamples < 2; ++Step)
			{
				const FVector Along = FMath::Lerp(Tsunami.Direction, Coastal.Direction, Step / 14.0).GetSafeNormal();
				if (GetCountryAtDirection(Along) != 0) ++LandSamples;
			}
			if (LandSamples >= 2) continue;
			FEarthfallPopulationCell& Cell = Cells[Coastal.Index];
			Cell.PowerGridHealth = FMath::Max(0.0f, Cell.PowerGridHealth - Strength * 2.0f);
			const double Before = Cell.LivingPopulation;
			if (Before > 0.0)
			{
				const double Casualties = Before * FMath::Min(0.30, static_cast<double>(Strength) * 0.22);
				Cell.DisplacedPopulation = FMath::Max(0.0, Cell.DisplacedPopulation - Casualties * (Cell.DisplacedPopulation / Before));
				Cell.LivingPopulation = Before - Casualties;
				const double NotYetDisplaced = FMath::Max(0.0, Cell.LivingPopulation - Cell.DisplacedPopulation);
				Cell.DisplacedPopulation += NotYetDisplaced * FMath::Min(0.60, static_cast<double>(Strength) * 0.6);
				NewCasualties += Casualties;
			}
			bAnyLandfall = true;
			const uint8 Country = CellCountry[Coastal.Index];
			if (Country > 0 && !Tsunami.LandfallCountries.Contains(Country))
			{
				Tsunami.LandfallCountries.Add(Country);
				if (CountryStates.IsValidIndex(Country) && CountryStates[Country].InitialPopulation >= 1.0e6)
				{
					EnqueueNews(FString::Printf(TEXT("[MEGA-TSUNAMI LANDFALL \u00B7 %s] Wall of water strikes the coast  //  shoreline power grid failing  //  city lights going dark"),
						*FString(GCountryTable[Country].Name).ToUpper()), 8);
				}
			}
		}
	}
	Tsunamis.RemoveAll([](const FTsunami& Tsunami)
	{
		return Tsunami.Radius >= PI || Tsunami.Amplitude / FMath::Sqrt(1.0f + Tsunami.Radius * 10.0f) * FMath::Exp(-Tsunami.Radius / 1.1f) < 0.10f;
	});
	if (bAnyLandfall)
	{
		Telemetry.TotalCasualties += NewCasualties;
		Telemetry.TsunamiCasualties += NewCasualties;
		RefreshPopulationTotals();
	}
}

double UEarthfallPopulationSubsystem::ApplyGlobalAttrition(double Fraction)
{
	Fraction = FMath::Clamp(Fraction, 0.0, 1.0);
	if (Fraction <= 0.0) return 0.0;
	double Dead = 0.0;
	for (FEarthfallPopulationCell& Cell : Cells)
	{
		if (Cell.LivingPopulation <= 0.0) continue;
		const double Loss = Cell.LivingPopulation * Fraction;
		Cell.DisplacedPopulation = FMath::Max(0.0, Cell.DisplacedPopulation * (1.0 - Fraction));
		Cell.LivingPopulation -= Loss;
		Dead += Loss;
	}
	if (Dead > 0.0)
	{
		Telemetry.TotalCasualties += Dead;
		RefreshPopulationTotals();
	}
	return Dead;
}

void UEarthfallPopulationSubsystem::ForEachCellInRadius(const FVector& LocalDirection, float RadiusRadians, TFunctionRef<void(FEarthfallPopulationCell&)> Visit)
{
	if (Cells.Num() != GridWidth * GridHeight) return;
	const FVector Centre = LocalDirection.GetSafeNormal();
	const double Latitude = FMath::Asin(FMath::Clamp(Centre.Z, -1.0, 1.0));
	const double CosLimit = FMath::Cos(static_cast<double>(RadiusRadians));
	// Only the rows of the latitude band the circle can touch.
	const int32 FirstRow = FMath::Clamp(FMath::FloorToInt((PI * 0.5 - (Latitude + RadiusRadians)) / PI * GridHeight), 0, GridHeight - 1);
	const int32 LastRow = FMath::Clamp(FMath::CeilToInt((PI * 0.5 - (Latitude - RadiusRadians)) / PI * GridHeight), 0, GridHeight - 1);
	for (int32 Y = FirstRow; Y <= LastRow; ++Y)
	{
		const double CellLatitude = PI * 0.5 - (Y + 0.5) / GridHeight * PI;
		const double CosLat = FMath::Cos(CellLatitude);
		const double SinLat = FMath::Sin(CellLatitude);
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const double Longitude = (X + 0.5) / GridWidth * 2.0 * PI - PI;
			const FVector Direction(CosLat * FMath::Cos(Longitude), -CosLat * FMath::Sin(Longitude), SinLat);
			if (FVector::DotProduct(Direction, Centre) >= CosLimit) Visit(Cells[Y * GridWidth + X]);
		}
	}
}

double UEarthfallPopulationSubsystem::PopulationInRadius(const FVector& LocalDirection, float RadiusRadians) const
{
	double Total = 0.0;
	const_cast<UEarthfallPopulationSubsystem*>(this)->ForEachCellInRadius(LocalDirection, RadiusRadians,
		[&Total](FEarthfallPopulationCell& Cell) { Total += Cell.LivingPopulation; });
	return Total;
}

double UEarthfallPopulationSubsystem::ApplyRegionalAttrition(const FVector& LocalDirection, float RadiusRadians, double KillFraction, double DisplaceFraction, float GridDamage)
{
	KillFraction = FMath::Clamp(KillFraction, 0.0, 1.0);
	DisplaceFraction = FMath::Clamp(DisplaceFraction, 0.0, 1.0);
	double Dead = 0.0;
	ForEachCellInRadius(LocalDirection, RadiusRadians, [&](FEarthfallPopulationCell& Cell)
	{
		Cell.PowerGridHealth = FMath::Clamp(Cell.PowerGridHealth - GridDamage, 0.0f, 1.0f);
		if (Cell.LivingPopulation <= 0.0) return;
		const double Loss = Cell.LivingPopulation * KillFraction;
		Cell.LivingPopulation -= Loss;
		Cell.DisplacedPopulation = FMath::Min(Cell.LivingPopulation,
			Cell.DisplacedPopulation + (Cell.LivingPopulation - Cell.DisplacedPopulation) * DisplaceFraction);
		Dead += Loss;
	});
	if (Dead > 0.0 || GridDamage > 0.0f)
	{
		Telemetry.TotalCasualties += Dead;
		RefreshPopulationTotals();
	}
	return Dead;
}

void UEarthfallPopulationSubsystem::RepairRegion(const FVector& LocalDirection, float RadiusRadians)
{
	ForEachCellInRadius(LocalDirection, RadiusRadians, [](FEarthfallPopulationCell& Cell)
	{
		Cell.PowerGridHealth = 1.0f;
		Cell.HazardHeat = 0.0f;
		Cell.DisplacedPopulation *= 0.25;
	});
	// Fallout plumes centred in the blessed region rain out.
	const FVector Centre = LocalDirection.GetSafeNormal();
	Plumes.RemoveAll([&](const FEarthfallAshPlume& Plume)
	{
		const FVector Direction(FMath::Cos(Plume.Latitude) * FMath::Cos(Plume.Longitude), -FMath::Cos(Plume.Latitude) * FMath::Sin(Plume.Longitude), FMath::Sin(Plume.Latitude));
		return FVector::DotProduct(Direction, Centre) >= FMath::Cos(RadiusRadians);
	});
	RefreshPopulationTotals();
}

void UEarthfallPopulationSubsystem::SetBorderStress(const TMap<int32, float>& StressByCountry)
{
	bool bChanged = false;
	for (int32 Id = 1; Id < CountryStates.Num(); ++Id)
	{
		const float* Value = StressByCountry.Find(Id);
		const float Next = Value ? FMath::Clamp(*Value, 0.0f, 0.35f) : 0.0f;
		bChanged |= !FMath::IsNearlyEqual(Next, CountryStates[Id].BorderStress, 0.005f);
		CountryStates[Id].BorderStress = Next;
	}
	if (bChanged) RefreshPopulationTotals();
}

// ---------------------------------------------------------------------------
// Living cities
// ---------------------------------------------------------------------------
const TCHAR* UEarthfallPopulationSubsystem::CityTierLabel(int32 Tier)
{
	static const TCHAR* Labels[] = {TEXT("CITY"), TEXT("MEGACITY"), TEXT("ARCOLOGY")};
	return Labels[FMath::Clamp(Tier, 0, 2)];
}

void UEarthfallPopulationSubsystem::BoostCountryGrowth(int32 CountryId, float Multiplier, float Seconds)
{
	if (!CountryStates.IsValidIndex(CountryId)) return;
	CountryStates[CountryId].GrowthBoost = FMath::Max(CountryStates[CountryId].GrowthBoost, Multiplier);
	CountryStates[CountryId].GrowthBoostSeconds = FMath::Max(CountryStates[CountryId].GrowthBoostSeconds, Seconds);
}

void UEarthfallPopulationSubsystem::GrowCities(float DeltaTime)
{
	if (Cells.IsEmpty() || CellCountry.Num() != Cells.Num()) return;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		FEarthfallPopulationCell& Cell = Cells[Index];
		if (Cell.LivingPopulation <= 0.0 || Cell.BaselinePopulation <= 0.0) continue;
		const FCountryState& State = CountryStates[CellCountry[Index]];
		// Crews repair the grid wherever there are still people to do it.
		Cell.PowerGridHealth = FMath::Min(1.0f, Cell.PowerGridHealth + 0.006f * DeltaTime);
		if (State.Stability >= EEarthfallStability::MartialLaw) continue;
		// The displaced come home once things are calm.
		Cell.DisplacedPopulation *= FMath::Max(0.0, 1.0 - 0.01 * DeltaTime);
		// Growth needs peace, power and people who are not on the road.
		if (Cell.PowerGridHealth < 0.7f || Cell.HazardHeat > 0.1f || Cell.DisplacedPopulation > Cell.LivingPopulation * 0.2) continue;
		const double Cap = Cell.BaselinePopulation * MaxGrowth;
		if (Cell.LivingPopulation >= Cap) continue;
		const double Rate = GrowthPerSecond * State.GrowthBoost * (State.Stability == EEarthfallStability::Stable ? 1.0 : 0.4);
		Cell.LivingPopulation = FMath::Min(Cap, Cell.LivingPopulation * (1.0 + Rate * DeltaTime));
	}
	for (FCountryState& State : CountryStates)
	{
		if (State.GrowthBoostSeconds <= 0.0f) continue;
		State.GrowthBoostSeconds -= DeltaTime;
		if (State.GrowthBoostSeconds <= 0.0f) State.GrowthBoost = 1.0f;
	}
	RefreshPopulationTotals();
	MeasureCities(false);
}

void UEarthfallPopulationSubsystem::MeasureCities(bool bReset)
{
	// A hub's size is everyone within about 220 km of it. On a reset every hub
	// is measured; otherwise a few per call, round-robin.
	constexpr float HubRadius = 0.035f;
	const int32 Count = bReset ? Cities.Num() : FMath::Min(8, Cities.Num());
	for (int32 Step = 0; Step < Count; ++Step)
	{
		FCityAnchor& City = Cities[bReset ? Step : (CityCursor++ % Cities.Num())];
		const double Region = PopulationInRadius(City.Direction, HubRadius);
		if (bReset)
		{
			City.InitialRegion = Region;
			City.Tier = 0;
			City.Ratio = 1.0f;
			City.CountryId = GetCountryAtDirection(City.Direction);
			continue;
		}
		if (City.InitialRegion < 1.0e5) continue;
		const double Ratio = Region / City.InitialRegion;
		City.Ratio = static_cast<float>(Ratio);
		const int32 Tier = Ratio >= 2.2 ? 2 : (Ratio >= 1.5 ? 1 : 0);
		if (Tier > City.Tier)
		{
			UE_LOG(LogTemp, Display, TEXT("EARTHFALL_CITY %s reaches %s tier at %.2fx baseline"), *City.Name, CityTierLabel(Tier), Ratio);
			EnqueueNews(FString::Printf(TEXT("[URBAN BOOM - %s REACHES %s TIER] %s people  //  +%.0f%% on baseline"),
				*City.Name.ToUpper(), CityTierLabel(Tier), *FormatPeople(Region), (Ratio - 1.0) * 100.0), 5);
		}
		else if (Tier < City.Tier)
		{
			EnqueueNews(FString::Printf(TEXT("[URBAN DECLINE - %s FALLS TO %s TIER]"), *City.Name.ToUpper(), CityTierLabel(Tier)), 4);
		}
		City.Tier = Tier;
	}
}

void UEarthfallPopulationSubsystem::FillCityGrowth(TArray<FColor>& Out) const
{
	Out.SetNumUninitialized(Cells.Num());
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FEarthfallPopulationCell& Cell = Cells[Index];
		// Empty cells (ocean, ice) carry baseline so they never dim a coast by bilinear bleed.
		const double Ratio = Cell.BaselinePopulation > 0.0
			? FMath::Max(0.0, Cell.LivingPopulation - Cell.DisplacedPopulation) / Cell.BaselinePopulation : 1.0;
		const int32 Tier = CellCountry.Num() == Cells.Num() ? CountryStates[CellCountry[Index]].TechTier : 0;
		Out[Index] = FColor(static_cast<uint8>(1 + FMath::RoundToInt32(254.0 * FMath::Clamp(Ratio / MaxGrowth, 0.0, 1.0))), static_cast<uint8>(Tier * 85),
			static_cast<uint8>(FMath::RoundToInt32(255.0f * FMath::Clamp(Cell.PowerGridHealth, 0.0f, 1.0f))), 255);
	}
}

void UEarthfallPopulationSubsystem::GetCities(TArray<FEarthfallCityInfo>& Out) const
{
	Out.Reset(Cities.Num());
	for (const FCityAnchor& City : Cities)
	{
		if (City.InitialRegion < 1.0e5) continue;
		FEarthfallCityInfo& Info = Out.AddDefaulted_GetRef();
		Info.Name = City.Name;
		Info.Direction = City.Direction;
		Info.Tier = City.Tier;
		Info.Growth = City.Ratio;
	}
}

bool UEarthfallPopulationSubsystem::GetLargestCity(int32 CountryId, FString& OutName, int32& OutTier, float& OutGrowth) const
{
	const FCityAnchor* Best = nullptr;
	for (const FCityAnchor& City : Cities)
	{
		if (City.CountryId == CountryId && City.InitialRegion >= 1.0e5 && (!Best || City.InitialRegion > Best->InitialRegion)) Best = &City;
	}
	if (!Best) return false;
	OutName = Best->Name;
	OutTier = Best->Tier;
	OutGrowth = static_cast<float>(PopulationInRadius(Best->Direction, 0.035f) / Best->InitialRegion);
	return true;
}

void UEarthfallPopulationSubsystem::SetCountryTech(int32 CountryId, int32 Tier)
{
	if (CountryStates.IsValidIndex(CountryId)) CountryStates[CountryId].TechTier = FMath::Clamp(Tier, 0, 3);
}

void UEarthfallPopulationSubsystem::RepairCountry(int32 CountryId)
{
	if (CountryId <= 0 || CellCountry.Num() != Cells.Num()) return;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (CellCountry[Index] != CountryId) continue;
		Cells[Index].PowerGridHealth = 1.0f;
		Cells[Index].HazardHeat = 0.0f;
		Cells[Index].DisplacedPopulation *= 0.5;
	}
	RefreshPopulationTotals();
}
