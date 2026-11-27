#include "EarthfallHUD.h"
#include "EarthOrbitPawn.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"

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
    DrawTextLine(TEXT("RMB DRAG  ORBIT     WHEEL  ZOOM     LMB  FIRE     1 / 2 / 3  WEAPONS     R  RESET"), 40.0f, H - 64.0f, 1.25f, White);

    const AEarthOrbitPawn* Pawn = Cast<AEarthOrbitPawn>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
    if (!Pawn) return;
    static const TCHAR* Names[] = {TEXT("KINETIC ROD"), TEXT("PLASMA LANCE"), TEXT("MANTLE CHARGE")};
    static const TCHAR* Yields[] = {TEXT("YIELD  //  1.0"), TEXT("YIELD  //  2.4"), TEXT("YIELD  //  4.6")};
    DrawTextLine(TEXT("ARSENAL"), W - 365.0f, 30.0f, 1.2f, White);
    for (int32 i = 0; i < 3; ++i)
    {
        const float Y = 64.0f + i * 56.0f;
        const bool bSelected = Pawn->GetWeaponIndex() == i;
        const FString Row = FString::Printf(TEXT("%s  %s   %s"),
            i == 0 ? TEXT("[1]") : (i == 1 ? TEXT("[2]") : TEXT("[3]")), Names[i], Yields[i]);
        DrawTextLine(Row, W - 420.0f, Y, 1.45f, bSelected ? Orange : White);
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
        FLinearColor(1.0f, 0.28f, 0.06f, 1.0f)
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
