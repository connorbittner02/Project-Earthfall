#include "EarthfallAudioComponent.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogEarthfallAudio, Log, All);

UEarthfallAudioComponent::UEarthfallAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAutoActivate = false;
	bAllowSpatialization = false;
	for (int32 Index = 0; Index < MaxBooms; ++Index)
	{
		BoomStrength[Index].store(0.0f);
		BoomDelaySamples[Index].store(-1);
		BoomAge[Index] = -1.0f;
		BoomLevel[Index] = 0.0f;
		BoomPhase[Index] = 0.0;
	}
}

bool UEarthfallAudioComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = static_cast<float>(SampleRate);
	return true;
}

void UEarthfallAudioComponent::TriggerBoom(float Strength, float DelaySeconds)
{
	for (int32 Index = 0; Index < MaxBooms; ++Index)
	{
		int32 Empty = -1;
		// Claim an empty slot; the audio thread sets it back to -1 when it starts the boom.
		if (BoomDelaySamples[Index].compare_exchange_strong(Empty, FMath::Max(0, FMath::RoundToInt32(DelaySeconds * Rate))))
		{
			BoomStrength[Index].store(FMath::Clamp(Strength, 0.05f, 1.0f));
			return;
		}
	}
}

float UEarthfallAudioComponent::Noise()
{
	// xorshift32: cheap, and good enough to be white to the ear.
	NoiseSeed ^= NoiseSeed << 13;
	NoiseSeed ^= NoiseSeed >> 17;
	NoiseSeed ^= NoiseSeed << 5;
	return static_cast<float>(NoiseSeed) * (2.0f / 4294967295.0f) - 1.0f;
}

int32 UEarthfallAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const float TargetDescent = Descent.load();
	const float TargetFirestorm = Firestorm.load();
	const float Ambience = AmbienceLevel.load();
	const float Step = 1.0f / Rate;
	const double TwoPi = 2.0 * PI;

	const int32 Launches = LaunchRequests.load();
	if (Launches != LaunchesSeen)
	{
		LaunchesSeen = Launches;
		LaunchAge = 0.0f;
	}
	const int32 Alerts = AlertRequests.load();
	if (Alerts != AlertsSeen)
	{
		AlertsSeen = Alerts;
		AlertAge = 0.0f;
	}
	// Count pending booms down by this buffer; start the ones that are due.
	for (int32 Index = 0; Index < MaxBooms; ++Index)
	{
		const int32 Delay = BoomDelaySamples[Index].load();
		if (Delay < 0) continue;
		if (Delay <= NumSamples)
		{
			BoomAge[Index] = 0.0f;
			BoomLevel[Index] = BoomStrength[Index].load();
			BoomPhase[Index] = 0.0;
			BoomDelaySamples[Index].store(-1);
		}
		else
		{
			BoomDelaySamples[Index].store(Delay - NumSamples);
		}
	}

	for (int32 Sample = 0; Sample < NumSamples; ++Sample)
	{
		SmoothDescent += (TargetDescent - SmoothDescent) * 0.00005f;
		SmoothFirestorm += (TargetFirestorm - SmoothFirestorm) * 0.00005f;
		// The whole mix fades in over a quarter of a second, so starting the synth never clicks.
		MasterGain = FMath::Min(MasterGain + Step * 4.0f, 1.0f);
		const float WhiteNoise = Noise();
		float Out = 0.0f;

		// --- Orbital drone: three slow-beating low sines over a rumble of
		// heavily filtered noise. It thins out as the air thickens.
		DronePhaseA += TwoPi * 41.0 * Step;
		DronePhaseB += TwoPi * 41.6 * Step;
		DronePhaseC += TwoPi * 61.7 * Step;
		DroneNoise += (WhiteNoise - DroneNoise) * 0.002f;
		const float Drone = static_cast<float>(FMath::Sin(DronePhaseA) + FMath::Sin(DronePhaseB) + 0.5 * FMath::Sin(DronePhaseC)) * 0.055f
			+ DroneNoise * 1.6f;
		Out += Drone * (1.0f - 0.8f * SmoothDescent) * Ambience;

		// --- Wind: band-limited noise whose pitch and level wander with a slow gust.
		WindGust += (Noise() - WindGust) * 0.00002f;
		const float WindCutoff = 0.02f + 0.05f * FMath::Clamp(0.5f + WindGust * 30.0f, 0.0f, 1.0f);
		WindLow += (WhiteNoise - WindLow) * WindCutoff;
		WindBand += (WindLow - WindBand) * 0.004f;
		Out += (WindLow - WindBand) * 0.55f * SmoothDescent * Ambience;

		// --- Firestorm: sparse sharp crackles, more of them the lower and hotter it gets.
		if (Noise() > 1.0f - 0.0009f * SmoothFirestorm * (0.25f + 0.75f * SmoothDescent)) CrackleEnvelope = 1.0f;
		CrackleEnvelope *= 0.9965f;
		Out += WhiteNoise * CrackleEnvelope * CrackleEnvelope * 0.30f * SmoothFirestorm * Ambience;

		// --- Launch: a second and a bit of noise through a filter sweeping upward.
		if (LaunchAge >= 0.0f)
		{
			const float T = LaunchAge / 1.3f;
			const float Sweep = 0.01f + 0.25f * T * T;
			LaunchLow += (WhiteNoise - LaunchLow) * Sweep;
			LaunchBand += (LaunchLow - LaunchBand) * Sweep * 0.25f;
			Out += (LaunchLow - LaunchBand) * FMath::Sin(PI * FMath::Min(T, 1.0f)) * 0.55f;
			LaunchAge += Step;
			if (T >= 1.0f) LaunchAge = -1.0f;
		}

		// --- Boom: a sine falling from 70 Hz to 28 Hz under a thump of dull noise.
		BoomNoise += (WhiteNoise - BoomNoise) * 0.01f;
		for (int32 Index = 0; Index < MaxBooms; ++Index)
		{
			if (BoomAge[Index] < 0.0f) continue;
			const float Age = BoomAge[Index];
			const float Envelope = FMath::Exp(-Age * 1.5f) * FMath::Min(Age * 40.0f, 1.0f);
			BoomPhase[Index] += TwoPi * (28.0 + 42.0 * FMath::Exp(-Age * 5.0f)) * Step;
			Out += (static_cast<float>(FMath::Sin(BoomPhase[Index])) * 0.75f + BoomNoise * 3.0f * FMath::Exp(-Age * 6.0f))
				* Envelope * BoomLevel[Index];
			BoomAge[Index] += Step;
			if (Age > 4.0f) BoomAge[Index] = -1.0f;
		}

		// --- Alert: a burst of radio static, then three two-tone beeps.
		if (AlertAge >= 0.0f)
		{
			AlertNoise += (WhiteNoise - AlertNoise) * 0.35f;
			// 10 ms attack: static that starts at full level is a click. (5 ms failed the self-test.)
			const float Static = (WhiteNoise - AlertNoise) * FMath::Exp(-AlertAge * 5.0f) * 0.22f * FMath::Min(AlertAge * 100.0f, 1.0f);
			const float BeepTime = AlertAge - 0.25f;
			float Tone = 0.0f;
			if (BeepTime >= 0.0f && BeepTime < 1.2f)
			{
				const float InBeep = FMath::Fmod(BeepTime, 0.4f);
				if (InBeep < 0.24f)
				{
					AlertPhase += TwoPi * (InBeep < 0.12f ? 853.0 : 960.0) * Step;
					// 5 ms attack and 10 ms release on every beep.
					Tone = static_cast<float>(FMath::Sin(AlertPhase)) * 0.10f * FMath::Min(InBeep * 200.0f, 1.0f) * FMath::Min((0.24f - InBeep) * 100.0f, 1.0f);
				}
			}
			Out += Static + Tone;
			AlertAge += Step;
			if (AlertAge > 1.6f) AlertAge = -1.0f;
		}

		// tanh saturates smoothly and can never reach the ceiling, however many
		// booms stack up.
		OutAudio[Sample] = OutputCeiling * FMath::Tanh(Out * 1.1f) * MasterGain;
	}
	return NumSamples;
}

bool UEarthfallAudioComponent::RunSelfTest()
{
	constexpr int32 TestRate = 48000;
	constexpr int32 Block = 1024;
	struct FResult
	{
		float Peak = 0.0f;
		double SumSquares = 0.0;
		float Onset = 0.0f;
		float Tail = 0.0f;
		int32 Samples = 0;
		float Rms() const { return Samples > 0 ? FMath::Sqrt(static_cast<float>(SumSquares / Samples)) : 0.0f; }
	};
	// Render Seconds of audio. Onset is the loudest sample in the first millisecond,
	// Tail the loudest in the last 50 ms; RMS is taken over [RmsFrom, RmsTo) seconds.
	auto Render = [](TFunctionRef<void(UEarthfallAudioComponent&)> Setup, float Seconds, float RmsFrom, float RmsTo, bool bSkipFadeIn)
	{
		UEarthfallAudioComponent* Voice = NewObject<UEarthfallAudioComponent>(GetTransientPackage());
		Voice->Rate = static_cast<float>(TestRate);
		if (bSkipFadeIn) Voice->MasterGain = 1.0f;
		Setup(*Voice);
		FResult Result;
		TArray<float> Buffer;
		Buffer.SetNumZeroed(Block);
		const int32 Total = FMath::RoundToInt32(Seconds * TestRate);
		for (int32 Done = 0; Done < Total; Done += Block)
		{
			const int32 Count = FMath::Min(Block, Total - Done);
			Voice->OnGenerateAudio(Buffer.GetData(), Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const int32 At = Done + Index;
				const float Level = FMath::Abs(Buffer[Index]);
				Result.Peak = FMath::Max(Result.Peak, Level);
				if (At < TestRate / 1000) Result.Onset = FMath::Max(Result.Onset, Level);
				if (At >= Total - TestRate / 20) Result.Tail = FMath::Max(Result.Tail, Level);
				if (At >= RmsFrom * TestRate && At < RmsTo * TestRate)
				{
					Result.SumSquares += static_cast<double>(Buffer[Index]) * Buffer[Index];
					++Result.Samples;
				}
			}
		}
		return Result;
	};
	bool bPass = true;
	auto Report = [&bPass](const TCHAR* Name, const FResult& Result, bool bNeedsSilentEdges)
	{
		const bool bCeiling = Result.Peak <= OutputCeiling;
		const bool bEdges = !bNeedsSilentEdges || (Result.Onset < 0.02f && Result.Tail < 0.01f);
		bPass &= bCeiling && bEdges;
		UE_LOG(LogEarthfallAudio, Display, TEXT("EARTHFALL_AUDIOCHECK %-22s peak=%.3f rms=%.3f onset_1ms=%.4f tail_50ms=%.4f  %s"),
			Name, Result.Peak, Result.Rms(), Result.Onset, Result.Tail, (bCeiling && bEdges) ? TEXT("ok") : TEXT("FAIL"));
	};

	// Ambience starts from the master fade-in; it has no tail because it never stops.
	FResult Orbit = Render([](UEarthfallAudioComponent& Voice) { Voice.SetDescent(0.0f); }, 4.0f, 1.0f, 4.0f, false);
	Orbit.Tail = 0.0f;
	Report(TEXT("ambience-orbit"), Orbit, true);
	FResult Low = Render([](UEarthfallAudioComponent& Voice) { Voice.SetDescent(1.0f); Voice.SetFirestorm(1.0f); }, 6.0f, 3.0f, 6.0f, false);
	Low.Tail = 0.0f;
	Report(TEXT("ambience-low-firestorm"), Low, true);

	// Each one-shot voice alone, ambience muted, so its own edges are what is measured.
	const FResult Launch = Render([](UEarthfallAudioComponent& Voice) { Voice.SetAmbienceLevel(0.0f); Voice.TriggerLaunch(); }, 2.0f, 0.0f, 1.3f, true);
	Report(TEXT("launch"), Launch, true);
	const FResult Boom = Render([](UEarthfallAudioComponent& Voice) { Voice.SetAmbienceLevel(0.0f); Voice.TriggerBoom(1.0f, 0.0f); }, 5.0f, 0.0f, 1.0f, true);
	Report(TEXT("boom"), Boom, true);
	const FResult Alert = Render([](UEarthfallAudioComponent& Voice) { Voice.SetAmbienceLevel(0.0f); Voice.TriggerAlert(); }, 2.0f, 0.25f, 1.45f, true);
	Report(TEXT("alert"), Alert, true);

	// Everything at once: low, burning, a launch, six full-strength booms and an alert.
	const FResult Worst = Render([](UEarthfallAudioComponent& Voice)
	{
		Voice.SetDescent(1.0f);
		Voice.SetFirestorm(1.0f);
		Voice.TriggerLaunch();
		Voice.TriggerAlert();
		for (int32 Index = 0; Index < MaxBooms; ++Index) Voice.TriggerBoom(1.0f, 0.0f);
	}, 6.0f, 0.0f, 6.0f, true);
	Report(TEXT("worst-case-mix"), Worst, false);

	const float Balance = 20.0f * FMath::LogX(10.0f, FMath::Max(Boom.Rms(), 1.0e-6f) / FMath::Max(Alert.Rms(), 1.0e-6f));
	const bool bBalanced = Balance >= 6.0f && Balance <= 24.0f;
	bPass &= bBalanced;
	UE_LOG(LogEarthfallAudio, Display, TEXT("EARTHFALL_AUDIOCHECK boom over alert = %.1f dB (wanted 6 to 24)  %s"), Balance, bBalanced ? TEXT("ok") : TEXT("FAIL"));
	UE_LOG(LogEarthfallAudio, Display, TEXT("EARTHFALL_AUDIOCHECK RESULT %s  (ceiling %.2f)"), bPass ? TEXT("PASS") : TEXT("FAIL"), OutputCeiling);
	return bPass;
}
