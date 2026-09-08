// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "GA_WeaponAttack.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UEquipmentItemDefinition;
struct FGameplayAbilityActorInfo;

/**
 * General melee attack: resolves which AnimMontage to play from whatever is (authoritatively)
 * equipped in MainHand - see UEquipmentComponent - then applies DamageEffectClass to every actor
 * UAnimNotifyState_WeaponTrace (placed on that montage) reports a hit against - see that class'
 * comment for how hit detection itself works. Formerly GA_SwordSwing, a single hardcoded montage
 * per ability instance; generalized (2026-09-07) so any melee weapon type (sword/axe/mace/...)
 * shares this one ability instead of needing its own copy.
 *
 * Montage resolution order (see ResolveAttackMontage):
 *   1. MainHand empty, or its item has no UEquipmentItemDefinition::AttackMontageOverride and no
 *      AttackMontagesByWeaponType entry for its WeaponTypeTag -> UnarmedMontage.
 *   2. The equipped item's AttackMontageOverride, if set.
 *   3. AttackMontagesByWeaponType[item's WeaponTypeTag].
 * If even UnarmedMontage is unset, ActivateAbility aborts - same as the old SwingMontage guard.
 * Whichever montage is chosen plays at the equipped item's SwingSpeed (1.0 for unarmed), so e.g.
 * a mace can swing slower than a sword without a distinct montage or a separate cooldown system.
 *
 * Damage is likewise read from the equipped item (UEquipmentItemDefinition::MinDamage/MaxDamage),
 * rolled independently per hit via FMath::RandRange in OnHitEventReceived - not a flat amount, and
 * not the ability's problem to author per weapon. UnarmedMinDamage/UnarmedMaxDamage below are the
 * "no weapon" fallback, same shape as UnarmedMontage. Replacing this with a value additionally
 * derived from an attacker stat (e.g. a future AttackPower attribute on UCombatAttributeSet, via a
 * GameplayEffectExecutionCalculation) only means changing what feeds SetSetByCallerMagnitude in
 * OnHitEventReceived; GE_SwordDamage and the hit-detection notify state do not need to change.
 */
UCLASS()
class TEST_COMBAT_API UGA_WeaponAttack : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_WeaponAttack();

	// Played when MainHand is empty, or holds an item whose WeaponTypeTag isn't in
	// AttackMontagesByWeaponType and has no AttackMontageOverride.
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Attack")
	TObjectPtr<UAnimMontage> UnarmedMontage = nullptr;

	// Keyed by UEquipmentItemDefinition::WeaponTypeTag. Add an entry here when introducing a new
	// weapon type - no C++ change needed. An individual item can bypass this via its own
	// AttackMontageOverride.
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Attack")
	TMap<FGameplayTag, TObjectPtr<UAnimMontage>> AttackMontagesByWeaponType;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon Attack")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// Damage range rolled when MainHand is empty or its item resolves no weapon data - the
	// "unarmed" counterpart to UnarmedMontage. Equipped weapons define their own
	// MinDamage/MaxDamage on UEquipmentItemDefinition instead (see the class comment). Integers
	// by design - see UEquipmentItemDefinition::MinDamage's comment.
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Attack", meta = (ClampMin = "0"))
	int32 UnarmedMinDamage = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon Attack", meta = (ClampMin = "0"))
	int32 UnarmedMaxDamage = 10;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	UFUNCTION()
	void OnHitEventReceived(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageCompletedOrInterrupted();

private:
	// Resolves whatever UEquipmentItemDefinition is (authoritatively) equipped in MainHand, or
	// nullptr if empty/unresolvable. Both montage and damage-range resolution key off this.
	const UEquipmentItemDefinition* ResolveEquippedWeaponItem(const FGameplayAbilityActorInfo* ActorInfo) const;

	// Montage resolution order - see the class comment. ItemDef may be null (unarmed).
	UAnimMontage* ResolveAttackMontage(const UEquipmentItemDefinition* ItemDef) const;

	// Cached at ActivateAbility from whichever weapon item resolved this swing, so
	// OnHitEventReceived (called once per target hit, potentially several times per swing) rolls
	// against the weapon that was equipped when the swing STARTED, not whatever might be equipped
	// by the time a given hit event arrives mid-swing.
	int32 CachedMinDamage = 0;
	int32 CachedMaxDamage = 0;
};
