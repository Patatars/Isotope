#include "MatchmakingSubsystem.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatchmakingSubsystem, Log, All);

namespace
{
	FString SerializeJson(const TSharedRef<FJsonObject>& JsonObject)
	{
		FString Body;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(JsonObject, Writer);
		return Body;
	}

	TSharedPtr<FJsonObject> DeserializeJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, JsonObject);
		return JsonObject;
	}

	FString MatchFormatToString(const EMatchFormat MatchFormat)
	{
		return MatchFormat == EMatchFormat::ONE_V_ONE
			? TEXT("one_v_one")
			: TEXT("three_v_three");
	}
}

void UMatchmakingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadConfig();
	FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Initialized. BaseUrl=%s WebSocketUrl=%s"), *BaseUrl, *WebSocketUrl);
}

void UMatchmakingSubsystem::Deinitialize()
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Deinitializing"));
	DisconnectLobbySocket();
	GameAuthToken.Empty();
	AuthenticatedPUID.Empty();
	Super::Deinitialize();
}

void UMatchmakingSubsystem::AuthenticateEOS(
	const FString& CredentialType,
	const FString& Credential,
	FOnGameAuthenticated Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("EOS backend authentication started. CredentialType=%s"), *CredentialType);
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("credential_type"), CredentialType);
	Json->SetStringField(TEXT("credential"), Credential);

	SendJsonRequest(TEXT("POST"), TEXT("/auth/eos"), SerializeJson(Json), ERequestAuth::None,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[this, Completion](const bool bSuccess, const FString& Response) mutable
			{
				FGameAuthData AuthData;
				const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Response);
				if (!bSuccess || !JsonObject.IsValid() ||
					!JsonObject->TryGetStringField(TEXT("access_token"), GameAuthToken) ||
					!JsonObject->TryGetStringField(TEXT("puid"), AuthenticatedPUID))
				{
					GameAuthToken.Empty();
					AuthenticatedPUID.Empty();
					const FString Error = GetResponseError(Response);
					UE_LOG(LogMatchmakingSubsystem, Error, TEXT("EOS backend authentication failed: %s"), *Error);
					Completion.ExecuteIfBound(false, AuthData, Error);
					return;
				}

				AuthData.PUID = AuthenticatedPUID;
				JsonObject->TryGetStringField(TEXT("expires_at"), AuthData.ExpiresAt);
				UE_LOG(LogMatchmakingSubsystem, Log, TEXT("EOS backend authentication succeeded. PUID=%s ExpiresAt=%s"), *AuthenticatedPUID, *AuthData.ExpiresAt);
				Completion.ExecuteIfBound(true, AuthData, TEXT(""));
			}));
}

void UMatchmakingSubsystem::RequestLobbyMemberProof(
	const FString& LobbyId,
	const FString& OwnerPUID,
	FOnLobbyMemberProofCreated Completion)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("lobby_id"), LobbyId);
	Json->SetStringField(TEXT("owner_puid"), OwnerPUID);

	SendJsonRequest(TEXT("POST"), TEXT("/lobbies/member-proof"), SerializeJson(Json), ERequestAuth::Player,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[Completion](const bool bSuccess, const FString& Response) mutable
			{
				FString PUID;
				FString Proof;
				const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Response);
				if (!bSuccess || !JsonObject.IsValid() ||
					!JsonObject->TryGetStringField(TEXT("puid"), PUID) ||
					!JsonObject->TryGetStringField(TEXT("proof"), Proof))
				{
					Completion.ExecuteIfBound(false, TEXT(""), TEXT(""), GetResponseError(Response));
					return;
				}

				Completion.ExecuteIfBound(true, PUID, Proof, TEXT(""));
			}));
}

void UMatchmakingSubsystem::ConnectLobbySocket(
	const FString& LobbyId,
	FOnLobbySocketConnectionComplete ConnectionCompletion,
	FOnMatchmakingSocketEvent EventHandler,
	FOnLobbySocketDisconnected DisconnectedHandler)
{
	UE_LOG(LogMatchmakingSubsystem, Log,
		TEXT("WebSocket connect requested. LobbyId=%s Pending=%s Connected=%s BackendAuthenticated=%s"),
		*LobbyId,
		bSocketConnectionPending ? TEXT("true") : TEXT("false"),
		bSocketConnected ? TEXT("true") : TEXT("false"),
		GameAuthToken.IsEmpty() ? TEXT("false") : TEXT("true"));
	if (bSocketConnectionPending || bSocketConnected)
	{
		UE_LOG(LogMatchmakingSubsystem, Warning, TEXT("WebSocket connect rejected: another connection is active"));
		ConnectionCompletion.ExecuteIfBound(false, TEXT("Lobby socket connection is already active"));
		return;
	}

	if (LobbyId.IsEmpty())
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("WebSocket connect rejected: LobbyId is empty"));
		ConnectionCompletion.ExecuteIfBound(false, TEXT("LobbyId is missing"));
		return;
	}

	if (GameAuthToken.IsEmpty())
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("WebSocket connect rejected: backend authentication is missing"));
		ConnectionCompletion.ExecuteIfBound(false, TEXT("AuthenticateEOS must succeed before ConnectLobbySocket"));
		return;
	}

	PendingSocketConnection = MoveTemp(ConnectionCompletion);
	MatchmakingSocketEventHandler = MoveTemp(EventHandler);
	SocketDisconnectedHandler = MoveTemp(DisconnectedHandler);
	bSocketConnectionPending = true;
	bSocketConnected = false;
	ConnectedLobbyId = LobbyId;
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GameAuthToken));

	LobbySocket = FWebSocketsModule::Get().CreateWebSocket(
		FString::Printf(TEXT("%s/ws/lobbies/%s"), *WebSocketUrl, *LobbyId),
		TEXT(""),
		Headers);
	if (!LobbySocket.IsValid())
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("CreateWebSocket returned an invalid socket. LobbyId=%s"), *LobbyId);
		FOnLobbySocketConnectionComplete Completion = MoveTemp(PendingSocketConnection);
		ReleaseLobbySocket(false);
		Completion.ExecuteIfBound(false, TEXT("Failed to create lobby WebSocket"));
		return;
	}

	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("WebSocket object created. Url=%s/ws/lobbies/%s"), *WebSocketUrl, *LobbyId);

	LobbySocket->OnConnected().AddUObject(this, &UMatchmakingSubsystem::HandleSocketConnected);
	LobbySocket->OnConnectionError().AddUObject(this, &UMatchmakingSubsystem::HandleSocketConnectionError);
	LobbySocket->OnClosed().AddUObject(this, &UMatchmakingSubsystem::HandleSocketClosed);
	LobbySocket->OnMessage().AddUObject(this, &UMatchmakingSubsystem::HandleSocketMessage);
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("WebSocket handshake starting. LobbyId=%s"), *LobbyId);
	LobbySocket->Connect();
}

void UMatchmakingSubsystem::DisconnectLobbySocket()
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("WebSocket disconnect requested. LobbyId=%s"), *ConnectedLobbyId);
	if (bSocketConnectionPending)
	{
		FOnLobbySocketConnectionComplete ConnectionCompletion = MoveTemp(PendingSocketConnection);
		ReleaseLobbySocket(true);
		ConnectionCompletion.ExecuteIfBound(false, TEXT("Lobby socket connection was cancelled"));
	}
	else if (bSocketConnected)
	{
		FOnLobbySocketDisconnected DisconnectedHandler = SocketDisconnectedHandler;
		ReleaseLobbySocket(true);
		DisconnectedHandler.ExecuteIfBound(1000, TEXT("Lobby socket was closed by the client"));
	}
	else
	{
		ReleaseLobbySocket(true);
	}
}

bool UMatchmakingSubsystem::IsLobbySocketConnected() const
{
	return LobbySocket.IsValid() && LobbySocket->IsConnected();
}

void UMatchmakingSubsystem::StartMatchmaking(
	const FString& LobbyId,
	const TArray<FMatchmakingMemberProof>& Members,
	const EMatchFormat MatchFormat,
	FOnBackendOperationComplete Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Matchmaking requested. LobbyId=%s Members=%d MatchFormat=%s"), *LobbyId, Members.Num(), *MatchFormatToString(MatchFormat));
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("lobby_id"), LobbyId);
	Json->SetStringField(TEXT("match_format"), MatchFormatToString(MatchFormat));

	TArray<TSharedPtr<FJsonValue>> MembersJson;
	MembersJson.Reserve(Members.Num());
	for (const FMatchmakingMemberProof& Member : Members)
	{
		TSharedRef<FJsonObject> MemberJson = MakeShared<FJsonObject>();
		MemberJson->SetStringField(TEXT("puid"), Member.PUID);
		MemberJson->SetStringField(TEXT("proof"), Member.Proof);
		MembersJson.Add(MakeShared<FJsonValueObject>(MemberJson));
	}
	Json->SetArrayField(TEXT("members"), MembersJson);

	SendJsonRequest(TEXT("POST"), TEXT("/matchmaking/queue"), SerializeJson(Json), ERequestAuth::Player,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[Completion](const bool bSuccess, const FString& Response) mutable
			{
				Completion.ExecuteIfBound(bSuccess, Response);
			}));
}

void UMatchmakingSubsystem::CancelMatchmaking(
	const FString& LobbyId,
	FOnBackendOperationComplete Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Matchmaking cancellation requested. LobbyId=%s"), *LobbyId);
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("lobby_id"), LobbyId);

	SendJsonRequest(TEXT("POST"), TEXT("/matchmaking/cancel"), SerializeJson(Json), ERequestAuth::Player,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[Completion](const bool bSuccess, const FString& Response) mutable
			{
				Completion.ExecuteIfBound(bSuccess, Response);
			}));
}

void UMatchmakingSubsystem::RequestJoinTicket(
	const FString& SessionId,
	FOnJoinTicketCreated Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Join ticket requested. SessionId=%s"), *SessionId);
	SendJsonRequest(TEXT("POST"), FString::Printf(TEXT("/sessions/%s/join-ticket"), *SessionId), TEXT(""), ERequestAuth::Player,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[Completion](const bool bSuccess, const FString& Response) mutable
			{
				FString JoinTicket;
				const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Response);
				if (!bSuccess || !JsonObject.IsValid() ||
					!JsonObject->TryGetStringField(TEXT("join_ticket"), JoinTicket))
				{
					Completion.ExecuteIfBound(false, TEXT(""), GetResponseError(Response));
					return;
				}

				UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Join ticket created"));
				Completion.ExecuteIfBound(true, JoinTicket, TEXT(""));
			}));
}

void UMatchmakingSubsystem::ConsumeJoinTicket(
	const int32 RequestId,
	const FString& ConnectionPUID,
	const FString& JoinTicket,
	FOnJoinTicketConsumed Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Join ticket consume requested. RequestId=%d ConnectionPUID=%s"), RequestId, *ConnectionPUID);
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("join_ticket"), JoinTicket);

	SendJsonRequest(TEXT("POST"), TEXT("/sessions/join-tickets/consume"), SerializeJson(Json), ERequestAuth::Server,
		FOnBackendRequestComplete::CreateWeakLambda(this,
			[RequestId, ConnectionPUID, Completion](const bool bSuccess, const FString& Response) mutable
			{
				FString PUID;
				FString SessionId;
				const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Response);
				if (!bSuccess || !JsonObject.IsValid() ||
					!JsonObject->TryGetStringField(TEXT("puid"), PUID) ||
					!JsonObject->TryGetStringField(TEXT("session_id"), SessionId))
				{
					Completion.ExecuteIfBound(RequestId, false, ConnectionPUID, TEXT(""), TEXT(""), GetResponseError(Response));
					return;
				}

				UE_LOG(LogMatchmakingSubsystem, Log, TEXT("Join ticket consumed. PUID=%s SessionId=%s"), *PUID, *SessionId);
				Completion.ExecuteIfBound(RequestId, true, ConnectionPUID, PUID, SessionId, TEXT(""));
			}));
}

void UMatchmakingSubsystem::ReportServerReady(const FString& SessionId)
{
	FString ServerId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ServerId="), ServerId))
	{
		UE_LOG(LogTemp, Error, TEXT("ReportServerReady: -ServerId is missing"));
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("session_id"), SessionId);
	SendJsonRequest(TEXT("POST"), FString::Printf(TEXT("/servers/%s/ready"), *ServerId), SerializeJson(Json), ERequestAuth::Server,
		FOnBackendRequestComplete::CreateLambda([](const bool bSuccess, const FString& Response)
			{
				if (!bSuccess)
				{
					UE_LOG(LogTemp, Error, TEXT("ReportServerReady failed: %s"), *Response);
				}
			}));
}

void UMatchmakingSubsystem::ReportServerShutdown()
{
	FString ServerId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ServerId="), ServerId))
	{
		UE_LOG(LogTemp, Error, TEXT("ReportServerShutdown: -ServerId is missing"));
		return;
	}

	SendJsonRequest(TEXT("POST"), FString::Printf(TEXT("/servers/%s/shutdown"), *ServerId), TEXT(""), ERequestAuth::Server,
		FOnBackendRequestComplete::CreateLambda([](const bool bSuccess, const FString& Response)
			{
				if (!bSuccess)
				{
					UE_LOG(LogTemp, Error, TEXT("ReportServerShutdown failed: %s"), *Response);
				}
			}));
}

void UMatchmakingSubsystem::SendJsonRequest(
	const FString& Verb,
	const FString& Endpoint,
	const FString& Body,
	const ERequestAuth RequestAuth,
	FOnBackendRequestComplete Completion)
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("HTTP request started. Method=%s Endpoint=%s"), *Verb, *Endpoint);
	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + Endpoint);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	if (RequestAuth == ERequestAuth::Player)
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *GameAuthToken));
	}
	else if (RequestAuth == ERequestAuth::Server)
	{
		Request->SetHeader(TEXT("X-Server-Token"), GetServerApiToken());
	}

	if (!Body.IsEmpty())
	{
		Request->SetContentAsString(Body);
	}

	Request->OnProcessRequestComplete().BindUObject(
		this,
		&UMatchmakingSubsystem::OnResponseReceived,
		MoveTemp(Completion));
	Request->ProcessRequest();
}

void UMatchmakingSubsystem::OnResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	const bool bConnectedSuccessfully,
	FOnBackendRequestComplete Completion)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("HTTP request failed without response. Url=%s"), Request.IsValid() ? *Request->GetURL() : TEXT("<invalid>"));
		Completion.ExecuteIfBound(false, TEXT("Backend request failed: no response"));
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	if (StatusCode >= 200 && StatusCode < 300)
	{
		UE_LOG(LogMatchmakingSubsystem, Log, TEXT("HTTP request completed. Status=%d Url=%s"), StatusCode, *Request->GetURL());
	}
	else
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("HTTP request failed. Status=%d Url=%s Error=%s"), StatusCode, *Request->GetURL(), *GetResponseError(Response->GetContentAsString()));
	}
	Completion.ExecuteIfBound(
		StatusCode >= 200 && StatusCode < 300,
		Response->GetContentAsString());
}

void UMatchmakingSubsystem::HandleSocketConnected()
{
	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("WebSocket connected. LobbyId=%s"), *ConnectedLobbyId);
	bSocketConnected = true;
	CompleteLobbySocketConnection(true, TEXT(""));
}

void UMatchmakingSubsystem::HandleSocketConnectionError(const FString& Error)
{
	UE_LOG(LogMatchmakingSubsystem, Error, TEXT("WebSocket connection failed. LobbyId=%s Error=%s"), *ConnectedLobbyId, *Error);
	FOnLobbySocketConnectionComplete ConnectionCompletion = MoveTemp(PendingSocketConnection);
	ReleaseLobbySocket(false);
	ConnectionCompletion.ExecuteIfBound(false, Error);
}

void UMatchmakingSubsystem::HandleSocketClosed(
	const int32 StatusCode,
	const FString& Reason,
	const bool bWasClean)
{
	UE_LOG(LogMatchmakingSubsystem, Warning, TEXT("WebSocket closed. LobbyId=%s Status=%d Clean=%s Reason=%s"), *ConnectedLobbyId, StatusCode, bWasClean ? TEXT("true") : TEXT("false"), *Reason);
	if (bSocketConnectionPending)
	{
		FOnLobbySocketConnectionComplete ConnectionCompletion = MoveTemp(PendingSocketConnection);
		ReleaseLobbySocket(false);
		ConnectionCompletion.ExecuteIfBound(false, Reason);
	}
	else if (bSocketConnected)
	{
		FOnLobbySocketDisconnected DisconnectedHandler = SocketDisconnectedHandler;
		ReleaseLobbySocket(false);
		DisconnectedHandler.ExecuteIfBound(StatusCode, Reason);
	}
	else
	{
		ReleaseLobbySocket(false);
	}
}

void UMatchmakingSubsystem::CompleteLobbySocketConnection(
	const bool bSuccess,
	const FString& Error)
{
	if (!bSocketConnectionPending)
	{
		return;
	}

	bSocketConnectionPending = false;
	FOnLobbySocketConnectionComplete Completion = MoveTemp(PendingSocketConnection);
	PendingSocketConnection.Unbind();
	Completion.ExecuteIfBound(bSuccess, Error);
}

void UMatchmakingSubsystem::EmitMatchmakingSocketEvent(const FMatchmakingSocketEvent& Event)
{
	MatchmakingSocketEventHandler.ExecuteIfBound(Event);
}

void UMatchmakingSubsystem::ReleaseLobbySocket(const bool bCloseSocket)
{
	if (LobbySocket.IsValid())
	{
		LobbySocket->OnConnected().RemoveAll(this);
		LobbySocket->OnConnectionError().RemoveAll(this);
		LobbySocket->OnClosed().RemoveAll(this);
		LobbySocket->OnMessage().RemoveAll(this);
		if (bCloseSocket)
		{
			LobbySocket->Close();
		}
		LobbySocket.Reset();
	}

	PendingSocketConnection.Unbind();
	MatchmakingSocketEventHandler.Unbind();
	SocketDisconnectedHandler.Unbind();
	bSocketConnectionPending = false;
	bSocketConnected = false;
	ConnectedLobbyId.Empty();
}

void UMatchmakingSubsystem::HandleSocketMessage(const FString& Message)
{
	const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Message);
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogMatchmakingSubsystem, Error, TEXT("WebSocket received invalid JSON"));
		return;
	}

	FString EventName;
	if (!JsonObject->TryGetStringField(TEXT("event"), EventName) || EventName == TEXT("socket_ready"))
	{
		return;
	}

	FMatchmakingSocketEvent Event;
	Event.LobbyId = ConnectedLobbyId;

	if (EventName == TEXT("queued"))
	{
		Event.Type = EMatchmakingSocketEventType::Queued;
	}
	else if (EventName == TEXT("server_starting"))
	{
		Event.Type = EMatchmakingSocketEventType::ServerStarting;
	}
	else if (EventName == TEXT("session_ready"))
	{
		Event.Type = EMatchmakingSocketEventType::SessionReady;
		JsonObject->TryGetStringField(TEXT("session_id"), Event.SessionId);
	}
	else if (EventName == TEXT("cancelled"))
	{
		Event.Type = EMatchmakingSocketEventType::Cancelled;
	}
	else if (EventName == TEXT("failed"))
	{
		Event.Type = EMatchmakingSocketEventType::MatchmakingFailed;
		JsonObject->TryGetStringField(TEXT("reason"), Event.Error);
	}
	else if (EventName == TEXT("session_closed"))
	{
		Event.Type = EMatchmakingSocketEventType::SessionClosed;
	}
	else
	{
		UE_LOG(LogMatchmakingSubsystem, Warning, TEXT("WebSocket received unknown event: %s"), *EventName);
		return;
	}

	UE_LOG(LogMatchmakingSubsystem, Log, TEXT("WebSocket event received. LobbyId=%s Event=%s SessionId=%s Error=%s"), *Event.LobbyId, *EventName, *Event.SessionId, *Event.Error);
	EmitMatchmakingSocketEvent(Event);
}

FString UMatchmakingSubsystem::GetServerApiToken() const
{
	return FPlatformMisc::GetEnvironmentVariable(TEXT("ISOTOPE_SERVER_TOKEN"));
}

FString UMatchmakingSubsystem::GetResponseError(const FString& Response)
{
	const TSharedPtr<FJsonObject> JsonObject = DeserializeJson(Response);
	if (JsonObject.IsValid())
	{
		FString Detail;
		if (JsonObject->TryGetStringField(TEXT("detail"), Detail))
		{
			return Detail;
		}
	}

	return Response.IsEmpty() ? TEXT("Backend request failed") : Response;
}
