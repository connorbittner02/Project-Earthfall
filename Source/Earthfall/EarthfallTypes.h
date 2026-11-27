#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"

/**
 * Shared, dependency-free definitions used by the planet, pawn, HUD, projectiles
 * and the population simulation so they can never disagree about a weapon's
 * numbers, where the sun is, or which way the wind blows.
 */

namespace EarthfallSolar
{
	/**
	 * Unit vector from Earth's centre toward the Sun in planet-local axes
	 * (+X = 0E on the equator, -Y = 90E, +Z = north pole; Unreal is left-handed), from UTC time.
	 * -EarthfallSunLon= / -EarthfallSunLat= pin the subsolar point for repeatable captures.
	 */
	inline FVector ComputeToSun()
	{
		float OverrideLon = 0.0f;
		float OverrideLat = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("EarthfallSunLon="), OverrideLon))
		{
			FParse::Value(FCommandLine::Get(), TEXT("EarthfallSunLat="), OverrideLat);
			const double Lon = FMath::DegreesToRadians(static_cast<double>(OverrideLon));
			const double Lat = FMath::DegreesToRadians(static_cast<double>(OverrideLat));
			return FVector(FMath::Cos(Lat) * FMath::Cos(Lon), -FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
		}
		const FDateTime UtcNow = FDateTime::UtcNow();
		const double DayPhase = 2.0 * PI * (static_cast<double>(UtcNow.GetDayOfYear()) - 81.0) / 365.2422;
		const double Declination = FMath::DegreesToRadians(23.44) * FMath::Sin(DayPhase);
		const double UtcHours = UtcNow.GetHour() + UtcNow.GetMinute() / 60.0 + UtcNow.GetSecond() / 3600.0;
		const double HourAngle = FMath::DegreesToRadians((UtcHours - 12.0) * 15.0);
		return FVector(FMath::Cos(Declination) * FMath::Cos(HourAngle),
			FMath::Cos(Declination) * FMath::Sin(HourAngle), FMath::Sin(Declination));
	}

	inline FVector DirectionFromLatLon(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector(FMath::Cos(Lat) * FMath::Cos(Lon), -FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
	}
}

namespace EarthfallWind
{
	/**
	 * Wind in radians per second as (eastward, northward). This is a line-for-line
	 * mirror of WindVelocity() in Scripts/shaders/ash_advection.hlsl: the shader
	 * moves the visible plume, this moves the CPU plumes that cause casualties.
	 */
	inline FVector2D Velocity(double Lat, double Lon, double Time)
	{
		double Zonal = -0.016 * FMath::Cos(3.0 * Lat) * (0.7 + 0.3 * FMath::Cos(Lat));
		Zonal += 0.004 * FMath::Sin(Lat * 9.0 + Lon * 2.0 + Time * 0.021);
		const double Meridional = 0.006 * FMath::Sin(Lon * 3.0 + Lat * 5.0 + Time * 0.03) * FMath::Cos(Lat)
			+ 0.003 * FMath::Sin(Lon * 7.0 - Time * 0.05);
		return FVector2D(Zonal, Meridional);
	}
}

enum class EEarthfallProjectile : uint8
{
	Rod,        // tungsten rod: fast, thin, white-hot re-entry streak
	Beam,       // plasma lance: near-instant beam from the firing platform
	Penetrator, // mantle charge: heavy drilling warhead with an orange burn
	Singularity,// micro black hole: dark core in a violet accretion glow
	Meteor,     // cryo impactor: icy bolide with a long cyan tail
	Missile     // thermonuclear: boosted warhead with a bright exhaust plume
};

struct FEarthfallWeaponSpec
{
	const TCHAR* Name;
	const TCHAR* Descriptor;
	const TCHAR* ReportName;
	EEarthfallProjectile Projectile;
	/** Blast radius in world units on a 100-unit planet, before multipliers. */
	float BlastRadius;
	float Energy;
	float Cooldown;
	/** Seconds from launch to detonation. */
	float TravelSeconds;
	FLinearColor Color;
	float ShakeStrength;
	float ShakeDuration;
	/** Soot and radioactive fallout lofted per strike at 1x multipliers. */
	float Soot;
	float Fallout;
};

namespace EarthfallWeapons
{
	constexpr int32 Count = 6;
	constexpr int32 Nuclear = 5;

	inline const FEarthfallWeaponSpec& Get(int32 Index)
	{
		static const FEarthfallWeaponSpec Specs[Count] = {
			{TEXT("KINETIC ROD"), TEXT("TIGHT / FAST"), TEXT("kinetic impact"), EEarthfallProjectile::Rod,
				8.0f, 50.0f, 0.45f, 0.75f, FLinearColor(1.0f, 0.72f, 0.42f), 0.8f, 0.30f, 0.30f, 0.0f},
			{TEXT("PLASMA LANCE"), TEXT("BEAM / STEAM"), TEXT("plasma lance"), EEarthfallProjectile::Beam,
				15.0f, 100.0f, 0.80f, 0.16f, FLinearColor(0.16f, 0.86f, 1.0f), 1.4f, 0.40f, 0.35f, 0.0f},
			{TEXT("MANTLE CHARGE"), TEXT("SEISMIC / DEEP"), TEXT("mantle charge"), EEarthfallProjectile::Penetrator,
				25.0f, 180.0f, 1.50f, 1.50f, FLinearColor(1.0f, 0.28f, 0.06f), 3.0f, 0.80f, 0.90f, 0.05f},
			{TEXT("MICRO BLACK HOLE"), TEXT("INWARD / GRAVITY"), TEXT("micro black hole"), EEarthfallProjectile::Singularity,
				19.0f, 145.0f, 2.00f, 1.90f, FLinearColor(0.60f, 0.25f, 0.95f), 2.2f, 0.60f, 0.10f, 0.0f},
			{TEXT("CRYO IMPACTOR"), TEXT("FROST / CLIMATE"), TEXT("cryo impactor"), EEarthfallProjectile::Meteor,
				13.0f, 110.0f, 1.25f, 1.30f, FLinearColor(0.48f, 0.88f, 1.0f), 1.7f, 0.45f, 0.20f, 0.0f},
			{TEXT("THERMONUCLEAR"), TEXT("FLASH / EMP / FALLOUT"), TEXT("thermonuclear strike"), EEarthfallProjectile::Missile,
				32.0f, 155.0f, 3.50f, 2.00f, FLinearColor(0.90f, 0.95f, 1.0f), 4.8f, 1.20f, 1.00f, 1.00f},
		};
		return Specs[FMath::Clamp(Index, 0, Count - 1)];
	}
}

/** The three player-adjustable weapon multipliers. */
enum class EEarthfallMultiplier : uint8
{
	Yield,
	BlastRadius,
	Fallout,
	Count
};

namespace EarthfallMultipliers
{
	inline TConstArrayView<float> Steps(EEarthfallMultiplier Kind)
	{
		static const float Yield[] = {0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f};
		static const float Radius[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
		static const float Fallout[] = {0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f};
		switch (Kind)
		{
		case EEarthfallMultiplier::Yield: return Yield;
		case EEarthfallMultiplier::BlastRadius: return Radius;
		default: return Fallout;
		}
	}

	/** Index of the 1.0x step in every table. */
	constexpr int32 DefaultIndex = 2;

	inline const TCHAR* Label(EEarthfallMultiplier Kind)
	{
		switch (Kind)
		{
		case EEarthfallMultiplier::Yield: return TEXT("YIELD");
		case EEarthfallMultiplier::BlastRadius: return TEXT("BLAST RADIUS");
		default: return TEXT("FALLOUT");
		}
	}
}

/**
 * The physical body behind a strike. The blast-radius multiplier scales its
 * diameter and the yield multiplier scales its kinetic energy, so entry speed
 * goes with the square root of yield: E = 1/2 m v^2.
 */
struct FEarthfallImpactor
{
	/** False for beams, singularities and warheads, whose energy is not kinetic. */
	bool bKinetic = false;
	double DiameterMeters = 0.0;
	double SpeedKmS = 0.0;
	double MassKg = 0.0;
	double EnergyJoules = 0.0;

	double Megatons() const { return EnergyJoules / 4.184e15; }

	/** Transient cavity radius in deep water, metres. Gravity-regime scaling: R = 1/2 (E / (rho g))^(1/4). */
	double WaterCavityRadiusMeters() const
	{
		return 0.5 * FMath::Pow(FMath::Max(EnergyJoules, 0.0) / (1000.0 * 9.81), 0.25);
	}

	/** Water thrown out of a hemispherical cavity of that radius, cubic kilometres. */
	double DisplacedWaterKm3() const
	{
		const double RadiusKm = WaterCavityRadiusMeters() / 1000.0;
		return (2.0 / 3.0) * PI * RadiusKm * RadiusKm * RadiusKm;
	}

	/** 0 = no tsunami, otherwise 0.25..1 on a log scale of displaced volume (1 near 10,000 km3). */
	float TsunamiStrength() const
	{
		const double Volume = DisplacedWaterKm3();
		if (Volume < 0.05) return 0.0f;
		return FMath::Clamp(0.25f + 0.18f * static_cast<float>(FMath::LogX(10.0, Volume + 1.0)), 0.25f, 1.0f);
	}
};

namespace EarthfallPhysics
{
	inline FEarthfallImpactor MakeImpactor(int32 WeaponIndex, float YieldMultiplier, float RadiusMultiplier)
	{
		struct FBody { bool bKinetic; double Diameter; double SpeedKmS; double Density; double Megatons; };
		// Rod: tungsten. Mantle charge: steel penetrator. Cryo impactor: an ice body.
		static const FBody Bodies[EarthfallWeapons::Count] = {
			{true, 20.0, 11.0, 19250.0, 0.0},
			{false, 0.0, 0.0, 0.0, 5.0},
			{true, 350.0, 12.0, 7800.0, 0.0},
			{false, 0.0, 0.0, 0.0, 100.0},
			{true, 1200.0, 20.0, 917.0, 0.0},
			{false, 0.0, 0.0, 0.0, 50.0},
		};
		const FBody& Body = Bodies[FMath::Clamp(WeaponIndex, 0, EarthfallWeapons::Count - 1)];
		const double Yield = FMath::Max(static_cast<double>(YieldMultiplier), 0.01);
		FEarthfallImpactor Impactor;
		Impactor.bKinetic = Body.bKinetic;
		if (!Body.bKinetic)
		{
			Impactor.EnergyJoules = Body.Megatons * Yield * 4.184e15;
			return Impactor;
		}
		Impactor.DiameterMeters = Body.Diameter * FMath::Max(static_cast<double>(RadiusMultiplier), 0.05);
		Impactor.SpeedKmS = Body.SpeedKmS * FMath::Sqrt(Yield);
		Impactor.MassKg = Body.Density * (PI / 6.0) * FMath::Pow(Impactor.DiameterMeters, 3.0);
		const double SpeedMetres = Impactor.SpeedKmS * 1000.0;
		Impactor.EnergyJoules = 0.5 * Impactor.MassKg * SpeedMetres * SpeedMetres;
		return Impactor;
	}
}

/** Everything the planet needs to resolve one detonation. */
struct FEarthfallStrike
{
	FVector WorldLocation = FVector::ZeroVector;
	int32 WeaponIndex = 0;
	/** Final blast radius in world units, after multipliers. */
	float Radius = 8.0f;
	/** Final energy, after the yield multiplier. */
	float Energy = 50.0f;
	float YieldMultiplier = 1.0f;
	float FalloutMultiplier = 1.0f;
	/** The body and its kinetic energy in real units. */
	FEarthfallImpactor Impactor;

	/** Blast radius grows with the fourth root of yield so 100x is ~3x wider, not 100x. */
	static FEarthfallStrike Make(const FVector& InWorldLocation, int32 InWeaponIndex,
		float YieldMultiplier, float RadiusMultiplier, float FalloutMultiplier)
	{
		const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(InWeaponIndex);
		FEarthfallStrike Strike;
		Strike.WorldLocation = InWorldLocation;
		Strike.WeaponIndex = FMath::Clamp(InWeaponIndex, 0, EarthfallWeapons::Count - 1);
		Strike.Radius = BlastRadius(InWeaponIndex, YieldMultiplier, RadiusMultiplier);
		Strike.Energy = Spec.Energy * YieldMultiplier;
		Strike.YieldMultiplier = YieldMultiplier;
		Strike.FalloutMultiplier = FalloutMultiplier;
		Strike.Impactor = EarthfallPhysics::MakeImpactor(InWeaponIndex, YieldMultiplier, RadiusMultiplier);
		return Strike;
	}

	static float BlastRadius(int32 InWeaponIndex, float YieldMultiplier, float RadiusMultiplier)
	{
		return EarthfallWeapons::Get(InWeaponIndex).BlastRadius * RadiusMultiplier
			* FMath::Pow(FMath::Max(YieldMultiplier, 0.01f), 0.25f);
	}
};
