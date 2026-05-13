#include "Commands/UnrealMCPPCGCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "EditorAssetLibrary.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGEdge.h"

FUnrealMCPPCGCommands::FUnrealMCPPCGCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPPCGCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("read_pcg_graph"))
    {
        return HandleReadPCGGraph(Params);
    }
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown PCG command: %s"), *CommandType));
}

static TSharedPtr<FJsonObject> SerializePCGPin(UPCGPin* Pin)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Pin) return Obj;
    Obj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
    Obj->SetNumberField(TEXT("edge_count"), Pin->Edges.Num());
    return Obj;
}

static TSharedPtr<FJsonObject> SerializePCGNode(UPCGNode* Node)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Node) return Obj;
    Obj->SetStringField(TEXT("name"), Node->GetName());
    Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
    if (UPCGSettings* Settings = Node->GetSettings())
    {
        Obj->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetName());
    }

    TArray<TSharedPtr<FJsonValue>> Inputs;
    for (UPCGPin* P : Node->GetInputPins())
    {
        Inputs.Add(MakeShared<FJsonValueObject>(SerializePCGPin(P)));
    }
    Obj->SetArrayField(TEXT("input_pins"), Inputs);

    TArray<TSharedPtr<FJsonValue>> Outputs;
    for (UPCGPin* P : Node->GetOutputPins())
    {
        Outputs.Add(MakeShared<FJsonValueObject>(SerializePCGPin(P)));
    }
    Obj->SetArrayField(TEXT("output_pins"), Outputs);

    return Obj;
}

TSharedPtr<FJsonObject> FUnrealMCPPCGCommands::HandleReadPCGGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    if (!Params->TryGetStringField(TEXT("graph_path"), Path))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'graph_path' parameter"));
    }

    UPCGGraph* Graph = Cast<UPCGGraph>(UEditorAssetLibrary::LoadAsset(Path));
    if (!Graph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load PCGGraph: %s"), *Path));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("graph_path"), Path);
    Result->SetStringField(TEXT("graph_name"), Graph->GetName());

    TArray<TSharedPtr<FJsonValue>> NodeArr;
    for (UPCGNode* Node : Graph->GetNodes())
    {
        NodeArr.Add(MakeShared<FJsonValueObject>(SerializePCGNode(Node)));
    }
    Result->SetArrayField(TEXT("nodes"), NodeArr);

    // Input/output node entries too (these are special accessors on UPCGGraph).
    if (UPCGNode* InputNode = Graph->GetInputNode())
    {
        Result->SetObjectField(TEXT("input_node"), SerializePCGNode(InputNode));
    }
    if (UPCGNode* OutputNode = Graph->GetOutputNode())
    {
        Result->SetObjectField(TEXT("output_node"), SerializePCGNode(OutputNode));
    }

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}
