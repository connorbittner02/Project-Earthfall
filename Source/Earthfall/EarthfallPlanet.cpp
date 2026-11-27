#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "EarthfallPopulationSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 LatitudeSegments = 160;
constexpr int32 LongitudeSegments = 320;
constexpr int32 ImpactMaskWidth = 2048;
constexpr int32 ImpactMaskHeight = 1024;
constexpr int32 ImpactStampSize = 128;
}

AEarthfallPlanet::AEarthfallPlanet()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    PlanetMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Planet"));
    PlanetMesh->SetupAttachment(Root);
    PlanetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AimCollisionProxy = CreateDefaultSubobject<USphereComponent>(TEXT("AimCollisionProxy"));
    AimCollisionProxy->SetupAttachment(Root);
    AimCollisionProxy->SetSphereRadius(PlanetRadius);
    AimCollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    AimCollisionProxy->SetCollisionObjectType(ECC_WorldStatic);
    AimCollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
    AimCollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    AimCollisionProxy->SetGenerateOverlapEvents(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SurfaceMaterial(
        TEXT("/Game/Earth/Materials/M_EarthPhotoreal.M_EarthPhotoreal"));
    SurfaceMaterialInstance = SurfaceMaterial.Succeeded()
        ? UMaterialInstanceDynamic::Create(SurfaceMaterial.Object, this) : nullptr;
    if (SurfaceMaterialInstance)
    {
        PlanetMesh->SetMaterial(0, SurfaceMaterialInstance);
    }
}

void AEarthfallPlanet::BeginPlay()
{
    Super::BeginPlay();
    BuildImpactMaskResources();
    BuildPlanetMesh();
    BuildAtmosphereAndCloudShells();
    UpdateSunDirection();
    UpdateImpactMask(0.0f, false);
}

void AEarthfallPlanet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AddActorLocalRotation(FRotator(0.0f, RotationDegreesPerSecond * DeltaSeconds, 0.0f));
    if (CloudLayer) CloudLayer->AddLocalRotation(FRotator(0.0f, 0.08f * DeltaSeconds, 0.0f));
    SunDirectionUpdateAccumulator += DeltaSeconds;
    if (SunDirectionUpdateAccumulator >= 1.0f)
    {
        SunDirectionUpdateAccumulator = 0.0f;
        UpdateSunDirection();
    }
    ImpactMaskUpdateAccumulator += DeltaSeconds;
    if (ImpactMaskUpdateAccumulator >= 0.2f)
    {
        const float Step = ImpactMaskUpdateAccumulator;
        ImpactMaskUpdateAccumulator = 0.0f;
        UpdateImpactMask(Step, true);
    }
}

void AEarthfallPlanet::BuildPlanetMesh()
{
    BaseVertices.Reset();
    DeformedVertices.Reset();
    Triangles.Reset();
    Normals.Reset();
    UVs.Reset();
    VertexColors.Reset();
    BaseVertexColors.Reset();
    Tangents.Reset();
    for (int32 Lat = 0; Lat <= LatitudeSegments; ++Lat)
    {
        const float V = static_cast<float>(Lat) / LatitudeSegments;
        const float Phi = V * PI;
        for (int32 Lon = 0; Lon <= LongitudeSegments; ++Lon)
        {
            const float U = static_cast<float>(Lon) / LongitudeSegments;
            // NASA equirectangular maps begin at 180W on the left edge.
            const float Theta = (U - 0.5f) * 2.0f * PI;
            const FVector Direction(FMath::Sin(Phi) * FMath::Cos(Theta),
                                    FMath::Sin(Phi) * FMath::Sin(Theta),
                                    FMath::Cos(Phi));
            BaseVertices.Add(Direction * PlanetRadius);
            UVs.Add(FVector2D(U, V));
            BaseVertexColors.Add(FLinearColor::White);
            VertexColors.Add(FLinearColor::White);
            Tangents.Add(FProcMeshTangent(-FMath::Sin(Theta), FMath::Cos(Theta), 0.0f));
        }
    }

    for (int32 Lat = 0; Lat < LatitudeSegments; ++Lat)
    {
        for (int32 Lon = 0; Lon < LongitudeSegments; ++Lon)
        {
            const int32 A = Lat * (LongitudeSegments + 1) + Lon;
            const int32 B = A + LongitudeSegments + 1;
            if (Lat > 0) Triangles.Append({A, B, A + 1});
            if (Lat < LatitudeSegments - 1) Triangles.Append({A + 1, B, B + 1});
        }
    }

    DeformedVertices = BaseVertices;
    Normals.SetNumUninitialized(DeformedVertices.Num());
    for (int32 i = 0; i < DeformedVertices.Num(); ++i) Normals[i] = DeformedVertices[i].GetSafeNormal();
    // The high-density render mesh is visual-only; the lightweight sphere proxy
    // owns all aim traces, avoiding a Chaos recook after every crater update.
    PlanetMesh->CreateMeshSection_LinearColor(0, DeformedVertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
    RefreshSurfaceVertexColors();
    PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);
}

void AEarthfallPlanet::BuildAtmosphereAndCloudShells()
{
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* Atmosphere = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Earth/Materials/M_EarthAtmosphere.M_EarthAtmosphere"));
    UMaterialInterface* Clouds = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Earth/Materials/M_EarthClouds.M_EarthClouds"));
    if (SurfaceMaterialInstance && ImpactMaskRT)
    {
        SurfaceMaterialInstance->SetTextureParameterValue(TEXT("ImpactMaskRT"), ImpactMaskRT);
        PlanetMesh->SetMaterial(0, SurfaceMaterialInstance);
    }
    if (Sphere && !AtmosphereShell)
    {
        AtmosphereShell = NewObject<UStaticMeshComponent>(this, TEXT("AtmosphereShell"));
        AtmosphereShell->SetupAttachment(Root);
        AtmosphereShell->SetStaticMesh(Sphere);
        AtmosphereShell->SetRelativeScale3D(FVector(2.045f));
        AtmosphereShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Atmosphere)
        {
            AtmosphereMaterialInstance = UMaterialInstanceDynamic::Create(Atmosphere, this);
            AtmosphereShell->SetMaterial(0, AtmosphereMaterialInstance);
        }
        AtmosphereShell->RegisterComponent();
    }
    if (!CloudLayer)
    {
        CloudLayer = NewObject<UProceduralMeshComponent>(this, TEXT("CloudLayer"));
        CloudLayer->SetupAttachment(Root);
        CloudLayer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CloudLayer->SetCastShadow(false);
        CloudLayer->RegisterComponent();
    }
    CloudVertices = BaseVertices;
    for (FVector& Vertex : CloudVertices) Vertex = Vertex.GetSafeNormal() * PlanetRadius * 1.012f;
    CloudColors.SetNumUninitialized(CloudVertices.Num());
    for (FLinearColor& Color : CloudColors) Color = FLinearColor::White;
    if (CloudLayer && !CloudLayer->GetProcMeshSection(0))
    {
        CloudLayer->CreateMeshSection_LinearColor(0, CloudVertices, Triangles, Normals, UVs, CloudColors, Tangents, false);
    }
    if (Clouds)
    {
        CloudMaterialInstance = UMaterialInstanceDynamic::Create(Clouds, this);
        if (ImpactMaskRT) CloudMaterialInstance->SetTextureParameterValue(TEXT("ImpactMaskRT"), ImpactMaskRT);
        CloudLayer->SetMaterial(0, CloudMaterialInstance);
        CloudLayer->SetVisibility(true);
    }
}

void AEarthfallPlanet::BuildImpactMaskResources()
{
    ImpactMaskRT = NewObject<UTextureRenderTarget2D>(this, TEXT("ImpactMaskRT"));
    ImpactMaskRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    ImpactMaskRT->ClearColor = FLinearColor::Black;
    ImpactMaskRT->bAutoGenerateMips = false;
    ImpactMaskRT->InitCustomFormat(ImpactMaskWidth, ImpactMaskHeight, PF_B8G8R8A8, false);
    ImpactMaskRT->UpdateResourceImmediate(true);

    ImpactStampTexture = UTexture2D::CreateTransient(ImpactStampSize, ImpactStampSize, PF_B8G8R8A8);
    if (!ImpactStampTexture) return;
    ImpactStampTexture->Filter = TF_Bilinear;
    ImpactStampTexture->AddressX = TA_Clamp;
    ImpactStampTexture->AddressY = TA_Clamp;
    FTexturePlatformData* PlatformData = ImpactStampTexture->GetPlatformData();
    if (!PlatformData || PlatformData->Mips.IsEmpty()) return;
    FTexture2DMipMap& Mip = PlatformData->Mips[0];
    FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    for (int32 Y = 0; Y < ImpactStampSize; ++Y)
    {
        for (int32 X = 0; X < ImpactStampSize; ++X)
        {
            const float DX = (X + 0.5f - ImpactStampSize * 0.5f) / (ImpactStampSize * 0.5f);
            const float DY = (Y + 0.5f - ImpactStampSize * 0.5f) / (ImpactStampSize * 0.5f);
            const float R = FMath::Sqrt(DX * DX + DY * DY);
            const float Coverage = 1.0f - FMath::SmoothStep(0.72f, 1.0f, R);
            Pixels[Y * ImpactStampSize + X] = FColor(
                FMath::RoundToInt(Coverage * 255.0f), FMath::RoundToInt(Coverage * 255.0f),
                FMath::RoundToInt(Coverage * 255.0f), FMath::RoundToInt(Coverage * 255.0f));
        }
    }
    Mip.BulkData.Unlock();
    ImpactStampTexture->UpdateResource();
}

void AEarthfallPlanet::StampImpactMask(const FVector& LocalDirection, float Radius, bool bNuclear)
{
    FImpactMaskStamp& Stamp = ImpactMaskStamps.AddDefaulted_GetRef();
    Stamp.Direction = LocalDirection.GetSafeNormal();
    Stamp.Radius = Radius;
    Stamp.Age = 0.0f;
    Stamp.bNuclear = bNuclear;
    UpdateImpactMask(0.0f, false);
}

void AEarthfallPlanet::UpdateImpactMask(float DeltaSeconds, bool bAdvanceAge)
{
    if (!ImpactMaskRT || !ImpactStampTexture || !GetWorld()) return;
    if (bAdvanceAge)
    {
        for (FImpactMaskStamp& Stamp : ImpactMaskStamps) Stamp.Age += DeltaSeconds;
    }
    UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), ImpactMaskRT, FLinearColor::Black);
    UCanvas* Canvas = nullptr;
    FVector2D CanvasSize;
    FDrawToRenderTargetContext Context;
    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(GetWorld(), ImpactMaskRT, Canvas, CanvasSize, Context);
    if (Canvas)
    {
        for (const FImpactMaskStamp& Stamp : ImpactMaskStamps)
        {
            const FVector Direction = Stamp.Direction;
            const float U = (FMath::Atan2(Direction.Y, Direction.X) + PI) / (2.0f * PI);
            const float V = FMath::Acos(FMath::Clamp(Direction.Z, -1.0f, 1.0f)) / PI;
            const float AngularRadius = Stamp.Radius / PlanetRadius;
            const float HalfU = FMath::Clamp(AngularRadius / (2.0f * PI * FMath::Max(0.12f, FMath::Cos(V * PI - PI * 0.5f))), 0.0005f, 0.24f);
            const float HalfV = FMath::Clamp(AngularRadius / (2.0f * PI), 0.0005f, 0.24f);
            const float Width = HalfU * 2.0f * CanvasSize.X;
            const float Height = HalfV * 2.0f * CanvasSize.Y;
            const float Heat = FMath::Exp(-Stamp.Age / (Stamp.bNuclear ? 36.0f : 18.0f));
            const float CloudClear = FMath::Clamp(1.0f - Stamp.Age / (Stamp.bNuclear ? 36.0f : 16.0f), 0.0f, 1.0f);
            const FLinearColor Tint(0.86f, Heat, CloudClear, 1.0f);
            for (int32 Wrap = -1; Wrap <= 1; ++Wrap)
            {
                const float CenterX = (U + static_cast<float>(Wrap)) * CanvasSize.X;
                if (CenterX + Width * 0.5f < 0.0f || CenterX - Width * 0.5f > CanvasSize.X) continue;
                Canvas->K2_DrawTexture(ImpactStampTexture,
                    FVector2D(CenterX - Width * 0.5f, V * CanvasSize.Y - Height * 0.5f), FVector2D(Width, Height),
                    FVector2D::ZeroVector, FVector2D(1.0f, 1.0f), Tint, BLEND_Additive, 0.0f, FVector2D(0.5f, 0.5f));
            }
        }
    }
    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(GetWorld(), Context);
}

void AEarthfallPlanet::UpdateSunDirection()
{
    if (!SurfaceMaterialInstance || !GetWorld()) return;
    const FDateTime UtcNow = FDateTime::UtcNow();
    const double DayPhase = 2.0 * PI * (static_cast<double>(UtcNow.GetDayOfYear()) - 81.0) / 365.2422;
    const double Declination = FMath::DegreesToRadians(23.44) * FMath::Sin(DayPhase);
    const double UtcHours = UtcNow.GetHour() + UtcNow.GetMinute() / 60.0 + UtcNow.GetSecond() / 3600.0;
    const double HourAngle = FMath::DegreesToRadians((UtcHours - 12.0) * 15.0);
    const FVector ToSun(
        FMath::Cos(Declination) * FMath::Cos(HourAngle),
        -FMath::Cos(Declination) * FMath::Sin(HourAngle),
        FMath::Sin(Declination));
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        if (!It->ActorHasTag(FName(TEXT("EarthfallSun")))) continue;
        It->SetActorRotation((-ToSun).Rotation());
        break;
    }
    SurfaceMaterialInstance->SetVectorParameterValue(
        TEXT("SunDirection"), FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.0f));
}

void AEarthfallPlanet::RefreshSurfaceVertexColors()
{
    for (FLinearColor& Color : VertexColors) Color = FLinearColor::White;
}

void AEarthfallPlanet::ResetPlanet()
{
    if (UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>()) Population->ResetSimulation();
    ImpactMaskStamps.Reset();
    if (ImpactMaskRT) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), ImpactMaskRT, FLinearColor::Black);
    if (CloudLayer) CloudLayer->SetRelativeRotation(FRotator::ZeroRotator);
    BuildPlanetMesh();
    UpdateImpactMask(0.0f, false);
}

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex)
{
    const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLocation - GetActorLocation()).GetSafeNormal();
    const bool bNuclear = WeaponIndex == 5;
    StampImpactMask(LocalDirection, Radius * (bNuclear ? 3.5f : 1.0f), bNuclear);
    if (UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>())
    {
        Population->ApplyStrike(LocalDirection, Radius / PlanetRadius, Energy, WeaponIndex);
    }
    const FVector WorldNormal = GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
    const FVector SurfacePoint = WorldLocation;
    const float SafeRadius = FMath::Clamp(Radius, 2.0f, PlanetRadius * 0.45f);
    const float WeaponDepthScale[] = {1.0f, 0.72f, 1.38f, 1.55f, 0.5f, 0.82f};
    const float Depth = FMath::Clamp(Energy * 0.12f * WeaponDepthScale[FMath::Clamp(WeaponIndex, 0, 5)], 2.0f, PlanetRadius * 0.28f);
    const float CosAngle = FMath::Cos(SafeRadius / PlanetRadius);

    for (int32 i = 0; i < BaseVertices.Num(); ++i)
    {
        const FVector Direction = DeformedVertices[i].GetSafeNormal();
        const float Dot = FVector::DotProduct(Direction, LocalDirection);
        if (Dot <= CosAngle) continue;

        const float Falloff = FMath::Clamp((Dot - CosAngle) / (1.0f - CosAngle), 0.0f, 1.0f);
        const float SmoothFalloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);
        const float AngularDistance = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
        const float NormalizedDistance = AngularDistance / (SafeRadius / PlanetRadius);
        const float RimIn = FMath::SmoothStep(0.72f, 0.88f, NormalizedDistance);
        const float RimOut = 1.0f - FMath::SmoothStep(0.94f, 1.08f, NormalizedDistance);
        const float RimLift = RimIn * RimOut * Depth * 0.12f;
        const float NewRadius = FMath::Max(PlanetRadius * 0.55f,
            DeformedVertices[i].Size() - Depth * SmoothFalloff + RimLift);
        DeformedVertices[i] = Direction * NewRadius;
    }

    for (FVector& Normal : Normals) Normal = FVector::ZeroVector;
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        const int32 A = Triangles[i];
        const int32 B = Triangles[i + 1];
        const int32 C = Triangles[i + 2];
        const FVector FaceNormal = FVector::CrossProduct(
            DeformedVertices[B] - DeformedVertices[A], DeformedVertices[C] - DeformedVertices[A]);
        Normals[A] += FaceNormal;
        Normals[B] += FaceNormal;
        Normals[C] += FaceNormal;
    }
    for (int32 i = 0; i < Normals.Num(); ++i)
    {
        Normals[i] = Normals[i].IsNearlyZero() ? BaseVertices[i].GetSafeNormal() : Normals[i].GetSafeNormal();
    }

    // Update the existing section and refresh collision for subsequent aiming traces.
    PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AEarthImpactFeedback* Feedback = GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(WorldNormal).Rotator(), Params);
    if (Feedback)
    {
        static const FLinearColor Colors[] = {
            FLinearColor(1.0f, 0.66f, 0.34f),
            FLinearColor(0.2f, 0.86f, 1.0f),
            FLinearColor(1.0f, 0.22f, 0.035f),
            FLinearColor(0.55f, 0.22f, 0.95f),
            FLinearColor(0.55f, 0.88f, 1.0f),
            FLinearColor(0.82f, 0.92f, 1.0f)
        };
        static const float Lifetimes[] = {0.42f, 0.82f, 1.45f, 1.25f, 1.1f, 4.4f};
        static const float FlashIntensities[] = {18000.0f, 26000.0f, 38000.0f, 30000.0f, 22000.0f, 240000.0f};
        const int32 SafeWeapon = FMath::Clamp(WeaponIndex, 0, 5);
        Feedback->InitFeedback(SafeRadius, Colors[SafeWeapon], Lifetimes[SafeWeapon], SafeWeapon == 1, FlashIntensities[SafeWeapon], SafeWeapon == 3, SafeWeapon == 5);
    }
}
