//  Copyright (c) 2019 Hugo Amiard hugo.amiard@laposte.net
//  This software is licensed  under the terms of the GNU General Public License v3.0.
//  See the attached LICENSE.txt file or https://www.gnu.org/licenses/gpl-3.0.en.html.
//  This notice and the license may not be removed or altered from any source distribution.

#pragma once

#include <geom/Shape.h>
#include <geom/Symbol.h>
#include <core/Forward.h> 

#ifndef MUD_CPP_20
#include <stl/vector>
#endif

using namespace two; namespace toy
{
	class refl_ TOY_CORE_EXPORT Symbolic
	{
	public:
		constr_ Symbolic(Entity& entity, vector<Symbol> shapes = {});

		attr_ Entity& m_entity;
		attr_ vector<Symbol> m_symbols;
		attr_ size_t m_updated;

		void clearSymbols();
		void addSymbol(const Symbol& symbol);
	};
}
