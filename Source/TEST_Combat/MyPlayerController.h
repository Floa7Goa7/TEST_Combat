// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

/**
 * Base class for BP_ThirdPersonPlayerController. Exists solely to give Blueprint a race-free
 * signal for when this controller's own PlayerState is valid - see OnPlayerStateReady.
 *
 * AController::PlayerState is a *separately* replicated pointer from APawn::PlayerState (the one
 * AMyCharacter::OnPlayerStateReady is keyed off) - a controller reaching for its own PlayerState
 * (e.g. to build WB_HUD from PlayerState-hosted UInventoryComponent/UEquipmentComponent) races
 * against this property the same way the pawn's did, and needs its own signal for it.
 */
UCLASS()
class TEST_COMBAT_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

public:
	virtual void OnRep_PlayerState() override;

	// Fired once PlayerState is confirmed valid on THIS machine - from BeginPlay (server /
	// standalone / listen-server-host, where InitPlayerState has already run by BeginPlay) or
	// OnRep_PlayerState (remote client, where BeginPlay can run before replication delivers
	// PlayerState). Use this instead of building WB_HUD in BeginPlay + a Delay guess - see
	// AMyCharacter::OnPlayerStateReady for the identical rationale on the pawn side. Guard any
	// one-time setup (e.g. widget creation) with a Do Once node in case this ever fires more than
	// once for the same controller.
	UFUNCTION(BlueprintImplementableEvent, Category = "Player", meta = (DisplayName = "On Player State Ready"))
	void OnPlayerStateReady();
};
