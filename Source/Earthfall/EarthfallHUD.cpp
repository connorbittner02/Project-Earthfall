#include "EarthfallHUD.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallSandboxSubsystem.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
const FLinearColor Cyan(0.30f, 0.82f, 1.0f, 1.0f);
const FLinearColor White(0.90f, 0.94f, 1.0f, 1.0f);
const FLinearColor Dim(0.52f, 0.61f, 0.72f, 1.0f);
const FLinearColor Orange(1.0f, 0.46f, 0.14f, 1.0f);
const FLinearColor Red(1.0f, 0.26f, 0.18f, 1.0f);
const FLinearColor Green(0.36f, 0.94f, 0.56f, 1.0f);
const FLinearColor PanelFill(0.010f, 0.020f, 0.038f, 0.74f);
const FLinearColor PanelEdge(0.30f, 0.82f, 1.0f, 0.55f);
const FLinearColor TrackFill(0.10f, 0.14f, 0.20f, 0.90f);
constexpr float Margin = 36.0f;
constexpr float TickerHeight = 44.0f;
constexpr double EarthRadiusKm = 6371.0;

FSlateFontInfo MakeFont(bool bBold, bool bMono, float Pixels)
{
	// Slate sizes are points at 96 DPI, so convert from the pixel height we lay out in.
	const float Points = FMath::Max(Pixels * 0.75f, 5.0f);
	return FCoreStyle::GetDefaultFontStyle(bMono ? TEXT("Mono") : (bBold ? TEXT("Bold") : TEXT("Regular")), Points);
}

FString FormatPeople(double Count)
{
	if (Count >= 1.e9) return FString::Printf(TEXT("%.3f B"), Count / 1.e9);
	if (Count >= 1.e6) return FString::Printf(TEXT("%.2f M"), Count / 1.e6);
	if (Count >= 1.e3) return FString::Printf(TEXT("%.1f K"), Count / 1.e3);
	return FString::Printf(TEXT("%.0f"), Count);
}

FString FormatMultiplier(float Value)
{
	return FString::Printf(TEXT("x%g"), Value);
}
}

// ---------------------------------------------------------------------------
// Primitives. All coordinates are viewport pixels; sizes come through Px().
// ---------------------------------------------------------------------------
float AEarthfallHUD::MeasureText(const FString& String, float Points, EFace Face) const
{
	if (!FSlateApplication::IsInitialized()) return 0.0f;
	const FSlateFontInfo Font = MakeFont(Face == EFace::Bold, Face == EFace::Mono, Px(Points));
	return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font).X;
}

float AEarthfallHUD::Text(const FString& String, float X, float Y, float Points, const FLinearColor& Color, EFace Face, EAlign Align)
{
	if (!Canvas || String.IsEmpty()) return 0.0f;
	const float Width = MeasureText(String, Points, Face);
	const float Left = Align == EAlign::Left ? X : (Align == EAlign::Center ? X - Width * 0.5f : X - Width);
	FCanvasTextItem Item(FVector2D(Left, Y), FText::FromString(String), MakeFont(Face == EFace::Bold, Face == EFace::Mono, Px(Points)), Color);
	// FCanvasTextItem::HasValidText() tests the legacy UFont pointer even when a
	// Slate font is supplied, so it must be non-null or nothing is drawn.
	Item.Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), FVector2D(Px(1.0f), Px(1.0f)));
	Canvas->DrawItem(Item);
	return Width;
}

void AEarthfallHUD::Fill(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
	if (!Canvas || Width <= 0.0f || Height <= 0.0f) return;
	FCanvasTileItem Item(FVector2D(X, Y), FVector2D(Width, Height), Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AEarthfallHUD::Panel(float X, float Y, float Width, float Height, const TCHAR* Title)
{
	Fill(X, Y, Width, Height, PanelFill);
	Fill(X, Y, Width, Px(2.0f), PanelEdge);
	Fill(X, Y, Px(2.0f), Height, PanelEdge);
	Text(Title, X + Px(14.0f), Y + Px(9.0f), 13.0f, Cyan, EFace::Bold);
	Panels.Add(FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height)));
}

void AEarthfallHUD::Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color)
{
	Fill(X, Y, Width, Height, TrackFill);
	Fill(X, Y, Width * FMath::Clamp(Fraction, 0.0f, 1.0f), Height, Color);
}

bool AEarthfallHUD::Button(float X, float Y, float Width, float Height, EButton Kind, int32 Value)
{
	FButton& Entry = Buttons.AddDefaulted_GetRef();
	Entry.Bounds = FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
	Entry.Kind = Kind;
	Entry.Value = Value;
	return Entry.Bounds.IsInside(Cursor);
}

bool AEarthfallHUD::HandleClick(const FVector2D& ScreenPosition, AEarthOrbitPawn* Pawn)
{
	if (!Pawn) return false;
	for (const FButton& Entry : Buttons)
	{
		if (!Entry.Bounds.IsInside(ScreenPosition)) continue;
		switch (Entry.Kind)
		{
		case EButton::SelectWeapon:
			Pawn->SelectWeapon(Entry.Value);
			break;
		case EButton::MultiplierDown:
			Pawn->AdjustMultiplier(static_cast<EEarthfallMultiplier>(Entry.Value), -1);
			break;
		case EButton::MultiplierUp:
			Pawn->AdjustMultiplier(static_cast<EEarthfallMultiplier>(Entry.Value), 1);
			break;
		case EButton::CycleHud:
			Pawn->CycleHudMode();
			break;
		case EButton::ToggleInspect:
			Pawn->ToggleInspectMode();
			break;
		case EButton::SelectTab:
			Pawn->SelectTab(Entry.Value);
			break;
		case EButton::SelectTool:
			Pawn->SelectTool(Entry.Value / 8, Entry.Value % 8);
			break;
		}
		return true;
	}
	// A click on panel chrome is swallowed so it cannot fire through the HUD.
	for (const FBox2D& Bounds : Panels)
	{
		if (Bounds.IsInside(ScreenPosition)) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void AEarthfallHUD::DrawHUD()
{
	Super::DrawHUD();
	Buttons.Reset();
	Panels.Reset();
	if (!Canvas) return;
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	UIScale = FMath::Max(H / 1080.0f, 0.5f);
	float CursorX = W * 0.5f;
	float CursorY = H * 0.5f;
	bCursorValid = PlayerOwner && PlayerOwner->GetMousePosition(CursorX, CursorY);
	Cursor = FVector2D(CursorX, CursorY);

	const AEarthOrbitPawn* Pawn = Cast<AEarthOrbitPawn>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
	if (!Pawn) return;

	// The eye button survives every mode, so the HUD can always be brought back.
	DrawEyeButton(Pawn);
	const EEarthfallHudMode Mode = Pawn->GetHudMode();
	if (Mode == EEarthfallHudMode::Hidden) return;
	if (Mode == EEarthfallHudMode::TickerOnly)
	{
		FString CinemaTicker = FEarthfallTelemetry().LatestDispatch;
		if (const UWorld* World = GetWorld())
		{
			if (const UEarthfallPopulationSubsystem* Population = World->GetSubsystem<UEarthfallPopulationSubsystem>()) CinemaTicker = Population->GetDispatchTicker();
		}
		DrawDispatchTicker(CinemaTicker, W, H);
		return;
	}

	Text(TEXT("E A R T H F A L L"), Px(Margin + 34.0f), Px(24.0f), 30.0f, Cyan, EFace::Bold);
	Text(TEXT("ORBITAL STRIKE SIMULATOR"), Px(Margin + 36.0f), Px(62.0f), 12.0f, Dim);
	DrawInspectButton(Pawn);

	FEarthfallTelemetry Telemetry;
	FString Ticker = Telemetry.LatestDispatch;
	if (const UWorld* World = GetWorld())
	{
		if (const UEarthfallPopulationSubsystem* Population = World->GetSubsystem<UEarthfallPopulationSubsystem>())
		{
			Telemetry = Population->GetTelemetry();
			Ticker = Population->GetDispatchTicker();
		}
	}

	DrawStatusPanel(Telemetry, Pawn);
	DrawCountryDossier(Pawn);
	DrawArsenalPanel(Pawn, W);
	DrawReticle(Pawn, W, H);

	Text(TEXT("RMB DRAG  ORBIT     WHEEL  ZOOM     LMB  USE     TAB  TOOLS     1-6  SELECT     Q/E  YIELD     A/D  SIZE     Z/C  FALLOUT     M  INSPECT     H  HUD     R  RESET"),
		W * 0.5f, H - Px(TickerHeight + 28.0f), 11.5f, Dim, EFace::Regular, EAlign::Center);
	DrawDispatchTicker(Ticker, W, H);

	if (Pawn->GetMessageTimer() > 0.0f)
	{
		Text(Pawn->GetStatusMessage(), W * 0.5f, H * 0.80f, 18.0f, Orange, EFace::Bold, EAlign::Center);
	}
}

void AEarthfallHUD::DrawStatusPanel(const FEarthfallTelemetry& Telemetry, const AEarthOrbitPawn* Pawn)
{
	const float X = Px(Margin);
	const float Y = Px(98.0f);
	const float Width = Px(400.0f);
	const float RowHeight = Px(27.0f);
	const int32 RowCount = 11;
	Panel(X, Y, Width, Px(40.0f) + RowHeight * RowCount + Px(10.0f), TEXT("PLANETARY STATUS"));

	float RowY = Y + Px(38.0f);
	const float LabelX = X + Px(14.0f);
	const float ValueX = X + Width - Px(14.0f);
	auto Row = [&](const TCHAR* Label, const FString& Value, const FLinearColor& Color, float BarFraction = -1.0f)
	{
		Text(Label, LabelX, RowY, 13.0f, Dim);
		if (BarFraction >= 0.0f)
		{
			Bar(X + Px(190.0f), RowY + Px(7.0f), Px(110.0f), Px(6.0f), BarFraction, Color);
		}
		Text(Value, ValueX, RowY - Px(1.0f), 14.0f, Color, EFace::Mono, EAlign::Right);
		RowY += RowHeight;
	};

	const bool bAnyDead = Telemetry.TotalCasualties >= 1.0;
	Row(TEXT("POPULATION"), FormatPeople(Telemetry.LivingPopulation), White);
	Row(TEXT("CASUALTIES"), FormatPeople(Telemetry.TotalCasualties), bAnyDead ? Red : White);
	Row(TEXT("DISPLACED"), FormatPeople(Telemetry.DisplacedPopulation), Telemetry.DisplacedPopulation >= 1.0 ? Orange : White);
	Row(TEXT("FALLOUT DEATHS"), FormatPeople(Telemetry.FalloutCasualties), Telemetry.FalloutCasualties >= 1.0 ? Green : White);

	const float Blackout = Telemetry.GridBlackoutPercent / 100.0f;
	Row(TEXT("GRID BLACKOUT"), FString::Printf(TEXT("%.1f %%"), Telemetry.GridBlackoutPercent), Blackout > 0.001f ? Orange : White, Blackout);
	const float Atmosphere = Telemetry.AtmosphereHealth / 100.0f;
	Row(TEXT("ATMOSPHERE"), FString::Printf(TEXT("%.0f %%"), Telemetry.AtmosphereHealth),
		FMath::Lerp(Red, Cyan, FMath::Clamp(Atmosphere * 1.5f - 0.5f, 0.0f, 1.0f)), Atmosphere);
	const float Sunlight = Telemetry.SunlightPercent / 100.0f;
	Row(TEXT("SUNLIGHT"), FString::Printf(TEXT("%.0f %%"), Telemetry.SunlightPercent),
		FMath::Lerp(Orange, White, FMath::Clamp(Sunlight * 2.0f - 1.0f, 0.0f, 1.0f)), Sunlight);

	const float Anomaly = Telemetry.MeanTemperatureC - 15.0f;
	const FLinearColor ClimateColor = Anomaly < -0.25f ? Cyan : (Anomaly > 0.25f ? Orange : White);
	Row(TEXT("MEAN TEMP"), FString::Printf(TEXT("%.1f °C  (%+.1f)"), Telemetry.MeanTemperatureC, Anomaly), ClimateColor);
	Row(TEXT("AIRBORNE SOOT"), FString::Printf(TEXT("%.1f Tg"), Telemetry.SootTeragrams), Telemetry.SootTeragrams > 0.05f ? Orange : White);
	Row(TEXT("RADIATION"), FString::Printf(TEXT("%.0f / 100"), Telemetry.FalloutIndex),
		Telemetry.FalloutIndex > 0.5f ? Green : White, Telemetry.FalloutIndex / 100.0f);
	Row(TEXT("IMPACTS"), FString::Printf(TEXT("%04d   IN FLIGHT %d"), Pawn->GetImpactCount(),
		FMath::Max(Pawn->GetLaunchCount() - Pawn->GetImpactCount(), 0)), White);
}

void AEarthfallHUD::DrawArsenalPanel(const AEarthOrbitPawn* Pawn, float ScreenWidth)
{
	// The command dock: three tabs across the top, six tool rows, one hint line.
	const float Width = Px(380.0f);
	const float X = ScreenWidth - Px(Margin) - Width;
	const float Y = Px(24.0f);
	const float TabHeight = Px(34.0f);
	const float RowHeight = Px(44.0f);
	const float HintHeight = Px(36.0f);
	constexpr int32 Rows = 6;
	const float Height = TabHeight + Px(8.0f) + RowHeight * Rows + HintHeight;
	const int32 ActiveTab = Pawn->GetToolTab();

	Fill(X, Y, Width, Height, FLinearColor(0.006f, 0.012f, 0.024f, 0.90f));
	Fill(X, Y, Px(2.0f), Height, PanelEdge);
	Panels.Add(FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height)));

	const float TabWidth = Width / EarthfallTools::TabCount;
	for (int32 Tab = 0; Tab < EarthfallTools::TabCount; ++Tab)
	{
		const float TabX = X + TabWidth * Tab;
		const bool bActive = Tab == ActiveTab;
		const bool bHovered = Button(TabX, Y, TabWidth, TabHeight, EButton::SelectTab, Tab);
		Fill(TabX, Y, TabWidth, TabHeight, bActive ? FLinearColor(0.10f, 0.26f, 0.36f, 0.95f)
			: (bHovered ? FLinearColor(0.06f, 0.12f, 0.18f, 0.95f) : FLinearColor(0.02f, 0.04f, 0.07f, 0.95f)));
		Fill(TabX, Y, TabWidth, Px(bActive ? 3.0f : 1.0f), bActive ? Cyan : PanelEdge);
		if (Tab > 0) Fill(TabX, Y + Px(6.0f), Px(1.0f), TabHeight - Px(12.0f), PanelEdge);
		Text(EarthfallTools::TabShortName(Tab), TabX + TabWidth * 0.5f, Y + Px(9.0f), 12.5f, bActive ? White : Dim, EFace::Bold, EAlign::Center);
	}

	const int32 ActiveSlot = Pawn->GetToolSlot(ActiveTab);
	int32 HintSlot = ActiveSlot;
	const float RowsTop = Y + TabHeight + Px(8.0f);
	for (int32 Slot = 0; Slot < EarthfallTools::Count(ActiveTab); ++Slot)
	{
		const FEarthfallToolSpec Spec = EarthfallTools::Get(ActiveTab, Slot);
		const float RowY = RowsTop + RowHeight * Slot;
		const bool bSelected = ActiveSlot == Slot;
		const bool bHovered = Button(X + Px(6.0f), RowY, Width - Px(12.0f), RowHeight - Px(4.0f), EButton::SelectTool, ActiveTab * 8 + Slot);
		if (bHovered) HintSlot = Slot;
		if (bSelected || bHovered)
		{
			Fill(X + Px(6.0f), RowY, Width - Px(12.0f), RowHeight - Px(4.0f),
				FLinearColor(Spec.Color.R, Spec.Color.G, Spec.Color.B, bSelected ? 0.24f : 0.10f));
		}
		if (bSelected) Fill(X + Px(6.0f), RowY, Px(4.0f), RowHeight - Px(4.0f), Spec.Color);

		Text(FString::Printf(TEXT("[%d]"), Slot + 1), X + Px(18.0f), RowY + Px(4.0f), 14.0f, bSelected ? Spec.Color : Dim, EFace::Mono);
		Text(Spec.Name, X + Px(62.0f), RowY + Px(3.0f), 15.0f, bSelected ? White : Dim, EFace::Bold);
		Text(Spec.Descriptor, X + Px(62.0f), RowY + Px(22.0f), 10.5f, bSelected ? Spec.Color : Dim);
		if (bSelected)
		{
			// Armed marker, and the recharge bar while the tool cools down.
			const bool bReady = Pawn->GetCooldownRatio() <= 0.0f;
			Text(bReady ? TEXT("ARMED") : TEXT("CHARGING"), X + Width - Px(16.0f), RowY + Px(5.0f), 10.5f, bReady ? Spec.Color : Dim, EFace::Mono, EAlign::Right);
			if (!bReady) Bar(X + Px(6.0f), RowY + RowHeight - Px(7.0f), Width - Px(12.0f), Px(3.0f), 1.0f - Pawn->GetCooldownRatio(), Spec.Color);
		}
	}

	const float HintY = RowsTop + RowHeight * Rows;
	Fill(X + Px(6.0f), HintY, Width - Px(12.0f), Px(1.0f), PanelEdge);
	Text(EarthfallTools::TabName(ActiveTab), X + Px(14.0f), HintY + Px(5.0f), 9.5f, Cyan, EFace::Bold);
	Text(EarthfallTools::Get(ActiveTab, HintSlot).Hint, X + Px(14.0f), HintY + Px(19.0f), 9.5f, Dim);

	DrawMultiplierPanel(Pawn, ScreenWidth, Y + Height + Px(12.0f));
}

void AEarthfallHUD::DrawMultiplierPanel(const AEarthOrbitPawn* Pawn, float ScreenWidth, float Top)
{
	const float Width = Px(380.0f);
	const float X = ScreenWidth - Px(Margin) - Width;
	const float RowHeight = Px(38.0f);
	const int32 Kinds = static_cast<int32>(EEarthfallMultiplier::Count);
	Panel(X, Top, Width, Px(40.0f) + RowHeight * Kinds + Px(54.0f), TEXT("WEAPON MULTIPLIERS"));

	static const TCHAR* DownKeys[] = {TEXT("Q"), TEXT("A"), TEXT("Z")};
	static const TCHAR* UpKeys[] = {TEXT("E"), TEXT("D"), TEXT("C")};
	const float ButtonSize = Px(28.0f);
	for (int32 Index = 0; Index < Kinds; ++Index)
	{
		const EEarthfallMultiplier Kind = static_cast<EEarthfallMultiplier>(Index);
		const float RowY = Top + Px(38.0f) + RowHeight * Index;
		const float Value = Pawn->GetMultiplier(Kind);
		const bool bBoosted = !FMath::IsNearlyEqual(Value, 1.0f);
		Text(EarthfallMultipliers::Label(Kind), X + Px(14.0f), RowY + Px(5.0f), 13.0f, Dim);

		const float DownX = X + Px(176.0f);
		const float UpX = X + Width - Px(14.0f) - ButtonSize;
		const bool bDownHover = Button(DownX, RowY, ButtonSize, ButtonSize, EButton::MultiplierDown, Index);
		const bool bUpHover = Button(UpX, RowY, ButtonSize, ButtonSize, EButton::MultiplierUp, Index);
		Fill(DownX, RowY, ButtonSize, ButtonSize, bDownHover ? FLinearColor(0.30f, 0.82f, 1.0f, 0.45f) : TrackFill);
		Fill(UpX, RowY, ButtonSize, ButtonSize, bUpHover ? FLinearColor(0.30f, 0.82f, 1.0f, 0.45f) : TrackFill);
		Text(TEXT("-"), DownX + ButtonSize * 0.5f, RowY + Px(2.0f), 17.0f, White, EFace::Bold, EAlign::Center);
		Text(TEXT("+"), UpX + ButtonSize * 0.5f, RowY + Px(2.0f), 17.0f, White, EFace::Bold, EAlign::Center);
		Text(DownKeys[Index], DownX - Px(8.0f), RowY + Px(7.0f), 10.5f, Dim, EFace::Mono, EAlign::Right);
		Text(UpKeys[Index], UpX - Px(8.0f), RowY + Px(7.0f), 10.5f, Dim, EFace::Mono, EAlign::Right);
		Text(FormatMultiplier(Value), (DownX + ButtonSize + UpX - Px(22.0f)) * 0.5f, RowY + Px(2.0f), 17.0f,
			bBoosted ? Orange : White, EFace::Mono, EAlign::Center);
	}

	// What the dials add up to for the selected weapon.
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(Pawn->GetWeaponIndex());
	const float SummaryY = Top + Px(38.0f) + RowHeight * Kinds + Px(6.0f);
	const double RadiusKm = Pawn->GetCurrentBlastRadius() / 100.0 * EarthRadiusKm;
	Text(TEXT("BLAST ZONE"), X + Px(14.0f), SummaryY, 11.0f, Dim);
	Text(FString::Printf(TEXT("%.0f km"), RadiusKm), X + Width - Px(14.0f), SummaryY - Px(1.0f), 13.0f, Spec.Color, EFace::Mono, EAlign::Right);
	// The multipliers in real units: blast radius scales the body, yield its
	// kinetic energy, so entry speed follows the square root of yield.
	const FEarthfallImpactor Impactor = EarthfallPhysics::MakeImpactor(Pawn->GetWeaponIndex(),
		Pawn->GetMultiplier(EEarthfallMultiplier::Yield), Pawn->GetMultiplier(EEarthfallMultiplier::BlastRadius));
	const double Megatons = Impactor.Megatons();
	const FString EnergyText = Megatons >= 1.0e6 ? FString::Printf(TEXT("%.2f Tt"), Megatons / 1.0e6)
		: Megatons >= 1000.0 ? FString::Printf(TEXT("%.1f Gt"), Megatons / 1000.0) : FString::Printf(TEXT("%.1f Mt"), Megatons);
	FString BodyText = EnergyText;
	if (Impactor.bKinetic)
	{
		const FString SizeText = Impactor.DiameterMeters >= 1000.0
			? FString::Printf(TEXT("%.2f km"), Impactor.DiameterMeters / 1000.0) : FString::Printf(TEXT("%.0f m"), Impactor.DiameterMeters);
		BodyText = FString::Printf(TEXT("%s  %.1f km/s  %s"), *SizeText, Impactor.SpeedKmS, *EnergyText);
	}
	Text(Impactor.bKinetic ? TEXT("IMPACTOR") : TEXT("YIELD"), X + Px(14.0f), SummaryY + Px(20.0f), 11.0f, Dim);
	Text(BodyText, X + Width - Px(14.0f), SummaryY + Px(19.0f), 13.0f, Spec.Color, EFace::Mono, EAlign::Right);
}

void AEarthfallHUD::DrawReticle(const AEarthOrbitPawn* Pawn, float ScreenWidth, float ScreenHeight)
{
	if (!bCursorValid || !Pawn->IsAimEnabled()) return;
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(Pawn->GetWeaponIndex());
	const bool bLocked = Pawn->IsAimOnPlanet();
	const FLinearColor AimColor = bLocked ? Spec.Color : Dim;
	const float Gap = Px(4.0f);
	const float Arm = Px(13.0f);
	const float Thickness = FMath::Max(Px(2.0f), 1.0f);
	DrawLine(Cursor.X - Arm, Cursor.Y, Cursor.X - Gap, Cursor.Y, AimColor, Thickness);
	DrawLine(Cursor.X + Gap, Cursor.Y, Cursor.X + Arm, Cursor.Y, AimColor, Thickness);
	DrawLine(Cursor.X, Cursor.Y - Arm, Cursor.X, Cursor.Y - Gap, AimColor, Thickness);
	DrawLine(Cursor.X, Cursor.Y + Gap, Cursor.X, Cursor.Y + Arm, AimColor, Thickness);

	for (const FBox2D& Bounds : Panels)
	{
		if (Bounds.IsInside(Cursor)) return;
	}
	if (!bLocked)
	{
		Text(TEXT("NO SURFACE LOCK"), Cursor.X + Px(20.0f), Cursor.Y + Px(14.0f), 11.5f, Dim);
		return;
	}
	if (Pawn->IsInspectMode())
	{
		Text(TEXT("INSPECT  //  CLICK A COUNTRY"), Cursor.X + Px(20.0f), Cursor.Y + Px(14.0f), 11.5f, Cyan, EFace::Mono);
		return;
	}
	const FVector Direction = Pawn->GetAimHit().ImpactPoint.GetSafeNormal();
	const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0)));
	const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(-Direction.Y, Direction.X));
	Text(FString::Printf(TEXT("TARGET LOCK  //  %.1f°%s  %.1f°%s"),
		FMath::Abs(Latitude), Latitude >= 0.0 ? TEXT("N") : TEXT("S"),
		FMath::Abs(Longitude), Longitude >= 0.0 ? TEXT("E") : TEXT("W")),
		Cursor.X + Px(20.0f), Cursor.Y + Px(14.0f), 11.5f, Spec.Color, EFace::Mono);
	Text(FString::Printf(TEXT("%s  //  %.0f km"), Spec.Name, Pawn->GetCurrentBlastRadius() / 100.0 * EarthRadiusKm),
		Cursor.X + Px(20.0f), Cursor.Y + Px(32.0f), 11.5f, White, EFace::Mono);
}

void AEarthfallHUD::DrawDispatchTicker(const FString& String, float ScreenWidth, float ScreenHeight)
{
	const float BandHeight = Px(TickerHeight);
	const float BandTop = ScreenHeight - BandHeight;
	Fill(0.0f, BandTop, ScreenWidth, BandHeight, FLinearColor(0.018f, 0.035f, 0.065f, 0.94f));
	Fill(0.0f, BandTop, ScreenWidth, Px(2.0f), FLinearColor(1.0f, 0.23f, 0.08f, 1.0f));
	Panels.Add(FBox2D(FVector2D(0.0f, BandTop), FVector2D(ScreenWidth, ScreenHeight)));

	const FLinearColor TickerColor(1.0f, 0.78f, 0.64f, 1.0f);
	const float TextY = BandTop + Px(11.0f);
	const float Points = 15.0f;
	const float TextWidth = MeasureText(String, Points, EFace::Bold);
	const float Edge = Px(30.0f);

	// A dispatch that already fits stays pinned and readable.
	if (TextWidth <= ScreenWidth - Edge * 2.0f)
	{
		Text(String, Edge, TextY, Points, TickerColor, EFace::Bold);
		return;
	}
	// Otherwise march it right-to-left so the whole queue can be read. The text
	// enters at the right edge and wraps once it has fully cleared the left one.
	const UWorld* World = GetWorld();
	const float Time = World ? World->GetTimeSeconds() : 0.0f;
	const float Offset = FMath::Fmod(Time * Px(110.0f), TextWidth + ScreenWidth);
	Text(String, ScreenWidth - Offset, TextY, Points, TickerColor, EFace::Bold);
}

void AEarthfallHUD::DrawEyeButton(const AEarthOrbitPawn* Pawn)
{
	const float Size = Px(28.0f);
	const float X = Px(Margin);
	const float Y = Px(28.0f);
	const bool bHudHidden = Pawn->GetHudMode() == EEarthfallHudMode::Hidden;
	const bool bHovered = Button(X, Y, Size, Size, EButton::CycleHud, 0);
	Fill(X, Y, Size, Size, bHovered ? FLinearColor(0.30f, 0.82f, 1.0f, 0.40f) : FLinearColor(0.010f, 0.020f, 0.038f, bHudHidden ? 0.35f : 0.74f));

	// An eye drawn as two arcs and a pupil; it dims when the HUD is hidden.
	const FLinearColor Ink = bHudHidden ? FLinearColor(0.52f, 0.61f, 0.72f, 0.65f) : Cyan;
	const FVector2D Center(X + Size * 0.5f, Y + Size * 0.5f);
	const float HalfWidth = Size * 0.36f;
	const float HalfHeight = Size * 0.20f;
	constexpr int32 Segments = 10;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float T0 = static_cast<float>(Index) / Segments;
		const float T1 = static_cast<float>(Index + 1) / Segments;
		const float X0 = Center.X - HalfWidth + 2.0f * HalfWidth * T0;
		const float X1 = Center.X - HalfWidth + 2.0f * HalfWidth * T1;
		const float Y0 = HalfHeight * FMath::Sin(T0 * PI);
		const float Y1 = HalfHeight * FMath::Sin(T1 * PI);
		DrawLine(X0, Center.Y - Y0, X1, Center.Y - Y1, Ink, FMath::Max(Px(1.5f), 1.0f));
		DrawLine(X0, Center.Y + Y0, X1, Center.Y + Y1, Ink, FMath::Max(Px(1.5f), 1.0f));
	}
	const float Pupil = Size * 0.14f;
	Fill(Center.X - Pupil * 0.5f, Center.Y - Pupil * 0.5f, Pupil, Pupil, Ink);
	if (Pawn->GetHudMode() != EEarthfallHudMode::Full)
	{
		// A strike-through says the HUD is reduced; H or a click restores it.
		DrawLine(X + Size * 0.18f, Y + Size * 0.82f, X + Size * 0.82f, Y + Size * 0.18f, Ink, FMath::Max(Px(1.5f), 1.0f));
	}
}

void AEarthfallHUD::DrawInspectButton(const AEarthOrbitPawn* Pawn)
{
	const float Width = Px(150.0f);
	const float Height = Px(22.0f);
	const float X = Px(Margin + 250.0f);
	const float Y = Px(60.0f);
	const bool bActive = Pawn->IsInspectMode();
	const bool bHovered = Button(X, Y, Width, Height, EButton::ToggleInspect, 0);
	Fill(X, Y, Width, Height, bActive ? FLinearColor(0.30f, 0.82f, 1.0f, 0.55f)
		: (bHovered ? FLinearColor(0.30f, 0.82f, 1.0f, 0.30f) : TrackFill));
	Text(bActive ? TEXT("INSPECTING  [M]") : TEXT("INSPECT  [M]"), X + Width * 0.5f, Y + Px(3.0f), 11.5f, White, EFace::Bold, EAlign::Center);
}

void AEarthfallHUD::DrawCountryDossier(const AEarthOrbitPawn* Pawn)
{
	if (!Pawn->IsInspectMode()) return;
	const UWorld* World = GetWorld();
	const UEarthfallPopulationSubsystem* Population = World ? World->GetSubsystem<UEarthfallPopulationSubsystem>() : nullptr;
	if (!Population) return;

	const float X = Px(Margin);
	const float Y = Px(462.0f);
	const float Width = Px(400.0f);
	const float RowHeight = Px(27.0f);
	FEarthfallCountryStats Stats;
	if (!Population->GetCountryStats(Pawn->GetInspectedCountry(), Stats))
	{
		Panel(X, Y, Width, Px(74.0f), TEXT("COUNTRY DOSSIER"));
		Text(TEXT("Click any nation to open its dossier."), X + Px(14.0f), Y + Px(40.0f), 13.0f, Dim);
		return;
	}

	FEarthfallFactionInfo Faction;
	const UEarthfallSandboxSubsystem* Sandbox = World->GetSubsystem<UEarthfallSandboxSubsystem>();
	const bool bFaction = Sandbox && Sandbox->GetFactionInfo(Stats.Id, Faction);
	Panel(X, Y, Width, Px(72.0f) + RowHeight * (bFaction ? 11 : 7) + Px(10.0f), TEXT("COUNTRY DOSSIER"));
	Text(Stats.Name.ToUpper(), X + Px(14.0f), Y + Px(34.0f), 19.0f, White, EFace::Bold);
	float RowY = Y + Px(70.0f);
	const float LabelX = X + Px(14.0f);
	const float ValueX = X + Width - Px(14.0f);
	auto Row = [&](const TCHAR* Label, const FString& Value, const FLinearColor& Color, float BarFraction = -1.0f)
	{
		Text(Label, LabelX, RowY, 13.0f, Dim);
		if (BarFraction >= 0.0f) Bar(X + Px(190.0f), RowY + Px(7.0f), Px(110.0f), Px(6.0f), BarFraction, Color);
		Text(Value, ValueX, RowY - Px(1.0f), 14.0f, Color, EFace::Mono, EAlign::Right);
		RowY += RowHeight;
	};

	const double CasualtyFraction = Stats.InitialPopulation > 0.0 ? Stats.Casualties / Stats.InitialPopulation : 0.0;
	Row(TEXT("POPULATION"), FormatPeople(Stats.LivingPopulation), White);
	Row(TEXT("CASUALTIES"), FString::Printf(TEXT("%s  (%.1f%%)"), *FormatPeople(Stats.Casualties), CasualtyFraction * 100.0),
		Stats.Casualties >= 1.0 ? Red : White);
	Row(TEXT("REFUGEES"), FormatPeople(Stats.Displaced), Stats.Displaced >= 1.0 ? Orange : White);
	const float Grid = Stats.GridPercent / 100.0f;
	Row(TEXT("POWER GRID"), FString::Printf(TEXT("%.0f %%"), Stats.GridPercent),
		FMath::Lerp(Red, Green, FMath::Clamp(Grid, 0.0f, 1.0f)), Grid);
	Row(TEXT("GRID CAPACITY"), FString::Printf(TEXT("%.1f GW"), Stats.GridCapacityGW * Grid), White);
	Row(TEXT("DEFENSE READINESS"), FString::Printf(TEXT("%.0f / 100"), Stats.DefenseReadiness * (1.0f - static_cast<float>(CasualtyFraction))),
		White, Stats.DefenseReadiness / 100.0f * (1.0f - static_cast<float>(CasualtyFraction)));
	static const FLinearColor StabilityColors[] = {Green, Orange, Red, FLinearColor(0.75f, 0.20f, 0.95f, 1.0f)};
	Row(TEXT("GOVERNMENT"), UEarthfallPopulationSubsystem::StabilityLabel(Stats.Stability),
		StabilityColors[static_cast<int32>(Stats.Stability)]);
	if (bFaction)
	{
		// The nation as a power: what it can throw, and at whom.
		Row(TEXT("MILITARY"), FString::Printf(TEXT("%.0f / 100"), Faction.MilitaryStrength), White, Faction.MilitaryStrength / 100.0f);
		Row(TEXT("WARHEADS"), FString::Printf(TEXT("%d"), Faction.NuclearStockpile), Faction.NuclearStockpile > 0 ? Orange : Dim);
		Row(TEXT("AT WAR WITH"), Faction.AtWarWith.IsEmpty() ? FString(TEXT("NOBODY")) : FString::Join(Faction.AtWarWith, TEXT(" ")),
			Faction.AtWarWith.IsEmpty() ? Green : Red);
		Row(TEXT("ALLIES"), Faction.Allies.IsEmpty() ? FString(TEXT("NONE")) : FString::Join(Faction.Allies, TEXT(" ")), Cyan);
	}
}
