#include "includes.h"
Resolver g_resolver{};;

LagRecord* Resolver::FindIdealRecord(AimPlayer* data) {

	LagRecord* first_valid, * current;

	if (data->m_records.empty())
		return nullptr;

	first_valid = nullptr;

	// iterate records.
	for (const auto& it : data->m_records) {
		if (it->dormant() || it->immune() || !it->valid())
			continue;

		// get current record.
		current = it.get();

		// first record that was valid, store it for later.
		if (!first_valid)
			first_valid = current;

		// try to find a record with a shot, lby update, walking or no anti-aim.
		if (it->m_shot || it->m_mode == Modes::RESOLVE_BODY || it->m_mode == Modes::RESOLVE_WALK || it->m_mode == Modes::RESOLVE_NONE)
			return current;
	}

	// none found above, return the first valid record if possible.
	return (first_valid) ? first_valid : nullptr;
}

LagRecord* Resolver::FindLastRecord(AimPlayer* data) {

	LagRecord* current;

	if (data->m_records.empty())
		return nullptr;

	// iterate records in reverse.
	for (auto it = data->m_records.crbegin(); it != data->m_records.crend(); ++it) {
		current = it->get();

		// if this record is valid.
		// we are done since we iterated in reverse.
		if (current->valid() && !current->immune() && !current->dormant())
			return current;
	}

	return nullptr;
}

void Resolver::OnBodyUpdate(Player* player, float value) {

	AimPlayer* data = &g_aimbot.m_players[player->index() - 1];

	// set data.
	data->m_old_body = data->m_body;
	data->m_body = value;
}

float Resolver::GetAwayAngle(LagRecord* record) {

	float  delta{ std::numeric_limits< float >::max() };
	vec3_t pos;
	ang_t  away;

	if (g_cl.m_net_pos.empty()) {
		math::VectorAngles(g_cl.m_local->m_vecOrigin() - record->m_pred_origin, away);
		return away.y;
	}

	float owd = (g_cl.m_latency / 2.f);

	float target = record->m_pred_time;

	for (const auto& net : g_cl.m_net_pos) {
		float dt = std::abs(target - net.m_time);
		if (dt < delta) {
			delta = dt;
			pos = net.m_pos;
		}
	}
	math::VectorAngles(pos - record->m_pred_origin, away);
	return away.y;
}

bool Resolver::ShouldUseFreestand(LagRecord* record) {

	vec3_t src3D, dst3D, forward, right, up, src, dst;
	float back_two, right_two, left_two;
	CGameTrace tr;
	CTraceFilterSimple filter;

	math::AngleVectors(ang_t(0, GetAwayAngle(record), 0), &forward, &right, &up);

	filter.SetPassEntity(record->m_player);
	src3D = record->m_player->GetShootPosition();
	dst3D = src3D + (forward * 384);

	g_csgo.m_engine_trace->TraceRay(Ray(src3D, dst3D), MASK_SHOT_BRUSHONLY | MASK_OPAQUE_AND_NPCS, &filter, &tr);
	back_two = (tr.m_endpos - tr.m_startpos).length();

	g_csgo.m_engine_trace->TraceRay(Ray(src3D + right * 35, dst3D + right * 35), MASK_SHOT_BRUSHONLY | MASK_OPAQUE_AND_NPCS, &filter, &tr);
	right_two = (tr.m_endpos - tr.m_startpos).length();

	g_csgo.m_engine_trace->TraceRay(Ray(src3D - right * 35, dst3D - right * 35), MASK_SHOT_BRUSHONLY | MASK_OPAQUE_AND_NPCS, &filter, &tr);
	left_two = (tr.m_endpos - tr.m_startpos).length();

	if (left_two > right_two) {
		bFacingleft = true;
		bFacingright = false;
		return true;
	}
	else if (right_two > left_two) {
		bFacingright = true;
		bFacingleft = false;
		return true;
	}
	else
		return false;
}

void Resolver::AntiFreestand(LagRecord* record) {

	constexpr float STEP{ 4.f };
	constexpr float RANGE{ 32.f };

	vec3_t enemypos = record->m_player->GetShootPosition();
	float away = GetAwayAngle(record);

	std::vector< AdaptiveAngle > angles{ };
	angles.emplace_back(away - 180.f);
	angles.emplace_back(away + 90.f);
	angles.emplace_back(away - 90.f);

	vec3_t start = g_cl.m_local->GetShootPosition();

	bool valid{ false };

	for (auto it = angles.begin(); it != angles.end(); ++it) {

		vec3_t end{ enemypos.x + std::cos(math::deg_to_rad(it->m_yaw)) * RANGE,
			enemypos.y + std::sin(math::deg_to_rad(it->m_yaw)) * RANGE,
			enemypos.z };

		vec3_t dir = end - start;
		float len = dir.normalize();

		if (len <= 0.f)
			continue;

		for (float i{ 0.f }; i < len; i += STEP) {

			vec3_t point = start + (dir * i);

			int contents = g_csgo.m_engine_trace->GetPointContents(point, MASK_SHOT_HULL);

			if (!(contents & MASK_SHOT_HULL))
				continue;

			float mult = 1.f;

			if (i > (len * 0.5f))
				mult = 1.25f;

			if (i > (len * 0.75f))
				mult = 1.25f;

			if (i > (len * 0.9f))
				mult = 2.f;

			it->m_dist += (STEP * mult);

			valid = true;
		}
	}

	if (!valid) {
		record->m_eye_angles.y = math::NormalizedAngle(away + 180.f);
		return;
	}

	std::sort(angles.begin(), angles.end(),
		[](const AdaptiveAngle& a, const AdaptiveAngle& b) {
			return a.m_dist > b.m_dist;
		});

	AdaptiveAngle* best = &angles.front();

	record->m_eye_angles.y = math::NormalizedAngle(best->m_yaw);
}


void Resolver::MatchShot(AimPlayer* data, LagRecord* record) {

	float shoot_time = -1.f;
	Weapon* weapon = data->m_player->GetActiveWeapon();

	if (weapon)
		shoot_time = weapon->m_fLastShotTime() + g_csgo.m_globals->m_interval;

	if (game::TIME_TO_TICKS(shoot_time) == game::TIME_TO_TICKS(record->m_sim_time)) {

		if (record->m_lag <= 1)
			record->m_shot = true;

		else if (data->m_records.size() >= 2) {
			LagRecord* previous = data->m_records[1].get();

			if (previous && !previous->dormant())
				record->m_eye_angles.x = previous->m_eye_angles.x;
		}
	}
}

void Resolver::SetMode(LagRecord* record) {

	float speed = record->m_velocity.length_2d();

	if (record->m_flags & FL_ONGROUND && speed > 0.1f && !record->m_fake_walk)
		record->m_mode = Modes::RESOLVE_WALK;

	else if (record->m_flags & FL_ONGROUND && (speed <= 0.1f || record->m_fake_walk))
		record->m_mode = Modes::RESOLVE_STAND;

	else if (!(record->m_flags & FL_ONGROUND))
		record->m_mode = Modes::RESOLVE_AIR;
}

void Resolver::ResolveAngles(Player* player, LagRecord* record) {

	AimPlayer* data = &g_aimbot.m_players[player->index() - 1];

	MatchShot(data, record);
	SetMode(record);

	if (g_menu.main.config.mode.get() == 1)
		record->m_eye_angles.x = 90.f;

	// we arrived here we can do the acutal resolve.
	if (record->m_mode == Modes::RESOLVE_WALK)
		ResolveWalk(data, record);

	else if (record->m_mode == Modes::RESOLVE_STAND)
		ResolveStand(data, record);

	else if (record->m_mode == Modes::RESOLVE_AIR)
		ResolveAir(data, record);

	math::NormalizeAngle(record->m_eye_angles.y);
}

void Resolver::ResolveWalk(AimPlayer* data, LagRecord* record) {
	record->m_eye_angles.y = record->m_body;

	data->m_body_update = record->m_anim_time + 0.22f;

	data->m_stand_index = 0;
	data->m_stand_index2 = 0;
	data->m_body_index = 0;
	data->m_moved = false;

	std::memcpy(&data->m_walk_record, record, sizeof(LagRecord));
}

void Resolver::ResolveStand(AimPlayer* data, LagRecord* record) {

	float away = GetAwayAngle(record);
	LagRecord* move = &data->m_walk_record;
	C_AnimationLayer* curr = &record->m_layers[3];
	int act = data->m_player->GetSequenceActivity(curr->m_sequence);

	if (move->m_sim_time > 0.f) {
		vec3_t delta = move->m_origin - record->m_origin;

		if (delta.length() <= 16.f) {
			data->m_moved = true;
		}
	}

	if (data->m_body != data->m_old_body && data->m_body_index < 2) {
		record->m_eye_angles.y = record->m_body;
		data->m_body_update = record->m_anim_time + 1.1f;
		iPlayers[record->m_player->index()] = false;
		record->m_mode = Modes::RESOLVE_BODY;
	}

	else if (record->m_anim_time >= data->m_body_update && data->m_body_index > 2 && data->m_body_index < 4) {
		record->m_eye_angles.y = record->m_body;
		data->m_body_update = record->m_anim_time + 1.1f;
		iPlayers[record->m_player->index()] = false;
		record->m_mode = Modes::RESOLVE_BODY;
	}

	else if (data->m_moved && data->m_stand_index < 1 && !ShouldUseFreestand(record)) {
		record->m_mode = Modes::RESOLVE_STAND;
		record->m_eye_angles.y = move->m_body;
	}

	else if (data->m_stand_index < 2) {
		AntiFreestand(record);
		record->m_mode = Modes::RESOLVE_STAND;
	}

	else {
		record->m_mode = Modes::RESOLVE_STAND2;
		switch (data->m_stand_index2 % 6) {

		case 0:
			record->m_eye_angles.y = record->m_body - 110.f;
			break;

		case 1:
			record->m_eye_angles.y = record->m_body + 110.f;
			break;

		case 2:
			record->m_eye_angles.y = away - 90.f;
			break;

		case 3:
			record->m_eye_angles.y = away + 90.f;
			break;

		case 4:
			record->m_eye_angles.y = away - 180.f;
			break;

		case 5:
			record->m_eye_angles.y = record->m_body;
			break;

		default:
			break;
		}
	}
}

void Resolver::ResolveAir(AimPlayer* data, LagRecord* record) {

	float velyaw = math::rad_to_deg(std::atan2(record->m_velocity.y, record->m_velocity.x));
	switch (data->m_missed_shots % 3) {
	case 0:
		record->m_eye_angles.y = record->m_body;
		break;

	case 1:
		record->m_eye_angles.y = velyaw + 180.f;
		break;

	case 2:
		record->m_eye_angles.y = velyaw - 90.f;
		break;

	case 3:
		record->m_eye_angles.y = velyaw + 90.f;
		break;
	}
}

void Resolver::ResolvePoses(Player* player, LagRecord* record) {

	if (record->m_mode == Modes::RESOLVE_AIR) {
		player->m_flPoseParameter()[2] = g_csgo.RandomInt(0, 4) * 0.25f;

		player->m_flPoseParameter()[11] = g_csgo.RandomInt(1, 3) * 0.25f;
	}
}