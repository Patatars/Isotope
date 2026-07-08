#pragma once

#include "CoreMinimal.h"
#include "IsotopeError.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/Sessions.h"
#include "Online/OnlineServices.h"
#include "Online/Auth.h"
#include "Online/Lobbies.h"
#include "Online/ExternalUI.h"
#include "Online/UserInfo.h"
#include "Online/Social.h"
#include "Online/Presence.h"
#include "Online/OnlineServicesCommon.h"

#include "eos_lobby_types.h"

#include "LobbySubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLobbySubsystem, Log, All);

UENUM(BlueprintType)
enum class EIsotopeAttributeType : uint8
{
	String,
	Int64,
	Double,
	Bool
};

UENUM(BlueprintType)
enum class EIsotopeLobbyJoinPolicy : uint8
{
	PublicAdvertised UMETA(DisplayName = "Public Advertised"),
	FriendsOnly      UMETA(DisplayName = "Friends Only"),
	Private          UMETA(DisplayName = "Private")
};

USTRUCT(BlueprintType)
struct FIsotopeAttribute
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	EIsotopeAttributeType Type = EIsotopeAttributeType::String;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString AsString;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	int64 AsInt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	double AsDouble = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	bool AsBool = false;
};

USTRUCT(BlueprintType)
struct FIsotopeLobbyMemberBP
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString AccountId;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	bool bIsLocalMember = false;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	TMap<FString, FIsotopeAttribute> Attributes;
};

USTRUCT(BlueprintType)
struct FIsotopeLobbyBP
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString LobbyId;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString OwnerAccountId;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString LocalName;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString SchemaId;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	int32 MaxMembers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	FString JoinPolicy;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	TMap<FString, FIsotopeAttribute> Attributes;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Lobby")
	TArray<FIsotopeLobbyMemberBP> Members;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLoginSuccess, const FString&, AccountId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIsotopeLoginFailed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeSimpleResult, const FString&, Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLobbyChanged, const FIsotopeLobbyBP&, Lobby);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIsotopeLobbyMemberChanged, const FIsotopeLobbyBP&, Lobby, const FIsotopeLobbyMemberBP&, Member);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLobbyAttributeUpdatedDelegate, const FIsotopeAttribute&, Attribute);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnLobbyMemberAttributeUpdatedDelegate, const FIsotopeLobbyMemberBP&, Member, const FIsotopeAttribute&, Attribute);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLobbyInviteAccepted, const FString&, LobbyIdStr);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDisplayNameReady, FString, AccountId, FString, DisplayName);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnExternalAuthCredentialReady, bool, bSuccess, const FString&, CredentialType, const FString&, Credential);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionJoinStarted, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionJoinFailed, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionJoinSucceeded, const FString&, SessionId, const FString&, ConnectString);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnGameSessionLeaveComplete, bool, bSuccess);

UCLASS()
class ISOTOPE2_API ULobbySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- БЛУПРИНТОВЫЕ СОБЫТИЯ (ДЕЛЕГАТЫ) ---
	UPROPERTY(BlueprintAssignable, Category = "Isotope|Online")
	FOnIsotopeLoginSuccess OnLoginSuccess;
	UPROPERTY(BlueprintAssignable, Category = "Isotope|Online")
	FOnIsotopeLoginFailed OnLoginFailed;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Online")
	FOnIsotopeSimpleResult OnLogout;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyChanged OnLobbyCreated;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyChanged OnLobbyJoined;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyChanged OnLobbyLeft;

	UPROPERTY(BlueprintAssignable, Category = "Online|Lobby")
	FOnDisplayNameReady OnDisplayNameReady;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberJoined;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberLeft;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyChanged OnLobbyAttributesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberAttributesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyLeaderChanged;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyInviteAccepted OnLobbyInviteAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeError OnError;

	UPROPERTY(BlueprintAssignable, Category = "EOS|Sessions")
	FOnSessionJoinStarted OnSessionJoinStarted;

	UPROPERTY(BlueprintAssignable, Category = "EOS|Sessions")
	FOnSessionJoinFailed OnSessionJoinFailed;

	UPROPERTY(BlueprintAssignable, Category = "EOS|Sessions")
	FOnSessionJoinSucceeded OnSessionJoinSucceeded;

public:
	// --- ИНТЕРФЕЙС ДЛЯ BLUEPRINTS ---
	UFUNCTION(BlueprintCallable, Category = "Isotope|Online")
	void Login();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Online")
	void Logout();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void CreateLobby(int32 MaxMembers = 8, EIsotopeLobbyJoinPolicy LobbyJoinPolicy = EIsotopeLobbyJoinPolicy::PublicAdvertised);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void JoinLobby(const FString& LobbyIdStr);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void LeaveLobby();

	// Установка атрибутов лобби
	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttribute(const FString& Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeInt(const FString& Key, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeDouble(const FString& Key, double Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeBool(const FString& Key, bool Value);

	// Установка атрибутов локального игрока внутри лобби
	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttribute(const FString& Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeInt(const FString& Key, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeDouble(const FString& Key, double Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeBool(const FString& Key, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby|Attributes")
	void OnLobbyAttributeUpdated(const FString& Name, FOnLobbyAttributeUpdatedDelegate OnUpdated);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby|Attributes")
	void RemoveLobbyAttributeUpdated(const FString& Name, FOnLobbyAttributeUpdatedDelegate OnUpdated);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby|Attributes")
	void OnLobbyMemberAttributeUpdated(const FString& Name, FOnLobbyMemberAttributeUpdatedDelegate OnUpdated);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby|Attributes")
	void RemoveLobbyMemberAttributeUpdated(const FString& Name, FOnLobbyMemberAttributeUpdatedDelegate OnUpdated);

	// Геттеры состояния для UI
	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	bool GetCurrentLobby(FIsotopeLobbyBP& OutLobby) const;

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	bool GetLobbyMembers(TArray<FIsotopeLobbyMemberBP>& OutMembers) const;

	// Вызовы оверлеев Epic Online Services
	UFUNCTION(BlueprintCallable, Category = "Isotope|UI")
	void ShowFriendsOverlay();

	UFUNCTION(BlueprintCallable, Category = "Isotope|UI")
	void ShowLoginOverlay();

	// Утилиты / Отладка
	UFUNCTION(BlueprintCallable, Category = "Isotope|Debug")
	bool IsLoggedIn() const { return bLoggedIn; }

	UFUNCTION(BlueprintCallable, Category = "Isotope|Debug")
	FString GetLocalAccountIdString() const { return LocalAccountIdString; }

	UFUNCTION(BlueprintCallable, Category = "Isotope|Player", BlueprintPure)
	FString GetPlayerPUID(APlayerState* PlayerState) const;

	UFUNCTION(BlueprintCallable, Category = "EOS|Auth")
	void QueryExternalAuthCredential(FOnExternalAuthCredentialReady Completion);

	UFUNCTION(BlueprintCallable, Category = "EOS|Sessions")
	void ConnectToSessionById(const FString& SessionId, const FString& JoinTicket);

	UFUNCTION(BlueprintCallable, Category = "EOS|Sessions")
	void LeaveGameSession(FOnGameSessionLeaveComplete Completion);



protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	
	void InitializeOnlineServices();
	bool ResolveLocalAccountFromAuthCache();
	bool EnsureLoggedInAndReady(bool bAllowLoginUI);
	FString GetNativeEOSProductUserId(const UE::Online::FAccountId& AccountId) const;
	void QueryMemberDisplayName(const UE::Online::FAccountId& AccountId);
	bool GetCurrentJoinedLobbyNative(TSharedPtr<const UE::Online::FLobby>& OutLobby) const;
	FIsotopeLobbyBP BuildLobbySnapshot(const UE::Online::FLobby& NativeLobby) const;
	FIsotopeLobbyMemberBP BuildMemberSnapshot(const UE::Online::FLobbyMember& NativeMember) const;
	FIsotopeAttribute ConvertVariantToAttribute(const FString& Key, const UE::Online::FSchemaVariant& Variant) const;
	void BroadcastLobbyAttributeUpdated(const UE::Online::FSchemaAttributeId& AttributeId, const UE::Online::FSchemaVariant& Value);
	void BroadcastLobbyMemberAttributeUpdated(const FIsotopeLobbyMemberBP& Member, const UE::Online::FSchemaAttributeId& AttributeId, const UE::Online::FSchemaVariant& Value);
	void ModifyLobbyAttribute(const FString& Key, UE::Online::FSchemaVariant Value);
	void ModifyLobbyMemberAttribute(const FString& Key, UE::Online::FSchemaVariant Value);
	void ReportError(const FString& Method, const FString& Error);
	void ReportSessionJoinFailure(const FString& SessionId, const FString& Error);

private:
	// --- ХЭНДЛЫ НАТИВНЫХ СОБЫТИЙ ONLINE SERVICES (OSSv2) ---
	UE::Online::FOnlineEventDelegateHandle UILobbyJoinRequestedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyJoinedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyLeftHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyUpdatedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyMemberJoinedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyMemberLeftHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyMemberUpdatedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyLeaderChangedHandle;

	// --- МЕТОДЫ ОБРАБОТКИ НА ТИВНЫХ СОБЫТИЙ ---
	void HandleUILobbyJoinRequested(const UE::Online::FUILobbyJoinRequested& EventParams);
	void HandleNativeLobbyJoined(const UE::Online::FLobbyJoined& EventParams);
	void HandleNativeLobbyLeft(const UE::Online::FLobbyLeft& EventParams);
	void HandleNativeLobbyAttributesChanged(const UE::Online::FLobbyAttributesChanged& EventParams);
	void HandleNativeLobbyMemberJoined(const UE::Online::FLobbyMemberJoined& EventParams);
	void HandleNativeLobbyMemberLeft(const UE::Online::FLobbyMemberLeft& EventParams);
	void HandleNativeLobbyMemberAttributesChanged(const UE::Online::FLobbyMemberAttributesChanged& EventParams);
	void HandleNativeLobbyLeaderChanged(const UE::Online::FLobbyLeaderChanged& EventParams);

	// Прямая интеграция с EOS SDK для обработки системного оверлея (кнопка Leave Party)
	EOS_NotificationId LeaveLobbyRequestedNotificationId = EOS_INVALID_NOTIFICATIONID;
	static void EOS_CALL OnLeaveLobbyRequestedCallback(const EOS_Lobby_LeaveLobbyRequestedCallbackInfo* Data);
	void HandleEOSLeaveLobbyRequested(const char* NativeLobbyIdStr);

private:
	bool bServicesReady = false;
	bool bLoggedIn = false;

	FString LocalAccountIdString;

	FPlatformUserId LocalPlatformUserId = PLATFORMUSERID_NONE;
	UE::Online::FAccountId LocalAccountId;

	UE::Online::FLobbyId CachedNativeLobbyId;
	bool bHasCachedNativeLobbyId = false;

	TSharedPtr<UE::Online::IOnlineServices> Services;
	UE::Online::IAuthPtr Auth;
	UE::Online::ILobbiesPtr Lobbies;
	UE::Online::IExternalUIPtr ExternalUI;
	UE::Online::IUserInfoPtr UserInfo;
	UE::Online::ISocialPtr Social;
	UE::Online::IPresencePtr Presence;
	UE::Online::ISessionsPtr Sessions;


	FIsotopeLobbyBP CachedLobby;
	bool bHasCachedLobby = false;

	TMap<FString, TArray<FOnLobbyAttributeUpdatedDelegate>> LobbyAttributeUpdatedDelegates;
	TMap<FString, TArray<FOnLobbyMemberAttributeUpdatedDelegate>> LobbyMemberAttributeUpdatedDelegates;

};
