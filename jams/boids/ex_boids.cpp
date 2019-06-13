#define STD_HASHMAP

#if !defined STD_HASHMAP && !defined FLAT_HASHMAP
#define STD_HASHMAP
#endif

#ifdef STD_HASHMAP
#include <unordered_map>
#elif defined FLAT_HASHMAP
#include <flat_hash_map.hpp>
#endif

#include <boids/ex_boids.h>
#include <toy/toy.h>

#include <boids/Api.h>
#include <meta/_boids.meta.h>

#include <pool/Pool.hpp>
#include <pool/ObjectPool.hpp>
#include <jobs/JobLoop.hpp>
#include <ecs/Loop.hpp>

#include <stl/vector.hpp>
#include <stl/string.hpp>

#include <Tracy.hpp>

namespace boids
{
	constexpr size_t c_max_threads = 40;

#ifdef BOIDS_SIMD
	inline two::vec3 to_mud(const vec3& v) { return v; }
#else
	inline two::vec3 to_mud(const vec3& v) { return two::vec3(v); }
#endif

	struct GridHash
	{
#ifdef BOIDS_SIMD
		static inline int hash(const vec3& vec, float cellSize) { float3 q = floor(vec / cellSize); return hash(int(q[0]), int(q[1]), int(q[2])); }
#else
		static inline int hash(const vec3& vec, float cellSize) { vec3 q = floor(vec / cellSize); return hash(int(q[0]), int(q[1]), int(q[2])); }
#endif

		// Simple ivec3 hash based on a pseudo mix of :
		// 1) https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
		// 2) https://en.wikipedia.org/wiki/Jenkins_hash_function

		static inline int hash(int x, int y, int z)
		{
			int hash = x;
			hash = (hash * 397) ^ y;
			hash = (hash * 397) ^ z;
			hash += hash << 3;
			hash ^= hash >> 11;
			hash += hash << 15;
			return hash;
		}
	};

	struct BoidParams
	{
		float cell_radius = 1.f;
		float separation_weight = 1.f;
		float alignment_weight = 1.f;
		float target_weight = 2.f;
		float obstacle_aversion_distance = 5.f;
	};

#ifdef BOID_PARAMS4
	struct BoidParams4
	{
		BoidParams4(const BoidParams& p)
			: cell_radius(p.cell_radius), separation_weight(p.separation_weight), alignment_weight(p.alignment_weight)
			, target_weight(p.target_weight), obstacle_aversion_distance(p.obstacle_aversion_distance)
		{}
		vec3 cell_radius;
		vec3 separation_weight;
		vec3 alignment_weight;
		vec3 target_weight;
		//vec3 obstacle_aversion_distance;
		float obstacle_aversion_distance;
	};
#else
	using BoidParams4 = BoidParams;
#endif

	struct Cells
	{
		Cells() {}
		Cells(size_t count)
			: indices(count), hash(count), alignment(count), separation(count), obstacle(count), obstacle_distance(count), target(count), count(count), thread(count)
		{}
		vector<int>   indices;
		vector<int>	  hash;
		vector<int>	  thread;
		vector<vec3>  alignment;
		vector<vec3>  separation;
		vector<int>	  obstacle;
		vector<float> obstacle_distance;
		vector<int>   target;
		vector<int>   count;
		void resize(size_t size)
		{
			indices.resize(size); hash.resize(size), alignment.resize(size); separation.resize(size);
			obstacle.resize(size); obstacle_distance.resize(size); target.resize(size); count.resize(size);
			thread.resize(size);
		}
	};

#ifdef STD_HASHMAP
	using CellMap = std::unordered_map<int, uint32_t>;
#elif defined FLAT_HASHMAP
	using CellMap = ska::flat_hash_map<int, uint32_t>;
#endif

	struct BoidsData
	{
		BoidsData() {}
		Cells				cells;
		CellMap				cell_hashes[c_max_threads];
		vector<Position>    targets;
		vector<Position>    obstacles;
		void resize(size_t num_threads, size_t num_cells, size_t num_targets, size_t num_obstacles)
		{
			cells.resize(num_cells); targets.resize(num_targets); obstacles.resize(num_obstacles);
			for(size_t i = 0; i < num_threads; ++i)
				cell_hashes[i].reserve(num_cells / num_threads);
		}
	};

	struct HashPositions
	{
		span<Position>	positions;
		vector<int>&	hashes;
		vector<int>&	threads;
		float           cell_radius;
		size_t			num_threads;

		inline void operator()(JobSystem& js, Job* job, uint32_t index) const
		{
			UNUSED(js); UNUSED(job);
			const int hash = GridHash::hash(positions[index], cell_radius);
			hashes[index] = hash;
			threads[index] = hash % num_threads;
		}
	};


	void init_cell(Cells& cells, span<Position> targets, span<Position> obstacles, int index)
	{
		struct Result { size_t index; float distance; };
		auto nearest = [](span<Position> targets, const vec3& position) -> Result
		{
			size_t index = 0;
			float distance = length2(position - targets[0].m_value);
			for(size_t i = 1; i < targets.size(); i++)
			{
				const float d = length2(position - targets[i].m_value);
				const bool nearest = d < distance;

				distance = nearest ? d : distance;
				index = nearest ? i : index;
			}
			return { index, sqrtf(distance) };
		};

		vec3 position = cells.separation[index] / vec3(float(cells.count[index]));

		Result obstacle = nearest(obstacles, position);
		cells.obstacle[index] = int(obstacle.index);
		cells.obstacle_distance[index] = obstacle.distance;

		Result target = nearest(targets, position);
		cells.target[index] = int(target.index);

		cells.indices[index] = index;
	}

	void add_cell(Cells& cells, int first, int current)
	{
		cells.count[first] += 1;
		cells.alignment[first] = cells.alignment[first] + cells.alignment[current];
		cells.separation[first] = cells.separation[first] + cells.separation[current];
		cells.indices[current] = first;
	}

	struct MergeCells
	{
		Cells& cells;
		span<int> hashes;
		span<int> threads;
		CellMap* cell_hashes_mt;
		span<Position> targets;
		span<Position> obstacles;

		inline void operator()(JobSystem& js, Job* job, uint32_t start, uint32_t count) const
		{
			UNUSED(job);
			const size_t thread = js.thread();
			for(uint32_t index = start; index < start + count; ++index)
			{
				const size_t cell_thread = threads[index];
				if(cell_thread == thread)
				{
					CellMap& cell_hashes = cell_hashes_mt[thread];
					const auto& pair = cell_hashes.insert({ hashes[index], index });
					if(pair.second == true)
					{
						init_cell(cells, targets, obstacles, index);
					}
					else
					{
						add_cell(cells, pair.first->second, index);
					}
				}
			}
		}
	};

#ifdef BOIDS_SIMD
	void lookat(mat4& result, const vec3& eye, const vec3& view, const vec3& up)
	{
#if 0
		//quat rotation = look_at(eye, target);
		auto fillmat = [](mat4& result, const float3& eye, const float3& view, const float3& right, const float3& up, float dotre, float dotue, float dotve)
		{
			result[0] = { right[0], up[0], view[0], 0.f };
			result[1] = { right[1], up[1], view[1], 0.f };
			result[2] = { right[2], up[2], view[2], 0.f };
			result[3] = { dotre, dotue, dotve, 1.0f };
		};

		vec3 right = normalize(cross(up, view));
		vec3 realup = cross(view, right);
		fillmat(result, eye, view, right, realup, -dot(right, eye), -dot(up, eye), -dot(view, eye));
#else
		UNUSED(view); UNUSED(up);
		float3 pos = eye;
		result[3] = { pos[0], pos[1], pos[2], 1.f };
#endif
	}
#else
	inline vec3 normalize_safe(const vec3& vec)
	{
		float l2 = length2(vec);
		return l2 > 0.f ? vec / sqrt(l2) : vec;
	}
#endif

	inline vec3 steer(const BoidParams4& params, float delta, const vec3& forward, const vec3& position, int count, const vec3& alignment, const vec3& separation,
					  const vec3& obstacle_position, float obstacle_distance, const vec3& target_position)
	{
		vec3 countf3 = vec3(float(count));
		vec3 target_heading = params.target_weight * normalize(target_position - position);
		vec3 align = params.alignment_weight * normalize_safe((alignment / countf3) - forward);
		vec3 separate = params.separation_weight * normalize_safe((position * countf3) - separation);
		vec3 heading = normalize(align + separate + target_heading);

		vec3 avoid = position - obstacle_position;
		vec3 avoid_heading = (obstacle_position + normalize(avoid) * params.obstacle_aversion_distance) - position;
		float near_obstacle = obstacle_distance - params.obstacle_aversion_distance;
		vec3 desired = near_obstacle < 0.f ? heading : avoid_heading;

		return normalize(forward + delta * (desired - forward));
	}

	struct Steer
	{
		const BoidParams4&	m_params;
		const Cells&		m_cells;
		span<Position>		m_targets;
		span<Position>		m_obstacles;
		span<Position>		m_positions;
		vector<Heading>&	m_headings;
		float               m_dt;

		uint32_t m_start;
		uint32_t m_count;

		inline void operator()(JobSystem& js, Job* job, uint32_t start, uint32_t count) const
		{
			UNUSED(js); UNUSED(job);

			ZoneScopedNC("steer", tracy::Color::MediumOrchid);

			for(uint32_t index = start; index < start + count; ++index)
			{
				const int cell = m_cells.indices[index];
				const int obstacle = m_cells.obstacle[cell];
				const int target = m_cells.target[cell];

				m_headings[index] = steer(m_params, m_dt, m_headings[index], m_positions[index], m_cells.count[cell], m_cells.alignment[cell], m_cells.separation[cell],
										  m_obstacles[obstacle], m_cells.obstacle_distance[cell], m_targets[target]);
			}

		}

		inline void execute(JobSystem& js, Job* job)
		{
			(*this)(js, job, m_start, m_count);
		}
	};

	template <class T>
	struct ComponentArray
	{
		size_t size() const { return m_components.size(); }

		T& operator[](size_t index) { return *m_components[index]; }
		const T& operator[](size_t index) const { return *m_components[index]; }

		vector<T*> m_components;
	};

	class BoidSystem
	{
	public:
		//vector<BoidsData> m_data;
		BoidsData m_data;

		void update(JobSystem& job_system, ECS& ecs, const BoidParams& params, float delta)
		{
			uint64_t prototype = ecs.prototype<Position, Heading, Boid>();

			BoidsData& data = m_data;
			BoidParams4 params4 = params;

			ComponentArray<Position> obstacles = { ecs.gather<Position, BoidObstacle>() };
			ComponentArray<Position> targets = { ecs.gather<Position, BoidTarget>() };

			vector<EntityStream*> matches = ecs.match(prototype);
			for(EntityStream* stream : matches)
			{
				const TBuffer<Position>& positions = stream->buffer<Position>();
				TBuffer<Heading>& headings = stream->buffer<Heading>();
				const uint32_t count = uint32_t(positions.m_data.size());

				const size_t num_threads = job_system.m_thread_count + 1;
				data.resize(num_threads, count, targets.size(), obstacles.size());

				Job* job_prepare = job_system.job();

				//constexpr size_t batch = 64;
				//constexpr size_t batch = 1024;
				//constexpr size_t batch = 2048;
				constexpr size_t batch = 4096;

				{
					ZoneScopedNC("hash positions", tracy::Color::Firebrick1);

					HashPositions hash_positions = { positions.m_data, data.cells.hash, data.cells.thread, params.cell_radius, num_threads };
					Job* hash_positions_job = parallel_jobs<batch>(job_system, job_prepare, 0, count, hash_positions);
					job_system.complete(hash_positions_job);
				}

				{
					ZoneScopedNC("cells", tracy::Color::Firebrick1);

					Job* job_cells = job_system.job(job_prepare);

					parallel_copy<batch>(job_system, job_cells, headings.m_data, data.cells.alignment, count);
					parallel_copy<batch>(job_system, job_cells, positions.m_data, data.cells.separation, count);
					parallel_set<batch>(job_system, job_cells, 1, data.cells.count, count);

					job_system.complete(job_cells);
				}

				{
					ZoneScopedNC("copy", tracy::Color::Firebrick1);

					Job* job_copy = job_system.job(job_prepare);

					parallel_copy<64>(job_system, job_copy, targets, data.targets, uint32_t(targets.size()));
					parallel_copy<64>(job_system, job_copy, obstacles, data.obstacles, uint32_t(obstacles.size()));

					job_system.complete(job_copy);
				}

				job_system.complete(job_prepare);

				{
					ZoneScopedNC("merge cells", tracy::Color::ForestGreen);

					MergeCells merge_cells = { data.cells, data.cells.hash, data.cells.thread, data.cell_hashes, data.targets, data.obstacles };
					Job* job_merge_cells = split_jobs<batch>(job_system, nullptr, 0, count, merge_cells);
					job_system.complete(job_merge_cells);
				}

				{
					ZoneScopedNC("steer", tracy::Color::MediumOrchid);

					Steer steer = { params4, data.cells, data.targets, data.obstacles, positions.m_data, headings.m_data, delta };
					Job* job_steer = split_jobs<batch>(job_system, nullptr, 0, count, steer);

					job_system.complete(job_steer);
				}
			}
		}
	};

	class MoveForwardSystem
	{
	public:
		void update(JobSystem& job_system, ECS& ecs, float delta)
		{
			ZoneScopedNC("move forward", tracy::Color::Chocolate);

			Job* job_move = job_system.job();

			auto move_forward_rotation = [delta](Position& position, const Rotation& rotation, const MoveSpeed& move_speed)
			{
				position = position.m_value + (delta * move_speed.m_value * rotate(rotation.m_value, -z3), 0.f);
			};

			Job* job_move_rotation = for_components<Position, Rotation, MoveSpeed>(job_system, job_move, ecs, move_forward_rotation);
			job_system.run(job_move_rotation);

			auto move_forward_heading = [delta](Position& position, const Heading& heading, const MoveSpeed& move_speed)
			{
				position = position.m_value + (delta * move_speed.m_value * heading.m_value);
			};

			Job* job_move_heading = for_components<Position, Heading, MoveSpeed>(job_system, job_move, ecs, move_forward_heading);
			job_system.run(job_move_heading);

			job_system.complete(job_move);
		}
	};

	class TransformSystem
	{
	public:
		void update(JobSystem& job_system, ECS& ecs, float delta)
		{
			ZoneScopedNC("transform", tracy::Color::SteelBlue);

			auto transform_heading = [delta](const Position& position, const Heading& heading, Transform4& transform)
			{
				UNUSED(heading);
#ifdef BOIDS_SIMD
				static vec3 up = vec3(0.f, 1.f, 0.f);
				lookat(transform, position.m_value, heading.m_value, up);
#else
				//bxlookat(transform, position.m_value, position.m_value + heading.m_value, y3);
				//transform = bxTRS(vec3(1.f), look_dir(position.m_value, heading.m_value), position.m_value);
				transform = bxtranslation(two::vec3(position.m_value));
#endif
			};

			Job* job_transform = for_components<Position, Heading, Transform4>(job_system, nullptr, ecs, transform_heading);
			job_system.complete(job_transform);
		}
	};

	void ex_boids_scene(GameShell& app, GameScene& scene)
	{
		UNUSED(app); UNUSED(scene);
	}

	Player::Player(World& world)
		: m_world(&world)
	{}

	class ExBoids : public GameModule
	{
	public:
		ExBoids(Module& module) : GameModule(module) {}

		float m_extents = 10.f;
		size_t m_num_obstacles = 1;
		size_t m_num_targets = 1;

		size_t m_num_boids = 25'000;
		//size_t m_num_boids = 250'000;

		size_t m_num_visible = 4096U * 2;

		void destroy_entities(ECS& ecs)
		{
			ecs.stream<Position, Rotation, Transform4, BoidObstacle>().clear();
			ecs.stream<Position, Rotation, Transform4, BoidTarget>().clear();
			ecs.stream<Position, Heading, MoveForward, MoveSpeed, Transform4, Boid>().clear();
		}

		void create_entities(ECS& ecs)
		{
			for(size_t i = 0; i < m_num_obstacles; ++i)
			{
				Entity obstacle = ecs.create<Position, Rotation, Transform4, BoidObstacle>();
				ecs.set<Position>(obstacle, random_vec3(m_extents));
			}

			for(size_t i = 0; i < m_num_targets; ++i)
			{
				Entity target = ecs.create<Position, Rotation, Transform4, BoidTarget>();
				ecs.set<Position>(target, random_vec3(m_extents));
			}

			for(size_t i = 0; i < m_num_boids; ++i)
			{
				Entity entity = ecs.create<Position, Heading, MoveForward, MoveSpeed, Transform4, Boid>();
				ecs.set<Position>(entity, random_vec3(m_extents));
				ecs.set<Heading>(entity, normalize(random_vec3(1.f)));
			}
		}

		virtual void init(GameShell& app, Game& game) final
		{
			UNUSED(game);
			app.m_gfx->add_resource_path("examples/ex_boids");
		}

		vec3 random_vec3(float ext)
		{
			return vec3(randf(-ext, ext), randf(-ext, ext), randf(-ext, ext), 0.f);
		}

		virtual void start(GameShell& app, Game& game) final
		{
			UNUSED(app);
			DefaultWorld& default_world = global_pool<DefaultWorld>().construct("Arcadia", *app.m_job_system);
			World& world = default_world.m_world;
			game.m_world = &world;

			static Player player = { *game.m_world };
			game.m_player = Ref(&player);

			this->create_entities(world.m_ecs);
		}

		virtual void scene(GameShell& app, GameScene& scene) final
		{
			UNUSED(app);

			ECS& ecs = app.m_game.m_world->m_ecs;

			scene.painter("World", [&](size_t index, VisuScene& visu_scene, Gnode& parent) {
				UNUSED(visu_scene);
				Gnode& self = parent.subi((void*)index);
				parent.m_scene->m_env.m_radiance.m_energy = 0.2f;
				//parent.m_scene->m_env.m_radiance.m_ambient = 0.04f;
				gfx::radiance(self, "radiance/tiber_1_1k.hdr", BackgroundMode::Radiance);
			});

			auto paint_boids = [&](size_t index, VisuScene& scene, Gnode& parent)
			{
				UNUSED(index); UNUSED(scene);
				uint64_t prototype = ecs.prototype<Transform4, Boid>();

				Model& model = parent.m_scene->m_gfx.shape(Sphere(0.01f));
				Material& material = parent.m_scene->m_gfx.symbol_material(Symbol::plain(Colour::White), PLAIN);
				//Material& material = gfx::pbr_material(parent.m_scene->m_gfx, "boid", Colour::White);

				vector<EntityStream*> matches = ecs.match(prototype);
				for(EntityStream* stream : matches)
				{
					const TBuffer<Transform4>& components = stream->buffer<Transform4>();
					span<mat4> transforms = { (mat4*)components.m_data.data(), components.m_data.size() };

					const size_t size = min(m_num_visible, transforms.size());
					for(size_t i = 0; i < size; i += 4096)
					{
						const size_t count = min(size - i, size_t(4096U));
						Item& item = gfx::item(parent, model, ItemFlag::Default | ItemFlag::NoUpdate, &material);
						gfx::instances(parent, item, { transforms.data() + i, count });
					}
				}
			};

			auto paint_targets = [&](size_t index, VisuScene& scene, Gnode& parent)
			{ 
				UNUSED(index); UNUSED(scene);
				Model& model = app.m_gfx->shape(Sphere(0.1f), { Colour::White, Colour::White }, PLAIN);
				Material& target_material = app.m_gfx->symbol_material(Symbol::plain(Colour::Red), PLAIN);
				Material& obstacle_material = app.m_gfx->symbol_material(Symbol::plain(Colour::Black), PLAIN);

				ecs.loop<Position, BoidObstacle>([&](Position& position, BoidObstacle&)
				{
					Gnode& node = gfx::node(parent, to_mud(position.m_value));
					gfx::item(node, model, 0U, &obstacle_material);
				});

				ecs.loop<Position, BoidTarget>([&](Position& position, BoidTarget&)
				{
					Gnode& node = gfx::node(parent, to_mud(position.m_value));
					gfx::item(node, model, 0U, &target_material);
				});
			};
		
			scene.m_painters.push_back(construct<VisuPainter>("boids", scene.m_painters.size(), paint_boids));
			scene.m_painters.push_back(construct<VisuPainter>("targets", scene.m_painters.size(), paint_targets));
		}

		virtual void pump(GameShell& app, Game& game, Widget& ui) final
		{
			if(!game.m_world)
				this->start(app, game);

			auto pump = [&](Widget& parent, Dockbar* dockbar = nullptr)
			{
				UNUSED(dockbar);
				ECS& ecs = app.m_game.m_world->m_ecs;

				static GameScene& scene = app.add_scene();
				Viewer& viewer = ui::viewer(parent, scene.m_scene);
				ui::orbit_controller(viewer);

				static BoidParams params;

				Widget& header = ui::row(viewer);

				static Style panel_style("SpacePanel", styles().wedge, [](Layout& l) { l.m_space = Preset::Stack; l.m_align = { Align::Center, Align::Center }; 
																					   l.m_padding = vec4(15.f); l.m_spacing = vec2(10.f); });

				Widget& left = ui::widget(header, panel_style);
				Widget& numbers = ui::columns(left, { 0.15f, 0.85f });

				ui::slider_field(numbers, "num targets",   m_num_targets, { 0, 10, 1 });
				ui::slider_field(numbers, "num obstacles", m_num_obstacles, { 0, 10, 1 });
				ui::slider_field(numbers, "num boids",     m_num_boids, { 0, 250'000, 1000 });
				ui::slider_field(numbers, "num visible",   m_num_visible, { 0, 250'000, 100 });
				if(ui::button(numbers, "reset").activated())
				{
					this->destroy_entities(ecs);
					this->create_entities(ecs);
				}

				Widget& middle = ui::widget(header, panel_style);
				UNUSED(middle);

				Widget& right = ui::widget(header, panel_style);
				Widget& edit = ui::columns(middle, { 0.2f, 0.8f });

				ui::field<float>(edit, "cell radius",		params.cell_radius,		    { 1.f, 10.f, 0.1f });
				ui::field<float>(edit, "separation weight",	params.separation_weight,   { 0.f, 10.f, 0.1f });
				ui::field<float>(edit, "alignment weight",	params.alignment_weight,	{ 0.f, 10.f, 0.1f });
				ui::field<float>(edit, "target weight",		params.target_weight,		{ 0.f, 10.f, 0.1f });
				ui::field<float>(edit, "obstacle aversion", params.obstacle_aversion_distance, { 0.f, 10.f, 0.1f });

				static BoidSystem boid_system;
				static MoveForwardSystem move_forward_system;
				static TransformSystem transform_system;

				static Clock clock;
				float delta = float(clock.step());

				boid_system.update(*app.m_job_system, ecs, params, delta);
				move_forward_system.update(*app.m_job_system, ecs, delta);
				transform_system.update(*app.m_job_system, ecs, delta);
			};

			pump(ui);
		}
	};

}

using namespace boids;

#ifdef _EX_BOIDS_EXE
int main(int argc, char *argv[])
{
	GameShell app(TOY_RESOURCE_PATH, exec_path(argc, argv).c_str());

#if 0
	//Any any; TAnyHandlerImpl<Heading>::create(any, TAnyHandler<Heading>::me, static_cast<Heading&&>(Heading()));
	Any any; new ((void*)&any.m_storage) Heading(static_cast<Heading&&>(Heading())); any.m_handler = &TAnyHandler<Heading>::me;
	auto moveany = [](Any& any2, Any& any)
	{
		//printf("move\n");
		using two::move;
		TAnyHandlerImpl<Heading>::create(any2, *static_cast<const TAnyHandler<Heading>*>(any.m_handler), move(TAnyHandler<Heading>::value(any)));
		TAnyHandler<Heading>::me.destroy(any);
		//printf("move\n");
	};
	Any any2;
	any.m_handler->move(any2, any);
	//moveany(any2, any);
	//TAnyHandlerImpl<Heading>::create(any2, TAnyHandler<Heading>::me, move(*static_cast<Heading*>((void*)&any.m_storage))); TAnyHandler<Heading>::me.destroy(any);
	//TAnyHandlerImpl<Heading>::create(any2, TAnyHandler<Heading>::me, move(TAnyHandler<Heading>::value(any))); TAnyHandler<Heading>::me.destroy(any);

	//any.~Any();
	//Any any2(move(any));

	Any test0 = TAnyHandler<Heading>::create(static_cast<Heading&&>(Heading()));

	Var test = var(Heading());
#endif

	boids::ExBoids module = { _boids::m() };
	app.run_game(module);
}
#endif
