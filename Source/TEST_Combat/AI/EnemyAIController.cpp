// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Components/StateTreeAIComponent.h"

AEnemyAIController::AEnemyAIController()
{
	UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 800.0f;
	SightConfig->LoseSightRadius = 900.0f;
	// Half-angle relative to forward vector, max 180 - 180 here means the full 360 degrees around
	// the pawn, matching this project's "radius + LoS, no cone yet" bare-minimum design (see the
	// enemy AI design notes). Narrow this per-SightConfig instance later for a real cone.
	SightConfig->PeripheralVisionAngleDegrees = 180.0f;
	SightConfig->DetectionByAffiliation = FAISenseAffiliationFilter(true, true, true);

	Perception->ConfigureSense(*SightConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());
	Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnTargetPerceptionUpdated);

	StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeComponent"));
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// The constructor can only set a placeholder SightRadius - the real value comes from whichever
	// UEnemyDefinition this specific pawn instance was authored with, only known once possessed.
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(InPawn);
	if (Enemy && SightConfig)
	{
		const float AggroRadius = Enemy->GetAggroRadius();
		if (AggroRadius > 0.0f)
		{
			SightConfig->SightRadius = AggroRadius;
			SightConfig->LoseSightRadius = AggroRadius + 100.0f;
			if (UAIPerceptionComponent* Perception = GetAIPerceptionComponent())
			{
				Perception->ConfigureSense(*SightConfig);
			}
		}
	}
}

void AEnemyAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority() || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	APawn* SensedPawn = Cast<APawn>(Actor);
	if (!SensedPawn || !SensedPawn->GetPlayerState())
	{
		// Only real players aggro this way - GetPlayerState() being non-null is the established
		// "is this a real player, not an AI-controlled pawn" discriminator used elsewhere in this
		// codebase (see AMyCharacter/AEnemyCharacter both deriving from the same class: an
		// AI-possessed one never has a PlayerState under normal play).
		return;
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetPawn());
	if (Enemy && !Enemy->CurrentTarget)
	{
		Enemy->SetCurrentTarget(SensedPawn);
	}
}
