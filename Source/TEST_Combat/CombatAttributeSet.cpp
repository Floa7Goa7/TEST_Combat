#include "CombatAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h" // for FGameplayEffectModCallbackData definition

UCombatAttributeSet::UCombatAttributeSet()
{
	// Default values
	Health = 100.0f;
	HealthMax = 100.0f;
	Mana = 100.0f;
	ManaMax = 100.0f;
}

void UCombatAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UCombatAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCombatAttributeSet, HealthMax, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCombatAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCombatAttributeSet, ManaMax, COND_None, REPNOTIFY_Always);
}

void UCombatAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCombatAttributeSet, Health, OldValue);
}

void UCombatAttributeSet::OnRep_HealthMax(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCombatAttributeSet, HealthMax, OldValue);
}

void UCombatAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCombatAttributeSet, Mana, OldValue);
}

void UCombatAttributeSet::OnRep_ManaMax(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCombatAttributeSet, ManaMax, OldValue);
}

void UCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// If max changes, scale current value to maintain percentage
	if (Attribute == GetHealthMaxAttribute())
	{
		float CurrentHealth = (float)Health.GetCurrentValue();
		if (!FMath::IsNearlyZero(GetHealthMax()))
		{
			float NewMax = NewValue;
			float NewHealth = (CurrentHealth / GetHealthMax()) * NewMax;
			Health.SetCurrentValue(FMath::Clamp(NewHealth, 0.0f, NewMax));
		}
	}
	else if (Attribute == GetManaMaxAttribute())
	{
		float CurrentMana = (float)Mana.GetCurrentValue();
		if (!FMath::IsNearlyZero(GetManaMax()))
		{
			float NewMax = NewValue;
			float NewMana = (CurrentMana / GetManaMax()) * NewMax;
			Mana.SetCurrentValue(FMath::Clamp(NewMana, 0.0f, NewMax));
		}
	}
}

void UCombatAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Clamp values after effects
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		Health.SetCurrentValue(FMath::Clamp(Health.GetCurrentValue(), 0.0f, (float)HealthMax.GetCurrentValue()));
	}

	if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		Mana.SetCurrentValue(FMath::Clamp(Mana.GetCurrentValue(), 0.0f, (float)ManaMax.GetCurrentValue()));
	}
}
