// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "EnemyAIController.generated.h"

class UAISenseConfig_Sight;
class UStateTreeAIComponent;

/**
 * Possesses AEnemyCharacter and drives its behavior via a StateTree (see UStateTreeAIComponent
 * below) fed by Sight perception. Deliberately thin: this class only wires up Sight
 * detection -> AEnemyCharacter::SetCurrentTarget and hosts the StateTree component - everything
 * else (chase/attack/leash/dead state logic) lives in the StateTree asset assigned to
 * StateTreeComponent and the AEnemyCharacter functions it calls, not here.
 *
 * NOTE: does NOT redeclare a PerceptionComponent member - AAIController already has a public
 * TObjectPtr<UAIPerceptionComponent> PerceptionComponent (see AIController.h); this class just
 * creates one and registers it via SetPerceptionComponent in the constructor.
 */
UCLASS()
class TEST_COMBAT_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

protected:
	// Configures Sight radius from the possessed AEnemyCharacter's EnemyDefinition (AggroRadius) -
	// the constructor can only set a placeholder default since no pawn is possessed yet.
	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// Runs this controller's StateTree. Uses UStateTreeAIComponentSchema (set automatically by
	// UStateTreeAIComponent itself), which gives the tree bound access to this AIController and,
	// through it, the possessed AEnemyCharacter. Which StateTree ASSET to run is picked on this
	// component in the editor (its own StateTreeRef property), not in this class.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeComponent;

private:
	// Bound to PerceptionComponent's OnTargetPerceptionUpdated. Only auto-acquires a target while
	// currently idle (see AEnemyCharacter::CurrentTarget) - a second sighted player never overwrites
	// an already-engaged enemy's target. Losing sight does NOT by itself clear the target; giving up
	// a chase is the Leash state's job (distance-from-Home, not line-of-sight) - see the enemy AI
	// design notes.
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
};
