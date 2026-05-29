#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncAction_GetLobby.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGetLobbyResponse, const FString&, Response);

UCLASS()
class ISOTOPE2_API UAsyncAction_GetLobby : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FOnGetLobbyResponse OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FOnGetLobbyResponse OnFailure;

    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Backend|Lobby")
    static UAsyncAction_GetLobby* GetBackendLobby(UObject* WorldContextObject, const FString& BackendLobbyId);

    virtual void Activate() override;

private:
    UObject* WorldContext;
    FString BackendLobbyId;

    void OnRequestComplete(bool bSuccess, const FString& Response);
};