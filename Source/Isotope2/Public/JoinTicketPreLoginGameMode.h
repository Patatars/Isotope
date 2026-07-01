#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "JoinTicketPreLoginGameMode.generated.h"

UCLASS()
class ISOTOPE2_API AJoinTicketPreLoginGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void PreLoginAsync(
		const FString& Options,
		const FString& Address,
		const FUniqueNetIdRepl& UniqueId,
		const FOnPreLoginCompleteDelegate& OnComplete) override;

	UFUNCTION(BlueprintCallable, Category = "Server|Authentication")
	void CompleteJoinTicketPreLogin(int32 RequestId, bool bAllowLogin, const FString& Error);

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "Server|Authentication", meta = (DisplayName = "Validate Join Ticket"))
	void ValidateJoinTicket(int32 RequestId, const FString& ConnectionPUID, const FString& JoinTicket);

private:
	struct FPendingPreLogin
	{
		FString Options;
		FString Address;
		FUniqueNetIdRepl UniqueId;
		FOnPreLoginCompleteDelegate Completion;
	};

	TMap<int32, FPendingPreLogin> PendingPreLogins;
	int32 NextPreLoginRequestId = 1;
};
