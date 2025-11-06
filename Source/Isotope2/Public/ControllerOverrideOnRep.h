// ControllerOverrideOnRep.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ControllerOverrideOnRep.generated.h"

UCLASS()
class ISOTOPE2_API ACharacterControllerOverrideOnRep : public APlayerController
{
    GENERATED_BODY()

public:
    ACharacterControllerOverrideOnRep();

    virtual void OnRep_PlayerState() override;

    // Единое событие для Blueprints (вызывается и у клиента, и у сервера)
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "PlayerState|Replication")
    void OnReplicatedPlayerState();
    virtual void OnReplicatedPlayerState_Implementation();

protected:
    // Скрытый Server RPC, вызываемый с клиента-овнера
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_NotifyPlayerStateInitialized();

    virtual void BeginPlay() override;
};