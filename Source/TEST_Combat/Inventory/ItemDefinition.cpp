// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

FPrimaryAssetId UItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Item"), GetFName());
}

UItemDefinition* UItemDefinition::LoadItemDefinitionSynchronous(FPrimaryAssetId ItemID)
{
	if (!ItemID.IsValid())
	{
		return nullptr;
	}

	UAssetManager& Manager = UAssetManager::Get();
	if (UObject* AlreadyLoaded = Manager.GetPrimaryAssetObject(ItemID))
	{
		return Cast<UItemDefinition>(AlreadyLoaded);
	}

	const FSoftObjectPath AssetPath = Manager.GetPrimaryAssetPath(ItemID);
	if (!AssetPath.IsValid())
	{
		return nullptr;
	}

	return Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(AssetPath));
}

UTexture2D* UItemDefinition::LoadIconSynchronous() const
{
	return Icon.LoadSynchronous();
}

UStaticMesh* UItemDefinition::LoadPickupMeshSynchronous() const
{
	return PickupMesh.LoadSynchronous();
}

bool UItemDefinition::OnItemUsed_Implementation(AActor* UsingActor, UInventoryComponent* OwningInventory, int32 SlotIndex)
{
	// Data-only items (crafting materials, quest tokens with no on-use effect, etc.) have
	// nothing to do here. Subclasses that represent consumables/weapons/etc. override this.
	return false;
}
