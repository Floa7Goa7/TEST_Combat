// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "CombatAttributeSet.generated.h"

struct FGameplayEffectModCallbackData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, AActor*, DeadActor);

UCLASS()
class TEST_COMBAT_API UCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCombatAttributeSet();

	// Attributes
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_HealthMax)
	FGameplayAttributeData HealthMax;

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;

	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_ManaMax)
	FGameplayAttributeData ManaMax;

	// Attribute accessors
	ATTRIBUTE_ACCESSORS_BASIC(UCombatAttributeSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UCombatAttributeSet, HealthMax)
	ATTRIBUTE_ACCESSORS_BASIC(UCombatAttributeSet, Mana)
	ATTRIBUTE_ACCESSORS_BASIC(UCombatAttributeSet, ManaMax)

	// Replication
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	virtual void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()	virtual void OnRep_HealthMax(const FGameplayAttributeData& OldValue);
	UFUNCTION()	virtual void OnRep_Mana(const FGameplayAttributeData& OldValue);
	UFUNCTION()	virtual void OnRep_ManaMax(const FGameplayAttributeData& OldValue);
	// Override hooks
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// Broadcasts the first time Health reaches 0 - once from PostGameplayEffectExecute
	// (authoritative: only runs where the killing GameplayEffect actually executes, i.e. the
	// server) and independently again from OnRep_Health on every machine that receives the
	// resulting replicated Health value, including remote clients watching someone else die.
	// bIsDead below guards against firing twice for the same death. Bind to this from the
	// Character/Controller Blueprint to trigger ragdoll, a death animation, disabling input, etc.
	UPROPERTY(BlueprintAssignable, Category = "Attributes")
	FOnDeath OnDeath;

	// Called by the respawn flow (Blueprint - spawn point selection and respawn timing are
	// level/game-mode concerns, not this attribute set's) once ready to bring this actor back:
	// restores Health to HealthMax and clears TAG_State_Dead, re-enabling GA_WeaponAttack and any
	// other ability gated on that tag. Authority-only, matching every other mutating entry point
	// in this codebase's Inventory/Equipment components - no-ops on a client.
	UFUNCTION(BlueprintCallable, Category = "Attributes")
	void ResetForRespawn();

private:
	// Guards OnDeath from firing more than once per death - see OnDeath's comment for why both
	// PostGameplayEffectExecute and OnRep_Health independently check for the same transition.
	// Reset to false by ResetForRespawn.
	bool bIsDead = false;
};
