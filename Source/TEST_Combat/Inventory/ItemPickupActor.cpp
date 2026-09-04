// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemPickupActor.h"
#include "ItemDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

AItemPickupActor::AItemPickupActor()
{
	PrimaryActorTick.bCanEverTick = false;

	PickupMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMeshComponent"));
	SetRootComponent(PickupMeshComponent);
}

void AItemPickupActor::InitializePickup_Implementation(FPrimaryAssetId InItemID, int32 InQuantity)
{
	ItemID = InItemID;
	Quantity = InQuantity;

	const UItemDefinition* ItemDef = Cast<UItemDefinition>(UAssetManager::Get().GetPrimaryAssetObject(ItemID));
	if (!ItemDef || ItemDef->PickupMesh.IsNull())
	{
		// No mesh authored for this item - left as whatever PickupMeshComponent's default/
		// placeholder mesh is (set on the actor's class defaults or a Blueprint subclass), not
		// an error: not every item needs a bespoke pickup appearance.
		return;
	}

	// Async load: PickupMesh is a soft reference (see ItemDefinition.h) so a fresh drop doesn't
	// stall the spawning frame while the mesh streams in.
	MeshLoadHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
		ItemDef->PickupMesh.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &AItemPickupActor::OnPickupMeshLoaded));
}

void AItemPickupActor::OnPickupMeshLoaded()
{
	const UItemDefinition* ItemDef = Cast<UItemDefinition>(UAssetManager::Get().GetPrimaryAssetObject(ItemID));
	if (ItemDef && PickupMeshComponent)
	{
		PickupMeshComponent->SetStaticMesh(ItemDef->PickupMesh.Get());
	}
	MeshLoadHandle.Reset();
}

void AItemPickupActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MeshLoadHandle.IsValid())
	{
		MeshLoadHandle->CancelHandle();
		MeshLoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}
