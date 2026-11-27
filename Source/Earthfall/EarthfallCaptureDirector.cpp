#include "EarthfallCaptureDirector.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallTypes.h"
#include "AssetCompilingManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogEarthfallCapture, Log, All);

AEarthfallCaptureDirector::AEarthfallCaptureDirector()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AEarthfallCaptureDirector::BeginPlay()
{
	Super::BeginPlay();
	OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Capture"));
	BuildScript();
	UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE armed: %d steps, output %s"), Steps.Num(), *OutputDirectory);
}

AEarthOrbitPawn* AEarthfallCaptureDirector::Pawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return Controller ? Cast<AEarthOrbitPawn>(Controller->GetPawn()) : nullptr;
}

void AEarthfallCaptureDirector::At(float Time, TFunction<void()> Action)
{
	FStep& Step = Steps.AddDefaulted_GetRef();
	Step.Time = Time;
	Step.Action = MoveTemp(Action);
}

void AEarthfallCaptureDirector::Shot(const TCHAR* Name)
{
	const FString Path = OutputDirectory / FString::Printf(TEXT("%s.png"), Name);
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	++ShotCount;
	UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE shot %s at t=%.2f"), Name, ScriptSeconds);
}

void AEarthfallCaptureDirector::BuildScript()
{
	auto View = [this](float Longitude, float Latitude, float Distance)
	{
		if (AEarthOrbitPawn* P = Pawn()) P->SetOrbitView(Longitude, Latitude, Distance);
	};
	auto Weapon = [this](int32 Index, int32 YieldIndex)
	{
		if (AEarthOrbitPawn* P = Pawn())
		{
			P->SelectWeapon(Index);
			P->SetMultiplierIndex(EEarthfallMultiplier::Yield, YieldIndex);
		}
	};
	auto Fire = [this](double Latitude, double Longitude)
	{
		if (AEarthOrbitPawn* P = Pawn()) P->FireAtDirection(EarthfallSolar::DirectionFromLatLon(Latitude, Longitude));
	};

	// The shot list assumes -EarthfallSunLon=-60 -EarthfallSunLat=10: the Americas
	// in daylight, Asia in darkness and the terminator over Africa and Europe.
	At(0.0f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetAimEnabled(false); });
	At(0.0f, [=]() { View(-85.0f, 22.0f, 420.0f); });
	At(2.5f, [this]() { Shot(TEXT("01-earth-day")); });

	// From behind the planet, with the Sun just clear of the limb.
	At(2.7f, [=]() { View(109.0f, -10.0f, 560.0f); });
	At(5.0f, [this]() { Shot(TEXT("02-sun-limb-stars")); });

	// Pulled back along the galactic plane, so the Milky Way crosses the frame.
	At(5.02f, [=]() { View(-19.3f, 0.0f, 1100.0f); });
	At(5.18f, [this]() { Shot(TEXT("02b-milky-way")); });

	At(5.2f, [=]() { View(95.0f, 24.0f, 330.0f); });
	At(7.5f, [this]() { Shot(TEXT("03-night-city-lights")); });

	// Thermonuclear strike on New York, followed from launch to aftermath.
	At(7.7f, [=]() { View(-92.0f, 34.0f, 380.0f); Weapon(EarthfallWeapons::Nuclear, EarthfallMultipliers::DefaultIndex); });
	At(9.0f, [=]() { Fire(40.71, -74.01); });
	At(9.35f, [this]() { Shot(TEXT("04-nuke-launch")); });
	At(10.3f, [this]() { Shot(TEXT("05-nuke-inflight")); });
	At(11.12f, [this]() { Shot(TEXT("06-nuke-flash")); });
	At(12.2f, [this]() { Shot(TEXT("07-nuke-fireball-shockwave")); });
	At(14.5f, [this]() { Shot(TEXT("08-shockwave-cloud-clearing")); });
	At(19.5f, [this]() { Shot(TEXT("09-nuke-aftermath")); });

	// Kinetic rod at 10x yield into the Amazon basin.
	At(19.7f, [=]() { View(-48.0f, 0.0f, 300.0f); Weapon(0, 5); });
	At(21.0f, [=]() { Fire(-4.0, -62.0); });
	At(21.3f, [this]() { Shot(TEXT("10-kinetic-launch")); });
	At(22.3f, [this]() { Shot(TEXT("11-kinetic-impact")); });

	// Mantle charge, then a close pass over the cooling crater.
	At(22.5f, [=]() { Weapon(2, EarthfallMultipliers::DefaultIndex); Fire(-15.0, -48.0); });
	At(25.0f, [this]() { Shot(TEXT("12-mantle-impact")); });
	At(25.2f, [=]() { View(-56.0f, -9.0f, 150.0f); });
	At(28.0f, [this]() { Shot(TEXT("13-crater-closeup")); });

	// Let the wind carry the plumes before the final wide shot.
	At(28.2f, [=]() { View(-80.0f, 30.0f, 430.0f); });
	At(46.0f, [this]() { Shot(TEXT("14-ash-drift")); });
	// Country Dossier for the nation that took the nuclear strike, then the HUD modes.
	At(46.2f, [this]() { if (AEarthOrbitPawn* P = Pawn()) { P->SetOrbitView(-98.0f, 38.0f, 330.0f); P->InspectDirection(EarthfallSolar::DirectionFromLatLon(39.0, -98.0)); } });
	At(47.4f, [this]() { Shot(TEXT("15-country-dossier")); });
	At(47.5f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::TickerOnly); });
	At(47.9f, [this]() { Shot(TEXT("16-hud-ticker-only")); });
	At(48.0f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Hidden); });
	At(48.4f, [this]() { Shot(TEXT("17-hud-hidden")); });
	At(48.5f, [this]() { if (AEarthOrbitPawn* P = Pawn()) { P->SetHudMode(EEarthfallHudMode::Full); P->ToggleInspectMode(); } });
	// A cryo impactor (the asteroid) into the Bay of Bengal on the night side: the
	// wave should cross to the lit coasts of India, Bangladesh and Myanmar.
	At(49.0f, [=]() { View(86.0f, 14.0f, 235.0f); Weapon(4, EarthfallMultipliers::DefaultIndex); });
	At(49.3f, [=]() { Fire(11.0, 88.5); });
	At(52.0f, [this]() { Shot(TEXT("18-tsunami-crest")); });
	At(54.6f, [this]() { Shot(TEXT("19-tsunami-landfall-blackout")); });

	// Wildfires spreading from the nuclear strike's rim, with their smoke.
	At(54.8f, [=]() { View(-96.0f, 37.0f, 230.0f); });
	At(56.6f, [this]() { Shot(TEXT("20-wildfire-smoke")); });

	// The mantle crater in daylight, off-centre so parallax depth and rim shadow read.
	At(56.8f, [=]() { View(-36.0f, -12.0f, 215.0f); });
	At(58.6f, [this]() { Shot(TEXT("21-pom-crater")); });
	At(59.8f, [this]() { Finish(); });

	Steps.StableSort([](const FStep& A, const FStep& B) { return A.Time < B.Time; });
}

void AEarthfallCaptureDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished) return;

	if (!bReady)
	{
		// First launch compiles every material and builds the 8K textures; a shot
		// taken before that finishes would capture the grey fallback material.
		WarmupSeconds += DeltaSeconds;
		const int32 Remaining = FAssetCompilingManager::Get().GetNumRemainingAssets();
		QuietSeconds = Remaining == 0 ? QuietSeconds + DeltaSeconds : 0.0f;
		if (WarmupSeconds >= NextProgressLog)
		{
			NextProgressLog = WarmupSeconds + 5.0f;
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE warming up: %d assets compiling, %.0fs elapsed"), Remaining, WarmupSeconds);
		}
		if ((WarmupSeconds > 6.0f && QuietSeconds > 3.0f && Pawn()) || WarmupSeconds > 600.0f)
		{
			bReady = true;
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE ready after %.1fs"), WarmupSeconds);
		}
		return;
	}

	ScriptSeconds += DeltaSeconds;
	FrameSecondsTotal += DeltaSeconds;
	WorstFrameSeconds = FMath::Max(WorstFrameSeconds, static_cast<double>(DeltaSeconds));
	++FrameCount;
	while (Steps.IsValidIndex(NextStep) && Steps[NextStep].Time <= ScriptSeconds)
	{
		Steps[NextStep].Action();
		++NextStep;
		if (bFinished) return;
	}
}

void AEarthfallCaptureDirector::Finish()
{
	bFinished = true;
	const double Average = FrameCount > 0 ? FrameSecondsTotal / FrameCount : 0.0;
	UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE_COMPLETE shots=%d frames=%d avg_ms=%.2f avg_fps=%.1f worst_ms=%.1f"),
		ShotCount, FrameCount, Average * 1000.0, Average > 0.0 ? 1.0 / Average : 0.0, WorstFrameSeconds * 1000.0);
	if (const UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>())
	{
		const FEarthfallTelemetry T = Population->GetTelemetry();
		UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE telemetry casualties=%.0f fallout_deaths=%.0f displaced=%.0f blackout=%.1f atmosphere=%.1f temp=%.2f soot=%.2f radiation=%.1f"),
			T.TotalCasualties, T.FalloutCasualties, T.DisplacedPopulation, T.GridBlackoutPercent, T.AtmosphereHealth, T.MeanTemperatureC, T.SootTeragrams, T.FalloutIndex);
	}
	FPlatformMisc::RequestExit(false);
}
