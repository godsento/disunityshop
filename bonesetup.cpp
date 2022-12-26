#include "includes.h"

Bones g_bones{};;

bool Bones::setup(Player* player, BoneArray* out, LagRecord* record) {
	// if the record isnt setup yet.
	if (!record->m_setup) {
		// run setupbones rebuilt.
		if (!BuildBones(player, 0x7FF00, record->m_bones, record))
			return false;

		// we have setup this record bones.
		record->m_setup = true;
	}

	// record is setup.
	if (out && record->m_setup)
		std::memcpy(out, record->m_bones, sizeof(BoneArray) * 128);

	return true;
}

bool Bones::BuildBones(Player* target, int mask, BoneArray* out, LagRecord* record) {
	vec3_t		     pos[128];
	quaternion_t     q[128];
	vec3_t           backup_origin;
	ang_t            backup_angles;
	float            backup_poses[24];
	C_AnimationLayer backup_layers[13];

	// get hdr.
	CStudioHdr* hdr = target->GetModelPtr();
	if (!hdr)
		return false;

	// get ptr to bone accessor.
	CBoneAccessor* accessor = &target->m_BoneAccessor();
	if (!accessor)
		return false;

	// store origial output matrix.
	// likely cachedbonedata.
	BoneArray* backup_matrix = accessor->m_pBones;
	if (!backup_matrix)
		return false;

	// prevent the game from calling ShouldSkipAnimationFrame.
	auto bSkipAnimationFrame = *reinterpret_cast<int*>(uintptr_t(target) + 0x260);
	*reinterpret_cast<int*>(uintptr_t(target) + 0x260) = NULL;

	// backup original.
	backup_origin = target->GetAbsOrigin();
	backup_angles = target->GetAbsAngles();
	target->GetPoseParameters(backup_poses);
	target->GetAnimLayers(backup_layers);

	// compute transform from raw data.
	matrix3x4_t transform;
	math::AngleMatrix(target->GetAbsAngles(), record->m_pred_origin, transform);

	// set non interpolated data.
	target->AddEffect(EF_NOINTERP);
	target->SetAbsOrigin(record->m_pred_origin);

	// force game to call AccumulateLayers - pvs fix.
	m_running = true;

	// set bone array for write.
	accessor->m_pBones = out;

	// compute and build bones.
	target->StandardBlendingRules(hdr, pos, q, record->m_pred_time, mask);

	uint8_t computed[0x100];
	std::memset(computed, 0, 0x100);
	target->BuildTransformations(hdr, pos, q, transform, mask, computed);

	// restore old matrix.
	accessor->m_pBones = backup_matrix;

	// restore original interpolated entity data.
	target->SetAbsOrigin(backup_origin);

	// revert to old game behavior.
	m_running = false;

	// allow the game to call ShouldSkipAnimationFrame.
	*reinterpret_cast<int*>(uintptr_t(target) + 0x260) = bSkipAnimationFrame;

	return true;
}

bool Bones::BuildBonesEx(Player* target, int mask, BoneArray* out) {
	vec3_t		     pos[128];
	quaternion_t     q[128];
	vec3_t           backup_origin;

	// get hdr.
	CStudioHdr* hdr = target->GetModelPtr();
	if (!hdr)
		return false;

	// get ptr to bone accessor.
	CBoneAccessor* accessor = &target->m_BoneAccessor();
	if (!accessor)
		return false;

	// store origial output matrix.
	// likely cachedbonedata.
	BoneArray* backup_matrix = accessor->m_pBones;
	if (!backup_matrix)
		return false;

	// prevent the game from calling ShouldSkipAnimationFrame.
	auto bSkipAnimationFrame = *reinterpret_cast<int*>(uintptr_t(target) + 0x260);
	*reinterpret_cast<int*>(uintptr_t(target) + 0x260) = NULL;

	// backup original.
	backup_origin = target->GetAbsOrigin();

	// compute transform from raw data.
	matrix3x4_t transform;
	math::AngleMatrix(target->GetAbsAngles(), target->m_vecOrigin(), transform);

	// set non interpolated data.
	target->AddEffect(EF_NOINTERP);
	target->SetAbsOrigin(target->m_vecOrigin());

	// force game to call AccumulateLayers - pvs fix.
	m_running = true;

	// set bone array for write.
	// accessor->m_pBones = out;

	// compute and build bones.
	target->StandardBlendingRules(hdr, pos, q, g_csgo.m_globals->m_curtime, mask);

	uint8_t computed[0x100];
	std::memset(computed, 0, 0x100);
	target->BuildTransformations(hdr, pos, q, transform, mask, computed);

	// restore old matrix.
	// accessor->m_pBones = backup_matrix;

	// restore original interpolated entity data.
	target->SetAbsOrigin(backup_origin);

	// revert to old game behavior.
	m_running = false;

	// allow the game to call ShouldSkipAnimationFrame.
	*reinterpret_cast<int*>(uintptr_t(target) + 0x260) = bSkipAnimationFrame;

	return true;
}

bool Bones::BuildBonesGame(Player* target, int mask, BoneArray* out, LagRecord* record) {

	// get hdr.
	CStudioHdr* hdr = target->GetModelPtr();
	if (!hdr)
		return false;

	// get ptr to bone accessor.
	CBoneAccessor* accessor = &target->m_BoneAccessor();
	if (!accessor)
		return false;

	// store origial output matrix.
	// likely cachedbonedata.
	BoneArray* backup_matrix = accessor->m_pBones;
	if (!backup_matrix)
		return false;

	const vec3_t m_abs_backup_origin = target->GetAbsOrigin();
	const int m_pIk = target->m_pIK();
	const int Effect = target->m_fEffects();
	const int ClientEntFlag = target->m_ClientEntEffects();
	const int Animlod = target->m_nAnimLODflags();

	// make a backup of globals
	const auto backup_frametime = g_csgo.m_globals->m_frametime;
	const auto backup_curtime = g_csgo.m_globals->m_curtime;

	// fixes for networked players
	g_csgo.m_globals->m_frametime = g_csgo.m_globals->m_interval;
	g_csgo.m_globals->m_curtime = target->m_flSimulationTime();

	// keep track of old occlusion values
	const auto backup_occlusion_flags = target->m_iOcclusionFlags();
	const auto backup_occlusion_framecount = target->m_iOcclusionFramecount();

	// skip occlusion checks in c_cs_player::setup_bones
	target->m_iOcclusionFlags() = 0;
	target->m_iOcclusionFramecount() = 0;

	// invalidate bone cache
	target->InvalidateBoneCache();

	// stop interpolation
	target->AddEffect(EF_NOINTERP);
	target->SetAbsOrigin(record->m_pred_origin);
	target->m_pIK() = 0;
	target->m_nAnimLODflags() &= ~2u;
	target->m_ClientEntEffects() |= 2u;

	// change bone accessor
	accessor->m_pBones = out;

	// build bones
	target->GameSetupBones(nullptr, -1, 0x7FF00, g_csgo.m_globals->m_curtime);

	// restore bone accessor
	accessor->m_pBones = backup_matrix;

	// restore original occlusion
	target->m_iOcclusionFlags() = backup_occlusion_flags;
	target->m_iOcclusionFramecount() = backup_occlusion_framecount;

	// start interpolation again
	target->m_fEffects() = Effect;
	target->SetAbsOrigin(m_abs_backup_origin);
	target->m_pIK() = m_pIk;
	target->m_ClientEntEffects() = ClientEntFlag;
	target->m_nAnimLODflags() = Animlod;

	// restore globals
	g_csgo.m_globals->m_curtime = backup_curtime;
	g_csgo.m_globals->m_frametime = backup_frametime;
}