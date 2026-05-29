#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncAction_DeleteLobby.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeleteLobbyResponse, const FString&, Response);

UCLASS()
class ISOTOPE2_API UAsyncAction_DeleteLobby : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FOnDeleteLobbyResponse OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FOnDeleteLobbyResponse OnFailure;

    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Backend|Lobby")
    static UAsyncAction_DeleteLobby* DeleteBackendLobby(UObject* WorldContextObject, const FString& BackendLobbyId);

    virtual void Activate() override;

private:
    UObject* WorldContext;
    FString BackendLobbyId;

    void OnRequestComplete(bool bSuccess, const FString& Response);
};