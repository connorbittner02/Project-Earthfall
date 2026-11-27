#include "EarthfallSandboxSubsystem.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallProjectile.h"
#include "EarthfallTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogEarthfallSandbox, Log, All);

namespace
{
/** At most this many AI missiles in the air at once: enough to read as a war, cheap to resolve. */
constexpr int32 MaxAiMissiles = 8;
constexpr int32 MaxUnits = 24;
constexpr int32 DebrisCount = 900;
/** Angular speed of a strike group, radians per second (a brisk few seconds per ocean). */
constexpr float UnitSpeed = 0.035f;

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
}

bool UEarthfallSandboxSubsystem::GetFactionInfo(int32 CountryId, FEarthfallFactionInfo& OutInfo) const
{
	const int32* Index = FactionByCountry.Find(CountryId);
	if (!Index || !Factions.IsValidIndex(*Index)) return false;
	const FFaction& Faction = Factions[*Index];
	OutInfo.Code = Faction.Code;
	OutInfo.MilitaryStrength = Faction.MilitaryStrength;
	OutInfo.NuclearStockpile = Faction.NuclearStockpile;
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
	BuildMarkers();
}

void UEarthfallSandboxSubsystem::LaunchMissile(FFaction& From, const FFaction& At, bool bNuclear)
{
	AEarthfallPlanet* Planet = FindPlanet();
	if (!Planet || !GetWorld()) return;
	// Aimed at the enemy capital, landing anywhere within a few hundred kilometres of it.
	FVector TangentA;
	FVector TangentB;
	At.Capital.FindBestAxisVectors(TangentA, TangentB);
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const FVector Aim = (At.Capital + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * FMath::FRandRange(0.01f, 0.09f)).GetSafeNormal();
	const FTransform& Transform = Planet->GetActorTransform();
	const FVector Start = Planet->GetActorLocation() + Transform.TransformVectorNoScale(From.Capital) * Planet->PlanetRadius * 1.004f;
	const FVector Impact = Planet->GetActorLocation() + Transform.TransformVectorNoScale(Aim) * Planet->PlanetRadius;

	// Conventional salvos are small kinetic warheads; a nuclear one is a
	// quarter-yield, half-size weapon, so a war scars regions rather than ending the world at once.
	FEarthfallStrike Strike = bNuclear
		? FEarthfallStrike::Make(Impact, EarthfallWeapons::Nuclear, 0.25f, 0.5f, 1.0f)
		: FEarthfallStrike::Make(Impact, 0, 1.0f, 0.5f, 0.0f);
	Strike.AttackerCountry = From.CountryId;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AEarthfallProjectile* Missile = GetWorld()->SpawnActor<AEarthfallProjectile>(AEarthfallProjectile::StaticClass(), Start, FRotator::ZeroRotator, Params))
	{
		const float Range = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(From.Capital, Aim)), -1.0f, 1.0f));
		Missile->Launch(Planet, nullptr, Start, Strike, 2.2f + Range * 2.4f, true);
		++From.Exchanges;
		if (bNuclear) --From.NuclearStockpile;
	}
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

void UEarthfallSandboxSubsystem::SpawnUnit(int32 Owner, int32 Target, const FVector& From)
{
	if (Units.Num() >= MaxUnits || !Factions.IsValidIndex(Target)) return;
	FUnit& Unit = Units.AddDefaulted_GetRef();
	Unit.Direction = From.GetSafeNormal();
	Unit.Owner = Owner;
	Unit.Target = Target;
}

void UEarthfallSandboxSubsystem::TickUnits(float DeltaTime)
{
	AEarthfallPlanet* Planet = FindPlanet();
	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		FUnit& Unit = Units[Index];
		if (!Factions.IsValidIndex(Unit.Target) || Factions[Unit.Target].bFallen)
		{
			Units.RemoveAtSwap(Index);
			continue;
		}
		const FVector& Goal = Factions[Unit.Target].Capital;
		const float Range = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Unit.Direction, Goal)), -1.0f, 1.0f));
		if (Range > 0.13f)
		{
			Unit.Direction = SlerpDirection(Unit.Direction, Goal, UnitSpeed * DeltaTime);
			continue;
		}
		// On station off the enemy capital: three volleys, then the group is spent.
		Unit.FireTimer -= DeltaTime;
		if (Unit.FireTimer > 0.0f || !Planet) continue;
		Unit.FireTimer = 2.2f;
		FVector TangentA;
		FVector TangentB;
		Goal.FindBestAxisVectors(TangentA, TangentB);
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const FVector Aim = (Goal + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * FMath::FRandRange(0.0f, 0.05f)).GetSafeNormal();
		FEarthfallStrike Strike = FEarthfallStrike::Make(
			Planet->GetActorLocation() + Planet->GetActorTransform().TransformVectorNoScale(Aim) * Planet->PlanetRadius, 0, 0.5f, 0.5f, 0.0f);
		Strike.AttackerCountry = Factions.IsValidIndex(Unit.Owner) ? Factions[Unit.Owner].CountryId : -1;
		Planet->ApplyStrike(Strike);
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
		FEarthfallMarker& Marker = Markers.AddDefaulted_GetRef();
		const FVector World = Transform.TransformVectorNoScale(Unit.Direction);
		Marker.Position = Center + World * Radius * 1.006f;
		const FVector Goal = Factions.IsValidIndex(Unit.Target) ? Transform.TransformVectorNoScale(Factions[Unit.Target].Capital) : World;
		Marker.Forward = (Goal - World * FVector::DotProduct(Goal, World)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		Marker.Size = 0.20f;
		Marker.Stretch = 2.6f;
		Marker.Kind = EarthfallMarkers::Unit;
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
	if (Units.Num() >= MaxUnits) return TEXT("STRIKE GROUP  //  TOO MANY GROUPS AT SEA");
	// It sails for the nearest capital that is at war; with the world at peace it has no orders.
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
	SpawnUnit(Factions[Target].AtWar[0], Target, LocalDirection);
	return FString::Printf(TEXT("STRIKE GROUP  //  UNDER WAY FOR %s"), *Factions[Target].Name);
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
