#include "EarthfallSandboxSubsystem.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallProjectile.h"
#include "EarthfallTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogEarthfallSandbox, Log, All);

namespace
{
/** At most this many AI missiles in the air at once: enough to read as a war, cheap to resolve. */
constexpr int32 MaxAiMissiles = 8;
constexpr int32 MaxUnits = 24;
constexpr int32 DebrisCount = 900;
/** Angular speed of a strike group, radians per second (a brisk few seconds per ocean). */
constexpr float UnitSpeed = 0.035f;
constexpr float AirWingSpeed = 0.075f;
/** The navigation grid: 1.5 degree cells. */
constexpr int32 NavWidth = 240;
constexpr int32 NavHeight = 120;
/** A fleet holds this far or nearer from a capital it can reach; further inland calls for an air wing. */
constexpr float FleetReach = 0.40f;

struct FMajorPower
{
	const TCHAR* Code;
	const TCHAR* Name;
	double Latitude;
	double Longitude;
	float Military;
	int32 Stockpile;
	/** 1 = Western alliance, 2 = Eastern bloc, 0 = non-aligned. */
	int32 Bloc;
};

// Capitals, relative conventional strength and rough stockpiles. Members of a
// bloc are allies and follow each other into a war.
const FMajorPower MajorPowers[] = {
	{TEXT("USA"), TEXT("UNITED STATES"), 38.9, -77.0, 100.0f, 60, 1},
	{TEXT("CHN"), TEXT("CHINA"), 39.9, 116.4, 92.0f, 40, 2},
	{TEXT("RUS"), TEXT("RUSSIA"), 55.75, 37.6, 88.0f, 60, 2},
	{TEXT("GBR"), TEXT("UNITED KINGDOM"), 51.5, -0.1, 62.0f, 18, 1},
	{TEXT("FRA"), TEXT("FRANCE"), 48.85, 2.35, 64.0f, 20, 1},
	{TEXT("DEU"), TEXT("GERMANY"), 52.5, 13.4, 58.0f, 0, 1},
	{TEXT("IND"), TEXT("INDIA"), 28.6, 77.2, 78.0f, 16, 0},
	{TEXT("PAK"), TEXT("PAKISTAN"), 33.7, 73.1, 55.0f, 16, 0},
	{TEXT("BRA"), TEXT("BRAZIL"), -15.8, -47.9, 52.0f, 0, 0},
	{TEXT("JPN"), TEXT("JAPAN"), 35.7, 139.7, 66.0f, 0, 1},
	{TEXT("KOR"), TEXT("SOUTH KOREA"), 37.57, 126.98, 60.0f, 0, 1},
	{TEXT("PRK"), TEXT("NORTH KOREA"), 39.0, 125.75, 48.0f, 6, 2},
	{TEXT("IRN"), TEXT("IRAN"), 35.7, 51.4, 54.0f, 0, 2},
	{TEXT("AUS"), TEXT("AUSTRALIA"), -35.3, 149.1, 50.0f, 0, 1},
};

FVector SlerpDirection(const FVector& From, const FVector& To, float Radians)
{
	const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(From, To)), -1.0f, 1.0f));
	if (Angle < 1.0e-4f) return To;
	return FVector::SlerpNormals(From, To, FMath::Clamp(Radians / Angle, 0.0f, 1.0f));
}
}

TStatId UEarthfallSandboxSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEarthfallSandboxSubsystem, STATGROUP_Tickables);
}

AEarthfallPlanet* UEarthfallSandboxSubsystem::FindPlanet() const
{
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AEarthfallPlanet> It(World); It; ++It) return *It;
	}
	return nullptr;
}

UEarthfallPopulationSubsystem* UEarthfallSandboxSubsystem::Population() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>() : nullptr;
}

void UEarthfallSandboxSubsystem::ResetSandbox()
{
	PendingMeteors.Reset();
	PendingJoins.Reset();
	Units.Reset();
	Interceptors.Reset();
	Satellites.Reset();
	BatteryReadyAt.Reset();
	Markers.Reset();
	Factions.Reset();
	FactionByCountry.Reset();
	bFactionsBuilt = false;
	DebrisLevel = 0.0f;
	DebrisNewsLevel = 0.0f;
	PendingWarCountry = 0;
	InterceptCount = 0;
	BioZones.Reset();
	Routes.Reset();
	BorderCrises.Reset();
	if (UEarthfallPopulationSubsystem* People = Population()) People->SetBorderStress(TMap<int32, float>());
	InfectedNow = 0.0;
	CuredTotal = 0.0;
	PlagueDead = 0.0;
	if (AEarthfallPlanet* Planet = FindPlanet()) Planet->SetBioZones(TArray<FVector4>(), TArray<FLinearColor>());
}

// ---------------------------------------------------------------------------
// Factions
// ---------------------------------------------------------------------------
void UEarthfallSandboxSubsystem::BuildFactions()
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People || People->GetCountryCount() == 0) return;
	bFactionsBuilt = true;
	for (const FMajorPower& Power : MajorPowers)
	{
		const FVector Capital = EarthfallSolar::DirectionFromLatLon(Power.Latitude, Power.Longitude);
		const int32 CountryId = People->GetCountryAtDirection(Capital);
		if (CountryId <= 0 || FactionByCountry.Contains(CountryId)) continue;
		FFaction& Faction = Factions.AddDefaulted_GetRef();
		Faction.CountryId = CountryId;
		Faction.Code = Power.Code;
		Faction.Name = Power.Name;
		Faction.Capital = Capital;
		Faction.MilitaryStrength = Power.Military;
		Faction.NuclearStockpile = Power.Stockpile;
		Faction.Bloc = Power.Bloc;
		FactionByCountry.Add(CountryId, Factions.Num() - 1);
	}
	for (int32 A = 0; A < Factions.Num(); ++A)
	{
		for (int32 B = 0; B < Factions.Num(); ++B)
		{
			if (A != B && Factions[A].Bloc != 0 && Factions[A].Bloc == Factions[B].Bloc) Factions[A].Allies.Add(B);
		}
	}
	// India and Pakistan sit outside the blocs but Pakistan leans east.
	if (Debris.IsEmpty())
	{
		FRandomStream Random(7919);
		Debris.Reserve(DebrisCount);
		for (int32 Index = 0; Index < DebrisCount; ++Index)
		{
			FDebris& Piece = Debris.AddDefaulted_GetRef();
			// Mostly low-inclination shells with a scatter of polar orbits.
			Piece.Axis = (FVector::UpVector + Random.VRand() * (Random.FRand() < 0.25f ? 2.5f : 0.45f)).GetSafeNormal();
			FVector Other;
			Piece.Axis.FindBestAxisVectors(Piece.Start, Other);
			Piece.Start = Piece.Start.RotateAngleAxis(Random.FRandRange(0.0f, 360.0f), Piece.Axis);
			Piece.Altitude = Random.FRandRange(1.085f, 1.17f);
			Piece.Speed = Random.FRandRange(0.10f, 0.19f);
		}
	}
}

int32 UEarthfallSandboxSubsystem::FactionFor(int32 CountryId, const FVector& Hint)
{
	if (!bFactionsBuilt) BuildFactions();
	if (CountryId <= 0) return -1;
	if (const int32* Existing = FactionByCountry.Find(CountryId)) return *Existing;
	UEarthfallPopulationSubsystem* People = Population();
	FEarthfallCountryStats Stats;
	if (!People || !People->GetCountryStats(CountryId, Stats)) return -1;
	// Any nation can be drawn in: it fights from wherever it was first touched,
	// with a military in proportion to its size and readiness.
	FFaction& Faction = Factions.AddDefaulted_GetRef();
	Faction.CountryId = CountryId;
	Faction.Name = Stats.Name.ToUpper();
	Faction.Code = Faction.Name.Left(3);
	Faction.Capital = Hint.GetSafeNormal();
	Faction.MilitaryStrength = FMath::Clamp(Stats.DefenseReadiness * 0.6f + static_cast<float>(FMath::LogX(10.0, FMath::Max(Stats.InitialPopulation, 1.0e4)) - 4.0) * 6.0f, 8.0f, 60.0f);
	FactionByCountry.Add(CountryId, Factions.Num() - 1);
	return Factions.Num() - 1;
}

bool UEarthfallSandboxSubsystem::AreAtWar(int32 A, int32 B) const
{
	return Factions.IsValidIndex(A) && Factions[A].AtWar.Contains(B);
}

void UEarthfallSandboxSubsystem::DeclareWar(int32 A, int32 B, const TCHAR* Reason)
{
	if (A == B || !Factions.IsValidIndex(A) || !Factions.IsValidIndex(B) || AreAtWar(A, B)) return;
	Factions[A].AtWar.Add(B);
	Factions[B].AtWar.Add(A);
	Factions[A].Allies.Remove(B);
	Factions[B].Allies.Remove(A);
	Factions[A].LaunchTimer = 1.2f;
	Factions[B].LaunchTimer = 2.6f;
	if (UEarthfallPopulationSubsystem* People = Population())
	{
		People->PostDispatch(FString::Printf(TEXT("[WAR DECLARED: %s vs %s] %s"), *Factions[A].Name, *Factions[B].Name, Reason), 8);
	}
	UE_LOG(LogEarthfallSandbox, Display, TEXT("EARTHFALL_WAR %s vs %s (%s)"), *Factions[A].Code, *Factions[B].Code, Reason);
	// Allies honour their treaties a few seconds later, one at a time.
	auto CallAllies = [this](int32 Of, int32 Against)
	{
		for (int32 Ally : Factions[Of].Allies)
		{
			if (Ally == Against || AreAtWar(Ally, Against)) continue;
			FPendingJoin& Join = PendingJoins.AddDefaulted_GetRef();
			Join.Ally = Ally;
			Join.Enemy = Against;
			Join.Delay = 5.0f + 2.5f * PendingJoins.Num();
		}
	};
	CallAllies(B, A);
	CallAllies(A, B);
}

void UEarthfallSandboxSubsystem::MakePeace(int32 Faction)
{
	if (!Factions.IsValidIndex(Faction)) return;
	for (int32 Enemy : Factions[Faction].AtWar)
	{
		if (Factions.IsValidIndex(Enemy)) Factions[Enemy].AtWar.Remove(Faction);
	}
	Factions[Faction].AtWar.Reset();
	PendingJoins.RemoveAll([Faction](const FPendingJoin& Join) { return Join.Ally == Faction || Join.Enemy == Faction; });
	Units.RemoveAll([Faction](const FUnit& Unit) { return Unit.Owner == Faction || Unit.Target == Faction; });
	// Missiles already in the air from anyone now at peace are destroyed in flight.
	int32 Recalled = 0;
	for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It)
	{
		const int32* Launcher = FactionByCountry.Find(It->GetStrike().AttackerCountry);
		if (It->IsInFlight() && Launcher && Factions[*Launcher].AtWar.IsEmpty())
		{
			It->Intercept();
			++Recalled;
		}
	}
	UE_LOG(LogEarthfallSandbox, Display, TEXT("EARTHFALL_PEACE %s stands down: %d wars still active worldwide, %d missiles destroyed in flight"),
		*Factions[Faction].Code, GetActiveWarCount(), Recalled);
}

bool UEarthfallSandboxSubsystem::GetFactionInfo(int32 CountryId, FEarthfallFactionInfo& OutInfo) const
{
	const int32* Index = FactionByCountry.Find(CountryId);
	if (!Index || !Factions.IsValidIndex(*Index)) return false;
	const FFaction& Faction = Factions[*Index];
	OutInfo.Code = Faction.Code;
	OutInfo.MilitaryStrength = Faction.MilitaryStrength;
	OutInfo.NuclearStockpile = Faction.NuclearStockpile;
	OutInfo.Infected = GetCountryInfected(CountryId);
	OutInfo.AtWarWith.Reset();
	OutInfo.Allies.Reset();
	for (int32 Enemy : Faction.AtWar) if (Factions.IsValidIndex(Enemy)) OutInfo.AtWarWith.Add(Factions[Enemy].Code);
	for (int32 Ally : Faction.Allies) if (Factions.IsValidIndex(Ally)) OutInfo.Allies.Add(Factions[Ally].Code);
	return true;
}

void UEarthfallSandboxSubsystem::NotifyStrike(const FVector& LocalDirection, const FEarthfallStrike& Strike)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return;
	if (!bFactionsBuilt) BuildFactions();
	// Only a strike nobody owns (the player's) leaves a nation looking for someone to blame.
	if (Strike.AttackerCountry != 0) return;
	const int32* Index = FactionByCountry.Find(People->GetCountryAtDirection(LocalDirection));
	if (!Index) return;
	FFaction& Victim = Factions[*Index];
	const bool bGrave = Strike.WeaponIndex == EarthfallWeapons::Nuclear || Strike.WeaponIndex == 2 || Strike.WeaponIndex == 4;
	if (!Victim.AtWar.IsEmpty() || Victim.bFallen || FMath::FRand() > (bGrave ? 0.65f : 0.20f)) return;
	// It blames a rival bloc if it has one, otherwise whoever is strongest.
	int32 Blamed = -1;
	float BestScore = -1.0f;
	for (int32 Other = 0; Other < Factions.Num(); ++Other)
	{
		if (Other == *Index || Victim.Allies.Contains(Other) || Factions[Other].bFallen) continue;
		const bool bRival = Victim.Bloc != 0 && Factions[Other].Bloc != 0 && Factions[Other].Bloc != Victim.Bloc;
		const float Score = Factions[Other].MilitaryStrength * (bRival ? 3.0f : 1.0f) * FMath::FRandRange(0.6f, 1.0f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Blamed = Other;
		}
	}
	if (Blamed >= 0) DeclareWar(*Index, Blamed, TEXT("RETALIATION FOR AN UNATTRIBUTED STRIKE"));
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void UEarthfallSandboxSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	WorldTime += DeltaTime;
	if (!bFactionsBuilt) BuildFactions();

	// Meteor showers arrive as a ragged volley, not a single frame of launches.
	for (int32 Index = PendingMeteors.Num() - 1; Index >= 0; --Index)
	{
		PendingMeteors[Index].Delay -= DeltaTime;
		if (PendingMeteors[Index].Delay > 0.0f) continue;
		if (AEarthOrbitPawn* Pawn = MeteorPawn.Get()) Pawn->LaunchWeaponAt(PendingMeteors[Index].Direction, 0, 2.0f, 1.0f, 0.0f);
		PendingMeteors.RemoveAtSwap(Index);
	}

	TickWars(DeltaTime);
	TickUnits(DeltaTime);
	TickDefense(DeltaTime);
	TickDebrisField(DeltaTime);
	TickBio(DeltaTime);
	TickTraffic(DeltaTime);
	TickBorders(DeltaTime);
	BuildMarkers();
}

void UEarthfallSandboxSubsystem::FireMissile(const FVector& FromDirection, float FromAltitudeScale, const FVector& AimDirection, int32 AttackerCountry, bool bNuclear)
{
	AEarthfallPlanet* Planet = FindPlanet();
	if (!Planet || !GetWorld()) return;
	const FTransform& Transform = Planet->GetActorTransform();
	const FVector Start = Planet->GetActorLocation() + Transform.TransformVectorNoScale(FromDirection) * Planet->PlanetRadius * FromAltitudeScale;
	const FVector Impact = Planet->GetActorLocation() + Transform.TransformVectorNoScale(AimDirection) * Planet->PlanetRadius;

	// Conventional salvos are small kinetic warheads; a nuclear one is a
	// quarter-yield, half-size weapon, so a war scars regions rather than ending the world at once.
	FEarthfallStrike Strike = bNuclear
		? FEarthfallStrike::Make(Impact, EarthfallWeapons::Nuclear, 0.25f, 0.5f, 1.0f)
		: FEarthfallStrike::Make(Impact, 0, 1.0f, 0.5f, 0.0f);
	Strike.AttackerCountry = AttackerCountry;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AEarthfallProjectile* Missile = GetWorld()->SpawnActor<AEarthfallProjectile>(AEarthfallProjectile::StaticClass(), Start, FRotator::ZeroRotator, Params))
	{
		const float Range = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(FromDirection, AimDirection)), -1.0f, 1.0f));
		Missile->Launch(Planet, nullptr, Start, Strike, 1.6f + Range * 2.8f, true);
	}
}

void UEarthfallSandboxSubsystem::LaunchMissile(FFaction& From, const FFaction& At, bool bNuclear)
{
	// Aimed at the enemy capital, landing anywhere within a few hundred kilometres of it.
	FVector TangentA;
	FVector TangentB;
	At.Capital.FindBestAxisVectors(TangentA, TangentB);
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const FVector Aim = (At.Capital + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * FMath::FRandRange(0.01f, 0.09f)).GetSafeNormal();
	FireMissile(From.Capital, 1.004f, Aim, From.CountryId, bNuclear);
	++From.Exchanges;
	if (bNuclear) --From.NuclearStockpile;
}

void UEarthfallSandboxSubsystem::TickWars(float DeltaTime)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People || Factions.IsEmpty()) return;

	for (int32 Index = PendingJoins.Num() - 1; Index >= 0; --Index)
	{
		PendingJoins[Index].Delay -= DeltaTime;
		if (PendingJoins[Index].Delay > 0.0f) continue;
		const FPendingJoin Join = PendingJoins[Index];
		PendingJoins.RemoveAt(Index);
		if (Factions.IsValidIndex(Join.Ally) && Factions.IsValidIndex(Join.Enemy)
			&& !Factions[Join.Ally].bFallen && !Factions[Join.Enemy].AtWar.IsEmpty())
		{
			DeclareWar(Join.Ally, Join.Enemy, TEXT("TREATY OBLIGATIONS INVOKED"));
		}
	}

	int32 MissilesAloft = 0;
	for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It)
	{
		if (It->IsInFlight() && It->GetStrike().AttackerCountry != 0) ++MissilesAloft;
	}

	for (int32 Index = 0; Index < Factions.Num(); ++Index)
	{
		FFaction& Faction = Factions[Index];
		if (Faction.AtWar.IsEmpty() || Faction.bFallen) continue;
		FEarthfallCountryStats Stats;
		if (!People->GetCountryStats(Faction.CountryId, Stats)) continue;

		// A collapsed government stops fighting; its enemies stand down.
		if (Stats.Stability == EEarthfallStability::Collapsed)
		{
			Faction.bFallen = true;
			People->PostDispatch(FString::Printf(TEXT("[WAR] %s HAS COLLAPSED  //  ITS FORCES CEASE FIRE"), *Faction.Name), 7);
			MakePeace(Index);
			continue;
		}

		// No power, no launches: knocking out the grid silences a nation's missiles.
		const float Grid = Stats.GridPercent / 100.0f;
		Faction.LaunchTimer -= DeltaTime * FMath::Clamp(Grid * 1.4f, 0.0f, 1.0f);
		if (Faction.LaunchTimer <= 0.0f && MissilesAloft < MaxAiMissiles)
		{
			const int32 Enemy = Faction.AtWar[FMath::RandRange(0, Faction.AtWar.Num() - 1)];
			// Wars start conventional and escalate after a few exchanges.
			const bool bNuclear = Faction.NuclearStockpile > 0 && Faction.Exchanges >= 3 && FMath::FRand() < 0.35f;
			LaunchMissile(Faction, Factions[Enemy], bNuclear);
			++MissilesAloft;
			Faction.LaunchTimer = FMath::FRandRange(3.2f, 5.6f) * (120.0f / (60.0f + Faction.MilitaryStrength));
			if (bNuclear && Faction.Exchanges < 100)
			{
				People->PostDispatch(FString::Printf(TEXT("[NUCLEAR] %s LAUNCHES A WARHEAD AT %s"), *Faction.Name, *Factions[Enemy].Name), 8);
			}
		}

		// Now and then a strike group puts to sea for an enemy coast.
		Faction.GroupTimer -= DeltaTime;
		if (Faction.GroupTimer <= 0.0f)
		{
			Faction.GroupTimer = FMath::FRandRange(9.0f, 15.0f);
			SpawnUnit(Index, Faction.AtWar[FMath::RandRange(0, Faction.AtWar.Num() - 1)], Faction.Capital);
		}
	}
}

void UEarthfallSandboxSubsystem::BuildWaterGrid()
{
	const UEarthfallPopulationSubsystem* People = Population();
	if (!People || People->GetCountryCount() == 0) return;
	WaterGrid.SetNumZeroed(NavWidth * NavHeight);
	for (int32 Y = 0; Y < NavHeight; ++Y)
	{
		const double Latitude = 90.0 - (Y + 0.5) * 180.0 / NavHeight;
		// Fleets stay out of the polar pack.
		if (FMath::Abs(Latitude) > 76.0) continue;
		for (int32 X = 0; X < NavWidth; ++X)
		{
			const double Longitude = -180.0 + (X + 0.5) * 360.0 / NavWidth;
			WaterGrid[Y * NavWidth + X] = People->GetCountryAtDirection(EarthfallSolar::DirectionFromLatLon(Latitude, Longitude)) == 0 ? 1 : 0;
		}
	}
}

bool UEarthfallSandboxSubsystem::FindSeaRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	if (WaterGrid.Num() != NavWidth * NavHeight) return false;
	auto CellDirection = [](int32 Cell)
	{
		return EarthfallSolar::DirectionFromLatLon(90.0 - (Cell / NavWidth + 0.5) * 180.0 / NavHeight, -180.0 + (Cell % NavWidth + 0.5) * 360.0 / NavWidth);
	};
	// Put to sea at the open water nearest the starting point.
	int32 StartCell = -1;
	double BestStart = -2.0;
	for (int32 Cell = 0; Cell < WaterGrid.Num(); ++Cell)
	{
		if (!WaterGrid[Cell]) continue;
		const double Near = FVector::DotProduct(CellDirection(Cell), From);
		if (Near > BestStart)
		{
			BestStart = Near;
			StartCell = Cell;
		}
	}
	if (StartCell < 0) return false;

	// Dijkstra over every water cell this sea connects to. The station is the
	// reachable cell nearest the target, so a landlocked sea is never chosen.
	TArray<float> Cost;
	Cost.Init(TNumericLimits<float>::Max(), WaterGrid.Num());
	TArray<int32> Parent;
	Parent.Init(-1, WaterGrid.Num());
	TArray<TPair<float, int32>> Open;
	auto Cheaper = [](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; };
	Cost[StartCell] = 0.0f;
	Open.HeapPush(TPair<float, int32>(0.0f, StartCell), Cheaper);
	int32 StationCell = StartCell;
	double BestStation = FVector::DotProduct(CellDirection(StartCell), To);
	while (!Open.IsEmpty())
	{
		TPair<float, int32> Node;
		Open.HeapPop(Node, Cheaper);
		const int32 Cell = Node.Value;
		if (Node.Key > Cost[Cell]) continue;
		const FVector Here = CellDirection(Cell);
		const double Near = FVector::DotProduct(Here, To);
		if (Near > BestStation)
		{
			BestStation = Near;
			StationCell = Cell;
		}
		const int32 X = Cell % NavWidth;
		const int32 Y = Cell / NavWidth;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const int32 NY = Y + DY;
			if (NY < 0 || NY >= NavHeight) continue;
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				if (DX == 0 && DY == 0) continue;
				const int32 Next = NY * NavWidth + (X + DX + NavWidth) % NavWidth;
				if (!WaterGrid[Next]) continue;
				// A diagonal may not cut the corner of a coast.
				if (DX != 0 && DY != 0 && (!WaterGrid[Y * NavWidth + (X + DX + NavWidth) % NavWidth] || !WaterGrid[NY * NavWidth + X])) continue;
				const float Step = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Here, CellDirection(Next))), -1.0f, 1.0f));
				if (Cost[Cell] + Step < Cost[Next])
				{
					Cost[Next] = Cost[Cell] + Step;
					Parent[Next] = Cell;
					Open.HeapPush(TPair<float, int32>(Cost[Next], Next), Cheaper);
				}
			}
		}
	}
	// Too far inland to bombard from the sea.
	if (BestStation < FMath::Cos(FleetReach)) return false;
	for (int32 Cell = StationCell; Cell >= 0; Cell = Parent[Cell]) OutPath.Insert(CellDirection(Cell), 0);
	return OutPath.Num() > 0;
}

bool UEarthfallSandboxSubsystem::SpawnUnit(int32 Owner, int32 Target, const FVector& From, int32 Mode)
{
	if (Units.Num() >= MaxUnits || !Factions.IsValidIndex(Target)) return false;
	if (WaterGrid.IsEmpty()) BuildWaterGrid();
	FUnit Unit;
	Unit.Owner = Owner;
	Unit.Target = Target;
	Unit.Direction = From.GetSafeNormal();
	if (Mode != 2 && FindSeaRoute(Unit.Direction, Factions[Target].Capital, Unit.Path))
	{
		Unit.bNaval = true;
		Unit.Direction = Unit.Path[0];
	}
	else if (Mode == 1)
	{
		return false;
	}
	Units.Add(MoveTemp(Unit));
	return true;
}

int32 UEarthfallSandboxSubsystem::GetFleetCount() const
{
	int32 Count = 0;
	for (const FUnit& Unit : Units) Count += Unit.bNaval ? 1 : 0;
	return Count;
}

int32 UEarthfallSandboxSubsystem::GetActiveWarCount() const
{
	int32 Count = 0;
	for (const FFaction& Faction : Factions) Count += Faction.AtWar.Num();
	return Count / 2;
}

void UEarthfallSandboxSubsystem::TickUnits(float DeltaTime)
{
	const UEarthfallPopulationSubsystem* People = Population();
	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		FUnit& Unit = Units[Index];
		if (!Factions.IsValidIndex(Unit.Target) || Factions[Unit.Target].bFallen)
		{
			Units.RemoveAtSwap(Index);
			continue;
		}
		const FVector& Capital = Factions[Unit.Target].Capital;
		bool bOnStation = false;
		if (Unit.bNaval)
		{
			// Waypoint to waypoint, every one of them open water.
			if (Unit.PathIndex < Unit.Path.Num())
			{
				const FVector& Waypoint = Unit.Path[Unit.PathIndex];
				Unit.Direction = SlerpDirection(Unit.Direction, Waypoint, UnitSpeed * DeltaTime);
				if (FVector::DotProduct(Unit.Direction, Waypoint) > 0.99996) ++Unit.PathIndex;
			}
			bOnStation = Unit.PathIndex >= Unit.Path.Num();
		}
		else
		{
			const float Range = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Unit.Direction, Capital)), -1.0f, 1.0f));
			bOnStation = Range <= 0.11f;
			if (!bOnStation) Unit.Direction = SlerpDirection(Unit.Direction, Capital, AirWingSpeed * DeltaTime);
		}
		if (!bOnStation) continue;

		Unit.FireTimer -= DeltaTime;
		if (Unit.FireTimer > 0.0f) continue;
		Unit.FireTimer = 1.8f;
		FVector Aim = Capital;
		if (Unit.bNaval && People)
		{
			// A fleet shells the coast in front of it: the first land on the line
			// toward the capital, and a little way inland of that.
			for (float Along = 0.012f; Along < FleetReach; Along += 0.012f)
			{
				const FVector Probe = SlerpDirection(Unit.Direction, Capital, Along);
				if (People->GetCountryAtDirection(Probe) != 0)
				{
					Aim = SlerpDirection(Unit.Direction, Capital, Along + 0.025f);
					break;
				}
			}
		}
		FVector TangentA;
		FVector TangentB;
		Aim.FindBestAxisVectors(TangentA, TangentB);
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		Aim = (Aim + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * FMath::FRandRange(0.0f, 0.03f)).GetSafeNormal();
		FireMissile(Unit.Direction, Unit.bNaval ? 1.004f : 1.03f, Aim, Factions.IsValidIndex(Unit.Owner) ? Factions[Unit.Owner].CountryId : -1, false);
		if (--Unit.Volleys <= 0) Units.RemoveAtSwap(Index);
	}
}

void UEarthfallSandboxSubsystem::TickDefense(float DeltaTime)
{
	UEarthfallPopulationSubsystem* People = Population();
	AEarthfallPlanet* Planet = FindPlanet();
	if (!People || !Planet) return;
	const FTransform& Transform = Planet->GetActorTransform();

	// Satellites circle the planet on the orbit they were placed in.
	for (FSatellite& Satellite : Satellites)
	{
		Satellite.Direction = Satellite.Direction.RotateAngleAxis(DeltaTime * 4.0f, Satellite.Axis).GetSafeNormal();
	}

	for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It)
	{
		AEarthfallProjectile* Munition = *It;
		if (Munition->bDefenseChecked || !Munition->IsInFlight() || Munition->GetProgress() < 0.35f) continue;
		Munition->bDefenseChecked = true;
		const FEarthfallStrike& Strike = Munition->GetStrike();
		// Rods, bolides and warheads can be hit. Beams, singularities and mantle charges cannot.
		if (Strike.WeaponIndex != 0 && Strike.WeaponIndex != 4 && Strike.WeaponIndex != EarthfallWeapons::Nuclear) continue;

		const FVector Target = Transform.InverseTransformVectorNoScale(Strike.WorldLocation - Planet->GetActorLocation()).GetSafeNormal();
		const int32 CountryId = People->GetCountryAtDirection(Target);
		FString Defender;
		bool bKill = false;

		// Ground batteries: readiness, squared grid power, and one shot per reload.
		FEarthfallCountryStats Stats;
		if (CountryId > 0 && CountryId != Strike.AttackerCountry && People->GetCountryStats(CountryId, Stats))
		{
			const float Ready = BatteryReadyAt.FindRef(CountryId);
			const float Alive = Stats.InitialPopulation > 0.0 ? static_cast<float>(Stats.LivingPopulation / Stats.InitialPopulation) : 0.0f;
			const float Chance = Stats.DefenseReadiness / 100.0f * Alive * FMath::Square(Stats.GridPercent / 100.0f) * 0.85f;
			if (WorldTime >= Ready && FMath::FRand() < Chance)
			{
				bKill = true;
				Defender = Stats.Name.ToUpper();
				BatteryReadyAt.Add(CountryId, WorldTime + 1.6f);
			}
		}
		// Defense satellites cover a wide cap of ground beneath them.
		if (!bKill)
		{
			for (FSatellite& Satellite : Satellites)
			{
				if (WorldTime < Satellite.ReadyAt || FVector::DotProduct(Satellite.Direction, Target) < 0.80f) continue;
				if (FMath::FRand() < 0.75f)
				{
					bKill = true;
					Defender = TEXT("DEFENSE SATELLITE");
				}
				Satellite.ReadyAt = WorldTime + 2.0f;
				break;
			}
		}
		// A thick enough debris shell kills launches by itself.
		if (!bKill && DebrisLevel > 0.75f && FMath::FRand() < (DebrisLevel - 0.75f) * 1.4f)
		{
			Munition->Intercept();
			++InterceptCount;
			People->PostDispatch(FString::Printf(TEXT("[KESSLER] ORBITAL DEBRIS SHREDS AN INBOUND %s"), EarthfallWeapons::Get(Strike.WeaponIndex).Name), 5);
			UE_LOG(LogEarthfallSandbox, Display, TEXT("EARTHFALL_INTERCEPT debris destroyed a %s"), EarthfallWeapons::Get(Strike.WeaponIndex).Name);
			continue;
		}
		if (!bKill) continue;

		FInterceptor& Interceptor = Interceptors.AddDefaulted_GetRef();
		Interceptor.Target = Munition;
		Interceptor.Defender = Defender;
		// It must get there first: a rod lands in well under a second.
		Interceptor.Duration = FMath::Clamp(Munition->GetRemainingSeconds() * 0.6f, 0.08f, 0.6f);
		// It rises from the ground near the aim point, or drops from the satellite.
		Interceptor.Start = Planet->GetActorLocation() + Transform.TransformVectorNoScale(Target) * Planet->PlanetRadius
			* (Defender == TEXT("DEFENSE SATELLITE") ? 1.18f : 1.002f);
	}

	for (int32 Index = Interceptors.Num() - 1; Index >= 0; --Index)
	{
		FInterceptor& Interceptor = Interceptors[Index];
		Interceptor.Age += DeltaTime;
		AEarthfallProjectile* Munition = Interceptor.Target.Get();
		if (!Munition || !Munition->IsInFlight())
		{
			Interceptors.RemoveAtSwap(Index);
			continue;
		}
		if (Interceptor.Age < Interceptor.Duration) continue;
		const TCHAR* Kind = EarthfallWeapons::Get(Munition->GetStrike().WeaponIndex).Name;
		Munition->Intercept();
		++InterceptCount;
		AddDebris(0.05f);
		People->PostDispatch(FString::Printf(TEXT("[DEFENSE] %s INTERCEPTS AN INBOUND %s"), *Interceptor.Defender, Kind), 5);
		UE_LOG(LogEarthfallSandbox, Display, TEXT("EARTHFALL_INTERCEPT %s shot down a %s"), *Interceptor.Defender, Kind);
		Interceptors.RemoveAtSwap(Index);
	}
}

void UEarthfallSandboxSubsystem::AddDebris(float Amount)
{
	DebrisLevel = FMath::Clamp(DebrisLevel + Amount, 0.0f, 1.0f);
}

void UEarthfallSandboxSubsystem::TickDebrisField(float DeltaTime)
{
	// Low orbits decay, slowly.
	DebrisLevel = FMath::Max(0.0f, DebrisLevel - 0.0025f * DeltaTime);
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return;
	if (DebrisLevel >= 0.45f && DebrisNewsLevel < 0.45f)
	{
		People->PostDispatch(TEXT("[KESSLER] DEBRIS RING FORMING IN LOW ORBIT  //  LAUNCH WINDOWS NARROWING"), 5);
	}
	if (DebrisLevel >= 0.75f && DebrisNewsLevel < 0.75f)
	{
		People->PostDispatch(TEXT("[KESSLER] CASCADE UNDER WAY  //  LOW ORBIT IS NOW A KILL ZONE FOR LAUNCHES"), 7);
	}
	DebrisNewsLevel = DebrisLevel;
}

void UEarthfallSandboxSubsystem::BuildMarkers()
{
	Markers.Reset();
	AEarthfallPlanet* Planet = FindPlanet();
	if (!Planet) return;
	const FVector Center = Planet->GetActorLocation();
	const FTransform& Transform = Planet->GetActorTransform();
	const float Radius = Planet->PlanetRadius;

	for (const FUnit& Unit : Units)
	{
		const FVector World = Transform.TransformVectorNoScale(Unit.Direction);
		const FVector GoalLocal = Unit.bNaval && Unit.PathIndex < Unit.Path.Num() ? Unit.Path[Unit.PathIndex]
			: (Factions.IsValidIndex(Unit.Target) ? Factions[Unit.Target].Capital : Unit.Direction);
		const FVector Goal = Transform.TransformVectorNoScale(GoalLocal);
		const FVector Forward = (Goal - World * FVector::DotProduct(Goal, World)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector Side = FVector::CrossProduct(World, Forward);
		for (int32 Ship = 0; Ship < 3; ++Ship)
		{
			// A wedge: one ahead, two astern and abeam.
			const FVector Offset = Ship == 0 ? FVector::ZeroVector : (Side * (Ship == 1 ? 0.55f : -0.55f) - Forward * 0.7f);
			FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
			Marker.Position = Center + World * Radius * (Unit.bNaval ? 1.004f : 1.03f) + Offset * (Unit.bNaval ? 1.0f : 1.4f);
			Marker.Forward = Forward;
			Marker.Size = Unit.bNaval ? 0.30f : 0.20f;
			Marker.Stretch = Unit.bNaval ? 2.6f : 5.0f;
			Marker.Kind = Unit.bNaval ? EarthfallMarkers::Unit : EarthfallMarkers::AirWing;
		}
	}
	for (const FInterceptor& Interceptor : Interceptors)
	{
		const AEarthfallProjectile* Munition = Interceptor.Target.Get();
		if (!Munition) continue;
		FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
		const FVector Goal = Munition->GetFlightPosition();
		const float Alpha = FMath::Clamp(Interceptor.Age / Interceptor.Duration, 0.0f, 1.0f);
		Marker.Position = FMath::Lerp(Interceptor.Start, Goal, Alpha * Alpha);
		Marker.Forward = (Goal - Interceptor.Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		Marker.Size = 0.13f;
		Marker.Stretch = 14.0f;
		Marker.Kind = EarthfallMarkers::Interceptor;
	}
	for (const FSatellite& Satellite : Satellites)
	{
		FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
		const FVector World = Transform.TransformVectorNoScale(Satellite.Direction);
		Marker.Position = Center + World * Radius * 1.18f;
		Marker.Forward = FVector::CrossProduct(Transform.TransformVectorNoScale(Satellite.Axis), World).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		Marker.Size = 0.34f;
		Marker.Stretch = 1.6f;
		Marker.Kind = EarthfallMarkers::Satellite;
	}
	// Fighting flickers inside rage and insurgency zones. Down over streamed
	// terrain the ground overlay draws the fires instead; sparks would hang in the sky.
	const APlayerController* ViewController = GetWorld()->GetFirstPlayerController();
	const AEarthOrbitPawn* ViewPawn = ViewController ? Cast<AEarthOrbitPawn>(ViewController->GetPawn()) : nullptr;
	const bool bLowView = ViewPawn && ViewPawn->GetOrbitDistance() < Radius * 1.02f;
	for (const FBioZone& Zone : BioZones)
	{
		if (Zone.Kind == 0 || bLowView) continue;
		FVector TangentA;
		FVector TangentB;
		Zone.Direction.FindBestAxisVectors(TangentA, TangentB);
		for (int32 Spark = 0; Spark < 7; ++Spark)
		{
			// Each spark relights somewhere new a few times a second.
			FRandomStream Random(GetTypeHash(Zone.Direction) + Spark * 131 + FMath::FloorToInt(WorldTime * 3.0f + Spark * 0.37f));
			const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
			const FVector Local = (Zone.Direction + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * Random.FRandRange(0.0f, Zone.Radius * 0.8f)).GetSafeNormal();
			FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
			Marker.Position = Center + Transform.TransformVectorNoScale(Local) * Radius * 1.004f;
			Marker.Size = 0.12f;
			Marker.Kind = EarthfallMarkers::Combat;
		}
	}
	// Airliners and freighters, wherever their routes are still open.
	for (const FRoute& Route : Routes)
	{
		if (!Route.bOpen) continue;
		const float Span = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Route.From, Route.To)), -1.0f, 1.0f));
		if (Span < 1.0e-3f) continue;
		// Out and back along the great circle.
		const float Cycle = FMath::Frac(Route.Phase + WorldTime * Route.Speed / Span);
		const float Along = Cycle < 0.5f ? Cycle * 2.0f : 2.0f - Cycle * 2.0f;
		const FVector Local = FVector::SlerpNormals(Route.From, Route.To, Along);
		const FVector Ahead = FVector::SlerpNormals(Route.From, Route.To, FMath::Clamp(Along + (Cycle < 0.5f ? 0.02f : -0.02f), 0.0f, 1.0f));
		FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Position = Center + Transform.TransformVectorNoScale(Local) * Radius * (Route.bSea ? 1.003f : 1.024f);
		Marker.Forward = Transform.TransformVectorNoScale(Ahead - Local).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		Marker.Size = Route.bSea ? 0.13f : 0.11f;
		Marker.Stretch = Route.bSea ? 1.8f : 4.5f;
		Marker.Kind = Route.bSea ? EarthfallMarkers::Shipping : EarthfallMarkers::AirTraffic;
	}
	// The debris ring fills in piece by piece as the level climbs.
	const int32 Visible = FMath::Min(Debris.Num(), FMath::RoundToInt32(Debris.Num() * FMath::SmoothStep(0.03f, 0.95f, DebrisLevel)));
	for (int32 Index = 0; Index < Visible; ++Index)
	{
		const FDebris& Piece = Debris[Index];
		FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
		const FVector Local = Piece.Start.RotateAngleAxis(FMath::RadiansToDegrees(WorldTime * Piece.Speed), Piece.Axis);
		const FVector World = Transform.TransformVectorNoScale(Local);
		Marker.Position = Center + World * Radius * Piece.Altitude;
		Marker.Forward = FVector::CrossProduct(Transform.TransformVectorNoScale(Piece.Axis), World).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		Marker.Size = 0.17f;
		Marker.Stretch = 2.6f;
		Marker.Kind = EarthfallMarkers::Debris;
	}
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------
FString UEarthfallSandboxSubsystem::UseTool(int32 Tab, int32 Slot, const FVector& LocalDirection, AEarthOrbitPawn* Pawn)
{
	if (Tab == EarthfallTools::TabNature)
	{
		switch (Slot)
		{
		case EarthfallTools::Volcano: return UseVolcano(LocalDirection);
		case EarthfallTools::Tsunami: return UseTsunami(LocalDirection);
		case EarthfallTools::MeteorShower: return UseMeteorShower(LocalDirection, Pawn);
		case EarthfallTools::PlagueRain: return UsePlague(LocalDirection);
		case EarthfallTools::RageOutbreak: return UseRage(LocalDirection);
		case EarthfallTools::CureRain: return UseCure(LocalDirection);
		default: break;
		}
	}
	else if (Tab == EarthfallTools::TabDiplomacy)
	{
		switch (Slot)
		{
		case EarthfallTools::WhisperOfWar: return UseWhisperOfWar(LocalDirection);
		case EarthfallTools::ForcePeace: return UseForcePeace(LocalDirection);
		case EarthfallTools::DefenseSatellite: return UseSatellite(LocalDirection);
		case EarthfallTools::StrikeGroup: return UseStrikeGroup(LocalDirection);
		case EarthfallTools::RebelInsurgency: return UseInsurgency(LocalDirection);
		default: break;
		}
	}
	return FString::Printf(TEXT("%s  //  OFFLINE"), EarthfallTools::Get(Tab, Slot).Name);
}

FString UEarthfallSandboxSubsystem::UseWhisperOfWar(const FVector& LocalDirection)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return TEXT("NO PLANETARY CONTACT");
	const int32 CountryId = People->GetCountryAtDirection(LocalDirection);
	if (CountryId <= 0) return TEXT("WHISPER OF WAR  //  CLICK A NATION, NOT THE SEA");
	const int32 Faction = FactionFor(CountryId, LocalDirection);
	if (Faction < 0) return TEXT("WHISPER OF WAR  //  NO GOVERNMENT TO HEAR IT");
	if (PendingWarCountry == 0 || PendingWarCountry == CountryId)
	{
		PendingWarCountry = CountryId;
		return FString::Printf(TEXT("WHISPER OF WAR  //  %s LISTENS  //  NOW CLICK ITS ENEMY"), *Factions[Faction].Name);
	}
	const int32 Aggressor = FactionFor(PendingWarCountry, LocalDirection);
	PendingWarCountry = 0;
	if (Aggressor < 0 || Aggressor == Faction) return TEXT("WHISPER OF WAR  //  CHOOSE TWO DIFFERENT NATIONS");
	if (AreAtWar(Aggressor, Faction)) return FString::Printf(TEXT("%s AND %s ARE ALREADY AT WAR"), *Factions[Aggressor].Name, *Factions[Faction].Name);
	DeclareWar(Aggressor, Faction, TEXT("A WHISPER FROM ORBIT"));
	return FString::Printf(TEXT("WAR  //  %s vs %s"), *Factions[Aggressor].Name, *Factions[Faction].Name);
}

FString UEarthfallSandboxSubsystem::UseForcePeace(const FVector& LocalDirection)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return TEXT("NO PLANETARY CONTACT");
	if (!bFactionsBuilt) BuildFactions();
	const int32* Index = FactionByCountry.Find(People->GetCountryAtDirection(LocalDirection));
	if (!Index || Factions[*Index].AtWar.IsEmpty()) return TEXT("FORCE PEACE  //  THAT NATION IS NOT AT WAR");
	const FString Name = Factions[*Index].Name;
	MakePeace(*Index);
	People->PostDispatch(FString::Printf(TEXT("[CEASEFIRE] %s LAYS DOWN ARMS ON ALL FRONTS"), *Name), 6);
	return FString::Printf(TEXT("FORCE PEACE  //  %s STANDS DOWN"), *Name);
}

FString UEarthfallSandboxSubsystem::UseSatellite(const FVector& LocalDirection)
{
	if (Satellites.Num() >= 12) return TEXT("DEFENSE SATELLITE  //  ORBIT IS FULL (12)");
	FSatellite& Satellite = Satellites.AddDefaulted_GetRef();
	Satellite.Direction = LocalDirection.GetSafeNormal();
	// An inclined orbit through the point it was placed over.
	FVector East = FVector::CrossProduct(FVector::UpVector, Satellite.Direction).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
	Satellite.Axis = FVector::CrossProduct(Satellite.Direction, (East + FVector::UpVector * 0.35f).GetSafeNormal()).GetSafeNormal();
	if (UEarthfallPopulationSubsystem* People = Population())
	{
		People->PostDispatch(FString::Printf(TEXT("[ORBIT] DEFENSE SATELLITE %d ON STATION"), Satellites.Num()), 3);
	}
	return FString::Printf(TEXT("DEFENSE SATELLITE  //  %d OF 12 IN ORBIT"), Satellites.Num());
}

FString UEarthfallSandboxSubsystem::UseStrikeGroup(const FVector& LocalDirection)
{
	if (!bFactionsBuilt) BuildFactions();
	if (Units.Num() >= MaxUnits) return TEXT("STRIKE GROUP  //  TOO MANY GROUPS DEPLOYED");
	// It makes for the nearest capital that is at war; with the world at peace it has no orders.
	int32 Target = -1;
	float Best = -2.0f;
	for (int32 Index = 0; Index < Factions.Num(); ++Index)
	{
		if (Factions[Index].AtWar.IsEmpty() || Factions[Index].bFallen) continue;
		const float Near = static_cast<float>(FVector::DotProduct(Factions[Index].Capital, LocalDirection));
		if (Near > Best)
		{
			Best = Near;
			Target = Index;
		}
	}
	if (Target < 0) return TEXT("STRIKE GROUP  //  NO NATION IS AT WAR  //  WHISPER ONE FIRST");
	// Placed at sea it is a naval fleet; placed over land, an air strike wing.
	const UEarthfallPopulationSubsystem* People = Population();
	const bool bAtSea = People && People->GetCountryAtDirection(LocalDirection) == 0;
	const int32 Owner = Factions[Target].AtWar[0];
	if (bAtSea && SpawnUnit(Owner, Target, LocalDirection, 1))
	{
		return FString::Printf(TEXT("NAVAL FLEET  //  UNDER WAY FOR THE COAST OF %s"), *Factions[Target].Name);
	}
	SpawnUnit(Owner, Target, LocalDirection, 2);
	if (bAtSea) return FString::Printf(TEXT("NO SEA ROUTE  //  AIR STRIKE WING INBOUND FOR %s"), *Factions[Target].Name);
	return FString::Printf(TEXT("AIR STRIKE WING  //  INBOUND FOR %s"), *Factions[Target].Name);
}

FString UEarthfallSandboxSubsystem::UseVolcano(const FVector& LocalDirection)
{
	AEarthfallPlanet* Planet = FindPlanet();
	UEarthfallPopulationSubsystem* People = Population();
	if (!Planet || !People) return TEXT("NO PLANETARY CONTACT");
	if (People->GetCountryAtDirection(LocalDirection) == 0) return TEXT("SUPER-VOLCANO NEEDS LAND  //  TRY TSUNAMI TRIGGER AT SEA");
	// The crust does what a mantle charge does, with no munition in flight: melt,
	// a crater, an ash column, ejecta and a fire front.
	const FVector Surface = Planet->GetActorLocation()
		+ Planet->GetActorTransform().TransformVectorNoScale(LocalDirection) * Planet->PlanetRadius;
	FEarthfallStrike Strike = FEarthfallStrike::Make(Surface, 2, 2.0f, 1.0f, 2.0f);
	// Nobody is to blame for a volcano.
	Strike.AttackerCountry = -1;
	Planet->ApplyStrike(Strike);
	People->PostDispatch(TEXT("[TECTONIC] SUPER-VOLCANO ERUPTS  //  ASH COLUMN CLIMBING THROUGH THE STRATOSPHERE"), 6);
	return TEXT("SUPER-VOLCANO  //  ERUPTION UNDER WAY");
}

FString UEarthfallSandboxSubsystem::UseTsunami(const FVector& LocalDirection)
{
	AEarthfallPlanet* Planet = FindPlanet();
	UEarthfallPopulationSubsystem* People = Population();
	if (!Planet || !People) return TEXT("NO PLANETARY CONTACT");
	if (People->GetCountryAtDirection(LocalDirection) != 0) return TEXT("TSUNAMI TRIGGER NEEDS OPEN WATER");
	Planet->TriggerTsunami(LocalDirection, 0.75f);
	People->PostDispatch(TEXT("[SEISMIC] SEA FLOOR RUPTURE  //  TSUNAMI WARNING FOR ALL FACING COASTS"), 6);
	return TEXT("TSUNAMI TRIGGER  //  WAVE RELEASED");
}

FString UEarthfallSandboxSubsystem::UseMeteorShower(const FVector& LocalDirection, AEarthOrbitPawn* Pawn)
{
	if (!Pawn) return TEXT("NO FIRING PLATFORM");
	FVector TangentA;
	FVector TangentB;
	LocalDirection.FindBestAxisVectors(TangentA, TangentB);
	FRandomStream Random(FMath::Rand());
	// Nine bolides scattered over roughly a thousand kilometres.
	constexpr int32 Count = 9;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
		const float Offset = Random.FRandRange(0.01f, 0.16f);
		FPendingMeteor& Meteor = PendingMeteors.AddDefaulted_GetRef();
		Meteor.Direction = (LocalDirection + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * Offset).GetSafeNormal();
		Meteor.Delay = Random.FRandRange(0.0f, 2.4f);
	}
	MeteorPawn = Pawn;
	if (UEarthfallPopulationSubsystem* People = Population())
	{
		People->PostDispatch(TEXT("[IMPACT] METEOR SHOWER INBOUND  //  MULTIPLE BOLIDES ON TERMINAL TRACK"), 5);
	}
	return TEXT("METEOR SHOWER  //  9 BOLIDES INBOUND");
}

// ---------------------------------------------------------------------------
// Plague, rage, insurgency and cure
// ---------------------------------------------------------------------------
void UEarthfallSandboxSubsystem::AddBioZone(const FVector& Direction, float ZoneRadius, float Strength, int32 Kind, int32 Generation)
{
	if (BioZones.Num() >= 40) return;
	UEarthfallPopulationSubsystem* People = Population();
	FBioZone& Zone = BioZones.AddDefaulted_GetRef();
	Zone.Direction = Direction.GetSafeNormal();
	Zone.Radius = ZoneRadius;
	Zone.Strength = Strength;
	Zone.Kind = Kind;
	Zone.Generation = Generation;
	Zone.CountryId = People ? People->GetCountryAtDirection(Zone.Direction) : 0;
	Zone.SpreadTimer = FMath::FRandRange(4.0f, 7.0f);
	bBioMaskDirty = true;
}

double UEarthfallSandboxSubsystem::GetCountryInfected(int32 CountryId) const
{
	double Total = 0.0;
	for (const FBioZone& Zone : BioZones)
	{
		if (Zone.Kind == 0 && Zone.CountryId == CountryId) Total += Zone.Infected;
	}
	return Total;
}

void UEarthfallSandboxSubsystem::TickBio(float DeltaTime)
{
	UEarthfallPopulationSubsystem* People = Population();
	AEarthfallPlanet* Planet = FindPlanet();
	if (!People || !Planet) return;
	if (BioZones.IsEmpty() && !bBioMaskDirty) return;

	// The cloud drifts on the same wind as the ash; unrest stays where it is.
	const double WindTime = People->GetWindTime();
	for (FBioZone& Zone : BioZones)
	{
		Zone.Age += DeltaTime;
		if (Zone.Kind != 0) continue;
		const double Latitude = FMath::Asin(FMath::Clamp(Zone.Direction.Z, -1.0, 1.0));
		const double Longitude = FMath::Atan2(-Zone.Direction.Y, Zone.Direction.X);
		const FVector2D Wind = EarthfallWind::Velocity(Latitude, Longitude, WindTime);
		const double NewLatitude = FMath::Clamp(Latitude + Wind.Y * DeltaTime * 0.35, -1.4, 1.4);
		const double NewLongitude = Longitude + Wind.X * DeltaTime * 0.35 / FMath::Max(FMath::Cos(Latitude), 0.2);
		Zone.Direction = FVector(FMath::Cos(NewLatitude) * FMath::Cos(NewLongitude), -FMath::Cos(NewLatitude) * FMath::Sin(NewLongitude), FMath::Sin(NewLatitude));
	}

	BioTimer += DeltaTime;
	if (BioTimer >= 0.5f)
	{
		const float Step = BioTimer;
		BioTimer = 0.0f;
		double Infected = 0.0;
		for (int32 Index = BioZones.Num() - 1; Index >= 0; --Index)
		{
			FBioZone& Zone = BioZones[Index];
			if (Zone.Kind == 0)
			{
				// Infection climbs, the cloud widens, and a slice of the infected die.
				Zone.Strength = FMath::Min(Zone.Strength + 0.035f * Step, 1.0f);
				Zone.Radius = FMath::Min(Zone.Radius + 0.0016f * Step, 0.16f);
				Zone.Infected = People->PopulationInRadius(Zone.Direction, Zone.Radius) * Zone.Strength * 0.6;
				PlagueDead += People->ApplyRegionalAttrition(Zone.Direction, Zone.Radius, 0.0022 * Zone.Strength * Step, 0.004 * Zone.Strength * Step, 0.004f * Zone.Strength * Step);
				Infected += Zone.Infected;
				// It travels: by air to a hub city, or over the nearest border.
				Zone.SpreadTimer -= Step;
				if (Zone.SpreadTimer <= 0.0f && Zone.Generation < 3 && Zone.Strength > 0.45f)
				{
					Zone.SpreadTimer = FMath::FRandRange(7.0f, 12.0f);
					FVector Destination = Zone.Direction;
					if (!Routes.IsEmpty() && FMath::FRand() < 0.6f)
					{
						// Carried along whichever open air route starts nearest the outbreak.
						float Best = 0.90f;
						for (const FRoute& Route : Routes)
						{
							if (Route.bSea || !Route.bOpen) continue;
							const float NearFrom = static_cast<float>(FVector::DotProduct(Route.From, Zone.Direction));
							const float NearTo = static_cast<float>(FVector::DotProduct(Route.To, Zone.Direction));
							if (NearFrom > Best) { Best = NearFrom; Destination = Route.To; }
							if (NearTo > Best) { Best = NearTo; Destination = Route.From; }
						}
					}
					if (Destination.Equals(Zone.Direction))
					{
						FVector TangentA;
						FVector TangentB;
						Zone.Direction.FindBestAxisVectors(TangentA, TangentB);
						const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
						Destination = (Zone.Direction + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * (Zone.Radius * 1.7f)).GetSafeNormal();
					}
					const int32 DestinationCountry = People->GetCountryAtDirection(Destination);
					if (DestinationCountry > 0)
					{
						const int32 Generation = Zone.Generation + 1;
						FEarthfallCountryStats Stats;
						if (DestinationCountry != Zone.CountryId && People->GetCountryStats(DestinationCountry, Stats))
						{
							People->PostDispatch(FString::Printf(TEXT("[PLAGUE] OUTBREAK REACHES %s  //  %.1f MILLION INFECTED WORLDWIDE"), *Stats.Name.ToUpper(), InfectedNow / 1.0e6), 5);
						}
						AddBioZone(Destination, 0.035f, 0.12f, 0, Generation);
					}
				}
			}
			else
			{
				// Rage and insurgency: people turn on each other and on the grid.
				const bool bRage = Zone.Kind == 1;
				People->ApplyRegionalAttrition(Zone.Direction, Zone.Radius, (bRage ? 0.006 : 0.004) * Step, (bRage ? 0.03 : 0.045) * Step, (bRage ? 0.02f : 0.03f) * Step);
				Zone.SpreadTimer -= Step;
				if (bRage && Zone.SpreadTimer <= 0.0f && Zone.Generation < 2)
				{
					// Rage spills over the nearest border.
					Zone.SpreadTimer = FMath::FRandRange(6.0f, 9.0f);
					FVector TangentA;
					FVector TangentB;
					Zone.Direction.FindBestAxisVectors(TangentA, TangentB);
					const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
					const FVector Destination = (Zone.Direction + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * (Zone.Radius * 1.6f)).GetSafeNormal();
					const int32 Generation = Zone.Generation + 1;
					if (People->GetCountryAtDirection(Destination) > 0) AddBioZone(Destination, Zone.Radius * 0.8f, 0.8f, 1, Generation);
				}
				// It burns itself out; an insurgency ends when the government falls.
				FEarthfallCountryStats Stats;
				const bool bToppled = Zone.Kind == 2 && People->GetCountryStats(Zone.CountryId, Stats) && Stats.Stability == EEarthfallStability::Collapsed;
				if (bToppled)
				{
					People->PostDispatch(FString::Printf(TEXT("[COUP] REBELS TOPPLE THE GOVERNMENT OF %s"), *Stats.Name.ToUpper()), 7);
				}
				if (bToppled || Zone.Age > (bRage ? 26.0f : 40.0f))
				{
					BioZones.RemoveAtSwap(Index);
				}
			}
		}
		InfectedNow = Infected;
		bBioMaskDirty = true;

		BioNewsTimer += Step;
		if (BioNewsTimer >= 12.0f && InfectedNow >= 1.0e5)
		{
			BioNewsTimer = 0.0f;
			People->PostDispatch(FString::Printf(TEXT("[PLAGUE] INFECTED %.1f MILLION  //  CURED %.1f MILLION  //  DEAD %.1f MILLION"),
				InfectedNow / 1.0e6, CuredTotal / 1.0e6, PlagueDead / 1.0e6), 5);
		}
	}

	if (bBioMaskDirty)
	{
		bBioMaskDirty = false;
		TArray<FVector4> Zones;
		TArray<FLinearColor> Colours;
		for (const FBioZone& Zone : BioZones)
		{
			Zones.Add(FVector4(Zone.Direction.X, Zone.Direction.Y, Zone.Direction.Z, Zone.Radius * 1.25f));
			Colours.Add(Zone.Kind == 0 ? FLinearColor(Zone.Strength, 0.0f, 0.0f, 1.0f) : FLinearColor(0.0f, 0.9f, 0.0f, 1.0f));
		}
		Planet->SetBioZones(Zones, Colours);
	}
}

FString UEarthfallSandboxSubsystem::UsePlague(const FVector& LocalDirection)
{
	if (!bFactionsBuilt) BuildFactions();
	AddBioZone(LocalDirection, 0.06f, 0.25f, 0, 0);
	if (UEarthfallPopulationSubsystem* People = Population())
	{
		People->PostDispatch(TEXT("[PLAGUE] UNKNOWN PATHOGEN FALLING WITH THE RAIN  //  HOSPITALS REPORT FIRST CASES"), 6);
	}
	return TEXT("PLAGUE RAIN  //  BIO-CLOUD RELEASED");
}

FString UEarthfallSandboxSubsystem::UseRage(const FVector& LocalDirection)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People || People->GetCountryAtDirection(LocalDirection) == 0) return TEXT("RAGE OUTBREAK  //  NEEDS PEOPLE, NOT OPEN SEA");
	AddBioZone(LocalDirection, 0.05f, 0.9f, 1, 0);
	People->PostDispatch(TEXT("[RAGE] MASS VIOLENCE ERUPTS  //  POLICE LINES OVERRUN  //  CITIES BURNING"), 6);
	return TEXT("RAGE OUTBREAK  //  THE REGION TURNS ON ITSELF");
}

FString UEarthfallSandboxSubsystem::UseInsurgency(const FVector& LocalDirection)
{
	UEarthfallPopulationSubsystem* People = Population();
	FEarthfallCountryStats Stats;
	if (!People || !People->GetCountryStats(People->GetCountryAtDirection(LocalDirection), Stats)) return TEXT("REBEL INSURGENCY  //  CLICK A NATION");
	FactionFor(Stats.Id, LocalDirection);
	AddBioZone(LocalDirection, 0.07f, 0.9f, 2, 0);
	People->PostDispatch(FString::Printf(TEXT("[UPRISING] REBEL INSURGENCY ERUPTS IN %s  //  GARRISONS UNDER ATTACK"), *Stats.Name.ToUpper()), 6);
	return FString::Printf(TEXT("REBEL INSURGENCY  //  %s IN REVOLT"), *Stats.Name.ToUpper());
}

FString UEarthfallSandboxSubsystem::UseCure(const FVector& LocalDirection)
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return TEXT("NO PLANETARY CONTACT");
	constexpr float CureRadius = 0.22f;
	const float CosLimit = FMath::Cos(CureRadius);
	double Cured = 0.0;
	int32 Cleared = 0;
	for (int32 Index = BioZones.Num() - 1; Index >= 0; --Index)
	{
		if (FVector::DotProduct(BioZones[Index].Direction, LocalDirection) < CosLimit) continue;
		Cured += BioZones[Index].Infected;
		BioZones.RemoveAtSwap(Index);
		++Cleared;
	}
	CuredTotal += Cured;
	InfectedNow = FMath::Max(0.0, InfectedNow - Cured);
	People->RepairRegion(LocalDirection, CureRadius);
	bBioMaskDirty = true;
	People->PostDispatch(FString::Printf(TEXT("[BLESSING] HEALING RAIN  //  %.1f MILLION CURED  //  POWER RESTORED  //  CITY LIGHTS RETURN"), Cured / 1.0e6), 6);
	return FString::Printf(TEXT("CURE RAIN  //  %d OUTBREAKS CLEARED  //  %.1f M CURED"), Cleared, Cured / 1.0e6);
}

// ---------------------------------------------------------------------------
// Living traffic
// ---------------------------------------------------------------------------
void UEarthfallSandboxSubsystem::BuildRoutes()
{
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return;
	// Hub airports, and sea lanes as pairs of open-water waypoints.
	static const double Hubs[][2] = {
		{40.7, -74.0}, {34.0, -118.2}, {41.9, -87.6}, {19.4, -99.1}, {-23.5, -46.6}, {51.5, -0.1}, {48.85, 2.35}, {50.1, 8.7},
		{55.75, 37.6}, {25.2, 55.3}, {28.6, 77.2}, {1.35, 103.8}, {39.9, 116.4}, {31.2, 121.5}, {35.7, 139.7}, {-33.9, 151.2},
		{-26.2, 28.0}, {30.0, 31.2}, {6.5, 3.4}, {37.57, 126.98}};
	static const double Lanes[][4] = {
		{40.0, -68.0, 49.0, -9.0}, {34.0, -122.0, 34.0, 142.0}, {-33.0, 18.0, -34.0, -52.0}, {12.0, 52.0, 6.0, 80.0},
		{2.0, 105.0, 22.0, 119.0}, {36.0, -8.0, 14.0, -60.0}, {-35.0, 116.0, 5.0, 82.0}, {25.0, -88.0, 37.0, -72.0},
		{-10.0, -35.0, 5.0, -12.0}, {55.0, -160.0, 38.0, 145.0}, {-38.0, 150.0, -40.0, -75.0}, {58.0, 2.0, 62.0, -22.0}};
	const int32 HubCount = UE_ARRAY_COUNT(Hubs);
	FRandomStream Random(4242);
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const int32 A = Random.RandRange(0, HubCount - 1);
		const int32 B = (A + Random.RandRange(1, HubCount - 1)) % HubCount;
		FRoute& Route = Routes.AddDefaulted_GetRef();
		Route.From = EarthfallSolar::DirectionFromLatLon(Hubs[A][0], Hubs[A][1]);
		Route.To = EarthfallSolar::DirectionFromLatLon(Hubs[B][0], Hubs[B][1]);
		Route.FromCountry = People->GetCountryAtDirection(Route.From);
		Route.ToCountry = People->GetCountryAtDirection(Route.To);
		Route.Phase = Random.FRand();
		Route.Speed = Random.FRandRange(0.020f, 0.032f);
	}
	for (const double* Lane : Lanes)
	{
		for (int32 Ship = 0; Ship < 4; ++Ship)
		{
			FRoute& Route = Routes.AddDefaulted_GetRef();
			Route.From = EarthfallSolar::DirectionFromLatLon(Lane[0], Lane[1]);
			Route.To = EarthfallSolar::DirectionFromLatLon(Lane[2], Lane[3]);
			Route.Phase = Random.FRand();
			Route.Speed = Random.FRandRange(0.006f, 0.009f);
			Route.bSea = true;
		}
	}
}

void UEarthfallSandboxSubsystem::TickTraffic(float DeltaTime)
{
	if (Routes.IsEmpty()) BuildRoutes();
	RouteTimer += DeltaTime;
	if (RouteTimer < 1.0f) return;
	RouteTimer = 0.0f;
	UEarthfallPopulationSubsystem* People = Population();
	const AEarthfallPlanet* Planet = FindPlanet();
	if (!People) return;
	// A nation at war, blacked out or fallen grounds its flights; a tsunami clears the sea lanes.
	auto Grounded = [this, People](int32 CountryId)
	{
		FEarthfallCountryStats Stats;
		if (CountryId <= 0 || !People->GetCountryStats(CountryId, Stats)) return false;
		if (Stats.GridPercent < 55.0f || Stats.Stability >= EEarthfallStability::MartialLaw) return true;
		const int32* Faction = FactionByCountry.Find(CountryId);
		return Faction && !Factions[*Faction].AtWar.IsEmpty();
	};
	const bool bSeaClosed = Planet && Planet->IsTsunamiActive();
	for (FRoute& Route : Routes)
	{
		Route.bOpen = Route.bSea ? !bSeaClosed : !(Grounded(Route.FromCountry) || Grounded(Route.ToCountry));
	}
}

// ---------------------------------------------------------------------------
// Refugee crises at the border
// ---------------------------------------------------------------------------
void UEarthfallSandboxSubsystem::BuildNeighbours()
{
	const UEarthfallPopulationSubsystem* People = Population();
	if (!People || People->GetCountryCount() == 0) return;
	// Two nations are neighbours if they meet anywhere on a one-degree grid.
	constexpr int32 Width = 360;
	constexpr int32 Height = 180;
	TArray<int32> Grid;
	Grid.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FVector Direction = EarthfallSolar::DirectionFromLatLon(89.5 - Y, -179.5 + X);
			Grid[Y * Width + X] = People->GetCountryAtDirection(Direction);
			if (Grid[Y * Width + X] > 0) CountryCentres.FindOrAdd(Grid[Y * Width + X]) += Direction;
		}
	}
	for (TPair<int32, FVector>& Centre : CountryCentres) Centre.Value = Centre.Value.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	auto Link = [this](int32 A, int32 B)
	{
		if (A <= 0 || B <= 0 || A == B) return;
		Neighbours.FindOrAdd(A).AddUnique(B);
		Neighbours.FindOrAdd(B).AddUnique(A);
	};
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			Link(Grid[Y * Width + X], Grid[Y * Width + (X + 1) % Width]);
			if (Y + 1 < Height) Link(Grid[Y * Width + X], Grid[(Y + 1) * Width + X]);
		}
	}
}

void UEarthfallSandboxSubsystem::TickBorders(float DeltaTime)
{
	BorderTimer += DeltaTime;
	if (BorderTimer < 4.0f) return;
	BorderTimer = 0.0f;
	UEarthfallPopulationSubsystem* People = Population();
	if (!People) return;
	if (Neighbours.IsEmpty()) BuildNeighbours();

	// A quarter of a stricken nation's refugees press on each neighbour, and the
	// strain is that crowd measured against the neighbour's own population.
	TMap<int32, float> Stress;
	TMap<int32, int32> WorstSource;
	TMap<int32, double> WorstCrowd;
	FEarthfallCountryStats Source;
	FEarthfallCountryStats Host;
	for (int32 Id = 1; Id < People->GetCountryCount(); ++Id)
	{
		const TArray<int32>* Adjacent = Neighbours.Find(Id);
		if (!Adjacent || !People->GetCountryStats(Id, Source)) continue;
		// Only a real exodus counts: five million people and a fifth of the nation.
		if (Source.Displaced < 5.0e6 || Source.Displaced < Source.InitialPopulation * 0.20) continue;
		for (int32 Neighbour : *Adjacent)
		{
			if (!People->GetCountryStats(Neighbour, Host) || Host.InitialPopulation < 1.0e6) continue;
			const double Crowd = Source.Displaced * 0.25;
			Stress.FindOrAdd(Neighbour) += static_cast<float>(Crowd / Host.InitialPopulation) * 0.4f;
			if (Crowd > WorstCrowd.FindRef(Neighbour))
			{
				WorstCrowd.Add(Neighbour, Crowd);
				WorstSource.Add(Neighbour, Id);
			}
		}
	}
	People->SetBorderStress(Stress);

	for (const TPair<int32, float>& Entry : Stress)
	{
		if (Entry.Value < 0.06f) continue;
		const int32 SourceId = WorstSource.FindRef(Entry.Key);
		if (!People->GetCountryStats(Entry.Key, Host) || !People->GetCountryStats(SourceId, Source)) continue;
		if (!BorderCrises.Contains(Entry.Key))
		{
			BorderCrises.Add(Entry.Key);
			People->PostDispatch(FString::Printf(TEXT("[BORDER CRISIS] %.1f MILLION REFUGEES FROM %s CROSS INTO %s"),
				WorstCrowd.FindRef(Entry.Key) / 1.0e6, *Source.Name.ToUpper(), *Host.Name.ToUpper()), 6);
			UE_LOG(LogEarthfallSandbox, Display, TEXT("EARTHFALL_BORDER %s strained by refugees from %s (stress %.2f)"), *Host.Name, *Source.Name, Entry.Value);
		}
		// A government that still stands may be blamed for the flood and attacked;
		// one that has already fallen leaves its neighbours only martial law.
		if (Entry.Value >= 0.18f && Source.Stability < EEarthfallStability::Collapsed && FMath::FRand() < 0.30f)
		{
			const int32 HostFaction = FactionFor(Entry.Key, CountryCentres.FindRef(Entry.Key));
			const int32 SourceFaction = FactionFor(SourceId, CountryCentres.FindRef(SourceId));
			if (HostFaction >= 0 && SourceFaction >= 0 && Factions[HostFaction].AtWar.IsEmpty() && !Factions[HostFaction].bFallen)
			{
				DeclareWar(HostFaction, SourceFaction, TEXT("REFUGEE CRISIS AT THE BORDER"));
			}
		}
	}
}
