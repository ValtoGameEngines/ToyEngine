//  Copyright (c) 2019 Hugo Amiard hugo.amiard@laposte.net
//  This software is licensed  under the terms of the GNU General Public License v3.0.
//  See the attached LICENSE.txt file or https://www.gnu.org/licenses/gpl-3.0.en.html.
//  This notice and the license may not be removed or altered from any source distribution.

#include <core/Types.h>
#include <core/WorldPage/WorldPage.h>

#include <ecs/Complex.h>
#include <geom/Shapes.h>
#include <geom/Geometry.h>
#include <geom/Geom.h>

#include <core/Spatial/Spatial.h>
#include <core/Movable/Movable.h>
#include <core/Physic/Scope.h>

#include <core/World/World.h>
#include <core/World/Section.h>
#include <core/Physic/PhysicWorld.h>

#include <core/Physic/Collider.h>
#include <core/Physic/Solid.h>

#include <cstdio>

namespace toy
{
	WorldMedium WorldMedium::me;

	WorldMedium::WorldMedium()
		: Medium("World", false)
	{}

	WorldPage::WorldPage(HSpatial spatial, bool open, const vec3& extents)
        : m_spatial(spatial)
		, m_open(open)
		, m_extents(extents)
		, m_world(spatial->m_world)
		//, m_scope(emitter.add_scope(WorldMedium::me, Cube(m_extents / 2.f), CM_SOURCE))
    {
		//m_spatial.m_contents.observe(*this);
	}

    WorldPage::~WorldPage()
    {
		//m_spatial.m_contents.unobserve(*this);
	}

	void WorldPage::next_frame(const Spatial& spatial, size_t tick, size_t delta)
	{
		UNUSED(spatial); UNUSED(tick); UNUSED(delta);
	}

	void WorldPage::update_geometry(size_t tick)
	{
		printf("[info] Updating WorldPage world geometry\n");
		m_solids.clear();
		for(Geometry& geom : m_chunks)
		{
			if(geom.m_vertices.empty() || geom.m_triangles.empty())
				continue;
			printf("[info] WorldPage geometry chunk, %zu vertices\n", geom.m_vertices.size());
			m_solids.push_back(Solid::create(m_spatial, HMovable(), geom, SolidMedium::me, CM_GROUND, true));
		}

		m_chunks.clear();
		m_last_rebuilt = tick;
	}

	/*
	void WorldPage::handle_add(Spatial& spatial)
	{
		UNUSED(spatial);
		//if(!is<Movable>(entity))
		//	m_updated = m_spatial.m_last_tick;
	}

	void WorldPage::handle_remove(Spatial& spatial)
	{
		UNUSED(spatial);
		//if(!is<Movable>(entity))
		//	m_updated = m_spatial.m_last_tick;
	}
	*/
	void WorldPage::ground_point(const vec3& position, bool relative, vec3& ground_point)
	{
		Spatial& spatial = m_spatial;

		// to absolute
		vec3 start(position.x, -m_extents.y / 2, position.z);
		vec3 end(position.x, +m_extents.y / 2, position.z);

		if(relative)
		{
			start += spatial.m_position;
			end += spatial.m_position;
		}

		Ray ray = { start, end, normalize(end - start), normalize(start - end) };
		ground_point = as<PhysicWorld>(m_world->m_complex).ground_point(ray) - spatial.m_position;

		if(any(isnan(ground_point)) || any(isinf(ground_point)))
			printf("[ERROR] raycast ground point failed, position result invalid\n");
	}

	void WorldPage::raycast_ground(const vec3& start, const vec3& end, vec3& ground_point)
	{
		Ray ray = { start, end, normalize(end - start), normalize(start - end) };
		ground_point = as<PhysicWorld>(m_world->m_complex).ground_point(ray);
	}
}
