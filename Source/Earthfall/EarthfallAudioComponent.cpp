#include "EarthfallAudioComponent.h"

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
		const float White = Noise();
		float Out = 0.0f;

		// --- Orbital drone: three slow-beating low sines over a rumble of
		// heavily filtered noise. It thins out as the air thickens.
		DronePhaseA += TwoPi * 41.0 * Step;
		DronePhaseB += TwoPi * 41.6 * Step;
		DronePhaseC += TwoPi * 61.7 * Step;
		DroneNoise += (White - DroneNoise) * 0.002f;
		const float Drone = static_cast<float>(FMath::Sin(DronePhaseA) + FMath::Sin(DronePhaseB) + 0.5 * FMath::Sin(DronePhaseC)) * 0.055f
			+ DroneNoise * 1.6f;
		Out += Drone * (1.0f - 0.8f * SmoothDescent);

		// --- Wind: band-limited noise whose pitch and level wander with a slow gust.
		WindGust += (Noise() - WindGust) * 0.00002f;
		const float WindCutoff = 0.02f + 0.05f * FMath::Clamp(0.5f + WindGust * 30.0f, 0.0f, 1.0f);
		WindLow += (White - WindLow) * WindCutoff;
		WindBand += (WindLow - WindBand) * 0.004f;
		Out += (WindLow - WindBand) * 0.55f * SmoothDescent;

		// --- Firestorm: sparse sharp crackles, more of them the lower and hotter it gets.
		if (Noise() > 1.0f - 0.0009f * SmoothFirestorm * (0.25f + 0.75f * SmoothDescent)) CrackleEnvelope = 1.0f;
		CrackleEnvelope *= 0.9965f;
		Out += White * CrackleEnvelope * CrackleEnvelope * 0.30f * SmoothFirestorm;

		// --- Launch: a second and a bit of noise through a filter sweeping upward.
		if (LaunchAge >= 0.0f)
		{
			const float T = LaunchAge / 1.3f;
			const float Sweep = 0.01f + 0.25f * T * T;
			LaunchLow += (White - LaunchLow) * Sweep;
			LaunchBand += (LaunchLow - LaunchBand) * Sweep * 0.25f;
			Out += (LaunchLow - LaunchBand) * FMath::Sin(PI * FMath::Min(T, 1.0f)) * 0.55f;
			LaunchAge += Step;
			if (T >= 1.0f) LaunchAge = -1.0f;
		}

		// --- Boom: a sine falling from 70 Hz to 28 Hz under a thump of dull noise.
		BoomNoise += (White - BoomNoise) * 0.01f;
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
			AlertNoise += (White - AlertNoise) * 0.35f;
			const float Static = (White - AlertNoise) * FMath::Exp(-AlertAge * 5.0f) * 0.22f;
			const float BeepTime = AlertAge - 0.25f;
			float Tone = 0.0f;
			if (BeepTime >= 0.0f && BeepTime < 1.2f)
			{
				const float InBeep = FMath::Fmod(BeepTime, 0.4f);
				if (InBeep < 0.24f)
				{
					AlertPhase += TwoPi * (InBeep < 0.12f ? 853.0 : 960.0) * Step;
					Tone = static_cast<float>(FMath::Sin(AlertPhase)) * 0.10f * FMath::Min(InBeep * 200.0f, 1.0f);
				}
			}
			Out += Static + Tone;
			AlertAge += Step;
			if (AlertAge > 1.6f) AlertAge = -1.0f;
		}

		// A soft clip keeps stacked booms from wrapping.
		OutAudio[Sample] = FMath::Clamp(Out * 0.8f, -1.0f, 1.0f) * (1.5f - 0.5f * FMath::Abs(FMath::Clamp(Out * 0.8f, -1.0f, 1.0f)));
	}
	return NumSamples;
}
