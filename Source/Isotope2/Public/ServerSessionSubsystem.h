#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/OnlineServices.h"

#include "EOSShared.h"
#include "IEOSSDKManager.h"
#include "eos_sessions.h"
#include "eos_sdk.h"

#include "ServerSessionSubsystem.generated.h"

class APlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerSessionCreated, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerSessionDestroyed, bool, bSuccess);

UCLASS()
class ISOTOPE2_API UServerSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Server|Sessions")
	void CreateServerSession(int32 MaxPlayers);

	UFUNCTION(BlueprintCallable, Category = "Server|Sessions")
	void DestroyServerSession();

	UFUNCTION(BlueprintPure, Category = "Server|Sessions")
	FString GetCurrentSessionId() const { return CurrentSessionId; }

	UFUNCTION(BlueprintPure, Category = "Server|Players")
	static FString GetPlayerUniqueNetIdString(APlayerState* PlayerState);

	UFUNCTION(BlueprintPure, Category = "Server|Players")
	static FString GetPlayerPUID(APlayerState* PlayerState);

	UFUNCTION(BlueprintPure, Category = "Server|Players")
	bool IsPlayerInCurrentSession(APlayerState* PlayerState) const;

	UPROPERTY(BlueprintAssignable, Category = "Server|Events")
	FOnServerSessionCreated OnServerSessionCreated;

	UPROPERTY(BlueprintAssignable, Category = "Server|Events")
	FOnServerSessionDestroyed OnServerSessionDestroyed;

private:
	void InitializeOnlineServices();
	bool InitEOSSDKHandles();

	void CreateNativeEOSSession(int32 MaxPlayers);

	bool AddStringAttribute(
		EOS_HSessionModification ModificationHandle,
		const char* Key,
		const char* Value,
		EOS_ESessionAttributeAdvertisementType AdvertisementType
	);

	static void EOS_CALL OnUpdateSessionComplete(
		const EOS_Sessions_UpdateSessionCallbackInfo* Data
	);

	static void EOS_CALL OnDestroySessionComplete(
		const EOS_Sessions_DestroySessionCallbackInfo* Data
	);

private:
	UE::Online::IOnlineServicesPtr Services;

	EOS_HSessions EOSSessionsHandle = nullptr;
	EOS_HSessionModification SessionModificationHandle = nullptr;

	FString CurrentSessionId;

	static constexpr const TCHAR* NativeSessionName = TEXT("GameSession");
	static constexpr const TCHAR* NativeBucketId = TEXT("Isotope");
};
