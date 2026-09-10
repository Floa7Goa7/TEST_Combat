// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../MyCharacter.h"
#include "EnemyCharacter.generated.h"

class UEnemyDefinition;

/**
 * AI-controlled combat archetype: reuses AMyCharacter's ASC/CombatAttributeSet/ability-granting
 * PossessedBy flow as-is (that flow was already generic - see AMyCharacter.h) and adds only what
 * AI behavior needs: a home location to leash back to, and the runtime hooks
 * AEnemyAIController/its StateTree drive to run the aggro/chase/attack/leash/dead loop.
 *
 * Per-enemy-TYPE variety (stats, abilities, loot) lives in EnemyDefinition (a data asset - see
 * UEnemyDefinition's class comment), not in a Blueprint subclass of this class. Only make a new
 * AEnemyCharacter/Blueprint archetype for a genuinely different creature shape (different
 * movement type, skeleton, or StateTree attack logic).
 */
UCLASS()
class TEST_COMBAT_API AEnemyCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	// Which enemy TYPE this instance is - set per placed instance or per spawner, not per class.
	// See UEnemyDefinition's class comment for why this is a hard reference rather than an
	// FPrimaryAssetId/soft reference like UItemDefinition.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	TObjectPtr<UEnemyDefinition> EnemyDefinition;

	// Current pursuit target, or nullptr if idle. READ this directly (BlueprintReadOnly) from the
	// StateTree's transition conditions - e.g. Idle -> Chase whenever this is valid. Do NOT set it
	// directly from a StateTree "Set Property" node; always go through SetCurrentTarget below, which
	// is what triggers the social-assist call to nearby enemies on a fresh aggro (see that
	// function's comment) - a raw property write would silently skip that.
	UPROPERTY(BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<AActor> CurrentTarget;

	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	//~ End AActor interface

	// The location this enemy leashes back to - captured once in BeginPlay (its spawn transform).
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	FVector GetHomeLocation() const { return HomeLocation; }

	// Safe (EnemyDefinition-null-tolerant) accessors for the StateTree to bind its transition
	// conditions against, instead of every condition needing its own "is EnemyDefinition valid"
	// branch. Return 0 if EnemyDefinition is unset.
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	float GetAggroRadius() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	float GetLeashRadius() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	float GetSocialRadius() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	float GetAttackRange() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	FGameplayTag GetSocialGroupTag() const;

	// Straight-line distance from this actor to CurrentTarget, or -1 if there is no current target.
	// Convenience for StateTree distance-based transition conditions (leash range, attack range).
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	float GetDistanceToCurrentTarget() const;

	// True once CurrentTarget has moved further than EnemyDefinition->LeashRadius from
	// GetHomeLocation() (NOT from this actor's current position) - see UEnemyDefinition::LeashRadius.
	// False if there is no current target.
	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	bool IsTargetBeyondLeashRange() const;

	// The one function that should ever assign CurrentTarget - see that property's comment. Setting
	// a non-null target while this enemy was previously idle (CurrentTarget was null) triggers
	// CallForSocialAssist so nearby same-group enemies join the fight; retargeting an already-engaged
	// enemy, or clearing to nullptr, does not. Authority-only (no-ops on a client) - target
	// assignment is server-authoritative AI decision-making, not something a client predicts.
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void SetCurrentTarget(AActor* NewTarget);

	// Sphere-overlaps SocialRadius for other AEnemyCharacters that are (a) currently idle
	// (CurrentTarget == nullptr) and (b) share this enemy's SocialGroupTag (both must be set and
	// equal - see UEnemyDefinition::SocialGroupTag), and calls SetCurrentTarget(Target) on each.
	// Called automatically by SetCurrentTarget on a fresh aggro - the StateTree does not need to
	// call this itself. Naturally cascades: a socially-pulled enemy's own SetCurrentTarget call
	// triggers its own CallForSocialAssist in turn, chain-pulling a whole group with no hop limit
	// (mob spacing in the level is the intended limiter - see the enemy AI design notes).
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void CallForSocialAssist(AActor* Target);

	// Tries EnemyDefinition->SpecialAbilitiesPriorityOrder in order, then MeleeAttackAbilityClass,
	// via TryActivateAbilityByClass; returns true on the first one GAS actually activates. GAS's own
	// TAG_State_Attacking ActivationBlockedTags check (see CombatTags.h) is what prevents this from
	// interrupting an attack already in progress - this function does not need to check that itself,
	// it just tries in priority order and lets activation fail naturally while one is mid-swing.
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	bool TryActivateAttack();

	// Drops the current target, adds TAG_State_Leashing (see that tag's comment - add it as an
	// Ignore Tag under each damage GameplayEffect's Application Tag Requirements so a leashing
	// enemy can't be re-aggroed by damage on the way home), and restores Health to HealthMax.
	// Authority-only. Call when entering the Leash/Evade state.
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void BeginLeashEvade();

	// Removes TAG_State_Leashing. Call once the Leash/Evade state's MoveTo-Home completes, before
	// returning to Idle.
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	void EndLeashEvade();

protected:
	UFUNCTION()
	void HandleDeath(AActor* DeadActor);

private:
	FVector HomeLocation = FVector::ZeroVector;

	// Authority-only: rolls EnemyDefinition->LootTable and spawns a pickup per successful entry via
	// the same AItemPickupActor/IItemPickupInterface pipeline UInventoryComponent::Server_DropItem
	// already uses for player-dropped items - see FEnemyLootEntry's comment.
	void RollAndDropLoot() const;
};
