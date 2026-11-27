#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "EarthfallPopulationSubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 LatitudeSegments = 160;
constexpr int32 LongitudeSegments = 320;
constexpr int32 ImpactMaskWidth = 2048;
constexpr int32 ImpactMaskHeight = 1024;
constexpr int32 AshWidth = 1024;
constexpr int32 AshHeight = 512;
constexpr float AshStepSeconds = 0.1f;
constexpr int32 SimWidth = 1024;
constexpr int32 SimHeight = 512;
/** Fixed wave step: explicit integration is only stable below this at SimWidth. */
constexpr float TsunamiStepSeconds = 1.0f / 60.0f;
/** Fire automaton rate, inside the 15-20 Hz budget. */
constexpr float FireStepSeconds = 1.0f / 16.0f;
constexpr float TsunamiTailSeconds = 110.0f;
constexpr float FireTailSeconds = 240.0f;
/** POM heights in radians: full bowl depth and full rim height. */
constexpr float CraterBowlDepth = 0.050f;
constexpr float CraterRimHeight = 0.020f;
/** The ash field keeps simulating this long after the last strike, then idles. */
constexpr float AshSimulationTailSeconds = 900.0f;
/** Crater dents are capped at 3% of the radius, and never deeper than 96% in total. */
constexpr float MaxCraterDepthFraction = 0.03f;
constexpr float MinCraterRadiusFraction = 0.96f;

// Weather: (seed, coverage threshold, zonal stretch, drift rad/s).
const FLinearColor LowCloudWeather(0.0f, 0.575f, 1.25f, 0.004f);
const FLinearColor HighCloudWeather(37.0f, 0.60f, 2.6f, 0.007f);
// Look: (opacity, self-shadow strength, ash visibility, sun intensity).
const FLinearColor LowCloudLook(1.0f, 1.7f, 1.0f, 1.55f);
const FLinearColor HighCloudLook(0.42f, 0.9f, 0.0f, 1.55f);
constexpr float SunIntensity = 1.75f;
constexpr float CityLightGain = 3.2f;
constexpr float SunglintGain = 2.6f;
}

AEarthfallPlanet::AEarthfallPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto MakeShell = [this](const TCHAR* Name)
	{
		UProceduralMeshComponent* Shell = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Shell->SetupAttachment(Root);
		Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shell->SetGenerateOverlapEvents(false);
		Shell->SetCastShadow(false);
		return Shell;
	};
	PlanetMesh = MakeShell(TEXT("Planet"));
	LowCloudLayer = MakeShell(TEXT("LowCloudLayer"));
	HighCloudLayer = MakeShell(TEXT("HighCloudLayer"));
	AtmosphereShell = MakeShell(TEXT("AtmosphereShell"));

	// The render mesh is visual-only; this cheap sphere owns every aim trace, so
	// crater updates never trigger a physics recook.
	AimCollisionProxy = CreateDefaultSubobject<USphereComponent>(TEXT("AimCollisionProxy"));
	AimCollisionProxy->SetupAttachment(Root);
	AimCollisionProxy->SetSphereRadius(PlanetRadius);
	AimCollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	AimCollisionProxy->SetCollisionObjectType(ECC_WorldStatic);
	AimCollisionProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	AimCollisionProxy->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	AimCollisionProxy->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Game/Earth/Materials/M_EarthPhotoreal.M_EarthPhotoreal"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clouds(TEXT("/Game/Earth/Materials/M_EarthClouds.M_EarthClouds"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Atmosphere(TEXT("/Game/Earth/Materials/M_EarthAtmosphere.M_EarthAtmosphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Advection(TEXT("/Game/Earth/Materials/M_AshAdvection.M_AshAdvection"));
	SurfaceMaterial = Surface.Succeeded() ? Surface.Object : nullptr;
	CloudMaterial = Clouds.Succeeded() ? Clouds.Object : nullptr;
	AtmosphereMaterial = Atmosphere.Succeeded() ? Atmosphere.Object : nullptr;
	AshAdvectionMaterial = Advection.Succeeded() ? Advection.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TsunamiStep(TEXT("/Game/Earth/Materials/M_TsunamiStep.M_TsunamiStep"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FireStep(TEXT("/Game/Earth/Materials/M_FireStep.M_FireStep"));
	TsunamiStepMaterial = TsunamiStep.Succeeded() ? TsunamiStep.Object : nullptr;
	FireStepMaterial = FireStep.Succeeded() ? FireStep.Object : nullptr;
}

void AEarthfallPlanet::BeginPlay()
{
	Super::BeginPlay();
	AimCollisionProxy->SetSphereRadius(PlanetRadius);
	CreateMaterialInstances();
	BuildImpactMaskResources();
	BuildAshPlumeResources();
	BuildWaveFireAndCraterResources();
	BuildSphereTopology();
	BuildPlanetMesh();
	BuildShell(LowCloudLayer, LowCloudScale, LowCloudMID, 10);
	BuildShell(HighCloudLayer, HighCloudScale, HighCloudMID, 11);
	BuildShell(AtmosphereShell, AtmosphereScale, AtmosphereMID, 12);
	UpdateSunDirection();
	UpdateEnvironment();
	RedrawImpactMask();
	PushShockParameters();
}

void AEarthfallPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FMath::IsNearlyZero(RotationDegreesPerSecond))
	{
		AddActorLocalRotation(FRotator(0.0f, RotationDegreesPerSecond * DeltaSeconds, 0.0f));
	}

	SunUpdateAccumulator += DeltaSeconds;
	if (SunUpdateAccumulator >= 5.0f)
	{
		SunUpdateAccumulator = 0.0f;
		UpdateSunDirection();
	}

	EnvironmentAccumulator += DeltaSeconds;
	if (EnvironmentAccumulator >= 0.5f)
	{
		EnvironmentAccumulator = 0.0f;
		UpdateEnvironment();
	}

	UpdateShocks(DeltaSeconds);

	// The mask only changes while a stamp is still cooling or its cloud hole is
	// still opening or healing, so an idle planet redraws nothing.
	bool bAnyLive = false;
	for (FImpactStamp& Stamp : ImpactStamps)
	{
		if (Stamp.IsLive())
		{
			Stamp.Age += DeltaSeconds;
			bAnyLive = true;
		}
	}
	MaskRedrawAccumulator += DeltaSeconds;
	if (bImpactMaskDirty || (bAnyLive && MaskRedrawAccumulator >= 1.0f / 30.0f))
	{
		MaskRedrawAccumulator = 0.0f;
		RedrawImpactMask();
	}

	if (TsunamiActiveSeconds > 0.0f)
	{
		TsunamiActiveSeconds -= DeltaSeconds;
		TsunamiStepAccumulator = FMath::Min(TsunamiStepAccumulator + DeltaSeconds, TsunamiStepSeconds * 3.0f);
		while (TsunamiStepAccumulator >= TsunamiStepSeconds)
		{
			TsunamiStepAccumulator -= TsunamiStepSeconds;
			StepTsunami();
		}
	}
	if (FireActiveSeconds > 0.0f)
	{
		FireActiveSeconds -= DeltaSeconds;
		FireStepAccumulator += DeltaSeconds;
		if (FireStepAccumulator >= FireStepSeconds)
		{
			StepFire(FireStepAccumulator);
			FireStepAccumulator = 0.0f;
		}
	}

	if (AshActiveSeconds > 0.0f)
	{
		AshActiveSeconds -= DeltaSeconds;
		AshStepAccumulator += DeltaSeconds;
		if (AshStepAccumulator >= AshStepSeconds)
		{
			AdvanceAsh(AshStepAccumulator);
			AshStepAccumulator = 0.0f;
		}
	}
}

void AEarthfallPlanet::SetHighlightedCountry(int32 CountryId)
{
	if (SurfaceMID) SurfaceMID->SetVectorParameterValue(TEXT("Inspect"), FLinearColor(static_cast<float>(CountryId), CountryId > 0 ? 1.0f : 0.0f, 0.0f, 0.0f));
}

FVector AEarthfallPlanet::GetSunDirectionWorld() const
{
	return GetActorTransform().TransformVectorNoScale(SunDirectionLocal).GetSafeNormal();
}

void AEarthfallPlanet::ForEachShadedMaterial(TFunctionRef<void(UMaterialInstanceDynamic*)> Visit) const
{
	for (UMaterialInstanceDynamic* Material : {SurfaceMID.Get(), LowCloudMID.Get(), HighCloudMID.Get(), AtmosphereMID.Get()})
	{
		if (Material) Visit(Material);
	}
}

void AEarthfallPlanet::CreateMaterialInstances()
{
	SurfaceMID = SurfaceMaterial ? UMaterialInstanceDynamic::Create(SurfaceMaterial, this) : nullptr;
	LowCloudMID = CloudMaterial ? UMaterialInstanceDynamic::Create(CloudMaterial, this) : nullptr;
	HighCloudMID = CloudMaterial ? UMaterialInstanceDynamic::Create(CloudMaterial, this) : nullptr;
	AtmosphereMID = AtmosphereMaterial ? UMaterialInstanceDynamic::Create(AtmosphereMaterial, this) : nullptr;
	AshAdvectionMID = AshAdvectionMaterial ? UMaterialInstanceDynamic::Create(AshAdvectionMaterial, this) : nullptr;

	if (SurfaceMID)
	{
		// The surface samples the low deck's weather so cloud shadows line up.
		SurfaceMID->SetVectorParameterValue(TEXT("CloudLayer"), LowCloudWeather);
		SurfaceMID->SetVectorParameterValue(TEXT("Tuning"), FLinearColor(SunIntensity, CityLightGain, 1.0f, SunglintGain));
	}
	if (LowCloudMID)
	{
		LowCloudMID->SetVectorParameterValue(TEXT("Layer"), LowCloudWeather);
		LowCloudMID->SetVectorParameterValue(TEXT("Look"), LowCloudLook);
	}
	if (HighCloudMID)
	{
		HighCloudMID->SetVectorParameterValue(TEXT("Layer"), HighCloudWeather);
		HighCloudMID->SetVectorParameterValue(TEXT("Look"), HighCloudLook);
	}
	if (AtmosphereMID)
	{
		AtmosphereMID->SetVectorParameterValue(TEXT("Shape"),
			FLinearColor(PlanetRadius, PlanetRadius * AtmosphereScale, 1.0f, 1.0f));
	}
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
void AEarthfallPlanet::BuildSphereTopology()
{
	BaseVertices.Reset();
	Triangles.Reset();
	SphereNormals.Reset();
	UVs.Reset();
	VertexColors.Reset();
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
			// Unreal is left-handed: east must be -Y or the map is mirrored from outside.
			const FVector Direction(FMath::Sin(Phi) * FMath::Cos(Theta), -FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi));
			BaseVertices.Add(Direction * PlanetRadius);
			SphereNormals.Add(Direction);
			UVs.Add(FVector2D(U, V));
			VertexColors.Add(FLinearColor::White);
			Tangents.Add(FProcMeshTangent(-FMath::Sin(Theta), -FMath::Cos(Theta), 0.0f));
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
}

void AEarthfallPlanet::BuildPlanetMesh()
{
	DeformedVertices = BaseVertices;
	Normals = SphereNormals;
	PlanetMesh->ClearMeshSection(0);
	PlanetMesh->CreateMeshSection_LinearColor(0, DeformedVertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
	if (SurfaceMID) PlanetMesh->SetMaterial(0, SurfaceMID);
}

void AEarthfallPlanet::BuildShell(UProceduralMeshComponent* Shell, float RadiusScale, UMaterialInstanceDynamic* Material, int32 SortPriority)
{
	if (!Shell) return;
	TArray<FVector> ShellVertices;
	ShellVertices.Reserve(BaseVertices.Num());
	for (const FVector& Normal : SphereNormals) ShellVertices.Add(Normal * PlanetRadius * RadiusScale);
	Shell->ClearMeshSection(0);
	Shell->CreateMeshSection_LinearColor(0, ShellVertices, Triangles, SphereNormals, UVs, VertexColors, Tangents, false);
	// Every shell shares the planet's origin, so distance sorting cannot order
	// them. Explicit priorities draw low cloud, then high cloud, then air.
	Shell->SetTranslucentSortPriority(SortPriority);
	Shell->SetVisibility(Material != nullptr);
	if (Material) Shell->SetMaterial(0, Material);
}

void AEarthfallPlanet::DeformCrater(const FVector& LocalDirection, const FEarthfallStrike& Strike)
{
	const float SafeRadius = FMath::Clamp(Strike.Radius, 2.0f, PlanetRadius * 0.45f);
	static const float DepthScale[] = {1.0f, 0.72f, 1.38f, 1.55f, 0.5f, 0.82f};
	// The dent stays shallow so triangles never tear; the shader's scorch, magma
	// and the Niagara and mesh FX carry the violence instead.
	const float Depth = FMath::Clamp(Strike.Energy * 0.12f * DepthScale[Strike.WeaponIndex], 0.0f, PlanetRadius * MaxCraterDepthFraction);
	const float AngularRadius = SafeRadius / PlanetRadius;
	const float CosAngle = FMath::Cos(AngularRadius * 1.08f);
	const float Floor = PlanetRadius * MinCraterRadiusFraction;

	bool bChanged = false;
	for (int32 i = 0; i < DeformedVertices.Num(); ++i)
	{
		const float Dot = FVector::DotProduct(SphereNormals[i], LocalDirection);
		if (Dot <= CosAngle) continue;
		const float Normalized = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)) / AngularRadius;
		const float Bowl = 1.0f - FMath::SmoothStep(0.0f, 0.90f, Normalized);
		const float Rim = FMath::SmoothStep(0.72f, 0.90f, Normalized) * (1.0f - FMath::SmoothStep(0.94f, 1.08f, Normalized));
		const float NewRadius = FMath::Max(Floor, DeformedVertices[i].Size() - Depth * Bowl + Depth * 0.12f * Rim);
		DeformedVertices[i] = SphereNormals[i] * NewRadius;
		bChanged = true;
	}
	if (!bChanged) return;

	for (FVector& Normal : Normals) Normal = FVector::ZeroVector;
	for (int32 i = 0; i < Triangles.Num(); i += 3)
	{
		const int32 A = Triangles[i];
		const int32 B = Triangles[i + 1];
		const int32 C = Triangles[i + 2];
		const FVector FaceNormal = FVector::CrossProduct(DeformedVertices[B] - DeformedVertices[A], DeformedVertices[C] - DeformedVertices[A]);
		Normals[A] += FaceNormal;
		Normals[B] += FaceNormal;
		Normals[C] += FaceNormal;
	}
	for (int32 i = 0; i < Normals.Num(); ++i)
	{
		// Guard: keep every normal on the outward side of the sphere.
		FVector Normal = Normals[i].GetSafeNormal();
		if (Normal.IsNearlyZero()) Normal = SphereNormals[i];
		if (FVector::DotProduct(Normal, SphereNormals[i]) < 0.0f) Normal = -Normal;
		Normals[i] = Normal;
	}
	PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);
}

// ---------------------------------------------------------------------------
// Impact mask: R = scorch over a 0.25 EMP plateau, G = heat, B = cloud clear.
// ---------------------------------------------------------------------------
UTexture2D* AEarthfallPlanet::CreateStampTexture(TFunctionRef<FColor(float, float)> Evaluate, int32 Size) const
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) return nullptr;
	// Stamp values are data, not colour: keep them linear so 0.25 lands as 0.25.
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float DX = (X + 0.5f - Size * 0.5f) / (Size * 0.5f);
			const float DY = (Y + 0.5f - Size * 0.5f) / (Size * 0.5f);
			Pixels[Y * Size + X] = Evaluate(FMath::Sqrt(DX * DX + DY * DY), FMath::Atan2(DY, DX));
		}
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

void AEarthfallPlanet::BuildImpactMaskResources()
{
	ImpactMaskRT = NewObject<UTextureRenderTarget2D>(this, TEXT("ImpactMaskRT"));
	ImpactMaskRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	ImpactMaskRT->ClearColor = FLinearColor::Black;
	ImpactMaskRT->bAutoGenerateMips = false;
	ImpactMaskRT->AddressX = TA_Wrap;
	ImpactMaskRT->AddressY = TA_Clamp;
	ImpactMaskRT->Filter = TF_Bilinear;
	ImpactMaskRT->InitCustomFormat(ImpactMaskWidth, ImpactMaskHeight, PF_B8G8R8A8, true);
	ImpactMaskRT->UpdateResourceImmediate(true);

	auto Byte = [](float Value) { return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f)); };
	ScorchStampTexture = CreateStampTexture([&Byte](float Distance, float)
	{
		const float Scorch = 1.0f - FMath::SmoothStep(0.55f, 0.95f, Distance);
		const float Heat = FMath::Exp(-Distance * Distance * 3.2f) * (1.0f - FMath::SmoothStep(0.85f, 1.0f, Distance));
		return FColor(Byte(Scorch), Byte(Heat), 0, 255);
	});
	EmpStampTexture = CreateStampTexture([&Byte](float Distance, float)
	{
		return FColor(Byte(0.25f * (1.0f - FMath::SmoothStep(0.80f, 1.0f, Distance))), 0, 0, 255);
	});
	ClearStampTexture = CreateStampTexture([&Byte](float Distance, float)
	{
		return FColor(0, 0, Byte(1.0f - FMath::SmoothStep(0.72f, 1.0f, Distance)), 255);
	});

	ForEachShadedMaterial([this](UMaterialInstanceDynamic* Material)
	{
		Material->SetTextureParameterValue(TEXT("ImpactMaskRT"), ImpactMaskRT);
	});
}

void AEarthfallPlanet::DrawStamp(UCanvas* Canvas, const FVector2D& CanvasSize, UTexture2D* Texture,
	const FVector& Direction, float AngularRadius, const FLinearColor& Tint) const
{
	if (!Canvas || !Texture) return;
	const float U = (FMath::Atan2(-Direction.Y, Direction.X) + PI) / (2.0f * PI);
	const float V = FMath::Acos(FMath::Clamp(Direction.Z, -1.0f, 1.0f)) / PI;
	// V spans PI radians and U spans 2*PI, stretched by 1/sin(colatitude) so the
	// footprint stays round on the globe away from the poles.
	const float SinColatitude = FMath::Max(0.10f, FMath::Sin(V * PI));
	const float HalfV = FMath::Clamp(AngularRadius / PI, 0.0005f, 0.5f);
	const float HalfU = FMath::Clamp(AngularRadius / (2.0f * PI * SinColatitude), 0.0005f, 0.5f);
	const float Width = HalfU * 2.0f * CanvasSize.X;
	const float Height = HalfV * 2.0f * CanvasSize.Y;
	for (int32 Wrap = -1; Wrap <= 1; ++Wrap)
	{
		const float CenterX = (U + static_cast<float>(Wrap)) * CanvasSize.X;
		if (CenterX + Width * 0.5f < 0.0f || CenterX - Width * 0.5f > CanvasSize.X) continue;
		Canvas->K2_DrawTexture(Texture, FVector2D(CenterX - Width * 0.5f, V * CanvasSize.Y - Height * 0.5f),
			FVector2D(Width, Height), FVector2D::ZeroVector, FVector2D(1.0f, 1.0f), Tint, BLEND_Additive, 0.0f, FVector2D(0.5f, 0.5f));
	}
}

void AEarthfallPlanet::RedrawImpactMask()
{
	bImpactMaskDirty = false;
	if (!ImpactMaskRT || !GetWorld()) return;
	UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), ImpactMaskRT, FLinearColor::Black);
	if (ImpactStamps.IsEmpty()) return;

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(GetWorld(), ImpactMaskRT, Canvas, CanvasSize, Context);
	for (const FImpactStamp& Stamp : ImpactStamps)
	{
		if (Stamp.bNuclear)
		{
			// Wide EMP plateau: city lights die far beyond the scorched core.
			DrawStamp(Canvas, CanvasSize, EmpStampTexture, Stamp.Direction, Stamp.Radius * 3.5f, FLinearColor::White);
		}
		const float Heat = FMath::Exp(-Stamp.Age / Stamp.HeatSeconds);
		DrawStamp(Canvas, CanvasSize, ScorchStampTexture, Stamp.Direction, Stamp.Radius, FLinearColor(1.0f, Heat, 0.0f, 1.0f));

		// The hole in the clouds opens behind the shock front, holds, then heals.
		const float ClearRadius = FMath::Min(Stamp.ClearSpeed * Stamp.Age, Stamp.ClearMaxRadius);
		const float ClearAmount = 1.0f - FMath::SmoothStep(Stamp.ClearHoldSeconds, Stamp.ClearHoldSeconds + Stamp.ClearFadeSeconds, Stamp.Age);
		if (ClearRadius > 0.004f && ClearAmount > 0.004f)
		{
			DrawStamp(Canvas, CanvasSize, ClearStampTexture, Stamp.Direction, ClearRadius, FLinearColor(0.0f, 0.0f, ClearAmount, 1.0f));
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(GetWorld(), Context);
}

// ---------------------------------------------------------------------------
// Ash: a ping-pong pair advected by M_AshAdvection. R soot, G fallout, B deposit.
// ---------------------------------------------------------------------------
void AEarthfallPlanet::BuildAshPlumeResources()
{
	AshPlumeRTs.SetNum(2);
	for (int32 Index = 0; Index < AshPlumeRTs.Num(); ++Index)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, *FString::Printf(TEXT("AshPlumeRT_%d"), Index));
		// Half-float: an 8-bit target cannot represent a slow exponential decay,
		// because x * 0.997 rounds straight back to x below ~0.65.
		Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Black;
		Target->bAutoGenerateMips = false;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Clamp;
		Target->Filter = TF_Bilinear;
		Target->InitAutoFormat(AshWidth, AshHeight);
		Target->UpdateResourceImmediate(true);
		AshPlumeRTs[Index] = Target;
	}
	AshStampTexture = CreateStampTexture([](float Distance, float)
	{
		const float Density = FMath::Exp(-Distance * Distance * 3.8f) * (1.0f - FMath::SmoothStep(0.82f, 1.0f, Distance));
		const uint8 Value = static_cast<uint8>(FMath::RoundToInt(Density * 255.0f));
		return FColor(Value, Value, Value, 255);
	});
	AshReadIndex = 0;
	BindAshTexture();
}

void AEarthfallPlanet::BindAshTexture()
{
	if (!AshPlumeRTs.IsValidIndex(AshReadIndex)) return;
	UTextureRenderTarget2D* Current = AshPlumeRTs[AshReadIndex];
	if (SurfaceMID) SurfaceMID->SetTextureParameterValue(TEXT("AshPlumeRT"), Current);
	if (LowCloudMID) LowCloudMID->SetTextureParameterValue(TEXT("AshPlumeRT"), Current);
	if (HighCloudMID) HighCloudMID->SetTextureParameterValue(TEXT("AshPlumeRT"), Current);
}

void AEarthfallPlanet::StampAsh(const FVector& LocalDirection, float AngularRadius, float Soot, float Fallout)
{
	if (!GetWorld() || AshPlumeRTs.Num() != 2 || !AshStampTexture || (Soot <= 0.0f && Fallout <= 0.0f)) return;
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(GetWorld(), AshPlumeRTs[AshReadIndex], Canvas, CanvasSize, Context);
	DrawStamp(Canvas, CanvasSize, AshStampTexture, LocalDirection, AngularRadius, FLinearColor(Soot, Fallout, 0.0f, 1.0f));
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(GetWorld(), Context);
	AshActiveSeconds = AshSimulationTailSeconds;
}

void AEarthfallPlanet::AdvanceAsh(float StepSeconds)
{
	if (!GetWorld() || AshPlumeRTs.Num() != 2 || !AshAdvectionMID) return;
	const UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
	const float WindTime = Population ? static_cast<float>(Population->GetWindTime()) : GetWorld()->GetTimeSeconds();
	const int32 WriteIndex = 1 - AshReadIndex;
	AshAdvectionMID->SetTextureParameterValue(TEXT("AshSource"), AshPlumeRTs[AshReadIndex]);
	if (FireRTs.IsValidIndex(FireReadIndex)) AshAdvectionMID->SetTextureParameterValue(TEXT("FireSource"), FireRTs[FireReadIndex]);
	AshAdvectionMID->SetVectorParameterValue(TEXT("Step"), FLinearColor(StepSeconds, WindTime, 1.0f / AshWidth, 1.0f / AshHeight));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), AshPlumeRTs[WriteIndex], AshAdvectionMID);
	AshReadIndex = WriteIndex;
	BindAshTexture();
}

// ---------------------------------------------------------------------------
// Shockwaves
// ---------------------------------------------------------------------------
void AEarthfallPlanet::StartShock(const FVector& LocalDirection, float BlastAngle, const FEarthfallStrike& Strike, float& OutMaxRadius, float& OutSpeed)
{
	const bool bNuclear = Strike.WeaponIndex == EarthfallWeapons::Nuclear;
	OutMaxRadius = FMath::Clamp(BlastAngle * (bNuclear ? 5.0f : 3.0f) + 0.06f, 0.10f, 2.9f);
	const float Duration = bNuclear ? 9.0f : FMath::Clamp(3.5f + BlastAngle * 6.0f, 3.5f, 8.0f);
	OutSpeed = OutMaxRadius / Duration;

	// Reuse an idle slot, otherwise retire whichever wave is closest to finishing.
	int32 Slot = 0;
	float BestProgress = -1.0f;
	for (int32 Index = 0; Index < ShockSlots; ++Index)
	{
		const float Progress = Shocks[Index].bActive ? Shocks[Index].Speed * Shocks[Index].Age / Shocks[Index].MaxRadius : 2.0f;
		if (Progress > BestProgress)
		{
			BestProgress = Progress;
			Slot = Index;
		}
	}

	static const float FlashPeaks[] = {0.7f, 1.2f, 1.6f, 0.9f, 1.0f, 3.2f};
	FShock& Shock = Shocks[Slot];
	Shock.bActive = true;
	Shock.bNuclear = bNuclear;
	Shock.Direction = LocalDirection;
	Shock.Age = 0.0f;
	Shock.MaxRadius = OutMaxRadius;
	Shock.Speed = OutSpeed;
	Shock.FlashPeak = FlashPeaks[Strike.WeaponIndex] * FMath::Pow(FMath::Max(Strike.YieldMultiplier, 0.01f), 0.25f);
	Shock.FlashRadius = BlastAngle * 1.6f + 0.10f;
	Shock.FlashColor = bNuclear ? FLinearColor(1.0f, 0.82f, 0.62f) : EarthfallWeapons::Get(Strike.WeaponIndex).Color;
	bShockParametersDirty = true;
}

void AEarthfallPlanet::UpdateShocks(float DeltaSeconds)
{
	bool bAnyActive = false;
	for (FShock& Shock : Shocks)
	{
		if (!Shock.bActive) continue;
		Shock.Age += DeltaSeconds;
		if (Shock.Speed * Shock.Age >= Shock.MaxRadius && Shock.Age > 4.0f)
		{
			Shock.bActive = false;
			bShockParametersDirty = true;
			continue;
		}
		bAnyActive = true;
	}
	if (bAnyActive || bShockParametersDirty) PushShockParameters();
}

void AEarthfallPlanet::PushShockParameters()
{
	bShockParametersDirty = false;
	for (int32 Index = 0; Index < ShockSlots; ++Index)
	{
		const FShock& Shock = Shocks[Index];
		FLinearColor A(0.0f, 0.0f, 0.0f, 0.0f);
		FLinearColor B(0.0f, 0.0f, 0.0f, 0.0f);
		FLinearColor C(0.0f, 0.0f, 0.0f, 0.0f);
		if (Shock.bActive)
		{
			const float Radius = FMath::Min(Shock.Speed * Shock.Age, Shock.MaxRadius);
			const float Progress = Radius / Shock.MaxRadius;
			const float Fade = FMath::Pow(FMath::Max(1.0f - Progress, 0.0f), 0.6f);
			// A nuclear fireball flashes twice: an instant spike, then a slower,
			// longer second pulse as the shock front turns transparent again.
			const float Flash = Shock.bNuclear
				? Shock.FlashPeak * (FMath::Exp(-Shock.Age / 0.05f) + 0.40f * (Shock.Age / 0.45f) * FMath::Exp(1.0f - Shock.Age / 0.45f))
				: Shock.FlashPeak * FMath::Exp(-Shock.Age / 0.35f);
			A = FLinearColor(Shock.Direction.X, Shock.Direction.Y, Shock.Direction.Z, Radius);
			B = FLinearColor(Fade, Flash, Shock.FlashRadius, Fade * (0.35f + 0.65f * (1.0f - Progress)));
			C = Shock.FlashColor;
		}
		const FName NameA(*FString::Printf(TEXT("ShockA%d"), Index));
		const FName NameB(*FString::Printf(TEXT("ShockB%d"), Index));
		const FName NameC(*FString::Printf(TEXT("ShockC%d"), Index));
		for (UMaterialInstanceDynamic* Material : {SurfaceMID.Get(), LowCloudMID.Get(), HighCloudMID.Get()})
		{
			if (!Material) continue;
			Material->SetVectorParameterValue(NameA, A);
			Material->SetVectorParameterValue(NameB, B);
			Material->SetVectorParameterValue(NameC, C);
		}
	}
}

// ---------------------------------------------------------------------------
// Sun and environment
// ---------------------------------------------------------------------------
void AEarthfallPlanet::UpdateSunDirection()
{
	if (!GetWorld()) return;
	SunDirectionLocal = EarthfallSolar::ComputeToSun();
	const FVector ToSunWorld = GetSunDirectionWorld();
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(FName(TEXT("EarthfallSun")))) continue;
		It->SetActorRotation((-ToSunWorld).Rotation());
		break;
	}
	const FLinearColor Sun(SunDirectionLocal.X, SunDirectionLocal.Y, SunDirectionLocal.Z, 0.0f);
	ForEachShadedMaterial([&Sun](UMaterialInstanceDynamic* Material)
	{
		Material->SetVectorParameterValue(TEXT("SunDirection"), Sun);
	});
}

void AEarthfallPlanet::UpdateEnvironment()
{
	const UEarthfallPopulationSubsystem* Population = GetWorld() ? GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>() : nullptr;
	if (!Population) return;
	const FEarthfallTelemetry Telemetry = Population->GetTelemetry();
	// A choked atmosphere turns the limb from Rayleigh blue toward soot brown and
	// dims the sun reaching the ground.
	const float Health = FMath::Clamp(Telemetry.AtmosphereHealth / 100.0f, 0.0f, 1.0f);
	const float Sunlight = FMath::Clamp(Telemetry.SunlightPercent / 100.0f, 0.25f, 1.0f);
	if (SurfaceMID)
	{
		SurfaceMID->SetVectorParameterValue(TEXT("Tuning"), FLinearColor(SunIntensity * Sunlight, CityLightGain, Health, SunglintGain));
	}
	if (AtmosphereMID)
	{
		AtmosphereMID->SetVectorParameterValue(TEXT("Shape"),
			FLinearColor(PlanetRadius, PlanetRadius * AtmosphereScale, 1.0f, Health));
	}
}

// ---------------------------------------------------------------------------
// Strikes
// ---------------------------------------------------------------------------
void AEarthfallPlanet::ResetPlanet()
{
	if (!GetWorld()) return;
	if (UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>()) Population->ResetSimulation();
	ImpactStamps.Reset();
	for (FShock& Shock : Shocks) Shock = FShock();
	for (UTextureRenderTarget2D* AshTarget : AshPlumeRTs)
	{
		if (AshTarget) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), AshTarget, FLinearColor::Black);
	}
	AshReadIndex = 0;
	AshActiveSeconds = 0.0f;
	AshStepAccumulator = 0.0f;
	BindAshTexture();
	for (UTextureRenderTarget2D* Target : TsunamiRTs) if (Target) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, FLinearColor(0.5f, 0.5f, 0.0f, 1.0f));
	for (UTextureRenderTarget2D* Target : FireRTs) if (Target) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, FLinearColor::Black);
	if (CraterRT) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), CraterRT, FLinearColor::Black);
	TsunamiReadIndex = 0;
	FireReadIndex = 0;
	TsunamiActiveSeconds = 0.0f;
	FireActiveSeconds = 0.0f;
	BindSimulationTextures();
	BuildPlanetMesh();
	RedrawImpactMask();
	PushShockParameters();
	UpdateEnvironment();
}

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex)
{
	FEarthfallStrike Strike;
	Strike.WorldLocation = WorldLocation;
	Strike.WeaponIndex = FMath::Clamp(WeaponIndex, 0, EarthfallWeapons::Count - 1);
	Strike.Radius = Radius;
	Strike.Energy = Energy;
	ApplyStrike(Strike);
}

void AEarthfallPlanet::ApplyStrike(const FEarthfallStrike& Strike)
{
	if (!GetWorld()) return;
	const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(Strike.WorldLocation - GetActorLocation()).GetSafeNormal();
	if (LocalDirection.IsNearlyZero()) return;
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(Strike.WeaponIndex);
	const bool bNuclear = Strike.WeaponIndex == EarthfallWeapons::Nuclear;
	const float BlastAngle = FMath::Clamp(Strike.Radius / PlanetRadius, 0.01f, 2.6f);
	const float YieldLog = FMath::Max(0.0f, FMath::Log2(FMath::Max(Strike.YieldMultiplier, 1.0f)));

	float ShockMaxRadius = 0.0f;
	float ShockSpeed = 0.0f;
	StartShock(LocalDirection, BlastAngle, Strike, ShockMaxRadius, ShockSpeed);

	if (ImpactStamps.Num() >= MaxImpactStamps) ImpactStamps.RemoveAt(0);
	FImpactStamp& Stamp = ImpactStamps.AddDefaulted_GetRef();
	Stamp.Direction = LocalDirection;
	Stamp.Radius = BlastAngle;
	Stamp.bNuclear = bNuclear;
	Stamp.HeatSeconds = (bNuclear ? 14.0f : 8.0f) * (1.0f + 0.15f * YieldLog);
	Stamp.ClearMaxRadius = FMath::Min(BlastAngle * (bNuclear ? 3.5f : 1.7f), 2.6f);
	Stamp.ClearSpeed = ShockSpeed;
	Stamp.ClearHoldSeconds = bNuclear ? 40.0f : 22.0f;
	Stamp.ClearFadeSeconds = bNuclear ? 60.0f : 35.0f;
	bImpactMaskDirty = true;

	// Visible plume: lofted mass scales sub-linearly so a 100x strike is a
	// continental smear rather than a solid black disc.
	const float PlumeScale = FMath::Pow(FMath::Max(Strike.YieldMultiplier, 0.01f), 0.4f) * FMath::Pow(Strike.FalloutMultiplier, 0.7f);
	StampAsh(LocalDirection, BlastAngle * 1.4f + 0.03f,
		FMath::Min(Spec.Soot * 0.9f * PlumeScale, 3.0f), FMath::Min(Spec.Fallout * 0.9f * PlumeScale, 3.0f));

	// Crater height field for parallax occlusion: depth scales with crater size.
	const float Relief = FMath::Clamp(BlastAngle * 3.0f, 0.25f, 1.0f);
	StampInto(CraterRT, CraterStampTexture, LocalDirection, BlastAngle * 1.25f, FLinearColor(Relief, Relief, 0.0f, 1.0f));

	UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
	if (Population)
	{
		Population->ApplyStrike(LocalDirection, BlastAngle, Strike.Energy, Strike.WeaponIndex, Strike.YieldMultiplier, Strike.FalloutMultiplier);
		const bool bOcean = Population->GetCountryAtDirection(LocalDirection) == 0;
		const int32 Weapon = Strike.WeaponIndex;
		if (bOcean && (Weapon == 0 || Weapon == 2 || Weapon == 4 || bNuclear))
		{
			// An ocean hit lifts the sea surface; the wave equation does the rest.
			const float Amplitude = FMath::Clamp(Strike.Energy / 160.0f, 0.35f, 1.0f);
			if (TsunamiRTs.IsValidIndex(TsunamiReadIndex))
			{
				StampInto(TsunamiRTs[TsunamiReadIndex], AshStampTexture, LocalDirection, BlastAngle * 0.9f + 0.03f, FLinearColor(Amplitude * 0.5f, 0.0f, 0.0f, 1.0f));
			}
			TsunamiActiveSeconds = TsunamiTailSeconds;
			Population->AddTsunami(LocalDirection, Amplitude * 2.0f);
		}
		else if (!bOcean && (Weapon == 0 || Weapon == 1 || Weapon == 2 || bNuclear))
		{
			// Thermal weapons light the blast rim; the automaton spreads it downwind.
			if (FireRTs.IsValidIndex(FireReadIndex))
			{
				StampInto(FireRTs[FireReadIndex], FireRingStampTexture, LocalDirection, BlastAngle * 1.12f, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
			}
			FireActiveSeconds = FireTailSeconds;
			AshActiveSeconds = FMath::Max(AshActiveSeconds, FireTailSeconds);
		}
	}
	UpdateEnvironment();
	DeformCrater(LocalDirection, Strike);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector WorldNormal = GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
	const FVector SurfacePoint = GetActorLocation() + WorldNormal * PlanetRadius;
	if (AEarthImpactFeedback* Feedback = GetWorld()->SpawnActor<AEarthImpactFeedback>(
		AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(WorldNormal).Rotator(), Params))
	{
		FEarthfallImpactFX FX;
		FX.PlanetCenter = GetActorLocation();
		FX.PlanetRadius = PlanetRadius;
		FX.Normal = WorldNormal;
		FX.WeaponIndex = Strike.WeaponIndex;
		FX.BlastRadius = FMath::Clamp(Strike.Radius, 2.0f, PlanetRadius * 0.9f);
		FX.ShockMaxRadius = ShockMaxRadius;
		FX.ShockSpeed = ShockSpeed;
		FX.YieldMultiplier = Strike.YieldMultiplier;
		Feedback->InitFeedback(FX);
	}
}

// ---------------------------------------------------------------------------
// Tsunami wave field, wildfire automaton and the crater height field
// ---------------------------------------------------------------------------
UTextureRenderTarget2D* AEarthfallPlanet::CreateFloatTarget(const TCHAR* Name, int32 Width, int32 Height, bool bFullFloat)
{
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, Name);
	Target->RenderTargetFormat = bFullFloat ? ETextureRenderTargetFormat::RTF_RGBA32f : ETextureRenderTargetFormat::RTF_RGBA16f;
	Target->ClearColor = FLinearColor::Black;
	Target->bAutoGenerateMips = false;
	Target->AddressX = TA_Wrap;
	Target->AddressY = TA_Clamp;
	Target->Filter = TF_Bilinear;
	Target->InitAutoFormat(Width, Height);
	Target->UpdateResourceImmediate(true);
	return Target;
}

void AEarthfallPlanet::BuildWaveFireAndCraterResources()
{
	// 32-bit float: the wave equation differences neighbouring heights, and with
	// the 0.5 storage bias a half-float target turns that into texel-scale noise.
	TsunamiRTs = {CreateFloatTarget(TEXT("TsunamiRT_0"), SimWidth, SimHeight, true), CreateFloatTarget(TEXT("TsunamiRT_1"), SimWidth, SimHeight, true)};
	// Calm sea is stored as (0.5, 0.5): see tsunami_step.hlsl.
	for (UTextureRenderTarget2D* Target : TsunamiRTs) UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), Target, FLinearColor(0.5f, 0.5f, 0.0f, 1.0f));
	FireRTs = {CreateFloatTarget(TEXT("FireMaskRT_0"), SimWidth, SimHeight), CreateFloatTarget(TEXT("FireMaskRT_1"), SimWidth, SimHeight)};

	CraterRT = NewObject<UTextureRenderTarget2D>(this, TEXT("CraterRT"));
	CraterRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	CraterRT->ClearColor = FLinearColor::Black;
	CraterRT->bAutoGenerateMips = false;
	CraterRT->AddressX = TA_Wrap;
	CraterRT->AddressY = TA_Clamp;
	CraterRT->Filter = TF_Bilinear;
	CraterRT->InitCustomFormat(ImpactMaskWidth, ImpactMaskHeight, PF_B8G8R8A8, true);
	CraterRT->UpdateResourceImmediate(true);

	auto Byte = [](float Value) { return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f)); };
	// Bowl in R; in G a raised rim at 0.8 of the stamp plus fracture rays that
	// streak outward from it. The stamp is drawn 1.25x the blast radius.
	CraterStampTexture = CreateStampTexture([&Byte](float Distance, float Angle)
	{
		const float Bowl = 1.0f - FMath::SmoothStep(0.0f, 0.74f, Distance);
		const float Rim = FMath::Exp(-FMath::Square((Distance - 0.80f) / 0.075f));
		const float Spokes = FMath::Pow(FMath::Abs(FMath::Sin(Angle * 9.0f + FMath::Sin(Angle * 4.0f) * 1.7f)), 10.0f);
		const float Rays = Spokes * FMath::SmoothStep(0.80f, 0.88f, Distance) * (1.0f - FMath::SmoothStep(0.88f, 1.0f, Distance)) * 0.55f;
		return FColor(Byte(Bowl), Byte(FMath::Max(Rim, Rays)), 0, 255);
	}, 512);
	// Fires start as a ragged ring on the thermal blast rim.
	FireRingStampTexture = CreateStampTexture([&Byte](float Distance, float Angle)
	{
		const float Ring = FMath::Exp(-FMath::Square((Distance - 0.84f) / 0.07f));
		const float Ragged = 0.55f + 0.45f * FMath::Sin(Angle * 13.0f + FMath::Sin(Angle * 5.0f) * 2.0f);
		return FColor(Byte(Ring * Ragged), 0, 0, 255);
	});

	TsunamiStepMID = TsunamiStepMaterial ? UMaterialInstanceDynamic::Create(TsunamiStepMaterial, this) : nullptr;
	FireStepMID = FireStepMaterial ? UMaterialInstanceDynamic::Create(FireStepMaterial, this) : nullptr;
	if (SurfaceMID)
	{
		SurfaceMID->SetTextureParameterValue(TEXT("CraterRT"), CraterRT);
		SurfaceMID->SetVectorParameterValue(TEXT("Pom"), FLinearColor(CraterBowlDepth, CraterRimHeight, 0.0f, 0.0f));
	}
	BindSimulationTextures();
}

void AEarthfallPlanet::BindSimulationTextures()
{
	if (!SurfaceMID) return;
	if (TsunamiRTs.IsValidIndex(TsunamiReadIndex)) SurfaceMID->SetTextureParameterValue(TEXT("TsunamiRT"), TsunamiRTs[TsunamiReadIndex]);
	if (FireRTs.IsValidIndex(FireReadIndex)) SurfaceMID->SetTextureParameterValue(TEXT("FireMaskRT"), FireRTs[FireReadIndex]);
}

void AEarthfallPlanet::StampInto(UTextureRenderTarget2D* Target, UTexture2D* Texture, const FVector& LocalDirection, float AngularRadius, const FLinearColor& Tint)
{
	if (!GetWorld() || !Target || !Texture) return;
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(GetWorld(), Target, Canvas, CanvasSize, Context);
	DrawStamp(Canvas, CanvasSize, Texture, LocalDirection, AngularRadius, Tint);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(GetWorld(), Context);
}

void AEarthfallPlanet::StepTsunami()
{
	if (!GetWorld() || TsunamiRTs.Num() != 2 || !TsunamiStepMID) return;
	const int32 WriteIndex = 1 - TsunamiReadIndex;
	TsunamiStepMID->SetTextureParameterValue(TEXT("WaveSource"), TsunamiRTs[TsunamiReadIndex]);
	TsunamiStepMID->SetVectorParameterValue(TEXT("Step"),
		FLinearColor(TsunamiStepSeconds, UEarthfallPopulationSubsystem::TsunamiSpeed, 1.0f / SimWidth, 1.0f / SimHeight));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), TsunamiRTs[WriteIndex], TsunamiStepMID);
	TsunamiReadIndex = WriteIndex;
	BindSimulationTextures();
}

void AEarthfallPlanet::StepFire(float StepSeconds)
{
	if (!GetWorld() || FireRTs.Num() != 2 || !FireStepMID) return;
	const UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
	const float WindTime = Population ? static_cast<float>(Population->GetWindTime()) : GetWorld()->GetTimeSeconds();
	const int32 WriteIndex = 1 - FireReadIndex;
	FireStepMID->SetTextureParameterValue(TEXT("FireSource"), FireRTs[FireReadIndex]);
	FireStepMID->SetVectorParameterValue(TEXT("Step"), FLinearColor(FMath::Min(StepSeconds, 0.2f), WindTime, 1.0f / SimWidth, 1.0f / SimHeight));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), FireRTs[WriteIndex], FireStepMID);
	FireReadIndex = WriteIndex;
	BindSimulationTextures();
}
