#include "Commands/UnrealMCPControlRigCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "ControlRigBlueprint.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"

FUnrealMCPControlRigCommands::FUnrealMCPControlRigCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPControlRigCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("read_control_rig"))
    {
        return HandleReadControlRig(Params);
    }
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown control rig command: %s"), *CommandType));
}

static TSharedPtr<FJsonObject> SerializeRigVMPin(URigVMPin* Pin)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Pin) return Obj;
    Obj->SetStringField(TEXT("name"), Pin->GetName());
    Obj->SetStringField(TEXT("cpp_type"), Pin->GetCPPType());
    Obj->SetStringField(TEXT("direction"), Pin->GetDirection() == ERigVMPinDirection::Input ? TEXT("Input")
                                          : Pin->GetDirection() == ERigVMPinDirection::Output ? TEXT("Output")
                                          : Pin->GetDirection() == ERigVMPinDirection::IO ? TEXT("IO")
                                          : Pin->GetDirection() == ERigVMPinDirection::Hidden ? TEXT("Hidden")
                                          : TEXT("Visible"));
    Obj->SetStringField(TEXT("default_value"), Pin->GetDefaultValue());
    Obj->SetBoolField(TEXT("is_array"), Pin->IsArray());
    return Obj;
}

static TSharedPtr<FJsonObject> SerializeRigVMNode(URigVMNode* Node)
{
    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
    if (!Node) return NodeObj;
    NodeObj->SetStringField(TEXT("name"), Node->GetName());
    NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle());

    TArray<TSharedPtr<FJsonValue>> PinArr;
    for (URigVMPin* P : Node->GetPins())
    {
        PinArr.Add(MakeShared<FJsonValueObject>(SerializeRigVMPin(P)));
    }
    NodeObj->SetArrayField(TEXT("pins"), PinArr);
    return NodeObj;
}

static TSharedPtr<FJsonObject> SerializeRigVMGraph(URigVMGraph* Graph)
{
    TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
    if (!Graph) return GraphObj;
    GraphObj->SetStringField(TEXT("name"), Graph->GetName());

    TArray<TSharedPtr<FJsonValue>> NodeArr;
    for (URigVMNode* N : Graph->GetNodes())
    {
        NodeArr.Add(MakeShared<FJsonValueObject>(SerializeRigVMNode(N)));
    }
    GraphObj->SetArrayField(TEXT("nodes"), NodeArr);

    TArray<TSharedPtr<FJsonValue>> LinkArr;
    for (URigVMLink* L : Graph->GetLinks())
    {
        if (!L) continue;
        TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
        if (URigVMPin* Src = L->GetSourcePin()) LinkObj->SetStringField(TEXT("source"), Src->GetPinPath());
        if (URigVMPin* Tgt = L->GetTargetPin()) LinkObj->SetStringField(TEXT("target"), Tgt->GetPinPath());
        LinkArr.Add(MakeShared<FJsonValueObject>(LinkObj));
    }
    GraphObj->SetArrayField(TEXT("links"), LinkArr);
    return GraphObj;
}

TSharedPtr<FJsonObject> FUnrealMCPControlRigCommands::HandleReadControlRig(const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), Path))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    UControlRigBlueprint* CRB = Cast<UControlRigBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
    if (!CRB)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load ControlRigBlueprint: %s"), *Path));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), Path);
    Result->SetStringField(TEXT("blueprint_name"), CRB->GetName());

    TArray<TSharedPtr<FJsonValue>> GraphArr;
    FRigVMClient* Client = CRB->GetRigVMClient();
    if (Client)
    {
        for (URigVMGraph* G : Client->GetAllModels(true, true))
        {
            GraphArr.Add(MakeShared<FJsonValueObject>(SerializeRigVMGraph(G)));
        }
    }
    Result->SetArrayField(TEXT("graphs"), GraphArr);

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}
