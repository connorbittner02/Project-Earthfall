#include "EarthImpactFeedback.h"
#include "EarthfallTypes.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 RingSegments = 96;
constexpr float FeedbackSphereRadius = 50.0f;
/** The ring rides just above the high cloud deck so it reads over weather. */
constexpr float RingAltitudeScale = 1.024f;
/** Effects draw after the planet's cloud and atmosphere shells (priorities 10-12). */
constexpr int32 FeedbackSortPriority = 20;
/** How long the black-body fireball and its soot ball last before the plume has taken over. */
constexpr float ThermalSeconds = 4.8f;
}

AEarthImpactFeedback::AEarthImpactFeedback()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Earthfall/Materials/M_FXGlow.M_FXGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ring(TEXT("/Game/Earthfall/Materials/M_FXRing.M_FXRing"));
	GlowMaterial = Glow.Succeeded() ? Glow.Object : nullptr;
	RingMaterial = Ring.Succeeded() ? Ring.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FireballFinder(TEXT("/Game/Earthfall/Materials/M_FXFireball.M_FXFireball"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WilsonFinder(TEXT("/Game/Earthfall/Materials/M_FXWilson.M_FXWilson"));
	FireballMaterial = FireballFinder.Succeeded() ? FireballFinder.Object : nullptr;
	WilsonMaterial = WilsonFinder.Succeeded() ? WilsonFinder.Object : nullptr;

	auto MakeGlowSphere = [this, Scene](const TCHAR* Name)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Scene);
		Component->SetMobility(EComponentMobility::Movable);
		if (Sphere.Succeeded()) Component->SetStaticMesh(Sphere.Object);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->SetTranslucentSortPriority(FeedbackSortPriority);
		Component->SetVisibility(false);
		return Component;
	};
	Fireball = MakeGlowSphere(TEXT("Fireball"));
	RisingCap = MakeGlowSphere(TEXT("RisingCap"));
	WilsonDome = MakeGlowSphere(TEXT("WilsonDome"));
	GroundFlash = MakeGlowSphere(TEXT("GroundFlash"));

	ShockRing = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShockRing"));
	ShockRing->SetupAttachment(Scene);
	// The band is rebuilt in world space every frame, so it ignores the actor transform.
	ShockRing->SetUsingAbsoluteLocation(true);
	ShockRing->SetUsingAbsoluteRotation(true);
	ShockRing->SetUsingAbsoluteScale(true);
	ShockRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShockRing->SetGenerateOverlapEvents(false);
	ShockRing->SetCastShadow(false);
	ShockRing->SetTranslucentSortPriority(FeedbackSortPriority);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallImpact.NS_EarthfallImpact"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SmokeSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallSmoke.NS_EarthfallSmoke"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> NukePlumeSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallNukePlume.NS_EarthfallNukePlume"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> NukeWaveSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallNukeShockwave.NS_EarthfallNukeShockwave"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> PlasmaBeamSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallPlasmaBeam.NS_EarthfallPlasmaBeam"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> PlasmaSparksSystem(TEXT("/Game/Earthfall/FX/NS_EarthfallPlasmaSparks.NS_EarthfallPlasmaSparks"));
	auto MakeNiagara = [this, Scene](const TCHAR* Name, UNiagaraSystem* System)
	{
		UNiagaraComponent* Component = CreateDefaultSubobject<UNiagaraComponent>(FName(Name));
		Component->SetupAttachment(Scene);
		Component->SetAsset(System);
		Component->SetAutoActivate(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetTranslucentSortPriority(FeedbackSortPriority);
		return Component;
	};
	ImpactFireballFX = MakeNiagara(TEXT("ImpactFireballFX"), ImpactSystem.Succeeded() ? ImpactSystem.Object : nullptr);
	SmokePlumeFX = MakeNiagara(TEXT("SmokePlumeFX"), SmokeSystem.Succeeded() ? SmokeSystem.Object : nullptr);
	NukePlumeFX = MakeNiagara(TEXT("NukePlumeFX"), NukePlumeSystem.Succeeded() ? NukePlumeSystem.Object : nullptr);
	NukeShockwaveFX = MakeNiagara(TEXT("NukeShockwaveFX"), NukeWaveSystem.Succeeded() ? NukeWaveSystem.Object : nullptr);
	PlasmaBeamFX = MakeNiagara(TEXT("PlasmaBeamFX"), PlasmaBeamSystem.Succeeded() ? PlasmaBeamSystem.Object : nullptr);
	PlasmaSparksFX = MakeNiagara(TEXT("PlasmaSparksFX"), PlasmaSparksSystem.Succeeded() ? PlasmaSparksSystem.Object : nullptr);
}

void AEarthImpactFeedback::InitFeedback(const FEarthfallImpactFX& InParams)
{
	Params = InParams;
	Params.WeaponIndex = FMath::Clamp(Params.WeaponIndex, 0, EarthfallWeapons::Count - 1);
	Accent = EarthfallWeapons::Get(Params.WeaponIndex).Color;
	bNuclear = Params.WeaponIndex == EarthfallWeapons::Nuclear;
	bImplosion = Params.WeaponIndex == 3;
	bThermal = FireballMaterial != nullptr && (Params.WeaponIndex == 0 || Params.WeaponIndex == 2 || bNuclear);
	Age = 0.0f;

	static const float FireballDurations[] = {1.1f, 1.3f, 2.4f, 1.8f, 1.5f, 5.5f};
	const float YieldLog = FMath::Max(0.0f, FMath::Log2(FMath::Max(Params.YieldMultiplier, 1.0f)));
	FireballSeconds = FireballDurations[Params.WeaponIndex] * (1.0f + 0.12f * YieldLog);
	FireballRadius = FMath::Min(Params.BlastRadius * (bNuclear ? 0.42f : 0.40f), Params.PlanetRadius * 0.45f);
	const float ShockSeconds = Params.ShockSpeed > KINDA_SMALL_NUMBER ? Params.ShockMaxRadius / Params.ShockSpeed : 0.0f;
	Lifetime = FMath::Max(FireballSeconds * (bNuclear ? 1.4f : 1.15f), ShockSeconds) + 0.25f;
	if (bThermal) Lifetime = FMath::Max(Lifetime, ThermalSeconds + 0.3f);
	SetLifeSpan(Lifetime + 0.5f);

	if (bThermal)
	{
		// Heat = (kelvin, brightness, soot, seed); Shape = (noise scale, turbulence, churn, edge exponent), as in fireball.hlsl.
		const float Seed = FMath::FRandRange(0.0f, 40.0f);
		FireballMID = UMaterialInstanceDynamic::Create(FireballMaterial, this);
		FireballMID->SetVectorParameterValue(TEXT("Heat"), FLinearColor(6500.0f, 12.0f, 0.0f, Seed));
		CapMID = UMaterialInstanceDynamic::Create(FireballMaterial, this);
		CapMID->SetVectorParameterValue(TEXT("Heat"), FLinearColor(2600.0f, 0.0f, 0.0f, Seed + 13.0f));
		Fireball->SetMaterial(0, FireballMID);
		RisingCap->SetMaterial(0, CapMID);
		Fireball->SetVisibility(true);
		RisingCap->SetVisibility(false);
		if (WilsonMaterial)
		{
			WilsonMID = UMaterialInstanceDynamic::Create(WilsonMaterial, this);
			WilsonDome->SetMaterial(0, WilsonMID);
		}
		if (GlowMaterial)
		{
			FlashMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
			FlashMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.6f, 1.5f, 2.0f, 0.0f));
			GroundFlash->SetMaterial(0, FlashMID);
		}
	}
	else if (GlowMaterial)
	{
		FireballMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		FireballMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(2.2f, 0.30f, 3.2f, 1.0f));
		CapMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		CapMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(2.2f, 0.0f, 2.4f, 1.0f));
		Fireball->SetMaterial(0, FireballMID);
		RisingCap->SetMaterial(0, CapMID);
		Fireball->SetVisibility(true);
		RisingCap->SetVisibility(bNuclear || Params.WeaponIndex == 2);
	}
	if (RingMaterial)
	{
		RingMID = UMaterialInstanceDynamic::Create(RingMaterial, this);
		BuildRingTopology();
		ShockRing->SetMaterial(0, RingMID);
	}

	// The bundled Niagara template copies are wired up but off by default: they are
	// authored at metre scale with fixed-size sprites, and on a 100-unit planet
	// they render as flat slabs. The fireball, cap and ring above carry the hit.
	if (!bUseNiagaraTemplates)
	{
		bInitialised = true;
		UpdateFireball();
		UpdateShockRing();
		return;
	}
	// The stock templates are authored at metre scale; on a 100-unit planet they
	// must be tiny or their sprites read as slabs.
	const float Scale = FMath::Clamp(Params.BlastRadius / 15.0f, 0.35f, 4.0f) * 0.035f;
	if (bNuclear)
	{
		ImpactFireballFX->SetRelativeScale3D(FVector(Scale * 3.5f));
		NukePlumeFX->SetRelativeLocation(FVector(0.0f, 0.0f, 8.0f));
		NukePlumeFX->SetRelativeScale3D(FVector(Scale * 2.6f, Scale * 2.6f, Scale * 5.0f));
		NukeShockwaveFX->SetRelativeScale3D(FVector(Scale * 8.0f));
		ImpactFireballFX->Activate(true);
	}
	else if (Params.WeaponIndex == 1)
	{
		PlasmaSparksFX->SetRelativeScale3D(FVector(Scale * 1.5f));
		PlasmaSparksFX->Activate(true);
		SmokePlumeFX->SetRelativeScale3D(FVector(Scale * 1.4f));
	}
	else
	{
		ImpactFireballFX->SetRelativeScale3D(FVector(Scale * (bImplosion ? 1.2f : 1.0f)));
		SmokePlumeFX->SetRelativeScale3D(FVector(Scale));
		ImpactFireballFX->Activate(true);
	}

	bInitialised = true;
	UpdateFireball();
	UpdateShockRing();
}

void AEarthImpactFeedback::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialised) return;
	Age += DeltaSeconds;
	UpdateFireball();
	UpdateShockRing();
}

void AEarthImpactFeedback::UpdateDetonation()
{
	const FVector GroundZero = Params.PlanetCenter + Params.Normal * Params.PlanetRadius;
	const float YieldGain = FMath::Pow(FMath::Max(Params.YieldMultiplier, 0.01f), 0.15f) * (bNuclear ? 1.35f : 1.0f);
	const float T = Age;

	// --- Stage 1, 0 to 0.25 s. A bomb flashes twice: the first spike is the bare
	// fireball, which the shock front then hides for a moment before it thins
	// and the second, longer maximum shines through.
	const float Flash = 5.0f * FMath::Exp(-T / 0.022f) + 3.2f * FMath::Exp(-FMath::Square((T - 0.135f) / 0.050f));
	if (FlashMID)
	{
		// The flash on the ground: a wide, flat sheet of light that is gone in a third of a second.
		const float Ground = (1.6f * FMath::Exp(-T / 0.07f) + 0.25f * Flash) * YieldGain;
		GroundFlash->SetVisibility(T < 0.45f);
		GroundFlash->SetRelativeLocation(FVector(0.0f, 0.0f, FireballRadius * 0.10f));
		GroundFlash->SetRelativeScale3D(FVector(FireballRadius * 2.7f, FireballRadius * 2.7f, FireballRadius * 0.42f) / FeedbackSphereRadius);
		FlashMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 0.96f, 0.88f, Ground));
	}
	if (WilsonMID)
	{
		// The Wilson dome: fog condensing in the low pressure behind the shock,
		// a crisp shell that races outward, tears and is gone within a second.
		const float Grow = 1.0f - FMath::Exp(-T * 3.4f);
		const float Radius = FireballRadius * (0.55f + 2.3f * Grow);
		const float Shown = FMath::SmoothStep(0.03f, 0.09f, T) * (1.0f - FMath::SmoothStep(0.40f, 1.05f, T));
		WilsonDome->SetVisibility(Shown > 0.002f);
		WilsonDome->SetRelativeLocation(FVector::ZeroVector);
		WilsonDome->SetRelativeScale3D(FVector(Radius, Radius, Radius * 0.80f) / FeedbackSphereRadius);
		WilsonMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.86f, 0.93f, 1.0f, 1.1f * Shown * YieldGain));
		// Shape = (limb sharpness, tearing, noise scale, unused), as in fx.hlsl.
		WilsonMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(2.6f, FMath::SmoothStep(0.22f, 0.95f, T), 3.4f, 0.0f));
	}

	// --- Stage 2, 0.25 to 4 s. The fireball is a black body. Its core cools from
	// 4000 K (white-yellow) through 2200 K (orange) to 1100 K (deep red), and the
	// soot jacket closes over it as it does.
	float Kelvin = 6500.0f;
	if (T >= 0.25f)
	{
		Kelvin = T < 1.5f ? FMath::Lerp(4000.0f, 2200.0f, (T - 0.25f) / 1.25f)
			: FMath::Lerp(2200.0f, 1100.0f, FMath::Clamp((T - 1.5f) / 2.5f, 0.0f, 1.0f));
	}
	const float Soot = FMath::SmoothStep(0.9f, 3.4f, T);
	const float Brightness = (T < 0.25f ? 2.2f + Flash : FMath::Lerp(4.2f, 2.2f, FMath::Clamp((T - 0.25f) / 3.75f, 0.0f, 1.0f))) * YieldGain;
	const float Swell = 0.30f + 0.70f * (1.0f - FMath::Exp(-T * 7.0f));
	const float Rise = FMath::Clamp(T / ThermalSeconds, 0.0f, 1.0f);
	// Past 4 s the soot ball thins from its rim inward and hands over to the plume.
	const float Thin = FMath::SmoothStep(4.0f, ThermalSeconds, T);
	const float Radius = FireballRadius * Swell * (1.0f + 0.30f * Rise);
	Fireball->SetVisibility(Thin < 0.995f);
	Fireball->SetWorldLocation(GroundZero + Params.Normal * FireballRadius * (0.15f + 0.55f * Rise * Rise));
	Fireball->SetWorldScale3D(FVector(FMath::Max(Radius, 0.01f) / FeedbackSphereRadius));
	if (FireballMID)
	{
		const float Seed = FireballMID->K2_GetVectorParameterValue(TEXT("Heat")).A;
		FireballMID->SetVectorParameterValue(TEXT("Heat"), FLinearColor(Kelvin, Brightness, Soot, Seed));
		// Turbulence comes in as the surface breaks up; the churn slows as it cools.
		FireballMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(2.6f, FMath::SmoothStep(0.10f, 0.9f, T), 0.75f - 0.4f * Rise, 1.2f + 7.0f * Thin));
	}

	// --- Stage 3, from 2 s. The hottest gas lifts off as the mushroom's cap: a
	// second, cooler black body that climbs, rolls and soots over while the plume
	// actor raises the stem beneath it and spreads the anvil above.
	const float CapAge = T - 1.6f;
	RisingCap->SetVisibility(CapAge > 0.0f && Thin < 0.995f);
	if (CapMID && CapAge > 0.0f)
	{
		const float U = FMath::Clamp(CapAge / (ThermalSeconds - 1.6f), 0.0f, 1.0f);
		const float CapRadius = FireballRadius * FMath::Lerp(0.55f, 1.05f, U);
		RisingCap->SetWorldLocation(GroundZero + Params.Normal * FireballRadius * FMath::Lerp(0.6f, 2.0f, FMath::Sqrt(U)));
		RisingCap->SetWorldScale3D(FVector(CapRadius, CapRadius, CapRadius * 0.72f) / FeedbackSphereRadius);
		const float Seed = CapMID->K2_GetVectorParameterValue(TEXT("Heat")).A;
		CapMID->SetVectorParameterValue(TEXT("Heat"), FLinearColor(FMath::Lerp(2500.0f, 1050.0f, U), 3.5f * YieldGain * FMath::SmoothStep(0.0f, 0.25f, CapAge), FMath::SmoothStep(0.15f, 0.85f, U), Seed));
		CapMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(3.4f, 1.0f, 0.55f, 1.1f + 7.0f * Thin));
	}
}

void AEarthImpactFeedback::UpdateFireball()
{
	if (bThermal)
	{
		UpdateDetonation();
		return;
	}
	const FVector GroundZero = Params.PlanetCenter + Params.Normal * Params.PlanetRadius;
	const float YieldGain = FMath::Pow(FMath::Max(Params.YieldMultiplier, 0.01f), 0.15f);

	// Each weapon cools from white-hot through its own mid tone to a dying ember.
	static const FLinearColor MidTones[] = {
		FLinearColor(1.00f, 0.50f, 0.12f), FLinearColor(0.20f, 0.80f, 1.00f), FLinearColor(1.00f, 0.36f, 0.06f),
		FLinearColor(0.62f, 0.22f, 1.00f), FLinearColor(0.55f, 0.90f, 1.00f), FLinearColor(1.00f, 0.52f, 0.14f)};
	static const FLinearColor EndTones[] = {
		FLinearColor(0.80f, 0.10f, 0.02f), FLinearColor(0.04f, 0.20f, 0.70f), FLinearColor(0.80f, 0.08f, 0.01f),
		FLinearColor(0.22f, 0.02f, 0.45f), FLinearColor(0.30f, 0.55f, 0.90f), FLinearColor(0.75f, 0.10f, 0.02f)};

	if (FireballMID)
	{
		const float U = FMath::Clamp(Age / FireballSeconds, 0.0f, 1.0f);
		const float Heat = 1.0f - U;
		const float Radius = bImplosion
			? FireballRadius * FMath::Sqrt(FMath::Max(Heat, 0.0f)) * (0.35f + 0.65f * (1.0f - FMath::Exp(-Age * 14.0f)))
			: FireballRadius * (0.25f + 0.75f * (1.0f - FMath::Exp(-Age * 9.0f))) * (1.0f + 0.25f * U);
		const FLinearColor HotWhite(1.0f, 0.95f, 0.85f);
		const FLinearColor Tone = Heat > 0.7f
			? FMath::Lerp(MidTones[Params.WeaponIndex], HotWhite, (Heat - 0.7f) / 0.3f)
			: FMath::Lerp(EndTones[Params.WeaponIndex], MidTones[Params.WeaponIndex], Heat / 0.7f);
		const float Intensity = (bNuclear ? 6.0f : 3.2f) * YieldGain * FMath::Pow(Heat, 1.5f);
		Fireball->SetWorldLocation(GroundZero + Params.Normal * FireballRadius * 0.25f * U);
		Fireball->SetWorldScale3D(FVector(FMath::Max(Radius, 0.01f) / FeedbackSphereRadius));
		FireballMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tone.R, Tone.G, Tone.B, Intensity));
		Fireball->SetVisibility(Heat > 0.001f);
	}

	if (CapMID && RisingCap->IsVisible())
	{
		// The cap lifts off the fireball and climbs through the cloud deck.
		const float CapSeconds = FireballSeconds * (bNuclear ? 1.4f : 1.15f);
		const float U = FMath::Clamp((Age - 0.25f) / FMath::Max(CapSeconds - 0.25f, 0.1f), 0.0f, 1.0f);
		const float Radius = FireballRadius * FMath::Lerp(0.50f, 0.90f, U);
		const FLinearColor Tone = FMath::Lerp(FLinearColor(1.0f, 0.55f, 0.16f), FLinearColor(0.42f, 0.12f, 0.05f), U);
		const float Intensity = Age < 0.25f ? 0.0f : 1.6f * YieldGain * FMath::Square(1.0f - U);
		RisingCap->SetWorldLocation(GroundZero + Params.Normal * FireballRadius * FMath::Lerp(0.3f, 1.7f, FMath::Sqrt(U)));
		RisingCap->SetWorldScale3D(FVector(Radius / FeedbackSphereRadius));
		CapMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tone.R, Tone.G, Tone.B, Intensity));
	}
}

void AEarthImpactFeedback::BuildRingTopology()
{
	TArray<FVector> Vertices;
	TArray<FVector> RingNormals;
	TArray<FVector2D> RingUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> RingTangents;
	TArray<int32> Indices;
	const int32 VertexCount = (RingSegments + 1) * 2;
	Vertices.Init(Params.PlanetCenter + Params.Normal * Params.PlanetRadius, VertexCount);
	RingNormals.Init(Params.Normal, VertexCount);
	Colors.Init(FLinearColor::White, VertexCount);
	RingTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);
	RingUVs.SetNum(VertexCount);
	for (int32 Segment = 0; Segment <= RingSegments; ++Segment)
	{
		const float U = static_cast<float>(Segment) / RingSegments;
		RingUVs[Segment * 2] = FVector2D(U, 0.0f);     // trailing edge
		RingUVs[Segment * 2 + 1] = FVector2D(U, 1.0f); // leading edge
	}
	for (int32 Segment = 0; Segment < RingSegments; ++Segment)
	{
		const int32 A = Segment * 2;
		Indices.Append({A, A + 1, A + 2, A + 2, A + 1, A + 3});
	}
	ShockRing->SetWorldTransform(FTransform::Identity);
	ShockRing->CreateMeshSection_LinearColor(0, Vertices, Indices, RingNormals, RingUVs, Colors, RingTangents, false);
}

void AEarthImpactFeedback::UpdateShockRing()
{
	if (!RingMID || !ShockRing->GetProcMeshSection(0)) return;
	const float Front = FMath::Min(Params.ShockSpeed * Age, Params.ShockMaxRadius);
	const float Progress = Params.ShockMaxRadius > KINDA_SMALL_NUMBER ? Front / Params.ShockMaxRadius : 1.0f;
	const float Fade = FMath::Pow(FMath::Max(1.0f - Progress, 0.0f), 0.6f);
	if (Fade <= 0.001f)
	{
		ShockRing->SetVisibility(false);
		return;
	}

	const float Width = 0.012f + Front * 0.07f;
	const float Trailing = FMath::Max(Front - Width, 0.0f);
	FVector TangentA;
	FVector TangentB;
	Params.Normal.FindBestAxisVectors(TangentA, TangentB);
	const float Altitude = Params.PlanetRadius * RingAltitudeScale;

	const int32 VertexCount = (RingSegments + 1) * 2;
	TArray<FVector> Vertices;
	TArray<FVector> RingNormals;
	TArray<FVector2D> RingUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> RingTangents;
	Vertices.SetNumUninitialized(VertexCount);
	RingNormals.SetNumUninitialized(VertexCount);
	RingUVs.SetNumUninitialized(VertexCount);
	Colors.Init(FLinearColor::White, VertexCount);
	RingTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);
	for (int32 Segment = 0; Segment <= RingSegments; ++Segment)
	{
		const float U = static_cast<float>(Segment) / RingSegments;
		const float Azimuth = U * 2.0f * PI;
		const FVector Radial = TangentA * FMath::Cos(Azimuth) + TangentB * FMath::Sin(Azimuth);
		const FVector Inner = Params.Normal * FMath::Cos(Trailing) + Radial * FMath::Sin(Trailing);
		const FVector Outer = Params.Normal * FMath::Cos(Front) + Radial * FMath::Sin(Front);
		Vertices[Segment * 2] = Params.PlanetCenter + Inner * Altitude;
		Vertices[Segment * 2 + 1] = Params.PlanetCenter + Outer * Altitude;
		RingNormals[Segment * 2] = Inner;
		RingNormals[Segment * 2 + 1] = Outer;
		RingUVs[Segment * 2] = FVector2D(U, 0.0f);
		RingUVs[Segment * 2 + 1] = FVector2D(U, 1.0f);
	}
	ShockRing->UpdateMeshSection_LinearColor(0, Vertices, RingNormals, RingUVs, Colors, RingTangents);
	ShockRing->SetVisibility(true);

	const FLinearColor Tone = bNuclear ? FLinearColor(1.0f, 0.90f, 0.76f) : FMath::Lerp(FLinearColor(1.0f, 0.92f, 0.80f), Accent, 0.45f);
	const float Intensity = (bNuclear ? 1.9f : 1.3f) * Fade;
	RingMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tone.R, Tone.G, Tone.B, Intensity));
}
