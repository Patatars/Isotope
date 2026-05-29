// IsotopePartyManager.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IsotopePartyManager.generated.h"

UCLASS(Blueprintable, BlueprintType)
class ISOTOPE2_API UIsotopePartyManager : public UObject
{
    GENERATED_BODY()

public:
    virtual class UWorld* GetWorld() const override;
    /** 1. Запуск автоматической авторизации в EOS */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void StartAutologin();

    /** 2. Создание виртуального лобби для пати */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void CreatePartyLobby();

    /** 3. Покинуть текущее лобби / распустить пати */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void LeavePartyLobby();

    /** 4. Запрос к твоему FastAPI бэкенду на поиск матча для этого пати */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void MatchmakingStart();

    /** 5. Отмена поиска матча */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void MatchmakingCancel();

    /** 6. Геттер для получения ID сессии (строки подключения), чтобы передать её бэкенду */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    FString GetPartyConnectString();

    /** 7. Проверка: является ли игрок лидером этой группы */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    bool IsPartyLeader();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Isotope|Party")
    void SendPartyInvite();
};