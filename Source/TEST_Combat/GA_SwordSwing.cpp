// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_SwordSwing.h"
#include "CombatTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"

UGA_SwordSwing::UGA_SwordSwing()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	AbilityTags.AddTag(TAG_Ability_SwordSwing);
}

void UGA_SwordSwing::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !SwingMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, SwingMontage);
	MontageTask->OnCompleted.AddDynamic(this, &UGA_SwordSwing::OnMontageCompletedOrInterrupted);
	MontageTask->OnInterrupted.AddDynamic(this, &UGA_SwordSwing::OnMontageCompletedOrInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UGA_SwordSwing::OnMontageCompletedOrInterrupted);
	MontageTask->ReadyForActivation();

	// OnlyTriggerOnce = false: a single swing can hit multiple targets, one Event per target -
	// see UAnimNotifyState_WeaponTrace, which sends one per newly-hit actor.
	UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Weapon_Hit, nullptr, false, false);
	HitTask->EventReceived.AddDynamic(this, &UGA_SwordSwing::OnHitEventReceived);
	HitTask->ReadyForActivation();
}

void UGA_SwordSwing::OnHitEventReceived(FGameplayEventData Payload)
{
	// Defense-in-depth, not load-bearing: UAnimNotifyState_WeaponTrace only ever sends this event
	// from the authoritative side, so this ability instance's WaitGameplayEvent task should never
	// even fire on a non-authoritative (client-predicted) instance in the first place.
	if (!HasAuthority(&CurrentActivationInfo) || !DamageEffectClass)
	{
		return;
	}

	AActor* TargetActor = const_cast<AActor*>(Cast<AActor>(Payload.Target.Get()));
	if (!TargetActor)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!TargetASC || !SourceASC)
	{
		return;
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, GetAbilityLevel(), ContextHandle);
	if (SpecHandle.IsValid())
	{
		// Negative: GE_SwordDamage's Health modifier is authored as an Add reading this tag, so
		// damage must be passed in as a negative delta (a positive value here would heal).
		SpecHandle.Data->SetSetByCallerMagnitude(TAG_Data_Damage, -DamageAmount);
		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

void UGA_SwordSwing::OnMontageCompletedOrInterrupted()
{
	EndAbility(GetCurrentAbilitySpecHandle(), CurrentActorInfo, CurrentActivationInfo, true, false);
}
