#include "CombatAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h" // for FGameplayEffectModCallbackData definition
#include "CombatTags.h"

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

	// Client-side counterpart to the death detection in PostGameplayEffectExecute (which only
	// runs where the killing effect actually executes, i.e. the server) - this is what lets a
	// remote client react to another player's death, not just the server/host. See OnDeath's
	// declaration comment.
	if (Health.GetCurrentValue() <= 0.0f)
	{
		if (!bIsDead)
		{
			bIsDead = true;
			OnDeath.Broadcast(GetOwningActor());
		}
	}
	else
	{
		// Health replicated back above 0 (a respawn) - clear the local guard so a FUTURE death can
		// broadcast OnDeath again. ResetForRespawn() itself only ever runs authoritatively on the
		// SERVER regardless of which client's RPC triggered it, so its own "bIsDead = false" never
		// executes on any client's local instance - including the owning client's. Without this,
		// bIsDead would stay true forever after the first death on every machine except the
		// server, silently blocking OnDeath from ever firing again there (the replicated Health
		// value and gameplay tag both still update correctly regardless, since those don't depend
		// on this flag - only OnDeath does).
		bIsDead = false;
	}
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

		// Authoritative death detection - PostGameplayEffectExecute only runs where the effect is
		// actually executed (the server, for every damage GE in this codebase - see
		// GA_WeaponAttack::OnHitEventReceived's HasAuthority guard). The Dead tag is added here,
		// not client-side, since tag state must be server-decided; it then replicates to every
		// client automatically as part of the ASC's own tag container. OnRep_Health handles
		// broadcasting OnDeath on clients once that replicated Health value arrives there.
		if (Health.GetCurrentValue() <= 0.0f && !bIsDead)
		{
			bIsDead = true;
			if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
			{
				ASC->AddLooseGameplayTag(TAG_State_Dead);
			}
			OnDeath.Broadcast(GetOwningActor());
		}
	}

	if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		Mana.SetCurrentValue(FMath::Clamp(Mana.GetCurrentValue(), 0.0f, (float)ManaMax.GetCurrentValue()));
	}
}

void UCombatAttributeSet::ResetForRespawn()
{
	AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Dead);
	}

	bIsDead = false;

	// Both Base and Current: GAS attributes track them separately, and a future Instant
	// GameplayEffect Add/Multiply against Health should operate relative to a properly reset
	// base, not a stale one left over from before death.
	Health.SetBaseValue(HealthMax.GetCurrentValue());
	Health.SetCurrentValue(HealthMax.GetCurrentValue());
}
