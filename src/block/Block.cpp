//  Copyright (c) 2019 Hugo Amiard hugo.amiard@laposte.net
//  This software is licensed  under the terms of the GNU General Public License v3.0.
//  See the attached LICENSE.txt file or https://www.gnu.org/licenses/gpl-3.0.en.html.
//  This notice and the license may not be removed or altered from any source distribution.

#ifdef TWO_MODULES
module toy.block
#else
#include <math/Grid.hpp>
#include <math/Image256.h>
#include <geom/Shapes.h>
#include <ecs/ECS.hpp>
#include <core/World/World.hpp>
#include <core/Spatial/Spatial.h>
#include <core/Physic/Scope.h>
#include <core/Physic/CollisionShape.h>
#include <core/WorldPage/WorldPage.h>
#include <block/Types.h>
#include <block/Block.h>
#include <block/Sector.h>
#endif

#define BLOCK_SUBDIV 20U

namespace toy
{
	void index_blocks(const uvec3& grid_size, vector2d<Block*>& grid, span<Block*> blocks, span<Sector*> sectors)
	{
		grid.reset(grid_size.x, grid_size.y, grid_size.z);

		for(size_t i = 0; i < blocks.size(); ++i)
		{
			uvec3 coord = sectors[i]->m_coordinate;
			grid.at(coord.x, coord.y, coord.z) = blocks[i];
		}

		for(size_t i = 0; i < blocks.size(); ++i)
		{
			uvec3 coord = sectors[i]->m_coordinate;
			size_t index = grid.index_at(coord.x, coord.y, coord.z);
			for(Side side : c_sides)
				blocks[i]->m_neighbours[side] = grid.border(index, side) ? nullptr : grid.neighbour_item(index, side);
		}
	}
	
	void paint_block_height(Block& block, Image256& image, Element& element)
	{
		block.reset();

		uint16_t subdiv = block.subdiv();
		for(uint16_t y = 0; y < subdiv; ++y)
			for(uint16_t x = 0; x < subdiv; ++x)
			{
				uint32_t height = image.at(uvec2(x, y));
				for(uint32_t z = 0; z <= height; ++z)
					block.chunk(x, z, y, element);
			}
	}

	void paint_block_elements(Block& block, Image256& image, span<Element*> elements)
	{
		uint16_t subdiv = block.subdiv();
		for(uint16_t y = 0; y < subdiv; ++y)
			for(uint16_t x = 0; x < subdiv; ++x)
				for(uint16_t z = 0; z < subdiv; ++z)
				{
					size_t colour = image.at(uvec2(x, y));
					Element* element = block.m_chunks.at(x, z, y);

					if(element->m_state == MatterState::Solid)
						block.chunk(x, z, y, *elements[colour]);
				}
	}

	Entity Block::create(ECS& ecs, HSpatial parent, HWorldPage world_page, const vec3& position, Block* parentblock, size_t index, const vec3& size)
	{
		Entity entity = ecs.create<Spatial, Block>();
		asa<Spatial>(entity) = Spatial(parent, position, ZeroQuat);
		asa<Block>(entity) = Block(HSpatial(entity), world_page, parentblock, index, size);
		//, m_emitter(*this, m_spatial, m_movable)
		return entity;
	}

	Block::Block() {}
	Block::Block(HSpatial spatial, HWorldPage world_page, Block* parentblock, size_t index, const vec3& size)
		: m_spatial(spatial)
		, m_world_page(world_page)
		, m_parentblock(parentblock)
		, m_index(index)
		, m_size(size)
		, m_chunks(BLOCK_SUBDIV)
		, m_subblocks(2)
		//, m_scope(m_emitter.add_scope(WorldMedium::me, Cube(m_size), CM_SOURCE))
	{}

	uint16_t Block::depth()
	{
		return m_parentblock ? m_parentblock->depth() + 1 : 0;
	}

	vec3 Block::min(Spatial& self)
	{
		vec3 half_size = m_size / 2.f;
		return self.absolute_position() - half_size;
	}

	vec3 Block::max(Spatial& self)
	{
		vec3 half_size = m_size / 2.f;
		return self.absolute_position() + half_size;
	}

	uvec3 Block::coordinates()
	{
		return m_parentblock ? m_parentblock->block_coord(*this) : uvec3(0U);
	}

	uint16_t Block::subdiv()
	{
		return BLOCK_SUBDIV;
	}

	vec3 Block::chunk_size()
	{
		return m_size / float(this->subdiv());
	}

	void Block::reset()
	{
		for(Element*& chunk : m_chunks)
			chunk = nullptr;
	}

	void Block::chunk(size_t x, size_t y, size_t z, Element& element)
	{
		m_chunks.at(x, y, z) = &element;
	}

	void Block::commit()
	{
		m_updated++;
		WorldPage& page = m_world_page;
		page.m_updated++;
	}

	uvec3 Block::local_block_coord(Block& child)
	{
		size_t index = child.m_index;
		return this->local_block_coord(index);
	}

	uvec3 Block::local_block_coord(size_t index)
	{
		return uvec3(uint(m_subblocks.x(index)), uint(m_subblocks.y(index)), uint(m_subblocks.z(index)));
	}

	uvec3 Block::block_coord(Block& subblock)
	{
		uvec3 coordinates = this->local_block_coord(subblock);

		if(m_parentblock)
			coordinates += m_parentblock->block_coord(*this) * 2U;

		return coordinates;
	}

	uvec3 Block::local_chunk_coord(size_t index)
	{
		return uvec3(uint(m_chunks.x(index)), uint(m_chunks.y(index)), uint(m_chunks.z(index)));
	}

	uvec3 Block::chunk_coord(size_t index)
	{
		uvec3 coordinates = this->local_chunk_coord(index);

		if(m_parentblock)
			coordinates += m_parentblock->block_coord(*this) * uint(this->subdiv());

		return coordinates;
	}

	vec3 Block::chunk_position(size_t index)
	{
		vec3 coordinates = vec3(this->local_chunk_coord(index)) * this->chunk_size() + this->chunk_size() / 2.f - m_size / 2.f;
		return coordinates;
	}

	Hunk Block::hunk_at(size_t index)
	{
		return Hunk(*this, index, m_chunks[index]);
	}

	void Block::subdivide()
	{
		Spatial& spatial = m_spatial;

		for(size_t index = 0; index < 8; ++index)
		{
			vec3 half_size = m_size / 2.f;
			vec3 half_subdiv_size = half_size / 2.f;
			vec3 position = vec3(this->local_block_coord(index)) * half_size - half_size + half_subdiv_size;

			HBlock block = construct<Block>(m_spatial, m_world_page, position, this, index, m_size / 2.f);

			m_subblocks.push_back(block);
		}

		m_subdived = true;
		// "update" trick
		spatial.set_position(spatial.m_position);
	}

	void Block::subdivide_to(uint16_t depth)
	{
		this->subdivide();

		if(depth - 1 > 0)
		{
			for(HBlock& block : m_subblocks)
				block->subdivide_to(depth - 1);
		}
	}

	Hunk Block::neighbour(size_t index, Side side)
	{
		if(m_chunks.border(index, side))
		{
			if(!m_neighbours[side])
				return Hunk();

			size_t neighbour = m_neighbours[side]->m_chunks.neighbour_mod(index, side);
			return m_neighbours[side]->hunk_at(neighbour);
		}
		else
		{
			size_t neighbour = m_chunks.neighbour(index, side);
			return this->hunk_at(neighbour);
		}
	}

	Hunk Block::neighbour(Hunk& hunk, Side side)
	{
		return this->neighbour(hunk.index, side);
	}
}
