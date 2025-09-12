#include "VMCLiveLinkSourceFactory.h"
#include "VMCLiveLinkSource.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"

static int32 ParsePort(const FString& Conn, int32 DefaultPort)
{
    int32 Port = DefaultPort;
    TArray<FString> Parts;
    Conn.ParseIntoArray(Parts, TEXT(";"), true);
    for (const FString& P : Parts)
    {
        FString K, V;
        if (P.Split(TEXT("="), &K, &V))
        {
            if (K.Equals(TEXT("port"), ESearchCase::IgnoreCase))
            {
                Port = FCString::Atoi(*V);
            }
        }
    }
    return Port;
}

static bool ParseUnityToUnreal(const FString& Conn, bool DefaultUnityToUnreal)
{
    bool UnityToUnreal = DefaultUnityToUnreal;
    TArray<FString> Parts;
    Conn.ParseIntoArray(Parts, TEXT(";"), true);
    for (const FString& P : Parts)
    {
        FString K, V;
        if (P.Split(TEXT("="), &K, &V))
        {
            if (K.Equals(TEXT("unity2ue"), ESearchCase::IgnoreCase))
            {
                UnityToUnreal = FCString::Atoi(*V) == 1;
            }
        }
    }
    return UnityToUnreal;
}

static bool ParseMetersToCm(const FString& Conn, bool DefaultMetersToCm)
{
    bool MetersToCm = DefaultMetersToCm;
    TArray<FString> Parts;
    Conn.ParseIntoArray(Parts, TEXT(";"), true);
    for (const FString& P : Parts)
    {
        FString K, V;
        if (P.Split(TEXT("="), &K, &V))
        {
            if (K.Equals(TEXT("meters2cm"), ESearchCase::IgnoreCase))
            {
                MetersToCm = FCString::Atoi(*V) == 1;
            }
        }
    }
    return MetersToCm;
}

static FString ParseSubject(const FString& Conn, FString DefaultSubject)
{
    FString Subject = DefaultSubject;
    TArray<FString> Parts;
    Conn.ParseIntoArray(Parts, TEXT(";"), true);
    for (const FString& P : Parts)
    {
        FString K, V;
        if (P.Split(TEXT("="), &K, &V))
        {
            if (K.Equals(TEXT("subject"), ESearchCase::IgnoreCase))
            {
                Subject = FString(*V);
            }
        }
    }
    return Subject;
}



TSharedPtr<ILiveLinkSource> UVMCLiveLinkSourceFactory::CreateSource(const FString& ConnectionString) const
{
    const int32 Port = ParsePort(ConnectionString, 39539);
    const bool UnityToUnreal = ParseUnityToUnreal(ConnectionString, true);
    const bool MetersToCm = ParseMetersToCm(ConnectionString, true);
    const FString Subject = ParseSubject(ConnectionString, "VMC_Subject");

    // Two-arg ctor is exported now (VMCLIVELINK_API on class)
    return MakeShared<FVMCLiveLinkSource>(TEXT("VMC"), Port, UnityToUnreal, MetersToCm, 0, Subject);
}

TSharedPtr<SWidget> UVMCLiveLinkSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated OnCreated) const
{
    struct FState { int32 Port = 39539; bool bUnityToUE = true; bool bMetersToCm = true; FString SubjectName = FString("VMC_Subject"); };

    TSharedRef<FState> State = MakeShared<FState>();
    TSharedPtr<SEditableTextBox> InputTextBox;

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Desc", "Listen for VMC over OSC"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Port", "Port"))]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SSpinBox<int32>)
                        .MinValue(1).MaxValue(65535)
                        .Value_Lambda([State] { return State->Port; })
                        .OnValueChanged_Lambda([State](int32 V) { State->Port = V; })
                ]
        ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Subject", "Subject Name"))]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(InputTextBox, SEditableTextBox)
                        .HintText(NSLOCTEXT("MySource", "InputHint", "Enter subject name..."))
                        .Text_Lambda([State] { return FText().FromString(State->SubjectName); })
                        .OnTextCommitted_Lambda([State](const FText& Text, ETextCommit::Type CommitType) { State->SubjectName = Text.ToString(); } )
                ]
        ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
                [
                    SNew(SCheckBox)
                        .IsChecked_Lambda([State] { return State->bUnityToUE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([State](ECheckBoxState S) { State->bUnityToUE = (S == ECheckBoxState::Checked); })
                        [SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "UnityToUE", "Unity→UE coords"))]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SCheckBox)
                        .IsChecked_Lambda([State] { return State->bMetersToCm ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([State](ECheckBoxState S) { State->bMetersToCm = (S == ECheckBoxState::Checked); })
                        [SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "MetersToCm", "Meters→cm"))]
                ]
        ]
    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4)
        [
            SNew(SUniformGridPanel).SlotPadding(FMargin(4))
                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SButton)
                        .Text(NSLOCTEXT("VMCLiveLink", "Create", "Create"))
                        .OnClicked_Lambda([OnCreated, State]()
                            {
                                const FString Conn = FString::Printf(TEXT("port=%d;unity2ue=%d;meters2cm=%d;subject=%s"),
                                    State->Port, State->bUnityToUE ? 1 : 0, State->bMetersToCm ? 1 : 0, *State->SubjectName);
                                const TSharedPtr<ILiveLinkSource> Src = MakeShared<FVMCLiveLinkSource>(TEXT("VMC"), State->Port, State->bUnityToUE, State->bMetersToCm, 0.0f, State->SubjectName);
                                if (OnCreated.IsBound())
                                {
                                    OnCreated.Execute(Src, Conn);
                                }
                                return FReply::Handled();
                            })
                ]
        ];
}
