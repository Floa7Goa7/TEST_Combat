// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_WeaponAttack.h"
#include "CombatTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Inventory/EquipmentComponent.h"
#include "Inventory/EquipmentItemDefinition.h"

UGA_WeaponAttack::UGA_WeaponAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	AbilityTags.AddTag(TAG_Ability_WeaponAttack);

	// Refused activation outright while dead - see UCombatAttributeSet::PostGameplayEffectExecute,
	// which adds this tag when Health reaches 0. No per-ability dead-check needed here; GAS's own
	// ActivationBlockedTags mechanism handles the rejection.
	ActivationBlockedTags.AddTag(TAG_State_Dead);
}

const UEquipmentItemDefinition* UGA_WeaponAttack::ResolveEquippedWeaponItem(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// The ASC (and this ability) live on the Pawn in this project (see AMyCharacter), not the
	// PlayerState - unlike UEquipmentComponent, which does live on PlayerState. Go by way of the
	// avatar's PlayerState to reach it, same relationship UEquipmentComponent::ResolveWearerActor
	// navigates in the other direction.
	const APawn* AvatarPawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const APlayerState* PS = AvatarPawn ? AvatarPawn->GetPlayerState() : nullptr;
	const UEquipmentComponent* EquipmentComponent = PS ? PS->FindComponentByClass<UEquipmentComponent>() : nullptr;

	// Authoritative, not predicted: an attack is already server-validated for damage, and reading
	// the confirmed slot avoids a one-frame desync if the player swings the instant after
	// equipping.
	return EquipmentComponent
		? EquipmentComponent->GetEquippedItemDefinition(EquipmentComponent->MainHandSlotTag)
		: nullptr;
}

UAnimMontage* UGA_WeaponAttack::ResolveAttackMontage(const UEquipmentItemDefinition* ItemDef) const
{
	if (!ItemDef)
	{
		return UnarmedMontage;
	}

	if (!ItemDef->AttackMontageOverride.IsNull())
	{
		if (UAnimMontage* Overridden = ItemDef->AttackMontageOverride.LoadSynchronous())
		{
			return Overridden;
		}
	}

	if (const TObjectPtr<UAnimMontage>* Found = AttackMontagesByWeaponType.Find(ItemDef->WeaponTypeTag))
	{
		if (*Found)
		{
			return *Found;
		}
	}

	return UnarmedMontage;
}

void UGA_WeaponAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const UEquipmentItemDefinition* ItemDef = ResolveEquippedWeaponItem(ActorInfo);
	UAnimMontage* MontageToPlay = ResolveAttackMontage(ItemDef);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !MontageToPlay)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CachedMinDamage = ItemDef ? ItemDef->MinDamage : UnarmedMinDamage;
	CachedMaxDamage = ItemDef ? ItemDef->MaxDamage : UnarmedMaxDamage;

	const float PlayRate = ItemDef ? ItemDef->SwingSpeed : 1.0f;
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, MontageToPlay, PlayRate);
	MontageTask->OnCompleted.AddDynamic(this, &UGA_WeaponAttack::OnMontageCompletedOrInterrupted);
	MontageTask->OnInterrupted.AddDynamic(this, &UGA_WeaponAttack::OnMontageCompletedOrInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UGA_WeaponAttack::OnMontageCompletedOrInterrupted);
	MontageTask->ReadyForActivation();

	// OnlyTriggerOnce = false: a single swing can hit multiple targets, one Event per target -
	// see UAnimNotifyState_WeaponTrace, which sends one per newly-hit actor.
	UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Weapon_Hit, nullptr, false, false);
	HitTask->EventReceived.AddDynamic(this, &UGA_WeaponAttack::OnHitEventReceived);
	HitTask->ReadyForActivation();
}

void UGA_WeaponAttack::OnHitEventReceived(FGameplayEventData Payload)
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
		// Rolled per hit, not once per swing: each target hit during a multi-hit swing gets its
		// own independent roll within [CachedMinDamage, CachedMaxDamage] - see ActivateAbility for
		// where that range is cached from (the weapon equipped when THIS swing started). Integer
		// roll (FMath::RandRange(int32, int32)) so damage is always a whole number; only cast to
		// float here, where SetSetByCallerMagnitude requires it - Health itself is still a float
		// attribute underneath.
		const int32 RolledDamage = FMath::RandRange(CachedMinDamage, CachedMaxDamage);

		// Negative: GE_SwordDamage's Health modifier is authored as an Add reading this tag, so
		// damage must be passed in as a negative delta (a positive value here would heal).
		SpecHandle.Data->SetSetByCallerMagnitude(TAG_Data_Damage, -static_cast<float>(RolledDamage));
		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

void UGA_WeaponAttack::OnMontageCompletedOrInterrupted()
{
	EndAbility(GetCurrentAbilitySpecHandle(), CurrentActorInfo, CurrentActivationInfo, true, false);
}
