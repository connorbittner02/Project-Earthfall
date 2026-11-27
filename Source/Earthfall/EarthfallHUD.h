#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "EarthfallTypes.h"
#include "EarthfallHUD.generated.h"

class AEarthOrbitPawn;
struct FEarthfallTelemetry;

/**
 * Canvas HUD laid out on a 1080p reference grid and scaled to the viewport, drawn
 * with Slate runtime fonts so text stays sharp at 4K. Weapon rows and multiplier
 * steppers are real click targets: the pawn offers every left click to
 * HandleClick() before treating it as a launch.
 */
UCLASS()
class EARTHFALL_API AEarthfallHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Returns true when the click landed on a HUD control or panel and was consumed. */
	bool HandleClick(const FVector2D& ScreenPosition, AEarthOrbitPawn* Pawn);

private:
	enum class EFace : uint8 { Regular, Bold, Mono };
	enum class EAlign : uint8 { Left, Center, Right };
	enum class EButton : uint8 { SelectWeapon, MultiplierDown, MultiplierUp, CycleHud, ToggleInspect, SelectTab, SelectTool, SelectMunition };

	struct FButton
	{
		FBox2D Bounds;
		EButton Kind = EButton::SelectWeapon;
		int32 Value = 0;
	};

	TArray<FButton> Buttons;
	TArray<FBox2D> Panels;
	/** Viewport pixels per 1080p reference pixel. */
	float UIScale = 1.0f;
	FVector2D Cursor = FVector2D::ZeroVector;
	bool bCursorValid = false;

	float Px(float Reference) const { return Reference * UIScale; }
	float Text(const FString& String, float X, float Y, float Points, const FLinearColor& Color,
		EFace Face = EFace::Regular, EAlign Align = EAlign::Left);
	float MeasureText(const FString& String, float Points, EFace Face) const;
	void Fill(float X, float Y, float Width, float Height, const FLinearColor& Color);
	void Panel(float X, float Y, float Width, float Height, const TCHAR* Title);
	void Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color);
	bool Button(float X, float Y, float Width, float Height, EButton Kind, int32 Value);

	void DrawStatusPanel(const FEarthfallTelemetry& Telemetry, const AEarthOrbitPawn* Pawn);
	void DrawArsenalPanel(const AEarthOrbitPawn* Pawn, float ScreenWidth);
	void DrawMultiplierPanel(const AEarthOrbitPawn* Pawn, float ScreenWidth, float Top);
	void DrawMunitionStrip(const AEarthOrbitPawn* Pawn, float ScreenWidth, float Top);
	void DrawReticle(const AEarthOrbitPawn* Pawn, float ScreenWidth, float ScreenHeight);
	void DrawDispatchTicker(const FString& String, float ScreenWidth, float ScreenHeight);
	void DrawEyeButton(const AEarthOrbitPawn* Pawn);
	void DrawInspectButton(const AEarthOrbitPawn* Pawn);
	void DrawCountryDossier(const AEarthOrbitPawn* Pawn);
};
