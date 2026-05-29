#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"

#include "Online/OnlineServices.h"
#include "Online/Auth.h"
#include "Online/Lobbies.h"
#include "Online/ExternalUI.h"
#include "Online/UserInfo.h"
#include "Online/Social.h"
#include "Online/Presence.h"
#include "Online/OnlineServicesCommon.h"

#include "eos_lobby_types.h"

#include "IsotopePartySubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIsotopePartySubsystem, Log, All);

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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLoginFailed, const FString&, ErrorText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeSimpleResult, const FString&, Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLobbyChanged, const FIsotopeLobbyBP&, Lobby);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIsotopeLobbyMemberChanged, const FIsotopeLobbyBP&, Lobby, const FIsotopeLobbyMemberBP&, Member);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIsotopeDisplayName, const FString&, AccountId, const FString&, DisplayName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeLobbyInviteAccepted, const FString&, LobbyIdStr);


UCLASS()
class ISOTOPE2_API UIsotopePartySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Isotope|Online")
	FOnIsotopeLoginSuccess OnLoginSuccess;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|UserInfo")
	FOnIsotopeDisplayName OnDisplayNameReady;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|UserInfo")
	FOnIsotopeSimpleResult OnDisplayNameFailed;

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

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberJoined;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberLeft;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyChanged OnLobbyAttributesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyMemberChanged OnLobbyMemberAttributesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeSimpleResult OnBecameLobbyOwner;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeSimpleResult OnInviteUIOpened;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeSimpleResult OnOnlineError;

	UPROPERTY(BlueprintAssignable, Category = "Isotope|Lobby")
	FOnIsotopeLobbyInviteAccepted OnLobbyInviteAccepted;

public:
	UFUNCTION(BlueprintCallable, Category = "Isotope|Online")
	void LoginDeveloper();

	// Читает -AUTH_TYPE, -AUTH_LOGIN, -AUTH_PASSWORD из командной строки и логинится.
	// Пример: -AUTH_TYPE=developer -AUTH_LOGIN=localhost:6547 -AUTH_PASSWORD=Daun2
	// Если аргументы не найдены — фолбэк на LoginDeveloper (кэш → UI).
	UFUNCTION(BlueprintCallable, Category = "Isotope|Online")
	void AutoLoginFromCommandLine();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Online")
	void Logout();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void CreateLobby(int32 MaxMembers = 8, EIsotopeLobbyJoinPolicy LobbyJoinPolicy = EIsotopeLobbyJoinPolicy::PublicAdvertised);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void JoinLobby(const FString& LobbyIdStr);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void LeaveLobby();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttribute(const FString& Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeInt(const FString& Key, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeDouble(const FString& Key, double Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetLobbyAttributeBool(const FString& Key, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttribute(const FString& Key, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeInt(const FString& Key, int64 Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeDouble(const FString& Key, double Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	void SetMemberAttributeBool(const FString& Key, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	bool GetCurrentLobby(FIsotopeLobbyBP& OutLobby) const;

	UFUNCTION(BlueprintCallable, Category = "Isotope|UserInfo")
	void QueryDisplayName(const FString& AccountIdStr);

	UFUNCTION(BlueprintCallable, Category = "Isotope|UserInfo")
	void QueryAllMemberDisplayNames();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Lobby")
	bool GetLobbyMembers(TArray<FIsotopeLobbyMemberBP>& OutMembers) const;

	UFUNCTION(BlueprintCallable, Category = "Isotope|UI")
	void ShowFriendsOverlay();

	UFUNCTION(BlueprintCallable, Category = "Isotope|UI")
	void ShowLoginOverlay();

	UFUNCTION(BlueprintCallable, Category = "Isotope|Debug")
	bool IsLoggedIn() const { return bLoggedIn; }

	UFUNCTION(BlueprintCallable, Category = "Isotope|Debug")
	FString GetLocalAccountIdString() const { return LocalAccountIdString; }


	UFUNCTION(BlueprintCallable, Category = "Isotope|Player", BlueprintPure)
	FString GetPlayerPUID(APlayerState* PlayerState);


protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void InitializeOnlineServices();
	void StartPolling();
	void StopPolling();
	void PollLobbyState();

	bool ResolveLocalAccountFromAuthCache();
	bool EnsureLoggedInAndReady(bool bAllowLoginUI);

	// Нативный LobbyId хранится отдельно, чтобы не конвертировать туда-обратно через FString
	bool GetCurrentJoinedLobbyNative(TSharedPtr<const UE::Online::FLobby>& OutLobby) const;

	FIsotopeLobbyBP BuildLobbySnapshot(const UE::Online::FLobby& NativeLobby) const;
	FIsotopeLobbyMemberBP BuildMemberSnapshot(const UE::Online::FLobbyMember& NativeMember) const;
	FIsotopeAttribute ConvertVariantToAttribute(const FString& Key, const UE::Online::FSchemaVariant& Variant) const;

	static bool AttributeEquals(const FIsotopeAttribute& A, const FIsotopeAttribute& B);
	static bool AttributeMapEquals(const TMap<FString, FIsotopeAttribute>& A, const TMap<FString, FIsotopeAttribute>& B);
	static bool MemberEquals(const FIsotopeLobbyMemberBP& A, const FIsotopeLobbyMemberBP& B);
	static bool LobbyEquals(const FIsotopeLobbyBP& A, const FIsotopeLobbyBP& B);

	UE::Online::FOnlineEventDelegateHandle UILobbyJoinRequestedHandle;
	UE::Online::FOnlineEventDelegateHandle LobbyLeftHandle;

	void HandleUILobbyJoinRequested(const UE::Online::FUILobbyJoinRequested& EventParams);
	void HandleLobbyLeft(const UE::Online::FLobbyLeft& EventParams);

	// EOS SDK direct integration for Leave Party button
	EOS_NotificationId LeaveLobbyRequestedNotificationId = EOS_INVALID_NOTIFICATIONID;
	static void EOS_CALL OnLeaveLobbyRequestedCallback(const EOS_Lobby_LeaveLobbyRequestedCallbackInfo* Data);
	void HandleEOSLeaveLobbyRequested(const char* LobbyId);

	void BroadcastLobbyDelta(const FIsotopeLobbyBP& OldLobby, const FIsotopeLobbyBP& NewLobby);

private:
	bool bServicesReady = false;
	bool bLoggedIn = false;

	FString LocalAccountIdString;

	FPlatformUserId LocalPlatformUserId = PLATFORMUSERID_NONE;
	UE::Online::FAccountId LocalAccountId;

	// Нативный LobbyId для использования в API-вызовах (не конвертируется через FString)
	UE::Online::FLobbyId CachedNativeLobbyId;
	bool bHasCachedNativeLobbyId = false;

	TSharedPtr<UE::Online::IOnlineServices> Services;
	UE::Online::IAuthPtr Auth;
	UE::Online::ILobbiesPtr Lobbies;
	UE::Online::IExternalUIPtr ExternalUI;
	UE::Online::IUserInfoPtr UserInfo;
	UE::Online::ISocialPtr Social;
	UE::Online::IPresencePtr Presence;

	FIsotopeLobbyBP CachedLobby;
	bool bHasCachedLobby = false;

	FTimerHandle PollTimerHandle;
};