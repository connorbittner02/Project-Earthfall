#include "EarthfallHUD.h"
#include "EarthOrbitPawn.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "EarthfallPopulationSubsystem.h"

void AEarthfallHUD::DrawTextLine(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCenter)
{
    if (!Canvas) return;
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    if (!Font) return;
    FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
    Item.Scale = FVector2D(Size, Size);
    Item.bCentreX = bCenter;
    Item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(Item);
}

void AEarthfallHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;
    const FLinearColor Cyan(0.18f, 0.78f, 1.0f, 1.0f);
    const FLinearColor White(0.88f, 0.93f, 1.0f, 1.0f);
    const FLinearColor Orange(1.0f, 0.36f, 0.12f, 1.0f);
    DrawTextLine(TEXT("E A R T H F A L L"), 38.0f, 28.0f, 2.0f, Cyan);
    DrawTextLine(TEXT("ORBITAL STRIKE SIMULATOR  //  PROTOTYPE 01"), 40.0f, 66.0f, 1.2f, White);
    DrawTextLine(TEXT("RMB DRAG ORBIT   WHEEL ZOOM   LMB STRIKE   1-6 WEAPONS   R RESET"), 40.0f, H - 102.0f, 1.0f, White);

    const AEarthOrbitPawn* Pawn = Cast<AEarthOrbitPawn>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
    if (!Pawn) return;
    static const TCHAR* Names[] = {TEXT("KINETIC ROD"), TEXT("PLASMA LANCE"), TEXT("MANTLE CHARGE"), TEXT("MICRO BLACK HOLE"), TEXT("CRYO IMPACTOR"), TEXT("THERMONUCLEAR")};
    static const TCHAR* Yields[] = {TEXT("TIGHT / FAST"), TEXT("BEAM / STEAM"), TEXT("SEISMIC / DEEP"), TEXT("INWARD / GRAVITY"), TEXT("FROST / CLIMATE"), TEXT("FLASH / EMP / FALLOUT")};
    DrawTextLine(TEXT("ARSENAL"), W - 365.0f, 30.0f, 1.2f, White);
    for (int32 i = 0; i < 6; ++i)
    {
        const float Y = 64.0f + i * 46.0f;
        const bool bSelected = Pawn->GetWeaponIndex() == i;
        static const TCHAR* Keys[] = {TEXT("[1]"), TEXT("[2]"), TEXT("[3]"), TEXT("[4]"), TEXT("[5]"), TEXT("[6]")};
        const FString Row = FString::Printf(TEXT("%s  %s"), Keys[i], Names[i]);
        DrawTextLine(Row, W - 430.0f, Y, 1.05f, bSelected ? Orange : White);
        DrawTextLine(Yields[i], W - 240.0f, Y + 18.0f, 0.68f, Cyan);
    }
    if (const UWorld* World = GetWorld())
    {
        if (const UEarthfallPopulationSubsystem* Population = World->GetSubsystem<UEarthfallPopulationSubsystem>())
        {
            const FEarthfallTelemetry T = Population->GetTelemetry();
            DrawTextLine(FString::Printf(TEXT("LIVING  %.2fB   CASUALTIES  %.2fM   DISPLACED  %.2fM"),
                T.LivingPopulation / 1.e9, T.TotalCasualties / 1.e6, T.DisplacedPopulation / 1.e6), 40.0f, 106.0f, 1.0f, White);
            DrawTextLine(FString::Printf(TEXT("GRID BLACKOUT  %.1f%%   ATMOSPHERE  %.0f%%   MEAN TEMP  %.1f C   DEBRIS  %.1f"),
                T.GridBlackoutPercent, T.AtmosphereHealth, T.MeanTemperatureC, T.DebrisMass), 40.0f, 132.0f, 0.92f, Cyan);
        FCanvasTileItem NewsBackground(FVector2D(0.0f, H - 48.0f), FVector2D(W, 48.0f), FLinearColor(0.018f, 0.035f, 0.065f, 0.94f));
            NewsBackground.BlendMode = SE_BLEND_Translucent;
            Canvas->DrawItem(NewsBackground);
            FCanvasTileItem NewsAccent(FVector2D(0.0f, H - 48.0f), FVector2D(W, 2.0f), FLinearColor(1.0f, 0.23f, 0.08f, 1.0f));
            Canvas->DrawItem(NewsAccent);
            DrawTextLine(T.LatestDispatch, 30.0f, H - 39.0f, 0.95f, FLinearColor(1.0f, 0.76f, 0.62f));
        }
    }
    DrawTextLine(FString::Printf(TEXT("CONFIRMED IMPACTS  //  %04d"), Pawn->GetImpactCount()),
        40.0f, H - 125.0f, 1.35f, White);
    float CursorX = W * 0.5f;
    float CursorY = H * 0.5f;
    if (PlayerOwner) PlayerOwner->GetMousePosition(CursorX, CursorY);
    DrawTextLine(Pawn->IsAimOnPlanet() ? TEXT("TARGET LOCK  //  EARTH") : TEXT("NO SURFACE LOCK"),
        CursorX + 18.0f, CursorY + 18.0f, 0.8f, Pawn->IsAimOnPlanet() ? Cyan : Orange);
    const FLinearColor WeaponColors[] = {
        FLinearColor(1.0f, 0.72f, 0.42f, 1.0f),
        FLinearColor(0.16f, 0.86f, 1.0f, 1.0f),
        FLinearColor(1.0f, 0.28f, 0.06f, 1.0f),
        FLinearColor(0.60f, 0.25f, 0.95f, 1.0f),
        FLinearColor(0.48f, 0.88f, 1.0f, 1.0f),
        FLinearColor(0.9f, 0.95f, 1.0f, 1.0f)
    };
    const FLinearColor AimColor = Pawn->IsAimOnPlanet() ? WeaponColors[Pawn->GetWeaponIndex()] : White;
    DrawLine(CursorX - 10.0f, CursorY, CursorX - 3.0f, CursorY, AimColor, 2.0f);
    DrawLine(CursorX + 3.0f, CursorY, CursorX + 10.0f, CursorY, AimColor, 2.0f);
    DrawLine(CursorX, CursorY - 10.0f, CursorX, CursorY - 3.0f, AimColor, 2.0f);
    DrawLine(CursorX, CursorY + 3.0f, CursorX, CursorY + 10.0f, AimColor, 2.0f);

    if (Pawn->IsAimOnPlanet() && PlayerOwner)
    {
        const FHitResult& AimHit = Pawn->GetAimHit();
        const FVector Normal = AimHit.ImpactNormal.GetSafeNormal();
        FVector TangentA = FVector::VectorPlaneProject(FVector::UpVector, Normal).GetSafeNormal();
        if (TangentA.IsNearlyZero()) TangentA = FVector::VectorPlaneProject(FVector::RightVector, Normal).GetSafeNormal();
        const FVector TangentB = FVector::CrossProduct(Normal, TangentA).GetSafeNormal();
        const float WorldRadius = Pawn->GetCurrentBlastRadius();
        constexpr int32 PreviewSegments = 40;
        FVector2D Previous = FVector2D::ZeroVector;
        bool bHavePrevious = false;
        for (int32 i = 0; i <= PreviewSegments; ++i)
        {
            const float Angle = 2.0f * PI * static_cast<float>(i) / PreviewSegments;
            const FVector WorldPoint = AimHit.ImpactPoint
                + (TangentA * FMath::Cos(Angle) + TangentB * FMath::Sin(Angle)) * WorldRadius;
            FVector2D ScreenPoint;
            if (!PlayerOwner->ProjectWorldLocationToScreen(WorldPoint, ScreenPoint, true))
            {
                bHavePrevious = false;
                continue;
            }
            if (bHavePrevious) DrawLine(Previous.X, Previous.Y, ScreenPoint.X, ScreenPoint.Y, AimColor, 2.0f);
            Previous = ScreenPoint;
            bHavePrevious = true;
        }
        DrawTextLine(FString::Printf(TEXT("BLAST RADIUS  //  %.0f"), WorldRadius),
            CursorX + 18.0f, CursorY + 40.0f, 0.72f, AimColor);
    }

    const float CenterX = W * 0.5f;
    const float CenterY = H * 0.5f;
    if (Pawn->CooldownRemaining > 0.0f)
    {
        const float BarW = 130.0f * (1.0f - Pawn->GetCooldownRatio());
        DrawLine(CenterX - 65.0f, CenterY + 50.0f, CenterX + 65.0f, CenterY + 50.0f, FLinearColor(0.12f, 0.16f, 0.21f, 1.0f), 4.0f);
        DrawLine(CenterX - 65.0f, CenterY + 50.0f, CenterX - 65.0f + BarW, CenterY + 50.0f, Orange, 4.0f);
    }
    if (Pawn->MessageTimer > 0.0f)
        DrawTextLine(Pawn->StatusMessage, CenterX, H * 0.72f, 1.25f, Orange, true);
}
