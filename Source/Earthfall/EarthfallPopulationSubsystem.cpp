#include "EarthfallPopulationSubsystem.h"

namespace
{
constexpr double EarthRadiusKm = 6371.0;

FVector DirectionFromLatLon(double LatitudeDegrees, double LongitudeDegrees)
{
	const double Latitude = FMath::DegreesToRadians(LatitudeDegrees);
	const double Longitude = FMath::DegreesToRadians(LongitudeDegrees);
	return FVector(FMath::Cos(Latitude) * FMath::Cos(Longitude),
		FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
}
}

void UEarthfallPopulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BuildCityAnchors();
	ResetSimulation();
}

void UEarthfallPopulationSubsystem::Deinitialize()
{
	Cells.Reset();
	Cities.Reset();
	Super::Deinitialize();
}

void UEarthfallPopulationSubsystem::BuildCityAnchors()
{
	Cities.Reset();
	auto AddCity = [this](const TCHAR* Name, double Lat, double Lon, float Weight)
	{
		FCityAnchor& City = Cities.AddDefaulted_GetRef();
		City.Name = Name;
		City.Direction = DirectionFromLatLon(Lat, Lon);
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
			const FVector Direction(FMath::Cos(Latitude) * FMath::Cos(Longitude),
				FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));

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

	GridPowerSum = 0.0f;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		FEarthfallPopulationCell& Cell = Cells[Index];
		Cell.LivingPopulation = TotalWeight > 0.0
			? InitialPopulation * Weights[Index] / TotalWeight
			: InitialPopulation / Cells.Num();
		Cell.DisplacedPopulation = 0.0;
		Cell.PowerGridHealth = 1.0f;
		Cell.HazardHeat = 0.0f;
		// Keep the emissive city-light mask localized to dense urban cells instead
		// of washing the entire globe with the low-density population background.
		Cell.CityLightPotential = FMath::Clamp((FMath::Loge(1.0 + Cell.LivingPopulation) - 12.0) / 3.2, 0.0, 1.0);
		GridPowerSum += 1.0f;
	}
}

int32 UEarthfallPopulationSubsystem::GetCellIndex(const FVector& LocalDirection) const
{
	const FVector Direction = LocalDirection.GetSafeNormal();
	const double Longitude = FMath::Atan2(Direction.Y, Direction.X);
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

	const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0)));
	if (FMath::Abs(Latitude) > 67.0) return TEXT("POLAR OCEAN");
	if (FMath::Abs(Longitude) > 125.0) return TEXT("PACIFIC OCEAN");
	if (Longitude > -25.0 && Longitude < 65.0 && Latitude < 35.0 && Latitude > -40.0) return TEXT("NORTH AFRICA");
	if (Longitude > 65.0 && Longitude < 150.0) return TEXT("EAST ASIA");
	if (Longitude < -25.0 && Longitude > -125.0) return TEXT("THE AMERICAS");
	return TEXT("OPEN OCEAN");
}

void UEarthfallPopulationSubsystem::ApplyStrike(const FVector& LocalDirection, float RadiusRadians, float Energy, int32 WeaponIndex)
{
	if (Cells.IsEmpty()) SeedPopulation();
	const FVector Center = LocalDirection.GetSafeNormal();
	const float StrikeAngle = FMath::Clamp(RadiusRadians, 0.002f, PI * 0.45f);
	static const float PeakCasualtyFractions[] = {0.38f, 0.24f, 0.62f, 0.85f, 0.42f};
	static const float PeakDisplacementFractions[] = {0.56f, 0.64f, 0.82f, 0.38f, 0.48f};
	const int32 SafeWeapon = FMath::Clamp(WeaponIndex, 0, 4);
	const float EnergyScale = FMath::Clamp(Energy / 180.0f, 0.2f, 1.15f);
	const float FalloutAngle = StrikeAngle * (SafeWeapon == 2 ? 2.1f : 1.7f);
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
			const FVector Direction(FMath::Cos(Latitude) * FMath::Cos(Longitude),
				FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
			const float Dot = FVector::DotProduct(Direction, Center);
			if (Dot < FMath::Cos(FalloutAngle)) continue;
			const float AngularDistance = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
			const float CoreT = FMath::Clamp(AngularDistance / StrikeAngle, 0.0f, 1.0f);
			const float CoreProfile = 1.0f - FMath::SmoothStep(0.0f, 1.0f, CoreT);
			const double PopulationBeforeImpact = Cell.LivingPopulation;
			const double Casualties = PopulationBeforeImpact * PeakCasualtyFractions[SafeWeapon] * EnergyScale * CoreProfile;
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
			const double Displacement = NotYetDisplaced * PeakDisplacementFractions[SafeWeapon]
				* EnergyScale * FalloutProfile;
			Cell.DisplacedPopulation += Displacement;
			NewDisplaced += Displacement;
			Cell.PowerGridHealth = FMath::Clamp(Cell.PowerGridHealth - FalloutProfile * (SafeWeapon == 3 ? 0.62f : 1.0f), 0.0f, 1.0f);
			Cell.HazardHeat = SafeWeapon == 4
				? FMath::Min(Cell.HazardHeat, -0.8f * CoreProfile)
				: FMath::Max(Cell.HazardHeat, CoreProfile * EnergyScale);
		}
	}

	Telemetry.TotalCasualties += NewCasualties;
	Telemetry.LivingPopulation = FMath::Max(0.0, Telemetry.LivingPopulation - NewCasualties);
	Telemetry.DisplacedPopulation += NewDisplaced;
	Telemetry.AtmosphereHealth = FMath::Max(0.0f, Telemetry.AtmosphereHealth - (SafeWeapon == 4 ? 0.06f : 0.22f + EnergyScale * 0.55f));
	Telemetry.MeanTemperatureC += SafeWeapon == 4 ? -0.55f : (SafeWeapon == 2 ? 0.22f : SafeWeapon == 3 ? -0.08f : 0.04f);
	Telemetry.DebrisMass += Energy * (SafeWeapon == 3 ? 0.012f : 0.028f);

	GridPowerSum = 0.0f;
	for (const FEarthfallPopulationCell& Cell : Cells) GridPowerSum += Cell.PowerGridHealth;
	Telemetry.GridBlackoutPercent = Cells.IsEmpty() ? 0.0f : (1.0f - GridPowerSum / Cells.Num()) * 100.0f;

	static const TCHAR* WeaponNames[] = {TEXT("kinetic impact"), TEXT("plasma lance"), TEXT("mantle charge"), TEXT("micro black hole"), TEXT("cryo impactor")};
	bool bNearCity = false;
	const FString Region = DescribeImpactRegion(Center, bNearCity);
	const double OffsetKm = StrikeAngle * EarthRadiusKm;
	const FString CasualtyText = NewCasualties >= 1000000.0
		? FString::Printf(TEXT("%.2fM"), NewCasualties / 1000000.0)
		: FString::Printf(TEXT("%.0f"), NewCasualties);
	const FString PowerText = FString::Printf(TEXT("%.0f%% grid blackout"), Telemetry.GridBlackoutPercent);
	const FString Alert = SafeWeapon == 2 ? TEXT("seismic and coastal wave watch")
		: (SafeWeapon == 1 ? TEXT("thermal plume advisory")
		: (SafeWeapon == 4 ? TEXT("regional freeze warning") : TEXT("impact response")));
	const FString Dispatch = FString::Printf(
		TEXT("[BREAKING · %s] Orbital %s  //  %s casualties  //  %s  //  %s"),
		*Region, WeaponNames[SafeWeapon], *CasualtyText, *PowerText, *Alert);
	if (!bNearCity)
	{
		EnqueueNews(Dispatch + FString::Printf(TEXT("  //  %.0f KM IMPACT ZONE"), OffsetKm),
			Telemetry.AtmosphereHealth < 70.0f ? 5 : (NewCasualties >= 100000000.0 ? 4 : (NewCasualties >= 1000000.0 ? 3 : 1)));
	}
	else
	{
		EnqueueNews(Dispatch, Telemetry.AtmosphereHealth < 70.0f ? 5 : (NewCasualties >= 100000000.0 ? 4 : (NewCasualties >= 1000000.0 ? 3 : 2)));
	}
}

void UEarthfallPopulationSubsystem::EnqueueNews(const FString& Text, int32 Priority)
{
	FEarthfallNewsEvent& Event = NewsQueue.AddDefaulted_GetRef();
	Event.Priority = Priority;
	Event.Sequence = ++NewsSequence;
	Event.Text = Text;
	NewsQueue.StableSort([](const FEarthfallNewsEvent& A, const FEarthfallNewsEvent& B)
	{
		return A.Priority == B.Priority ? A.Sequence > B.Sequence : A.Priority > B.Priority;
	});
	if (NewsQueue.Num() > 8) NewsQueue.SetNum(8);
	Telemetry.LatestDispatch = NewsQueue.IsEmpty() ? Text : NewsQueue[0].Text;
}

void UEarthfallPopulationSubsystem::ResetSimulation()
{
	SeedPopulation();
	Telemetry = FEarthfallTelemetry();
	Telemetry.LivingPopulation = InitialPopulation;
	GridPowerSum = static_cast<float>(Cells.Num());
	NewsQueue.Reset();
	NewsSequence = 0;
	EnqueueNews(Telemetry.LatestDispatch, 1);
}

float UEarthfallPopulationSubsystem::GetCityLightAtDirection(const FVector& LocalDirection) const
{
	if (Cells.IsEmpty()) return 0.0f;
	const FEarthfallPopulationCell& Cell = Cells[GetCellIndex(LocalDirection)];
	return FMath::Clamp(Cell.CityLightPotential * Cell.PowerGridHealth, 0.0f, 1.0f);
}
