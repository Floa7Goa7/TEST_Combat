// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "MyCharacter.generated.h"

class UAbilitySystemComponent;
class UAttributeSet;
class UCombatAttributeSet;
class UGameplayAbility;

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

	// Abilities granted to this character's ASC on the server the first time it's possessed.
	// Editor-side list instead of a C++ change per ability - see PossessedBy.
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	// IAbilitySystemInterface implementation
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// Blueprint-friendly typed accessor for AttributeSet above, which isn't Blueprint-visible at
	// all (plain UPROPERTY(), and typed as the base UAttributeSet besides). Use this to bind to
	// UCombatAttributeSet::OnDeath or call ResetForRespawn from this Character's event graph
	// (e.g. Get Combat Attribute Set -> Bind Event to On Death).
	UFUNCTION(BlueprintPure, Category = "Attributes")
	UCombatAttributeSet* GetCombatAttributeSet() const;

	// UCombatAttributeSet::ResetForRespawn is authority-only (no-ops on a client - see its
	// comment), and UCombatAttributeSet can't own a Server RPC itself (only Actors/Components
	// can). Call THIS from Blueprint instead of Reset For Respawn directly whenever the respawn
	// flow might run on a non-host client (e.g. reacting to this character's own OnDeath) - it
	// routes to the server like any other Server UFUNCTION regardless of which machine calls it.
	UFUNCTION(BlueprintCallable, Category = "Attributes", Server, Reliable)
	void Server_ResetForRespawn();

	// Initialize ASC when possessed (server) and when PlayerState replicates (client)
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	// Fired once PlayerState is confirmed valid on THIS machine - from PossessedBy (server) or
	// OnRep_PlayerState (client), whichever applies. This is the race-free signal for Blueprint
	// logic that needs PlayerState-hosted components (UEquipmentComponent, UInventoryComponent)
	// to exist - use it instead of a BeginPlay + fixed Delay guess. A fixed delay is inherently a
	// race: PlayerState replication timing isn't guaranteed to land within any specific window,
	// and adding more traffic at connection time (e.g. EquipmentList now replicating to everyone,
	// not just the owner) can push it past whatever delay used to "usually" be enough. May fire
	// more than once if this pawn is repossessed without being destroyed (e.g. on some respawn
	// flows) - guard any one-time binding logic in Blueprint (a Do Once node, or a bool flag) if
	// re-running it would cause a duplicate delegate binding.
	UFUNCTION(BlueprintImplementableEvent, Category = "Abilities", meta = (DisplayName = "On Player State Ready"))
	void OnPlayerStateReady();

private:
	// Guards DefaultAbilities from being granted again if PossessedBy fires more than once for
	// this ASC (e.g. respawn) - see PossessedBy.
	bool bDefaultAbilitiesGranted = false;
};
