// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "MyCharacter.generated.h"

class UAbilitySystemComponent;
class UAttributeSet;

UCLASS()
class TEST_COMBAT_API AMyCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMyCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public: 	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Gameplay Ability System component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
	UAbilitySystemComponent* AbilitySystemComponent;

	// Optional attribute set pointer (can be a custom subclass)		UPROPERTY()
	UAttributeSet* AttributeSet;

	// IAbilitySystemInterface implementation
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// Initialize ASC when possessed (server) and when PlayerState replicates (client)
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
};
