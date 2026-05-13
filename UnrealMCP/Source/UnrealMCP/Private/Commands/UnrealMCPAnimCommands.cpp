#include "Commands/UnrealMCPAnimCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "AnimStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"

FUnrealMCPAnimCommands::FUnrealMCPAnimCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPAnimCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("read_anim_blueprint"))
    {
        return HandleReadAnimBlueprint(Params);
    }
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown anim command: %s"), *CommandType));
}

static TSharedPtr<FJsonObject> SerializePin(UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Pin) return Obj;
    Obj->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Obj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
    Obj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
    Obj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
    Obj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
    if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0)
    {
        Obj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
        if (Pin->DefaultObject)
        {
            Obj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
        }
    }
    return Obj;
}

static TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
    if (!Node) return NodeObj;
    NodeObj->SetStringField(TEXT("name"), Node->GetName());
    NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

    TArray<TSharedPtr<FJsonValue>> PinArr;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        PinArr.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
    }
    NodeObj->SetArrayField(TEXT("pins"), PinArr);
    return NodeObj;
}

static TSharedPtr<FJsonObject> SerializeGraph(UEdGraph* Graph)
{
    TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
    if (!Graph) return GraphObj;
    GraphObj->SetStringField(TEXT("name"), Graph->GetName());
    GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());

    TArray<TSharedPtr<FJsonValue>> NodeArr;
    for (UEdGraphNode* N : Graph->Nodes)
    {
        NodeArr.Add(MakeShared<FJsonValueObject>(SerializeNode(N)));
    }
    GraphObj->SetArrayField(TEXT("nodes"), NodeArr);

    TArray<TSharedPtr<FJsonValue>> SubArr;
    for (UEdGraph* Sub : Graph->SubGraphs)
    {
        SubArr.Add(MakeShared<FJsonValueObject>(SerializeGraph(Sub)));
    }
    GraphObj->SetArrayField(TEXT("sub_graphs"), SubArr);
    return GraphObj;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimCommands::HandleReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), Path))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
    if (!AnimBP)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *Path));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), Path);
    Result->SetStringField(TEXT("blueprint_name"), AnimBP->GetName());
    Result->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetName() : TEXT("None"));
    Result->SetStringField(TEXT("target_skeleton"), AnimBP->TargetSkeleton ? AnimBP->TargetSkeleton->GetPathName() : TEXT(""));

    // AnimGraph and friends are in FunctionGraphs / UbergraphPages.
    TArray<TSharedPtr<FJsonValue>> FuncGraphs;
    for (UEdGraph* G : AnimBP->FunctionGraphs)
    {
        FuncGraphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));
    }
    Result->SetArrayField(TEXT("function_graphs"), FuncGraphs);

    TArray<TSharedPtr<FJsonValue>> UberGraphs;
    for (UEdGraph* G : AnimBP->UbergraphPages)
    {
        UberGraphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));
    }
    Result->SetArrayField(TEXT("ubergraph_pages"), UberGraphs);

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}
