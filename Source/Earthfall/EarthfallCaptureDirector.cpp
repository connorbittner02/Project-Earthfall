#include "EarthfallCaptureDirector.h"
#include "EngineUtils.h"
#include "EarthfallPlanet.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallProjectile.h"
#include "EarthfallSandboxSubsystem.h"
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
	Step.Time = Time + TimeOffset;
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
		// Scripted showcase strikes always land: defenders get no shot at them. The
		// interception shots use LaunchWeaponAt, which they can stop.
		for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It) It->bDefenseChecked = true;
	};

	// The shot list assumes -EarthfallSunLon=-60 -EarthfallSunLat=10: the Americas
	// in daylight, Asia in darkness and the terminator over Africa and Europe.
	At(0.0f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetAimEnabled(false); });
	// Lowest orbit first, HUD off, while the planet is still untouched: desert
	// relief, sunglint on open ocean, city lights at night, and a mid descent.
	// Each low-orbit view holds for a few seconds first, so streamed terrain
	// (when Cesium is installed and has a token) has time to load.
	At(0.0f, [=, this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Hidden); View(-112.0f, 27.0f, 103.5f); });
	At(12.0f, [this]() { Shot(TEXT("22-low-orbit-desert-coast")); });
	At(12.1f, [=]() { View(-35.0f, -45.0f, 103.5f); });
	At(17.0f, [this]() { Shot(TEXT("23-low-orbit-ocean")); });
	At(17.1f, [=]() { View(119.0f, 27.0f, 103.5f); });
	At(23.0f, [this]() { Shot(TEXT("24-low-orbit-night-cities")); });
	At(23.1f, [=]() { View(-112.0f, 27.0f, 106.0f); });
	At(28.0f, [this]() { Shot(TEXT("25-mid-descent")); });
	// The orbit floor only drops below 1.035 R once terrain has loaded; without
	// it these two views clamp to the procedural floor.
	At(28.1f, [=]() { View(-112.1f, 35.2f, 100.6f); });
	At(38.0f, [this]() { Shot(TEXT("26-terrain-40km")); });
	At(38.1f, [=]() { View(-112.1f, 35.2f, 99.0f); });
	At(50.0f, [this]() { Shot(TEXT("27-terrain-30000ft")); });
	At(50.1f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Full); });
	TimeOffset = 50.2f;
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
	// Down onto the mantle crater, to see the destruction on streamed terrain.
	At(58.7f, [=, this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Hidden); View(-48.0f, -18.0f, 103.5f); });
	// The floor drops only once terrain is ready again, so descend in a second step.
	At(61.5f, [=]() { View(-48.0f, -18.0f, 100.6f); });
	At(69.0f, [this]() { Shot(TEXT("28-terrain-crater")); });
	// Ocean impact south of Mexico: a 600 m ice body at 2x yield. The splash crown
	// from low orbit, then the wave reaching the streamed Oaxaca coast 7 degrees away.
	At(69.2f, [=, this]() { View(-99.0f, 3.5f, 112.0f); Weapon(4, 3); if (AEarthOrbitPawn* P = Pawn()) P->SetMultiplierIndex(EEarthfallMultiplier::BlastRadius, 1); });
	At(69.6f, [=]() { Fire(8.5, -99.0); });
	At(73.0f, [this]() { Shot(TEXT("29-splash-crown")); });
	At(73.2f, [=, this]() { View(-97.0f, 14.2f, 103.5f); if (AEarthOrbitPawn* P = Pawn()) P->SetMultiplierIndex(EEarthfallMultiplier::BlastRadius, EarthfallMultipliers::DefaultIndex); });
	At(75.6f, [=]() { View(-97.0f, 14.2f, 100.5f); });
	At(84.0f, [this]() { Shot(TEXT("30-tsunami-terrain-coast")); });

	// Mantle charge on the northern plains, seen near the limb so the ejecta
	// curtain climbs against black space and then falls back as streaks.
	At(84.2f, [=]() { View(0.0f, 45.0f, 430.0f); Weapon(2, 3); });
	At(84.6f, [=]() { Fire(45.0, -100.0); });
	At(88.3f, [this]() { Shot(TEXT("31-ejecta-launch")); });
	At(92.0f, [this]() { Shot(TEXT("32-ejecta-arc")); });
	At(97.0f, [this]() { Shot(TEXT("33-reentry-streaks")); });

	// The same strike's ash column from under the cloud decks, 25 degrees south
	// over streamed terrain, in the soot-reddened haze.
	At(97.2f, [=]() { View(-100.0f, 20.0f, 103.5f); });
	At(100.0f, [=]() { View(-100.0f, 20.0f, 101.2f); });
	At(110.0f, [this]() { Shot(TEXT("34-ash-column-terrain")); });
	// The command dock's second and third tabs (the first is in every HUD shot).
	At(110.2f, [=, this]() { View(-80.0f, 30.0f, 430.0f); if (AEarthOrbitPawn* P = Pawn()) { P->SetHudMode(EEarthfallHudMode::Full); P->SelectTool(EarthfallTools::TabNature, EarthfallTools::PlagueRain); } });
	At(111.4f, [this]() { Shot(TEXT("35-dock-bio-nature")); });
	At(111.5f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SelectTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar); });
	At(112.2f, [this]() { Shot(TEXT("36-dock-diplomacy")); });
	// Nuclear winter, pinned for the shot: the sea-ice and frost front the climate
	// model would reach below 6 C or 40% sunlight.
	At(112.3f, [=, this]()
	{
		View(-40.0f, 48.0f, 440.0f);
		if (AEarthOrbitPawn* P = Pawn()) { P->SetHudMode(EEarthfallHudMode::Hidden); P->SelectTool(EarthfallTools::TabOrbital, 0); }
		for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It) It->SetWinterOverride(0.75f);
	});
	At(114.5f, [this]() { Shot(TEXT("37-nuclear-winter-ice")); });
	// Thaw before the war shots, then hand the ice back to the climate.
	At(114.6f, [this]() { for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It) It->SetWinterOverride(0.0f); });
	At(114.8f, [this]() { for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It) It->SetWinterOverride(-1.0f); });
	// Whisper of War: Beijing, then New Delhi, two powers the script has not touched.
	// Seen from over the Himalaya on the night side, where the missiles glow.
	At(115.0f, [=, this]()
	{
		View(97.0f, 34.0f, 330.0f);
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->SetHudMode(EEarthfallHudMode::Full);
		P->SelectTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar, EarthfallSolar::DirectionFromLatLon(39.9, 116.4), P);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar, EarthfallSolar::DirectionFromLatLon(28.6, 77.2), P);
	});
	At(117.2f, [this]() { Shot(TEXT("38-war-declared")); });
	At(123.5f, [this]() { Shot(TEXT("39-ai-missile-exchange")); });
	At(123.7f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->InspectDirection(EarthfallSolar::DirectionFromLatLon(39.9, 116.4)); });
	At(124.9f, [this]() { Shot(TEXT("40-faction-dossier")); });
	// A salvo of six rods at Canberra, under a defense satellite: the battery and
	// the satellite each stop one, and reload too slowly to stop the rest.
	At(125.0f, [=, this]()
	{
		View(146.0f, -30.0f, 240.0f);
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->ToggleInspectMode();
		P->SetHudMode(EEarthfallHudMode::TickerOnly);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::DefenseSatellite, EarthfallSolar::DirectionFromLatLon(-35.3, 149.1), P);
	});
	At(126.2f, [this]() { if (AEarthOrbitPawn* P = Pawn()) for (int32 Rod = 0; Rod < 6; ++Rod) P->LaunchWeaponAt(EarthfallSolar::DirectionFromLatLon(-35.3 + Rod * 0.7, 149.1 - Rod * 1.1), 0, 1.0f, 1.0f, 0.0f); });
	At(126.60f, [this]() { Shot(TEXT("41-interceptors")); });
	At(128.5f, [this]() { Shot(TEXT("42-salvo-aftermath")); });
	// The debris ring, pinned high for the shot.
	At(128.6f, [=, this]() { View(-25.0f, 18.0f, 520.0f); if (UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>()) Sandbox->AddDebris(1.0f); if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Hidden); });
	At(130.6f, [this]() { Shot(TEXT("43-kessler-debris")); });
	At(131.0f, [this]()
	{
		if (const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>())
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE sandbox intercepts=%d debris=%.2f"), Sandbox->GetInterceptCount(), Sandbox->GetDebrisLevel());
		}
		Finish();
	});

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
