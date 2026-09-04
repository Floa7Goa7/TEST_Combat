// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_SwordSwing.generated.h"

class UAnimMontage;
class UGameplayEffect;

/**
 * Melee swing: plays SwingMontage and, for the duration of the montage, applies DamageEffectClass
 * to every actor UAnimNotifyState_WeaponTrace (placed on that montage) reports a hit against - see
 * that class' comment for how hit detection itself works. DamageAmount is written into the spec as
 * a Set-by-Caller magnitude under CombatTags.h's TAG_Data_Damage, so GE_SwordDamage (a Blueprint
 * GameplayEffect, not a C++ class - see the Content Browser) just needs a Health modifier reading
 * that tag; it does not need to know or care where the number came from.
 *
 * DamageAmount is a flat placeholder for now (per project decision, 2026-09-04) - replacing it with
 * a value calculated from an attacker stat (e.g. a future AttackPower attribute on
 * UCombatAttributeSet, via a GameplayEffectExecutionCalculation) only means changing what feeds
 * SetSetByCallerMagnitude in OnHitEventReceived; GE_SwordDamage and the hit-detection notify state
 * do not need to change.
 */
UCLASS()
class TEST_COMBAT_API UGA_SwordSwing : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_SwordSwing();

	UPROPERTY(EditDefaultsOnly, Category = "Sword Swing")
	TObjectPtr<UAnimMontage> SwingMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Sword Swing")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// Flat damage dealt per hit - see the class comment on why this is a placeholder.
	UPROPERTY(EditDefaultsOnly, Category = "Sword Swing", meta = (ClampMin = "0"))
	float DamageAmount = 10.0f;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	UFUNCTION()
	void OnHitEventReceived(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageCompletedOrInterrupted();
};
