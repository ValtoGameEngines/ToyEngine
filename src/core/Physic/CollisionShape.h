//  Copyright (c) 2019 Hugo Amiard hugo.amiard@laposte.net
//  This software is licensed  under the terms of the GNU General Public License v3.0.
//  See the attached LICENSE.txt file or https://www.gnu.org/licenses/gpl-3.0.en.html.
//  This notice and the license may not be removed or altered from any source distribution.

#pragma once

#include <type/Unique.h>
#include <math/Vec.h>
#include <geom/Shape.h>
#include <core/Forward.h>

namespace toy
{
	class refl_ TOY_CORE_EXPORT CollisionShape
	{
	public:
		constr_ CollisionShape();
		constr_ CollisionShape(const Shape& shape, const vec3& center = vec3(0.f), float margin = 0.f);
		~CollisionShape();

		CollisionShape(CollisionShape&& other) = default;
		CollisionShape& operator=(CollisionShape&& other) = default;

		CollisionShape(const CollisionShape& shape);
		CollisionShape& operator=(const CollisionShape& shape);

		object<Shape> m_shape;
		vec3 m_center = vec3(0.f);
		float m_margin = 0.f;

		bool checkInside(const vec3& position) { UNUSED(position); return true; }
	};
}
