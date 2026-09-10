// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "EnemyDefinition.generated.h"

class UGameplayAbility;

/**
 * One roll against AEnemyCharacter::HandleDeath's loot pass - see UEnemyDefinition::LootTable.
 * ItemID resolves through UItemDefinition::LoadItemDefinitionSynchronous exactly like every other
 * FPrimaryAssetId in this codebase's Inventory system (see ItemDefinition.h); the actual pickup
 * actor spawned is whatever that item's own PickupActorClass points at, same as
 * UInventoryComponent::Server_DropItem - loot drops reuse that pipeline rather than duplicating it.
 */
USTRUCT(BlueprintType)
struct FEnemyLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FPrimaryAssetId ItemID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	float DropChance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MaxQuantity = 1;
};

/**
 * Data-driven enemy "type": everything that varies between two enemies sharing the same
 * AEnemyCharacter archetype (skeleton/movement/StateTree logic shape) lives here instead of in a
 * Blueprint subclass - same rationale as UItemDefinition serving every item as data rather than a
 * Blueprint child per item (see ItemDefinition.h's class comment). A new enemy TYPE is a new
 * instance of this asset, not a new class; only a genuinely different creature shape (flight,
 * different attack-state logic, different skeleton) warrants a new AEnemyCharacter/Blueprint.
 *
 * Deliberately a plain UDataAsset, not a UPrimaryDataAsset like UItemDefinition: nothing needs to
 * look this up by FPrimaryAssetId at runtime (an AEnemyCharacter holds a direct, hard reference to
 * its own definition - see EnemyDefinition on that class), so there is no need for Asset Manager
 * registration or soft-reference/async-load plumbing here.
 */
UCLASS(BlueprintType)
class TEST_COMBAT_API UEnemyDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// How far (from the enemy's current position) AEnemyAIController's AIPerceptionComponent Sight
	// sense can detect a player pawn. Drives the Idle -> Chase transition.
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float AggroRadius = 800.0f;

	// Measured from the enemy's HOME location (its spawn point), not its current position - once
	// its target gets this far from Home, AEnemyCharacter::IsTargetBeyondLeashRange goes true and
	// the StateTree should transition to Leash/Evade. Larger than AggroRadius so a mob chases some
	// distance from its spawn point before giving up.
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float LeashRadius = 1500.0f;

	// How far AEnemyCharacter::CallForSocialAssist looks for other idle enemies sharing
	// SocialGroupTag to pull into the same fight when this enemy first aggroes.
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float SocialRadius = 500.0f;

	// Gates social aggro: only enemies with a MATCHING, non-empty SocialGroupTag call for or
	// respond to each other's social assist. Left unset (the default), this enemy never
	// participates in social aggro at all - neither pulling others in nor being pulled in itself.
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	FGameplayTag SocialGroupTag;

	// Distance from target at which the Attack state considers itself in range. 0 leaves whatever
	// the StateTree's own MoveTo acceptance radius is authoritative instead.
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float AttackRange = 150.0f;

	// 0 leaves the pawn's own default UCharacterMovementComponent::MaxWalkSpeed untouched - see
	// AEnemyCharacter::PostInitializeComponents.
	UPROPERTY(EditDefaultsOnly, Category = "AI", meta = (ClampMin = "0"))
	float MoveSpeed = 0.0f;

	// Tried in order by AEnemyCharacter::TryActivateAttack; the first one GAS actually activates
	// (respecting its own cooldown/cost/ActivationBlockedTags) wins for this attack. Every entry
	// here is also granted via AMyCharacter::DefaultAbilities at possession, same as MeleeAttackAbility
	// below - see AEnemyCharacter::PostInitializeComponents. Any such ability should add
	// TAG_State_Attacking to both its ActivationOwnedTags and ActivationBlockedTags (see that tag's
	// declaration comment in CombatTags.h) so it can't interrupt, or be interrupted by, another
	// attack already in progress.
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> SpecialAbilitiesPriorityOrder;

	// Fallback attack tried last by TryActivateAttack if every SpecialAbilitiesPriorityOrder entry
	// is unavailable (on cooldown, blocked, etc.) - ordinarily GA_WeaponAttack, which already rolls
	// its own unarmed damage/montage for an AI-possessed pawn with no equipped weapon (see that
	// class' comment).
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TSubclassOf<UGameplayAbility> MeleeAttackAbilityClass;

	// Rolled independently by AEnemyCharacter::HandleDeath on this actor's death - see
	// FEnemyLootEntry.
	UPROPERTY(EditDefaultsOnly, Category = "Loot")
	TArray<FEnemyLootEntry> LootTable;

	// How long the corpse lingers (SetLifeSpan) after HandleDeath before the actor is destroyed.
	UPROPERTY(EditDefaultsOnly, Category = "Death", meta = (ClampMin = "0"))
	float CorpseDestroyDelay = 5.0f;
};
