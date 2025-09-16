// Copyright (c) 2025 Shocap Entertainment | Athomas Goldberg. All Rights Reserved.
// copyright 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg All rights reserved.
#pragma once
#include "LiveLinkSourceFactory.h"
#include "VMCLiveLinkSourceFactory.generated.h"

UCLASS()
class VMCLIVELINKEDITOR_API UVMCLiveLinkSourceFactory : public ULiveLinkSourceFactory
{
    GENERATED_BODY()
public:
    virtual FText GetSourceDisplayName() const override { return NSLOCTEXT("VMCLiveLink", "DisplayName", "VMC Live Link Source"); }
    virtual FText GetSourceTooltip() const override { return NSLOCTEXT("VMCLiveLink", "Tooltip", "Receive VMC (OSC) motion/curves"); }

    // Use a custom UI panel
    virtual EMenuType GetMenuType() const override { return EMenuType::SubPanel; }

    // Build the UI and call OnLiveLinkSourceCreated when the user clicks Create
    virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const override;

    // Optional: allow pasting a connection string like "port=39539"
    virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;
};
