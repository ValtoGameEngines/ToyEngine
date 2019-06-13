//  Copyright (c) 2019 Hugo Amiard hugo.amiard@laposte.net
//  This software is licensed  under the terms of the GNU General Public License v3.0.
//  See the attached LICENSE.txt file or https://www.gnu.org/licenses/gpl-3.0.en.html.
//  This notice and the license may not be removed or altered from any source distribution.

#pragma once

#include <math/Vec.h>
#include <core/Forward.h>
#include <core/Spatial/Spatial.h>

namespace toy
{
	TOY_CORE_EXPORT func_ bool move_2d(Spatial& spatial, Movable& movable, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT func_ bool steer_2d(Spatial& spatial, Movable& movable, const vec3& target, float velocity, float time_step, float margin = 0.1f);

	TOY_CORE_EXPORT bool spin_2d(Spatial& spatial, const vec3& axis, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT bool lookat_2d(Spatial& spatial, const vec3& axis, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT bool move_2d(Spatial& spatial, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT bool steer_2d(Spatial& spatial, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT bool stick_2d(Spatial& spatial, const vec3& target, float velocity, float time_step, float margin = 0.1f);
	TOY_CORE_EXPORT bool track_2d(Spatial& spatial, const vec3& target, float velocity, float time_step, float margin = 0.1f);

	class refl_ TOY_CORE_EXPORT Movable
	{
	public:
		constr_ Movable() {}
		constr_ Movable(const vec3& position);
		~Movable();

		MotionState* m_motion_state = nullptr;

		attr_ vec3 m_linear_velocity = vec3(0.f);
		attr_ vec3 m_angular_velocity = vec3(0.f);

		vec3 m_direction = -z3;
		vec3 m_acceleration = vec3(0.f);

		attr_ bool m_moving = false;
		attr_ vec3 m_previous_position;

		vec3 m_max_linear_velocity = vec3(0.f);

		size_t m_updated = 0;
		size_t m_acceleration_updated = 0;
		size_t m_last_tick = 0;

        meth_ void set_linear_velocity(const vec3& velocity);
		meth_ void modify_linear_velocity(const vec3& velocity);

        meth_ void set_angular_velocity(const vec3& velocity);
		meth_ void modify_angular_velocity(const vec3& velocity);

		void set_acceleration(const vec3& acceleration, const vec3& maxLinVel);
		void modify_acceleration(const vec3& acceleration); 

		void next_frame(Spatial& spatial, size_t tick, size_t delta);
	};
}
