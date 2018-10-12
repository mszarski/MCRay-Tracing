#include "scene.h"

#include "objloader.h"
#include "ray.h"

#include <cmath>
#include <iostream>
#include <cassert>
#include <exception>
#include <random>


//template std::array<std::vector<ray_physics::segment>, 256> scene::cast_rays<256>();
template std::array<std::array<std::vector<ray_physics::segment>,5>, 512> scene::cast_rays<5,512>(transducer_ & transducer);

scene::scene(const nlohmann::json & config, transducer_ & transducer) :
    transducer(transducer)
{
    try
    {
        parse_config(config);
    }
    catch (const std::exception & ex)
    {
        throw std::runtime_error{ "Error while loading scene: " + std::string{ex.what()} };
    }

    create_empty_world();

    init();
}

scene::~scene()
{
    destroy_world();
}

void scene::init()
{
    for (auto & mesh : meshes)
    {
        const auto full_path = working_dir + mesh.filename;

        auto object = add_rigidbody_from_obj(full_path, mesh.deltas, scaling);

        object->setUserPointer(&mesh);
    }
}

template<unsigned int sample_count,unsigned int ray_count>
std::array<std::array<std::vector<ray_physics::segment>,sample_count>, ray_count> scene::cast_rays(transducer_ & transducer)
{
    using namespace ray_physics;

    //arreglo que guarda todos los segmentos de cada sample de cada rayo.
    std::array<std::array<std::vector<segment>,sample_count>, ray_count> segments;

    unsigned int tests = 0;
    unsigned int total_collisions = 0;

    ///step the simulation
    if (m_dynamicsWorld)
    {
        const float ray_start_step { 0.02f };

        for (auto & sample_vector : segments)
        {
            for (auto & segments_vector : sample_vector )
            {
                segments_vector.reserve(ray::max_depth);
            }
        }
        //para cada rayo del trasductor
        //#pragma omp parallel for
        for (size_t ray_i = 0; ray_i < ray_count; ray_i++)
        {
            auto & samples_vector = segments[ray_i];//santi ver si quitar o no
            // modificar xq ahora tengo un arreglo samples. y cada sample tiene un array de segments.

            std::array<ray,sample_count> samples;

            // Add first ray
            {
                ray first_ray
                {
                    //transducer_pos + btVector3(0,0,ray_start_step * ray_i),
                    transducer.element(ray_i).position,                          // from
                    transducer.element(ray_i).direction,                         // initial direction
                    0, //depth                                                          // depth
                    materials.at(starting_material),
                    nullptr,  //material outside
                    initial_intensity / sample_count,//reparto equitativamente la intensidad entre cada uno de los path
                    transducer.frequency,
                    units::length::millimeter_t(0),                           // distance traveled
                    0                                                            // previous ray
                };
                for (size_t sample_i = 0; sample_i < sample_count; sample_i++)
                {
                   samples.at(sample_i) = first_ray;
                }
            }
            for(size_t i = 0; i < ray::max_depth; i++)
            {
                //para cada sample: calculo nuevo segmento y lo guardo. actualizo sample.
                //#pragma omp parallel for
                for (size_t sample_i = 0; sample_i < sample_count; sample_i++)
                {
                    // Pop a ray from the samples array and check if it collides
                    auto & ray_ = samples.at(sample_i);
                    if (!ray_.null)
                    {
                        float r_length = ray_physics::max_ray_length(ray_);
                        auto to = ray_.from + enlarge(ray_.direction, r_length);

                        btCollisionWorld::ClosestRayResultCallback closestResults(ray_.from + 0.1f * ray_.direction,to);

                        m_dynamicsWorld->rayTest(ray_.from + 0.1f * ray_.direction,to,closestResults);
                        tests++;

                        if (closestResults.hasHit())
                        {
                            // Substract ray intensity according to distance traveled
                            auto distance_before_hit = ray_.distance_traveled;
                            auto intensity_before_hit = ray_.intensity;

                            const auto organ = static_cast<mesh*>(closestResults.m_collisionObject->getUserPointer());
                            assert(organ);

                        //Simulacion de la penetracion de la onda sobre el tejido para generar interfaces mas difusas y no tan "plasticas"
                            //cada material tiene un parametro thickness (s) que representa la media de los mm que puede penetrar la su superficie la onda.
                            //Por medio de una variable aleatoria calculamos la penetracion de cada rayo sobre la superficie. var: q variable que sigue una probabilidad normal de media 0 y desvio s.
                            std::random_device rd;  //Will be used to obtain a seed for the random number engine
                            std::mt19937 generator(rd()); //Standard mersenne_twister_engine seeded with rd()
                            std::normal_distribution<double> distribution(0.0,organ->material_inside.thickness);
                            float q = std::abs(distribution(generator));

                            //modifico el hit_point para simular la penetracion del rayo - actualizar la intensidad restante que queda al penetrar la malla.

                            auto inside_point = q * ray_.direction + closestResults.m_hitPointWorld;
                            ray_physics::travel(ray_, distance_in_mm(ray_.from, inside_point));//aca
                            std::cout << "inside : " << distance_in_mm(ray_.from, inside_point) << std::endl;
                            std::cout << "original" << distance_in_mm(ray_.from, closestResults.m_hitPointWorld) << std::endl;
                            // Calculate refraction and reflection directions and intensities

                            auto result = ray_physics::hit_boundary(ray_, closestResults.m_hitPointWorld, closestResults.m_hitNormalWorld, *organ);//aca

                            // Register collision creating a segment from the beggining of the ray to the collision point
                            samples_vector[sample_i].emplace_back(segment{ray_.from, inside_point, ray_.direction, result.reflected_intensity, intensity_before_hit, ray_.media.attenuation, distance_before_hit, ray_.media});//aca

                            // Spawn reflection or refraction rays
                            if (result.returned.intensity > ray::intensity_epsilon)
                            {
                                result.returned.parent_collision = samples_vector[sample_i].size()-1;
                                samples.at(sample_i) = result.returned;
                            }
                            else
                                samples.at(sample_i).null = true;

                        }
                        else
                        {
                            // Ray did not reach another media, add a data point at its end.
                            samples_vector[sample_i].emplace_back(segment{ray_.from, to, ray_.direction, 0.0f, ray_.intensity, ray_.media.attenuation, ray_.distance_traveled, ray_.media});
                            samples.at(sample_i).null = true;
                        }
                    }

                }
            }
        }

        for (auto & collision_vector : segments)
        {
            total_collisions += collision_vector.size();
        }
    }

    const float fps = 1.0f/(float( clock() - frame_start ) /  CLOCKS_PER_SEC);
    std::cout << fps << " " << tests << " " << total_collisions << std::endl;
    frame_start = clock();

    return segments;
}

void scene::parse_config(const nlohmann::json & config)
{
    using namespace units::angle;

    working_dir = config.find("workingDirectory") != config.end() ? config.at("workingDirectory") : "";

    const auto & t_pos = config.at("transducerPosition");
    transducer_pos = {t_pos[0], t_pos[1], t_pos[2]};

    const auto & orig = config.at("origin");
    origin = {orig[0], orig[1], orig[2]};

    const auto & spac = config.at("spacing");
    spacing = {spac[0], spac[1], spac[2]};

    starting_material = config.at("startingMaterial");

    scaling = config.at("scaling");

    const auto & mats = config.at("materials");
    if (mats.is_array())
    {
        for (const auto & mat : mats)
        {
            materials[mat.at("name")] =
                {
                    mat.at("impedance"),
                    mat.at("attenuation"),
                    mat.at("mu0"),
                    mat.at("mu1"),
                    mat.at("sigma"),
                    mat.at("specularity"),
                    mat.at("shininess"),
                    mat.at("thickness")
                };
        }
    }
    else
    {
        throw std::runtime_error("materials must be an array");
    }

    const auto & meshes_ = config.at("meshes");
    if (meshes_.is_array())
    {
        for (const auto & mesh_ : meshes_)
        {
            const auto & deltas { mesh_.at("deltas") };
            meshes.emplace_back(mesh{
                        mesh_.at("file"),
                        mesh_.at("rigid"),
                        mesh_.at("vascular"),
                        {deltas[0], deltas[1], deltas[2]},
                        mesh_.at("outsideNormals"),
                        materials.at(mesh_.at("material")),
                        materials.at(mesh_.at("outsideMaterial"))});
        }
    }
    else
    {
        throw std::runtime_error("meshes must be an array");
    }
}

void scene::create_empty_world()
{
    m_collisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();

    m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfiguration.get());

    m_broadphase = std::make_unique<btDbvtBroadphase>();

    m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();

    m_dynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(m_dispatcher.get(),m_broadphase.get(),m_solver.get(),m_collisionConfiguration.get());

    m_dynamicsWorld->setGravity(btVector3(0,-10,0));
}

void scene::destroy_world()
{
    //delete collision shapes
    for (int j = 0; j < m_collisionShapes.size(); j++)
    {
        btCollisionShape* shape = m_collisionShapes[j];
        delete shape;
    }
    m_collisionShapes.clear();

    m_dynamicsWorld.reset();
    m_solver.reset();
    m_broadphase.reset();
    m_dispatcher.reset();
    m_collisionConfiguration.reset();
}

units::length::millimeter_t scene::distance_in_mm(const btVector3 & v1, const btVector3 & v2) const
{
    using namespace std;

    auto x_dist = abs(v1.getX() - v2.getX()) * spacing[0];
    auto y_dist = abs(v1.getY() - v2.getY()) * spacing[1];
    auto z_dist = abs(v1.getZ() - v2.getZ()) * spacing[2];

    return units::length::millimeter_t(sqrt(pow(x_dist,2) + pow(y_dist,2) + pow(z_dist,2)) * 10);
}

btVector3 scene::enlarge(const btVector3 & versor, float mm) const
{
    assert(versor.length2() < 1.1f);
    return mm/100.0f * btVector3 ( spacing[0] * versor.getX(),
                                   spacing[1] * versor.getY(),
                                   spacing[2] * versor.getZ() );
}

btRigidBody * scene::add_rigidbody_from_obj(const std::string & fileName, std::array<float, 3> deltas, float scaling)
{
    GLInstanceGraphicsShape* glmesh = load_mesh_from_obj(fileName, "");
    printf("[INFO] Obj loaded: Extracted %d verticed from obj file [%s]\n", glmesh->m_numvertices, fileName.c_str());

    const GLInstanceVertex& v = glmesh->m_vertices->at(0);
    btTriangleIndexVertexArray* tiva = new btTriangleIndexVertexArray(glmesh->m_numIndices / 3, &glmesh->m_indices->at(0), 3* sizeof(int),
                                                                      glmesh->m_numvertices, (btScalar*)(&(v.xyzw[0])), sizeof(GLInstanceVertex));

    btBvhTriangleMeshShape* shape = new btBvhTriangleMeshShape(tiva, true);

    m_collisionShapes.push_back(shape);

    float _scaling[4] = {scaling,scaling,scaling,1};

    btVector3 localScaling(_scaling[0],_scaling[1],_scaling[2]);
    shape->setLocalScaling(localScaling);

    btTransform startTransform;
    startTransform.setIdentity();

    //std::array<float, 3> origin { -18, -22, -5 }; // origin for organs scene
    float pos[4] = {deltas[0]*_scaling[0]*_scaling[0],deltas[1]*_scaling[1]*_scaling[1],deltas[2]*_scaling[2]*_scaling[2],0};
    btVector3 position(pos[0] + origin[0], pos[1] + origin[1], pos[2] + origin[2]);
    startTransform.setOrigin(position);

    btScalar mass(0.f);
    btVector3 localInertia(0, 0, 0);
    auto myMotionState = new btDefaultMotionState(startTransform);
    btRigidBody::btRigidBodyConstructionInfo cInfo(mass, myMotionState, shape, localInertia);
    auto body = new btRigidBody(cInfo);
    body->setUserIndex(-1);
    m_dynamicsWorld->addRigidBody(body);
    return body;
}

void scene::step(float delta_time)
{
    m_dynamicsWorld->stepSimulation(delta_time);
}

// TODO: Is this equals to distance_in_mm?
units::length::millimeter_t scene::distance(const btVector3 & from, const btVector3 & to) const
{
    // TODO: Use scaling in this calculation
    return units::length::millimeter_t(from.distance(to)*10.0f);
}


