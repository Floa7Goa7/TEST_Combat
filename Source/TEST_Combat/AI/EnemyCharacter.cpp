// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyCharacter.h"
#include "EnemyDefinition.h"
#include "../CombatTags.h"
#include "../CombatAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "../Inventory/ItemDefinition.h"
#include "../Inventory/ItemPickupInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

void AEnemyCharacter::PostInitializeComponents()
{
	// Populate BEFORE Super: Super::PostInitializeComponents() is where APawn's AutoPossessAI
	// machinery runs (if enabled), which triggers AMyCharacter::PossessedBy and grants whatever is
	// in DefaultAbilities at that moment - see AMyCharacter.h's PossessedBy comment. Must be
	// populated before that, not in BeginPlay (which runs after possession).
	if (EnemyDefinition)
	{
		DefaultAbilities = EnemyDefinition->SpecialAbilitiesPriorityOrder;
		if (EnemyDefinition->MeleeAttackAbilityClass)
		{
			DefaultAbilities.Add(EnemyDefinition->MeleeAttackAbilityClass);
		}

		if (EnemyDefinition->MoveSpeed > 0.0f)
		{
			if (UCharacterMovementComponent* Movement = GetCharacterMovement())
			{
				Movement->MaxWalkSpeed = EnemyDefinition->MoveSpeed;
			}
		}
	}

	Super::PostInitializeComponents();
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	HomeLocation = GetActorLocation();

	if (UCombatAttributeSet* CombatAttrs = GetCombatAttributeSet())
	{
		CombatAttrs->OnDeath.AddDynamic(this, &AEnemyCharacter::HandleDeath);
	}
}

float AEnemyCharacter::GetAggroRadius() const
{
	return EnemyDefinition ? EnemyDefinition->AggroRadius : 0.0f;
}

float AEnemyCharacter::GetLeashRadius() const
{
	return EnemyDefinition ? EnemyDefinition->LeashRadius : 0.0f;
}

float AEnemyCharacter::GetSocialRadius() const
{
	return EnemyDefinition ? EnemyDefinition->SocialRadius : 0.0f;
}

float AEnemyCharacter::GetAttackRange() const
{
	return EnemyDefinition ? EnemyDefinition->AttackRange : 0.0f;
}

FGameplayTag AEnemyCharacter::GetSocialGroupTag() const
{
	return EnemyDefinition ? EnemyDefinition->SocialGroupTag : FGameplayTag();
}

float AEnemyCharacter::GetDistanceToCurrentTarget() const
{
	return CurrentTarget ? FVector::Dist(GetActorLocation(), CurrentTarget->GetActorLocation()) : -1.0f;
}

bool AEnemyCharacter::IsTargetBeyondLeashRange() const
{
	if (!CurrentTarget || !EnemyDefinition)
	{
		return false;
	}
	return FVector::Dist(CurrentTarget->GetActorLocation(), HomeLocation) > EnemyDefinition->LeashRadius;
}

void AEnemyCharacter::SetCurrentTarget(AActor* NewTarget)
{
	if (!HasAuthority() || NewTarget == CurrentTarget)
	{
		return;
	}

	// Only a transition from "had no target" to "has one" counts as a fresh aggro that should call
	// in nearby allies - retargeting an already-engaged enemy, or clearing to nullptr, should not.
	const bool bIsFreshAggro = (CurrentTarget == nullptr) && (NewTarget != nullptr);
	CurrentTarget = NewTarget;

	if (bIsFreshAggro)
	{
		CallForSocialAssist(NewTarget);
	}
}

void AEnemyCharacter::CallForSocialAssist(AActor* Target)
{
	if (!HasAuthority() || !Target || !EnemyDefinition || !EnemyDefinition->SocialGroupTag.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(EnemyDefinition->SocialRadius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySocialAssist), false, this);
	World->OverlapMultiByChannel(Overlaps, GetActorLocation(), FQuat::Identity, ECC_Pawn, Shape, Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AEnemyCharacter* Other = Cast<AEnemyCharacter>(Overlap.GetActor());
		if (!Other || Other == this || Other->CurrentTarget != nullptr)
		{
			continue;
		}
		if (!Other->EnemyDefinition || Other->EnemyDefinition->SocialGroupTag != EnemyDefinition->SocialGroupTag)
		{
			continue;
		}

		// Other->SetCurrentTarget cascades: it will call Other->CallForSocialAssist in turn, which
		// is what chain-pulls a whole group with no hop limit - see this function's declaration
		// comment. Safe against cycles: a mob can only be pulled in once, since it is no longer
		// idle (CurrentTarget != nullptr) by the time any subsequent call could reach it again.
		Other->SetCurrentTarget(Target);
	}
}

bool AEnemyCharacter::TryActivateAttack()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !EnemyDefinition)
	{
		return false;
	}

	for (const TSubclassOf<UGameplayAbility>& SpecialClass : EnemyDefinition->SpecialAbilitiesPriorityOrder)
	{
		if (SpecialClass && ASC->TryActivateAbilityByClass(SpecialClass))
		{
			return true;
		}
	}

	if (EnemyDefinition->MeleeAttackAbilityClass)
	{
		return ASC->TryActivateAbilityByClass(EnemyDefinition->MeleeAttackAbilityClass);
	}

	return false;
}

void AEnemyCharacter::BeginLeashEvade()
{
	if (!HasAuthority())
	{
		return;
	}

	SetCurrentTarget(nullptr);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	UCombatAttributeSet* CombatAttrs = GetCombatAttributeSet();
	if (ASC)
	{
		ASC->AddLooseGameplayTag(TAG_State_Leashing);
	}
	if (ASC && CombatAttrs)
	{
		// Direct attribute override, not a GameplayEffect - this is evade/reset healing, not
		// something that should run through the normal damage/healing pipeline (or be blocked by
		// the very TAG_State_Leashing tag it just added).
		ASC->ApplyModToAttribute(CombatAttrs->GetHealthAttribute(), EGameplayModOp::Override, CombatAttrs->GetHealthMax());
	}
}

void AEnemyCharacter::EndLeashEvade()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Leashing);
	}
}

void AEnemyCharacter::HandleDeath(AActor* DeadActor)
{
	if (!HasAuthority())
	{
		return;
	}

	SetCurrentTarget(nullptr);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
		Movement->StopMovementImmediately();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	RollAndDropLoot();

	SetLifeSpan(EnemyDefinition ? EnemyDefinition->CorpseDestroyDelay : 5.0f);
}

void AEnemyCharacter::RollAndDropLoot() const
{
	if (!HasAuthority() || !EnemyDefinition)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Mirrors UInventoryComponent::Server_DropItem_Implementation's own spawn logic - loot drops
	// reuse that pipeline rather than duplicating pickup-actor handling.
	for (const FEnemyLootEntry& Entry : EnemyDefinition->LootTable)
	{
		if (!Entry.ItemID.IsValid() || FMath::FRand() > Entry.DropChance)
		{
			continue;
		}

		const UItemDefinition* ItemDef = UItemDefinition::LoadItemDefinitionSynchronous(Entry.ItemID);
		if (!ItemDef || !ItemDef->PickupActorClass)
		{
			continue;
		}

		const int32 Quantity = FMath::RandRange(Entry.MinQuantity, FMath::Max(Entry.MinQuantity, Entry.MaxQuantity));

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* Pickup = World->SpawnActor<AActor>(ItemDef->PickupActorClass, GetActorLocation(), GetActorRotation(), SpawnParams);
		if (Pickup && Pickup->GetClass()->ImplementsInterface(UItemPickupInterface::StaticClass()))
		{
			IItemPickupInterface::Execute_InitializePickup(Pickup, Entry.ItemID, Quantity);
		}
	}
}
