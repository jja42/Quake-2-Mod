/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "g_local.h"


/*
=================
check_dodge

This is a support routine used when a client is firing
a non-instant attack weapon.  It checks to see if a
monster's dodge function should be called.
=================
*/
static void check_dodge (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	vec3_t	end;
	vec3_t	v;
	trace_t	tr;
	float	eta;

	// easy mode only ducks one quarter the time
	if (skill->value == 0)
	{
		if (random() > 0.25)
			return;
	}
	VectorMA (start, 8192, dir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if ((tr.ent) && (tr.ent->svflags & SVF_MONSTER) && (tr.ent->health > 0) && (tr.ent->monsterinfo.dodge) && infront(tr.ent, self))
	{
		VectorSubtract (tr.endpos, start, v);
		eta = (VectorLength(v) - tr.ent->maxs[0]) / speed;
		tr.ent->monsterinfo.dodge (tr.ent, self, eta);
	}
}


/*
=================
fire_hit

Used for all impact (hit/punch/slash) attacks
=================
*/
qboolean fire_hit (edict_t *self, vec3_t aim, int damage, int kick)
{
	trace_t		tr;
	vec3_t		forward, right, up;
	vec3_t		v;
	vec3_t		point;
	float		range;
	vec3_t		dir;

	//see if enemy is in range
	VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
	range = VectorLength(dir);
	if (range > aim[0])
		return false;

	if (aim[1] > self->mins[0] && aim[1] < self->maxs[0])
	{
		// the hit is straight on so back the range up to the edge of their bbox
		range -= self->enemy->maxs[0];
	}
	else
	{
		// this is a side hit so adjust the "right" value out to the edge of their bbox
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	VectorMA (self->s.origin, range, dir, point);

	tr = gi.trace (self->s.origin, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	AngleVectors(self->s.angles, forward, right, up);
	VectorMA (self->s.origin, range, forward, point);
	VectorMA (point, aim[1], right, point);
	VectorMA (point, aim[2], up, point);
	VectorSubtract (point, self->enemy->s.origin, dir);

	// do the damage
	T_Damage (tr.ent, self, self, dir, point, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		return false;

	// do our special form of knockback here
	VectorMA (self->enemy->absmin, 0.5, self->enemy->size, v);
	VectorSubtract (v, point, v);
	VectorNormalize (v);
	VectorMA (self->enemy->velocity, kick, v, self->enemy->velocity);
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = NULL;
	return true;
}


/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread, int mod)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	tr = gi.trace(self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction <  1.0))
	{
		vectoangles(aimdir, dir);
		AngleVectors(dir, forward, right, up);

		r = crandom()*hspread;
		u = crandom()*vspread;
		VectorMA(start, 8192, forward, end);
		VectorMA(end, r, right, end);
		VectorMA(end, u, up, end);

		if (gi.pointcontents(start) &  MASK_WATER)
		{
			water = true;
			VectorCopy(start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace(start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents &  MASK_WATER)
		{
			int		color;

			water = true;
			VectorCopy(tr.endpos, water_start);

			if (!VectorCompare(start, tr.endpos))
			{
				if (tr.contents &  CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents &  CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents &  CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_SPLASH);
					gi.WriteByte(8);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.WriteByte(color);
					gi.multicast(tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract(end, start, dir);
				vectoangles(dir, dir);
				AngleVectors(dir, forward, right, up);
				r = crandom()*hspread * 2;
				u = crandom()*vspread * 2;
				VectorMA(water_start, 8192, forward, end);
				VectorMA(end, r, right, end);
				VectorMA(end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace(water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) &&  (tr.surface->flags &  SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				if (Q_stricmp(tr.ent->type, "grass") == 0)
					T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage * 10, kick, DAMAGE_BULLET, mod);
				else{
				T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, mod);
			}
		}
			else
			{
				if (strncmp(tr.surface->name, "sky", 3) != 0)
				{
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(te_impact);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.multicast(tr.endpos, MULTICAST_PVS);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract(tr.endpos, water_start, dir);
		VectorNormalize(dir);
		VectorMA(tr.endpos, -2, dir, pos);
		if (gi.pointcontents(pos) &  MASK_WATER)
			VectorCopy(pos, tr.endpos);
		else
			tr = gi.trace(pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd(water_start, tr.endpos, pos);
		VectorScale(pos, 0.5, pos);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BUBBLETRAIL);
		gi.WritePosition(water_start);
		gi.WritePosition(tr.endpos);
		gi.multicast(pos, MULTICAST_PVS);
	}
}

/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int mod)
{
	fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod);
}


/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int mod)
{
	int		i;

	for (i = 0; i < count; i++)
		fire_lead(self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread, mod);
	
}
edict_t *select;
/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void blaster_water(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}
		gi.dprintf(" HYDRO PUMP ");
	if (other->takedamage)
	{
			if (other->type == "fire"){
					T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 20, 1, DAMAGE_ENERGY, MOD_BLASTER);
					gi.dprintf(" 20 Damage ");
					gi.dprintf(" Super Effective! ");
				}
			else{
				if (other->type == "water" || other->type == "grass"){
					T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 5, 1, DAMAGE_ENERGY, MOD_BLASTER);
					gi.dprintf(" 5 Damage ");
					gi.dprintf(" Not Very Effective ");
				}
				else
				{
					T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 10, 1, DAMAGE_ENERGY, MOD_BLASTER);
					gi.dprintf(" 10 Damage ");
				}
			}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void blaster_fighting(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}
		gi.dprintf("HI JUMP KICK");
	if (other->takedamage)
	{
		if (other->type == "normal"){
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 20, 1, DAMAGE_ENERGY, MOD_BLASTER);
			gi.dprintf(" 20 Damage ");
			gi.dprintf(" Super Effective! ");
		}
		else{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 10, 1, DAMAGE_ENERGY, MOD_BLASTER);
				gi.dprintf(" 10 Damage ");
			}
		}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void blaster_grass(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}
	gi.dprintf("SOLAR BEAM");
	if (other->takedamage)
	{
		if (other->type == "water"){
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 20, 1, DAMAGE_ENERGY, MOD_BLASTER);
			gi.dprintf(" 20 Damage ");
			gi.dprintf(" Super Effective! ");
		}
		else{
			if (other->type == "grass" || other->type == "fire"){
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 5, 1, DAMAGE_ENERGY, MOD_BLASTER);
				gi.dprintf(" 5 Damage ");
				gi.dprintf(" Not Very Effective ");
			}
			else
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 10, 1, DAMAGE_ENERGY, MOD_BLASTER);
				gi.dprintf(" 10 Damage ");
			}
		}
		}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void blaster_heat(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}
		gi.dprintf("FIRE BLAST");
	if (other->takedamage)
	{
		if (other->type == "grass"){
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 20, 1, DAMAGE_ENERGY, MOD_BLASTER);
			gi.dprintf(" 20 Damage ");
			gi.dprintf(" Super Effective! ");
		}
		else{
			if (other->type == "water" || other->type == "fire"){
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 5, 1, DAMAGE_ENERGY, MOD_BLASTER);
				gi.dprintf(" 5 Damage ");
				gi.dprintf(" Not Very Effective ");
			}
			else
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, 10, 1, DAMAGE_ENERGY, MOD_BLASTER);
				gi.dprintf(" 10 Damage ");
			}
		}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void fire_blaster_pokemon(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper, int num)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize(dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles(dir, bolt->s.angles);
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex("misc/lasfly.wav");
	bolt->owner = self;
	if (num == 1){
		bolt->touch = blaster_water;
	}
	if (num == 2){
		bolt->touch = blaster_fighting;
	}
	if (num == 3){
		bolt->touch = blaster_grass;
	}
	if (num == 4){
		bolt->touch = blaster_heat;
	}
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	gi.linkentity(bolt);

	if (self->client)
		check_dodge(self, bolt->s.origin, dir, speed);

	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch(bolt, tr.ent, NULL, NULL);
	}
}
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf &&  (surf->flags &  SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON)
		if (self->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void blaster_touch_0(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON){
			G_FreeEdict(self);
			edict_t	*ent = NULL;
			while ((ent = findradius(ent, other->s.origin, 150)) != NULL)
			{
				if (ent == NULL)
					continue;
				if (!ent->takedamage)
					continue;
				if (ent->svflags & SVF_MONSTER){
					other->enemy = ent;
					FoundTarget(other);
					break;
				}
			}
			
			return;
		}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void blaster_touch_1 (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON){
			G_FreeEdict(self);
			vec3_t forward;
			vec3_t right;
			AngleVectors(other->s.angles, forward, right, NULL);
			fire_blaster_pokemon(other, other->s.origin, forward, 10, 10000, 1, false, 1);
			return;
			}
		}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER);
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict (self);
}

void blaster_touch_2(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON){
			G_FreeEdict(self);
			vec3_t forward;
			vec3_t right;
			AngleVectors(other->s.angles, forward, right, NULL);
			fire_blaster_pokemon(other, other->s.origin, forward, 10, 10000, 1, false, 2);
			return;
		}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}
void blaster_touch_3(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON){
			G_FreeEdict(self);
			vec3_t forward;
			vec3_t right;
			AngleVectors(other->s.angles, forward, right, NULL);
			fire_blaster_pokemon(other, other->s.origin, forward, 10, 10000, 1, false, 3);
			return;
		}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}
void blaster_touch_4(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (other->monsterinfo.aiflags & AI_POKEMON){
			G_FreeEdict(self);
			vec3_t forward;
			vec3_t right;
			AngleVectors(other->s.angles, forward, right, NULL);
			fire_blaster_pokemon(other, other->s.origin, forward, 10, 10000, 1, false, 4);
			return;
		}
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}
void blaster_attack(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}


	if (other->takedamage)
	{	
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg * 10, 1, DAMAGE_ENERGY, MOD_BLASTER);
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(self->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void fire_blaster_client(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper,int num)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize(dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles(dir, bolt->s.angles);
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex("misc/lasfly.wav");
	bolt->owner = self;
	if (num == 0){
		bolt->touch = blaster_touch_0;
	}
	if (num == 1){
		bolt->touch = blaster_touch_1;
	}
	if (num == 2){
		bolt->touch = blaster_touch_2;
	}
	if (num == 3){
		bolt->touch = blaster_touch_3;
	}
	if (num == 4){
		bolt->touch = blaster_touch_4;
	}
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	gi.linkentity(bolt);

	if (self->client)
		check_dodge(self, bolt->s.origin, dir, speed);

	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch(bolt, tr.ent, NULL, NULL);
	}
}


void fire_blaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_attack;
	if (self->client){
		bolt->touch = blaster_touch;
	}
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	gi.linkentity (bolt);

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}	


/*
=================
fire_grenade
=================
*/
qboolean spawn = true;

static void Grenade_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	edict_t	*pokemon;
	if (other == ent->owner)
		return;
	if (surf && !spawn){
		G_FreeEdict(ent);
	}

	if (surf && spawn)
	{
		vec3_t start;
		VectorSet(start, ent->s.origin[0], ent->s.origin[1], 55);
		spawn = false;
		pokemon = G_Spawn();
		VectorCopy(start, pokemon->s.origin);
		SP_monster_soldier_light(pokemon);
		pokemon->monsterinfo.aiflags |= AI_GOOD_GUY;
		pokemon->monsterinfo.aiflags |= AI_POKEMON;
		pokemon->owner = ent->owner;
		pokemon->svflags ^= SVF_MONSTER;
		pokemon->type = "normal";
		G_FreeEdict(ent);
		return;
	}
	
	if (other->monsterinfo.aiflags & AI_POKEMON){
	spawn = true;
	G_FreeEdict(other);
	G_FreeEdict(ent);
		}
	else{
		return;
	}
}

void fire_grenade (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "grenade";

	gi.linkentity (grenade);
}

void fire_grenade2 (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius, qboolean held)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade2/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "hgrenade";
	gi.linkentity (grenade);
}


/*
=================
fire_rocket
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	char *s;
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		gi.cprintf(ent->owner, PRINT_HIGH, other->type, s);
		G_FreeEdict(ent);
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER);
		gi.WritePosition(ent->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(ent->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(ent);
}

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize(dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles(dir, bolt->s.angles);
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = rocket_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	gi.linkentity(bolt);

	if (self->client)
		check_dodge(self, bolt->s.origin, dir, speed);

	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch(bolt, tr.ent, NULL, NULL);
	}
}


/*
=================
fire_rail
=================
*/
void fire_rail (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;
	while (ignore)
	{
		tr = gi.trace (from, NULL, NULL, end, ignore, mask);

		if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
			water = true;
		}
		else
		{
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
				ignore = tr.ent;
			else
				ignore = NULL;

			if ((tr.ent != self) && (tr.ent->takedamage)){
				if (self->client){
					if (tr.ent->monsterinfo.aiflags & AI_POKEMON){
						vec3_t forward;
						vec3_t right;
						AngleVectors(tr.ent->s.angles, forward, right, NULL);
						fire_rail(tr.ent, tr.ent->s.origin, forward, damage, kick);
					}
				}
				if (Q_stricmp(tr.ent->type, "fire") == 0){
					T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, 100, kick, 0, MOD_RAILGUN);
					gi.cprintf(tr.ent->owner, PRINT_HIGH, "Super Effective");
				}
				else
					T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, 15, kick, 0, MOD_RAILGUN);
			}
		}

		VectorCopy (tr.endpos, from);
	}

	// send gun puff / flash
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_RAILTRAIL);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);
//	gi.multicast (start, MULTICAST_PHS);
	if (water)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_RAILTRAIL);
		gi.WritePosition (start);
		gi.WritePosition (tr.endpos);
		gi.multicast (tr.endpos, MULTICAST_PHS);
	}

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
}

void fire_rail_2(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	VectorMA(start, 8192, aimdir, end);
	VectorCopy(start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT | CONTENTS_SLIME | CONTENTS_LAVA;
	while (ignore)
	{
		tr = gi.trace(from, NULL, NULL, end, ignore, mask);

		if (tr.contents & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME | CONTENTS_LAVA);
			water = true;
		}
		else
		{
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
				ignore = tr.ent;
			else
				ignore = NULL;

			if ((tr.ent != self) && (tr.ent->takedamage)){
				if (self->client){
					if (tr.ent->monsterinfo.aiflags & AI_POKEMON){
						vec3_t forward;
						vec3_t right;
						AngleVectors(tr.ent->s.angles, forward, right, NULL);
						fire_rail_2(tr.ent, tr.ent->s.origin, forward, damage, kick);
					}
				}
				if (Q_stricmp(tr.ent->type, "grass") == 0){
					T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, 100, kick, 0, MOD_RAILGUN);
					gi.cprintf(tr.ent->owner, PRINT_HIGH, "Super Effective");
				}
				else
					T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, 15, kick, 0, MOD_RAILGUN);
			}
		}

		VectorCopy(tr.endpos, from);
	}

	// send gun puff / flash
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_RAILTRAIL);
	gi.WritePosition(start);
	gi.WritePosition(tr.endpos);
	gi.multicast(self->s.origin, MULTICAST_PHS);
	//	gi.multicast (start, MULTICAST_PHS);
	if (water)
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_RAILTRAIL);
		gi.WritePosition(start);
		gi.WritePosition(tr.endpos);
		gi.multicast(tr.endpos, MULTICAST_PHS);
	}

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
}
/*
=================
fire_bfg
=================
*/

void fire_bfg(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius)
{
	fire_rail_2(self, start, dir, damage, 5);
}
