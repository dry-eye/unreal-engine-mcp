#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class FUnrealMCPControlRigCommands
{
public:
    FUnrealMCPControlRigCommands();
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleReadControlRig(const TSharedPtr<FJsonObject>& Params);
};
