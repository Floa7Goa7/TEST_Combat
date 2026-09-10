// Fill out your copyright notice in the Description page of Project Settings.

#include "MyPlayerController.h"

void AMyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Server / standalone / listen-server-host path: PlayerState is already assigned by now
	// (AController::InitPlayerState runs in PostInitializeComponents, well before BeginPlay).
	// OnRep_PlayerState below covers the remote-client path, where PlayerState arrives later via
	// replication and BeginPlay can run before it does.
	if (PlayerState)
	{
		OnPlayerStateReady();
	}
}

void AMyPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	OnPlayerStateReady();
}
