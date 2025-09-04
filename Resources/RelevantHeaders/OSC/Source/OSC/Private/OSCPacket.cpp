// Copyright Epic Games, Inc. All Rights Reserved.
#include "OSCPacket.h"

#include "OSCMessagePacket.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"


namespace UE::OSC
{
	TSharedPtr<IPacket> IPacket::CreatePacket(const uint8* InPacketType, const FIPv4Endpoint& InIPEndpoint)
	{
		const FString PacketIdentifier(ANSI_TO_TCHAR((const ANSICHAR*)&InPacketType[0]));
	
		TSharedPtr<FPacketBase> Packet;
		if (PacketIdentifier.StartsWith(OSC::PathSeparator))
		{
			Packet = MakeShared<FMessagePacket>(InIPEndpoint);
		}
		else if (PacketIdentifier == UE::OSC::BundleTag)
		{
			Packet = MakeShared<OSC::FBundlePacket>(InIPEndpoint);
		}
		else
		{
			UE_LOG(LogOSC, Warning, TEXT("Failed to parse lead character of OSC packet. "
				"Lead identifier of '%c' not valid bundle tag ('%s') or message ('%s') identifier."), PacketIdentifier[0], *OSC::BundleTag, *OSC::PathSeparator);
			return nullptr;
		}

		return Packet;
	}
} // namespace UE::OSC
