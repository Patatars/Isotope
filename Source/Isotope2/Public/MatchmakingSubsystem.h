#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "IsotopeError.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "MatchmakingSubsystem.generated.h"

class IWebSocket;

UENUM(BlueprintType)
enum class EMatchFormat : uint8
{
	THREE_V_THREE UMETA(DisplayName = "Three versus Three"),
	ONE_V_ONE UMETA(DisplayName = "One versus One")
};

USTRUCT(BlueprintType)
struct FMatchmakingMemberProof
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Backend|Matchmaking")
	FString PUID;

	UPROPERTY(BlueprintReadWrite, Category = "Backend|Matchmaking")
	FString Proof;
};

USTRUCT(BlueprintType)
struct FGameAuthData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend|Auth")
	FString PUID;

	UPROPERTY(BlueprintReadOnly, Category = "Backend|Auth")
	FString ExpiresAt;
};

UENUM(BlueprintType)
enum class EMatchmakingSocketEventType : uint8
{
	Queued,
	ServerStarting,
	SessionReady,
	Cancelled,
	MatchmakingFailed,
	SessionClosed
};

USTRUCT(BlueprintType)
struct FMatchmakingSocketEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend|Matchmaking")
	EMatchmakingSocketEventType Type = EMatchmakingSocketEventType::Queued;

	UPROPERTY(BlueprintReadOnly, Category = "Backend|Matchmaking")
	FString LobbyId;

	UPROPERTY(BlueprintReadOnly, Category = "Backend|Matchmaking")
	FString SessionId;
};

DECLARE_DELEGATE_TwoParams(FOnBackendRequestComplete, bool, const FString&);

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnBackendOperationComplete, bool, bSuccess);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnGameAuthenticated, bool, bSuccess, const FGameAuthData&, AuthData);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnLobbyMemberProofCreated, bool, bSuccess, const FString&, PUID, const FString&, Proof);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnJoinTicketCreated, bool, bSuccess, const FString&, JoinTicket);
DECLARE_DYNAMIC_DELEGATE_SixParams(
	FOnJoinTicketConsumed,
	int32, RequestId,
	bool, bSuccess,
	const FString&, ConnectionPUID,
	const FString&, PUID,
	const FString&, SessionId,
	const FString&, Error);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLobbySocketConnectionComplete, bool, bSuccess);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnMatchmakingSocketEvent, const FMatchmakingSocketEvent&, Event);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnLobbySocketDisconnected, int32, StatusCode, const FString&, Reason);

UCLASS(config = Game)
class ISOTOPE2_API UMatchmakingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(config)
	FString BaseUrl = TEXT("http://127.0.0.1:8000");

	UPROPERTY(config)
	FString WebSocketUrl = TEXT("ws://127.0.0.1:8000");

	UPROPERTY(BlueprintAssignable, Category = "Backend|Errors")
	FOnIsotopeError OnError;

	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void AuthenticateEOS(const FString& CredentialType, const FString& Credential, FOnGameAuthenticated Completion);

	UFUNCTION(BlueprintPure, Category = "Backend|Auth")
	bool IsBackendAuthenticated() const { return !GameAuthToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Backend|Auth")
	FString GetAuthenticatedPUID() const { return AuthenticatedPUID; }

	UFUNCTION(BlueprintCallable, Category = "Backend|Lobby")
	void RequestLobbyMemberProof(const FString& LobbyId, const FString& OwnerPUID, FOnLobbyMemberProofCreated Completion);

	UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
	void ConnectLobbySocket(
		const FString& LobbyId,
		FOnLobbySocketConnectionComplete ConnectionCompletion,
		FOnMatchmakingSocketEvent EventHandler,
		FOnLobbySocketDisconnected DisconnectedHandler);

	UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
	void DisconnectLobbySocket();

	UFUNCTION(BlueprintPure, Category = "Backend|Matchmaking")
	bool IsLobbySocketConnected() const;

	UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
	void StartMatchmaking(const FString& LobbyId, const TArray<FMatchmakingMemberProof>& Members, EMatchFormat MatchFormat, FOnBackendOperationComplete Completion);

	UFUNCTION(BlueprintCallable, Category = "Backend|Matchmaking")
	void CancelMatchmaking(const FString& LobbyId, FOnBackendOperationComplete Completion);

	UFUNCTION(BlueprintCallable, Category = "Backend|Sessions")
	void RequestJoinTicket(const FString& SessionId, FOnJoinTicketCreated Completion);

	UFUNCTION(BlueprintCallable, Category = "Backend|Sessions")
	void ConsumeJoinTicket(int32 RequestId, const FString& ConnectionPUID, const FString& JoinTicket, FOnJoinTicketConsumed Completion);

	UFUNCTION(BlueprintCallable, Category = "Backend|Server")
	void ReportServerReady(const FString& SessionId);

	UFUNCTION(BlueprintCallable, Category = "Backend|Server")
	void ReportServerShutdown();

private:
	enum class ERequestAuth : uint8
	{
		None,
		Player,
		Server
	};

	void SendJsonRequest(
		const FString& Verb,
		const FString& Endpoint,
		const FString& Body,
		ERequestAuth RequestAuth,
		FOnBackendRequestComplete Completion);

	void OnResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully,
		FOnBackendRequestComplete Completion);

	void HandleSocketConnected();
	void HandleSocketConnectionError(const FString& Error);
	void HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleSocketMessage(const FString& Message);
	void CompleteLobbySocketConnection(bool bSuccess, const FString& Error);
	void EmitMatchmakingSocketEvent(const FMatchmakingSocketEvent& Event);
	void ReleaseLobbySocket(bool bCloseSocket);
	void ReportError(const FString& Method, const FString& Error);

	FString GetServerApiToken() const;
	static FString GetResponseError(const FString& Response);

	FString GameAuthToken;
	FString AuthenticatedPUID;
	FString ConnectedLobbyId;
	TSharedPtr<IWebSocket> LobbySocket;
	FOnLobbySocketConnectionComplete PendingSocketConnection;
	FOnMatchmakingSocketEvent MatchmakingSocketEventHandler;
	FOnLobbySocketDisconnected SocketDisconnectedHandler;
	bool bSocketConnectionPending = false;
	bool bSocketConnected = false;
};
