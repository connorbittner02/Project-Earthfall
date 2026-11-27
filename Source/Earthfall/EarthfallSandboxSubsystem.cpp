#include "EarthfallSandboxSubsystem.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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
}

void UEarthfallSandboxSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Meteor showers arrive as a ragged volley, not a single frame of launches.
	for (int32 Index = PendingMeteors.Num() - 1; Index >= 0; --Index)
	{
		PendingMeteors[Index].Delay -= DeltaTime;
		if (PendingMeteors[Index].Delay > 0.0f) continue;
		if (AEarthOrbitPawn* Pawn = MeteorPawn.Get())
		{
			Pawn->LaunchWeaponAt(PendingMeteors[Index].Direction, 0, 2.0f, 1.0f, 0.0f);
		}
		PendingMeteors.RemoveAtSwap(Index);
	}
}

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
	return FString::Printf(TEXT("%s  //  OFFLINE"), EarthfallTools::Get(Tab, Slot).Name);
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
	Planet->ApplyStrike(FEarthfallStrike::Make(Surface, 2, 2.0f, 1.0f, 2.0f));
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
