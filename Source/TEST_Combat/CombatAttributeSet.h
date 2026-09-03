// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "CombatAttributeSet.generated.h"

struct FGameplayEffectModCallbackData;

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
};
