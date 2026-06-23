// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Anim/DPWidgetAnimDriver.h"
#include "Pool/DPWidgetPoolSubsystem.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "Curves/CurveFloat.h"

/** Smallest duration we will divide by — keeps a zero-duration step instantaneous, not NaN. */
namespace
{
	constexpr float GMinTweenDuration = 1.0e-4f;
}

UDP_WidgetAnimDriver* UDP_WidgetAnimDriver::CreateFor(UWidget* Target)
{
	if (!Target)
	{
		UE_LOG(LogDP, Warning, TEXT("[AnimDriver] CreateFor called with a null target."));
		return nullptr;
	}

	// Outer to the target so the driver lives in a sensible package; it additionally self-roots
	// while playing so it is never GC'd mid-tween regardless of outer reachability.
	UDP_WidgetAnimDriver* Driver = NewObject<UDP_WidgetAnimDriver>(Target);
	Driver->WidgetTarget = Target;
	return Driver;
}

void UDP_WidgetAnimDriver::Fade(float From, float To, float Duration, UCurveFloat* Ease)
{
	FDP_WidgetTweenStep Step;
	Step.Channel = EDP_WidgetTweenChannel::Opacity;
	Step.From = FVector4(From, 0, 0, 0);
	Step.To = FVector4(To, 0, 0, 0);
	Step.DurationSeconds = Duration;
	Step.EaseCurve = Ease;
	PlaySequence({ Step });
}

void UDP_WidgetAnimDriver::Slide(FVector2D From, FVector2D To, float Duration, UCurveFloat* Ease)
{
	FDP_WidgetTweenStep Step;
	Step.Channel = EDP_WidgetTweenChannel::Translation;
	Step.From = FVector4(From.X, From.Y, 0, 0);
	Step.To = FVector4(To.X, To.Y, 0, 0);
	Step.DurationSeconds = Duration;
	Step.EaseCurve = Ease;
	PlaySequence({ Step });
}

void UDP_WidgetAnimDriver::Scale(FVector2D From, FVector2D To, float Duration, UCurveFloat* Ease)
{
	FDP_WidgetTweenStep Step;
	Step.Channel = EDP_WidgetTweenChannel::Scale;
	Step.From = FVector4(From.X, From.Y, 0, 0);
	Step.To = FVector4(To.X, To.Y, 0, 0);
	Step.DurationSeconds = Duration;
	Step.EaseCurve = Ease;
	PlaySequence({ Step });
}

void UDP_WidgetAnimDriver::PlaySequence(const TArray<FDP_WidgetTweenStep>& InSteps)
{
	// Stop any in-flight play first (no snap, no finished-fully) before swapping in the new steps.
	if (bPlaying)
	{
		TeardownTicker();
		bPlaying = false;
	}

	Steps = InSteps;
	CurrentStep = 0;
	StepElapsed = 0.0f;

	if (Steps.Num() == 0 || !WidgetTarget.IsValid())
	{
		FinishPlay(/*bCompletedFully*/ Steps.Num() == 0);
		return;
	}

	StartPlay();
}

void UDP_WidgetAnimDriver::StartPlay()
{
	bPlaying = true;

	if (!bRooted)
	{
		AddToRoot();
		bRooted = true;
	}

	// Register a per-frame ticker. FTSTicker is editor-safe and gives a real frame delta.
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UDP_WidgetAnimDriver::TickDriver));
	}
}

bool UDP_WidgetAnimDriver::TickDriver(float DeltaTime)
{
	// Target gone (widget GC'd / removed) — stop cleanly. Returning false also removes the ticker,
	// but we route through FinishPlay so OnFinished + un-root still happen.
	UWidget* Target = WidgetTarget.Get();
	if (!Target || !bPlaying || !Steps.IsValidIndex(CurrentStep))
	{
		FinishPlay(/*bCompletedFully*/ false);
		return false;
	}

	const FDP_WidgetTweenStep& Step = Steps[CurrentStep];
	StepElapsed += DeltaTime;

	// Honour an optional start delay before the step begins interpolating.
	const float ActiveElapsed = StepElapsed - Step.StartDelaySeconds;
	if (ActiveElapsed < 0.0f)
	{
		// Still in the delay window — hold the start value so there is no visible pop.
		ApplyStep(Step, 0.0f);
		return true;
	}

	const float Duration = FMath::Max(Step.DurationSeconds, GMinTweenDuration);
	const float RawAlpha = FMath::Clamp(ActiveElapsed / Duration, 0.0f, 1.0f);
	ApplyStep(Step, RawAlpha);

	if (RawAlpha >= 1.0f)
	{
		// Advance to the next step, carrying any overshoot time so long frames don't stall.
		++CurrentStep;
		StepElapsed = 0.0f;

		if (!Steps.IsValidIndex(CurrentStep))
		{
			FinishPlay(/*bCompletedFully*/ true);
			return false;
		}
	}

	return true; // keep ticking
}

void UDP_WidgetAnimDriver::ApplyStep(const FDP_WidgetTweenStep& Step, float Alpha) const
{
	UWidget* Target = WidgetTarget.Get();
	if (!Target)
	{
		return;
	}

	const float Eased = SampleEase(Step.EaseCurve, Alpha);

	switch (Step.Channel)
	{
	case EDP_WidgetTweenChannel::Opacity:
	{
		const float Value = FMath::Lerp(static_cast<float>(Step.From.X), static_cast<float>(Step.To.X), Eased);
		Target->SetRenderOpacity(Value);
		break;
	}
	case EDP_WidgetTweenChannel::Translation:
	{
		FWidgetTransform Xf = Target->GetRenderTransform();
		Xf.Translation = FVector2D(
			FMath::Lerp(static_cast<float>(Step.From.X), static_cast<float>(Step.To.X), Eased),
			FMath::Lerp(static_cast<float>(Step.From.Y), static_cast<float>(Step.To.Y), Eased));
		Target->SetRenderTransform(Xf);
		break;
	}
	case EDP_WidgetTweenChannel::Scale:
	{
		FWidgetTransform Xf = Target->GetRenderTransform();
		Xf.Scale = FVector2D(
			FMath::Lerp(static_cast<float>(Step.From.X), static_cast<float>(Step.To.X), Eased),
			FMath::Lerp(static_cast<float>(Step.From.Y), static_cast<float>(Step.To.Y), Eased));
		Target->SetRenderTransform(Xf);
		break;
	}
	case EDP_WidgetTweenChannel::Angle:
	{
		FWidgetTransform Xf = Target->GetRenderTransform();
		Xf.Angle = FMath::Lerp(static_cast<float>(Step.From.X), static_cast<float>(Step.To.X), Eased);
		Target->SetRenderTransform(Xf);
		break;
	}
	case EDP_WidgetTweenChannel::ColorAndOpacity:
	{
		if (UUserWidget* UserWidget = Cast<UUserWidget>(Target))
		{
			const FLinearColor Color(
				FMath::Lerp(static_cast<float>(Step.From.X), static_cast<float>(Step.To.X), Eased),
				FMath::Lerp(static_cast<float>(Step.From.Y), static_cast<float>(Step.To.Y), Eased),
				FMath::Lerp(static_cast<float>(Step.From.Z), static_cast<float>(Step.To.Z), Eased),
				FMath::Lerp(static_cast<float>(Step.From.W), static_cast<float>(Step.To.W), Eased));
			UserWidget->SetColorAndOpacity(Color);
		}
		break;
	}
	default:
		break;
	}
}

float UDP_WidgetAnimDriver::SampleEase(const UCurveFloat* Ease, float Alpha)
{
	if (Ease)
	{
		return Ease->GetFloatValue(Alpha);
	}
	return Alpha; // linear fallback
}

void UDP_WidgetAnimDriver::Stop(bool bSnapToEnd)
{
	if (!bPlaying)
	{
		return;
	}

	if (bSnapToEnd && Steps.Num() > 0)
	{
		// Snap to the final value of the last step.
		ApplyStep(Steps.Last(), 1.0f);
	}

	FinishPlay(/*bCompletedFully*/ false);
}

void UDP_WidgetAnimDriver::FinishPlay(bool bCompletedFully)
{
	const bool bWasPlaying = bPlaying;
	bPlaying = false;
	TeardownTicker();

	// Optionally recycle the widget back to the pool (fade-out-then-release pattern).
	if (bCompletedFully && bReleaseToPoolOnFinish)
	{
		if (UUserWidget* UserWidget = Cast<UUserWidget>(WidgetTarget.Get()))
		{
			if (UDP_WidgetPoolSubsystem* Pool =
				FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_WidgetPoolSubsystem>(UserWidget))
			{
				Pool->ReleaseWidget(UserWidget);
			}
		}
	}

	if (bWasPlaying)
	{
		OnFinished.Broadcast(bCompletedFully);
	}

	// Allow GC now the play is over.
	if (bRooted)
	{
		RemoveFromRoot();
		bRooted = false;
	}
}

void UDP_WidgetAnimDriver::TeardownTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void UDP_WidgetAnimDriver::BeginDestroy()
{
	// Backstop: never leak the ticker or a root reference if collected without a clean stop.
	TeardownTicker();
	if (bRooted)
	{
		RemoveFromRoot();
		bRooted = false;
	}
	Super::BeginDestroy();
}
