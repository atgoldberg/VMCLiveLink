// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "LiveLinkSourceFactory.h"
#include "VMCLiveLinkSourceFactory.generated.h"

#if WITH_EDITOR
class SWidget;
#endif

UCLASS()
class VMCLIVELINK_API UVMCLiveLinkSourceFactory : public ULiveLinkSourceFactory
{
	GENERATED_BODY()

public:
	virtual FText GetSourceDisplayName() const override { return NSLOCTEXT("VMCLiveLink", "DisplayName", "VMC Live Link Source"); }
	virtual FText GetSourceTooltip() const override { return NSLOCTEXT("VMCLiveLink", "Tooltip", "Receive VMC (OSC) motion/curves"); }

#if WITH_EDITOR
	// Editor-only UI surface — kept out of runtime builds
	virtual EMenuType GetMenuType() const override { return EMenuType::SubPanel; }
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const override;
#endif

	// Runtime-capable source creation (safe to keep in runtime module)
	virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;
};