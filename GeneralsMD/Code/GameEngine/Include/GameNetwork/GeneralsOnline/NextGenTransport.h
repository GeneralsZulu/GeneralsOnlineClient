#pragma once

#include "GameNetwork/udp.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/Transport.h"
#include "../NGMP_include.h"

#include <steam/isteamnetworkingmessages.h>
#include <deque>
#include <vector>

#pragma comment(lib, "Secur32.lib")

// it to be a MemoryPoolObject (srj)
class NextGenTransport : public Transport //: public MemoryPoolObject
{
	//MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Transport, "Transport")		
public:

	NextGenTransport();
	~NextGenTransport();

	Bool init( AsciiString ip, UnsignedShort port ) override;
	Bool init( UnsignedInt ip, UnsignedShort port ) override;
	void reset( void ) override;
	Bool update( void ) override;									///< Call this once a GameEngine tick, regardless of whether the frame advances.

	Bool doRecv( void ) override;		///< call this to service the receive packets
	Bool doSend( void ) override;		///< call this to service the send queue.

	Bool queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
		NetMessageFlags flags, Int id */);				///< Queue a packet for sending to the specified address and port.  This will be sent on the next update() call.

	inline Bool allowBroadcasts(Bool val) override { return false; }

	// GameNetworkingSockets with k_nSteamNetworkingSend_Reliable: delivery and
	// order are the library's job, see PlayerConnection::SendGamePacket.
	Bool isReliable( void ) const override { return TRUE; }

	// Helper to clear a packet from the receive buffer (accounts for zero-length packets)
	void clearInBufferSlot(int slotIndex)
	{
		if (slotIndex >= 0 && slotIndex < MAX_MESSAGES)
		{
			m_inBuffer[slotIndex].length = 0;
			m_inBufferOccupied[slotIndex] = false;
		}
	}

private:
	// Track which incoming buffer slots are occupied (handles zero-length packets)
	bool m_inBufferOccupied[MAX_MESSAGES];

	// TheSuperHackers @bugfix bill-rich 16/10/2026 Packets that arrive while
	// every m_inBuffer slot is still unread. The buffer is MAX_MESSAGES (256)
	// deep and drained once per engine tick, but a single doRecv can pull up to
	// 255 messages per peer out of the socket library, and after a resume
	// catch-up or during a resend storm it does: 7901 packets were dropped on
	// the floor on 2026-10-14, every one of them during a burst, and dropping
	// frame data in the middle of a resend storm is what keeps the storm going.
	// Held packets go into the buffer, oldest first, as slots free up.
	struct HeldPacket
	{
		TransportMessageHeader header;
		std::vector<UnsignedByte> data;
	};
	static constexpr size_t MAX_HELD_PACKETS = 8192;	// ~1.2 KB each at the worst, a burst's worth
	std::deque<HeldPacket> m_held;
	UnsignedInt m_heldHighWater = 0;
	UnsignedInt m_heldDropped = 0;
	UnsignedInt m_lastHeldLogMs = 0;

	Int findFreeInSlot();
	void writeInSlot(Int slot, const TransportMessageHeader& header, const UnsignedByte* payload, UnsignedInt payloadLen, int64_t userID);
	void drainHeld();
	bool storeIncoming(const TransportMessage& incoming, UnsignedInt payloadLen, int64_t userID);
	void logHeld(UnsignedInt now);
};
