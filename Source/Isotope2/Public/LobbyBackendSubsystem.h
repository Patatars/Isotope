#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LobbyBackendSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMatchFormat : uint8 
{
    THREE_V_THREE UMETA(DisplayName = "Three versus Three"),
    ONE_V_ONE UMETA(DisplayName = "One versus One")
};

USTRUCT(BlueprintType)
struct FMatchData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString MatchId;

	UPROPERTY(BlueprintReadOnly)
	FString ServerIP;

	UPROPERTY(BlueprintReadOnly)
	int32 ServerPort = 0;

	UPROPERTY(BlueprintReadOnly)
	FString AdditionalData;
};

DECLARE_DELEGATE_TwoParams(FOnBackendRequestComplete, bool, const FString&);

DECLARE_DELEGATE_TwoParams(FOnMatchmakingInternal, bool, const FString&);

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnMatchmakingQueueResponse, bool, bSuccess, const FString&, Response);

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnMatchFoundDelegate, bool, bSuccess, const FMatchData&, MatchData);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchFoundEvent, bool, bSuccess, const FMatchData&, MatchData);


UCLASS(config = Game)
class ISOTOPE2_API ULobbyBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(config)
    FString BaseUrl = "http://localhost:8000";

    UPROPERTY(config)
    FString AuthToken;

    UPROPERTY(config)
    float PollingInterval = 3.0f;

    UPROPERTY(BlueprintAssignable, Category = "Backend|Matchmaking")
    FOnMatchFoundEvent OnMatchFoundEvent;

    void CreateLobby(const FString& EosLobbyId, const FString& HostEosId, int32 MaxPlayers, FOnBackendRequestComplete OnComplete);
    void GetLobby(const FString& BackendLobbyId, FOnBackendRequestComplete OnComplete);
    void DeleteLobby(const FString& BackendLobbyId, FOnBackendRequestComplete OnComplete);

    // Запустить ranked матчмейкинг (3v3). Автоматически начнёт поллинг.
    // OnQueueResponse вызывается сразу после ответа от бэка на запрос добавления в очередь
    // OnMatchFound вызывается когда матч найден (с IP и портом сервера)
    UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
    void StartMatchmaking(const FString& LobbyId, const TArray<FString>& PlayerIds, EMatchFormat MatchFormat, FOnMatchmakingQueueResponse OnQueueResponse, FOnMatchFoundDelegate OnMatchFound);

    // Запустить кастомную игру. Автоматически начнёт поллинг.
    // OnQueueResponse вызывается сразу после ответа от бэка на запрос старта игры
    // OnMatchFound вызывается когда сервер готов (с IP и портом)
    UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
    void StartCustomGame(const FString& LobbyId, const TArray<FString>& Team1PlayerIds, const TArray<FString>& Team2PlayerIds, FOnMatchmakingQueueResponse OnQueueResponse, FOnMatchFoundDelegate OnMatchFound);

    // Отменить текущий матчмейкинг
    UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
    void CancelMatchmaking(const FString& LobbyId, FOnMatchmakingQueueResponse OnComplete);

    UFUNCTION(BlueprintCallable, Category = "Backend|Server")
    void ReportServerReady();

    UFUNCTION(BlueprintCallable, Category = "Backend|Server")
    void ReportServerShutdown();

private:
    void SendRequest(const FString& Verb, const FString& Endpoint, const FString& Body, FOnBackendRequestComplete OnComplete);
    void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnBackendRequestComplete OnComplete);

    void SendMatchmakingRequest(const FString& Verb, const FString& Endpoint, const FString& Body, FOnMatchmakingInternal OnComplete);
    void OnMatchmakingResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMatchmakingInternal OnComplete);

    FTimerHandle PollingTimerHandle;
    FString CurrentPollingLobbyId;
    FOnMatchFoundDelegate CurrentMatchFoundCallback;

    void StartAutoPolling(const FString& LobbyId);
    void StopAutoPolling();
    void DoPollTick();
    void OnPollResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

    FMatchData ParseMatchData(const FString& JsonResponse);
};
