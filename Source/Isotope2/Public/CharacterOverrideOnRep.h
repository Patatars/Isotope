// CharacterOverrideOnRep.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CharacterOverrideOnRep.generated.h"

UCLASS()
class ISOTOPE2_API ACharacterOverrideOnRep : public ACharacter
{
    GENERATED_BODY()

public:
    ACharacterOverrideOnRep();

    virtual void OnRep_PlayerState() override;

    // Единое событие для BP: вызывается и на клиенте, и на сервере
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PlayerState|Replication")
    void OnReplicatedPlayerState();
    virtual void OnReplicatedPlayerState_Implementation();

protected:
    // Приватный Server RPC, вызываемый с клиента-овнера
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_NotifyPlayerStateInitialized();

    virtual void BeginPlay() override;
};