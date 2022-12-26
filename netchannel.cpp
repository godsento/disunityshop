#include "includes.h"

#define NET_FRAMES_BACKUP 64 // must be power of 2. 
#define NET_FRAMES_MASK ( NET_FRAMES_BACKUP - 1 )

int Hooks::SendDatagram(void* data) {
	int backup2 = g_csgo.m_net->m_in_seq;

	if (g_aimbot.m_fake_latency) {
		int ping = g_menu.main.misc.fake_latency_amt.get();

		// the target latency.
		float correct = std::max(0.f, (ping / 1000.f) - g_cl.m_latency - g_cl.m_lerp);

		g_csgo.m_net->m_in_seq += 2 * NET_FRAMES_MASK - static_cast<uint32_t>(NET_FRAMES_MASK * correct);
	}

	int ret = g_hooks.m_net_channel.GetOldMethod< SendDatagram_t >(INetChannel::SENDDATAGRAM)(this, data);

	g_csgo.m_net->m_in_seq = backup2;

	return ret;
}



static bool m_bIsFlipedState = false;
void Hooks::ProcessPacket(void* packet, bool header) {
	if (!g_csgo.m_net) {
		g_hooks.m_net_channel.GetOldMethod< ProcessPacket_t >(INetChannel::PROCESSPACKET)(this, packet, header);
		return;
	}

	const auto orig = g_csgo.m_net->m_in_rel_state;

	g_hooks.m_net_channel.GetOldMethod< ProcessPacket_t >(INetChannel::PROCESSPACKET)(this, packet, header);

	if (g_csgo.m_net->m_in_rel_state != orig)
		m_bIsFlipedState = true;
}