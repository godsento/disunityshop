#include "includes.h"

void Hooks::CalcView(vec3_t& eye_origin, vec3_t& eye_angles, float& z_near, float& z_far, float& fov) {
	// cast thisptr to player ptr.
	Player* player = (Player*)this;

	if (!player || !player->m_bIsLocalPlayer())
		return g_hooks.m_CalcView(this, eye_origin, eye_angles, z_near, z_far, fov);

	// Prevent CalcView from calling CCSGOPlayerAnimState::ModifyEyePosition( ... ) 
	// this will fix inaccuracies, for example when fakeducking - and will enforce
	// us to use our own rebuilt version of CCSGOPlayerAnimState::ModifyEyePosition from the server.
	auto m_bUseNewAnimstate = player->get<bool>(0x39E1);

	player->set<int>(0x39E1, false);

	// call og.
	g_hooks.m_CalcView(this, eye_origin, eye_angles, z_near, z_far, fov);

	player->set<int>(0x39E1, m_bUseNewAnimstate);
}