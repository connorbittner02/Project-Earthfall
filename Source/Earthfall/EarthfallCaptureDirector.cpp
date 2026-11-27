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

/** Longitude and latitude the secondary-wildfire shot looks from, chosen at run time. */
static FVector2D GSecondaryFireView(-85.0, 40.0);

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
	// A secondary wildfire: the fire lit furthest from the mantle strike by one of
	// its own ejecta fragments falling back.
	At(110.05f, [this]()
	{
		const FVector Strike = EarthfallSolar::DirectionFromLatLon(45.0, -100.0);
		for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
		{
			const TArray<FVector>& Sites = It->GetSecondaryFireSites();
			FVector Site = Strike;
			for (const FVector& Candidate : Sites)
			{
				// In daylight, so the frame shows the scar as well as the flames, and well clear of the strike's own fire ring.
				const bool bDaylit = FVector::DotProduct(Candidate, It->GetActorTransform().InverseTransformVectorNoScale(It->GetSunDirectionWorld())) > 0.35;
				const double Near = FVector::DotProduct(Candidate, Strike);
				// The strike's own blast and fire ring reach about 2,100 km (0.33 rad), so
				// only a fire beyond 2,900 km (0.45 rad) can have been lit by ejecta alone.
				// And below 50 degrees of latitude, where there is forest or grass to burn
				// (a fragment that lands on tundra or ice lights nothing that lasts).
				const bool bFuel = FMath::Abs(Candidate.Z) < 0.766;
				if (bDaylit && bFuel && Near < 0.90 && (Site.Equals(Strike) || Near > FVector::DotProduct(Site, Strike))) Site = Candidate;
			}
			const float Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(Site.Z), -1.0f, 1.0f)));
			const float Longitude = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-Site.Y), static_cast<float>(Site.X)));
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE secondary fires=%d, framing the daylit one nearest the strike at lat %.1f lon %.1f, %.0f km from it"),
				Sites.Num(), Latitude, Longitude, FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Site, Strike)), -1.0f, 1.0f)) * 6371.0f);
			// From orbit the strike's soot veil hides the ground here, so the camera
			// goes under it: two degrees south of the fire, looking north across it.
			GSecondaryFireView = FVector2D(Longitude, Latitude - 2.0f);
			if (AEarthOrbitPawn* P = Pawn()) P->SetOrbitView(GSecondaryFireView.X, GSecondaryFireView.Y, 103.5f);
		}
	});
	At(112.8f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->SetOrbitView(GSecondaryFireView.X, GSecondaryFireView.Y, 100.9f); });
	At(121.0f, [this]() { Shot(TEXT("34b-secondary-wildfire")); });
	TimeOffset += 11.2f;
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
	// R: a clean world again. Everything after this starts from a reset.
	At(130.8f, [=, this]() { if (AEarthOrbitPawn* P = Pawn()) { P->ResetSimulation(); P->SetHudMode(EEarthfallHudMode::Full); } View(-85.0f, 22.0f, 420.0f); });
	At(132.6f, [this]() { Shot(TEXT("44-clean-reset")); });
	// Living traffic over the North Atlantic at night.
	At(132.7f, [=, this]() { View(70.0f, 30.0f, 300.0f); if (AEarthOrbitPawn* P = Pawn()) P->SetHudMode(EEarthfallHudMode::Hidden); });
	At(134.4f, [this]() { Shot(TEXT("45-living-traffic")); });
	// Plague Rain on the Ganges plain: the cloud, green city lights, and spread.
	At(134.5f, [=, this]()
	{
		View(82.0f, 25.0f, 235.0f);
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->SetHudMode(EEarthfallHudMode::Full);
		P->SelectTool(EarthfallTools::TabNature, EarthfallTools::PlagueRain);
		Sandbox->UseTool(EarthfallTools::TabNature, EarthfallTools::PlagueRain, EarthfallSolar::DirectionFromLatLon(26.5, 81.0), P);
	});
	At(141.0f, [this]() { Shot(TEXT("46-plague-rain")); });
	At(141.1f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->InspectDirection(EarthfallSolar::DirectionFromLatLon(26.5, 81.0)); });
	At(160.0f, [this]() { Shot(TEXT("47-plague-spread")); });
	// Cure Rain over the same ground.
	At(160.1f, [this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->ToggleInspectMode();
		P->SelectTool(EarthfallTools::TabNature, EarthfallTools::CureRain);
		Sandbox->UseTool(EarthfallTools::TabNature, EarthfallTools::CureRain, EarthfallSolar::DirectionFromLatLon(26.5, 81.0), P);
	});
	At(162.0f, [this]() { Shot(TEXT("48-cure-rain")); });
	// Rage in Central Europe and a rebel insurgency in Egypt.
	At(162.1f, [=, this]()
	{
		View(22.0f, 40.0f, 250.0f);
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->SelectTool(EarthfallTools::TabDiplomacy, EarthfallTools::RebelInsurgency);
		Sandbox->UseTool(EarthfallTools::TabNature, EarthfallTools::RageOutbreak, EarthfallSolar::DirectionFromLatLon(50.0, 19.0), P);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::RebelInsurgency, EarthfallSolar::DirectionFromLatLon(30.0, 31.2), P);
	});
	At(170.0f, [this]() { Shot(TEXT("49-rage-and-insurgency")); });
	At(170.5f, [this]()
	{
		if (const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>())
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE bio infected=%.0f cured=%.0f dead=%.0f"), Sandbox->GetInfected(), Sandbox->GetCured(), Sandbox->GetPlagueDead());
		}
	});
	At(130.7f, [this]()
	{
		if (const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>())
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE sandbox intercepts=%d debris=%.2f"), Sandbox->GetInterceptCount(), Sandbox->GetDebrisLevel());
		}
	});
	// --- Naval fleet and Force Peace, from a clean world. France and Brazil go to
	// war (France's allies follow by treaty); a fleet placed in the South Atlantic
	// sails round to the coast nearest Brasilia and shells it.
	At(171.0f, [=, this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->ResetSimulation();
		P->SetHudMode(EEarthfallHudMode::Full);
		View(-36.0f, -14.0f, 190.0f);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar, EarthfallSolar::DirectionFromLatLon(48.85, 2.35), P);
		Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::WhisperOfWar, EarthfallSolar::DirectionFromLatLon(-15.8, -47.9), P);
		P->SelectTool(EarthfallTools::TabDiplomacy, EarthfallTools::StrikeGroup);
		const FString Order = Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::StrikeGroup, EarthfallSolar::DirectionFromLatLon(-21.0, -30.0), P);
		UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE strike group order: %s (fleets at sea: %d)"), *Order, Sandbox->GetFleetCount());
	});
	At(174.0f, [this]() { Shot(TEXT("50-naval-fleet-under-way")); });
	At(179.2f, [this]() { if (AEarthOrbitPawn* P = Pawn()) P->InspectDirection(EarthfallSolar::DirectionFromLatLon(-15.8, -47.9)); });
	At(181.0f, [this]() { Shot(TEXT("51-naval-fleet-bombards-coast")); });
	At(183.0f, [this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		const int32 Before = Sandbox->GetActiveWarCount();
		P->SelectTool(EarthfallTools::TabDiplomacy, EarthfallTools::ForcePeace);
		const FString Result = Sandbox->UseTool(EarthfallTools::TabDiplomacy, EarthfallTools::ForcePeace, EarthfallSolar::DirectionFromLatLon(-15.8, -47.9), P);
		UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE force peace: wars %d -> %d  //  %s"), Before, Sandbox->GetActiveWarCount(), *Result);
	});
	At(185.0f, [this]() { Shot(TEXT("52-force-peace")); });
	At(190.0f, [this]()
	{
		int32 Aloft = 0;
		for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It) Aloft += It->IsInFlight() ? 1 : 0;
		if (const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>())
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE five seconds after peace: wars=%d missiles_in_flight=%d fleets=%d border_crises=%d"),
				Sandbox->GetActiveWarCount(), Aloft, Sandbox->GetFleetCount(), Sandbox->GetBorderCrisisCount());
		}
	});

	// --- Plague and rage seen from 40 km over streamed terrain in central Mexico.
	At(190.2f, [=, this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->ResetSimulation();
		P->ToggleInspectMode();
		P->SetHudMode(EEarthfallHudMode::Hidden);
		View(-100.0f, 16.6f, 103.5f);
		Sandbox->UseTool(EarthfallTools::TabNature, EarthfallTools::PlagueRain, EarthfallSolar::DirectionFromLatLon(19.4, -99.1), P);
	});
	At(195.0f, [=, this]()
	{
		View(-100.0f, 16.6f, 100.8f);
		if (UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>())
		{
			Sandbox->UseTool(EarthfallTools::TabNature, EarthfallTools::RageOutbreak, EarthfallSolar::DirectionFromLatLon(20.9, -101.4), Pawn());
		}
	});
	At(206.0f, [this]() { Shot(TEXT("53-terrain-plague-rage")); });
	At(206.2f, [=]() { View(-100.0f, 18.6f, 100.25f); });
	At(214.0f, [this]() { Shot(TEXT("54-terrain-rage-low")); });
	// --- Munitions, from a clean world. A MIRV thermonuclear bus over the Great
	// Plains: the split in flight, then six ground bursts.
	At(214.6f, [=, this]()
	{
		AEarthOrbitPawn* P = Pawn();
		if (!P) return;
		P->ResetSimulation();
		P->SetHudMode(EEarthfallHudMode::Full);
		View(-100.0f, 30.0f, 300.0f);
		Weapon(EarthfallWeapons::Nuclear, EarthfallMultipliers::DefaultIndex);
		P->SelectMunition(EarthfallMunitions::Cluster);
	});
	At(215.6f, [=]() { Fire(40.0, -99.0); });
	At(217.05f, [this]() { Shot(TEXT("55-mirv-split")); });
	At(218.3f, [this]() { Shot(TEXT("56-mirv-impacts")); });
	// An EMP grid-buster over northern India at night: lights out, nothing burned.
	At(218.5f, [=, this]() { View(79.0f, 22.0f, 300.0f); Weapon(0, EarthfallMultipliers::DefaultIndex); if (AEarthOrbitPawn* P = Pawn()) P->SelectMunition(EarthfallMunitions::Emp); });
	At(220.0f, [this]() { Shot(TEXT("57-before-emp")); });
	At(220.1f, [=]() { Fire(25.5, 80.0); });
	At(223.6f, [this]() { Shot(TEXT("58-emp-blackout")); });
	// A growing city: Japan before and after sixteen seconds of boosted growth.
	At(223.8f, [=, this]() { View(138.0f, 36.0f, 205.0f); if (AEarthOrbitPawn* P = Pawn()) { P->SelectMunition(EarthfallMunitions::Standard); P->InspectDirection(EarthfallSolar::DirectionFromLatLon(35.7, 139.7)); } });
	At(225.6f, [this]() { Shot(TEXT("59-city-before-growth")); });
	At(225.7f, [this]()
	{
		if (UEarthfallPopulationSubsystem* People = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>())
		{
			People->BoostCountryGrowth(People->GetCountryAtDirection(EarthfallSolar::DirectionFromLatLon(35.7, 139.7)), 28.0f, 16.0f);
		}
	});
	At(243.5f, [this]()
	{
		Shot(TEXT("60-city-sprawl-after-growth"));
		FString Name;
		int32 Tier = 0;
		float Growth = 1.0f;
		const UEarthfallPopulationSubsystem* People = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
		if (People && People->GetLargestCity(People->GetCountryAtDirection(EarthfallSolar::DirectionFromLatLon(35.7, 139.7)), Name, Tier, Growth))
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE city %s tier=%s growth=%.2fx"), *Name, UEarthfallPopulationSubsystem::CityTierLabel(Tier), Growth);
		}
	});
	// --- Civilization, from a clean world. Brazil gets resources and three tech
	// uplifts (Conventional to Fusion Shielded), then is commanded to attack Argentina.
	At(244.4f, [=, this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->ResetSimulation();
		P->SetHudMode(EEarthfallHudMode::Full);
		View(-50.0f, -22.0f, 215.0f);
		const FVector Brasilia = EarthfallSolar::DirectionFromLatLon(-15.8, -47.9);
		Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::DropResources, Brasilia, P);
		for (int32 Tier = 0; Tier < 3; ++Tier) Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::TechUplift, Brasilia, P);
		P->SelectTool(EarthfallTools::TabCiv, EarthfallTools::Command);
		Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::Command, Brasilia, P);
	});
	At(247.4f, [this]() { Shot(TEXT("61-tech-tier4-shield")); });
	At(247.5f, [this]()
	{
		AEarthOrbitPawn* P = Pawn();
		UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (!P || !Sandbox) return;
		P->SelectTool(EarthfallTools::TabCiv, EarthfallTools::OrderAttack);
		Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::OrderAttack, EarthfallSolar::DirectionFromLatLon(-34.6, -58.4), P);
	});
	At(250.6f, [this]() { Shot(TEXT("62-order-attack-army-and-fleet")); });
	At(254.5f, [this]() { Shot(TEXT("63-order-attack-munitions")); });
	At(254.6f, [this]()
	{
		const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		if (Sandbox) UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE during attack: units=%d armies=%d fleets=%d wars=%d"),
			Sandbox->GetUnitCount(), Sandbox->GetArmyCount(), Sandbox->GetFleetCount(), Sandbox->GetActiveWarCount());
	});
	// Mobilize, then Evacuate: the same nation fortifies, then stands down and rebuilds.
	At(254.8f, [this]() { if (UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>()) { if (AEarthOrbitPawn* P = Pawn()) P->SelectTool(EarthfallTools::TabCiv, EarthfallTools::OrderMobilize); Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::OrderMobilize, EarthfallSolar::DirectionFromLatLon(-15.8, -47.9), Pawn()); } });
	At(256.6f, [this]() { Shot(TEXT("64-order-mobilize")); });
	At(256.8f, [this]() { if (UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>()) { if (AEarthOrbitPawn* P = Pawn()) P->SelectTool(EarthfallTools::TabCiv, EarthfallTools::OrderEvacuate); Sandbox->UseTool(EarthfallTools::TabCiv, EarthfallTools::OrderEvacuate, EarthfallSolar::DirectionFromLatLon(-15.8, -47.9), Pawn()); } });
	At(258.6f, [this]() { Shot(TEXT("65-order-evacuate")); });
	// R: everything back to the start. The dossier should read Conventional, no orders, baseline city.
	At(258.8f, [this]()
	{
		AEarthOrbitPawn* P = Pawn();
		if (!P) return;
		P->ResetSimulation();
		P->InspectDirection(EarthfallSolar::DirectionFromLatLon(-15.8, -47.9));
	});
	At(261.4f, [this]()
	{
		Shot(TEXT("66-clean-reset-civ"));
		const UEarthfallSandboxSubsystem* Sandbox = GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>();
		const UEarthfallPopulationSubsystem* People = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
		const AEarthOrbitPawn* P = Pawn();
		FEarthfallFactionInfo Info;
		if (Sandbox && People && P && Sandbox->GetFactionInfo(People->GetCountryAtDirection(EarthfallSolar::DirectionFromLatLon(-15.8, -47.9)), Info))
		{
			UE_LOG(LogEarthfallCapture, Display, TEXT("EARTHFALL_CAPTURE after reset: brazil tier=%s orders=%s units=%d wars=%d shell=%s population=%.3fB"),
				UEarthfallSandboxSubsystem::TechTierLabel(Info.TechTier), *Info.Orders, Sandbox->GetUnitCount(), Sandbox->GetActiveWarCount(),
				EarthfallMunitions::Name(P->GetMunition()), People->GetTelemetry().LivingPopulation / 1.0e9);
		}
	});
	At(262.0f, [this]() { Finish(); });

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
