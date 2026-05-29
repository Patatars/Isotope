#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncAction_CreateLobby1.generated.h"

// Этот делегат создаст те самые пины Success и Failure
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyResponse, const FString&, Response);

UCLASS()
class ISOTOPE2_API UAsyncAction_CreateLobby1 : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FOnLobbyResponse OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FOnLobbyResponse OnFailure;

    // Название ноды в блюпринтах
    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Backend|Lobby")
    static UAsyncAction_CreateLobby1* CreateBackendLobby(UObject* WorldContextObject, const FString& EosLobbyId, const FString& HostEosId, int32 MaxPlayers);

    virtual void Activate() override;

private:
    UObject* WorldContext;
    FString EosLobbyId;
    FString HostEosId;
    int32 MaxPlayers;

    void OnRequestComplete(bool bSuccess, const FString& Response);
};