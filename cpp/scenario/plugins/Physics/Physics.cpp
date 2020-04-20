/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Physics.h"
#include "scenario/gazebo/components/ExternalWorldWrenchCmdWithDuration.h"
#include "scenario/gazebo/components/HistoryOfAppliedJointForces.h"
#include "scenario/gazebo/components/JointPositionReset.h"
#include "scenario/gazebo/components/JointVelocityReset.h"
#include "scenario/gazebo/components/SimulatedTime.h"
#include "scenario/gazebo/components/WorldVelocityCmd.h"
#include "scenario/gazebo/helpers.h"

#include <ignition/common/MeshManager.hh>
#include <ignition/gazebo/EntityComponentManager.hh>
#include <ignition/gazebo/Util.hh>
#include <ignition/gazebo/components/AngularAcceleration.hh>
#include <ignition/gazebo/components/AngularVelocity.hh>
#include <ignition/gazebo/components/BatterySoC.hh>
#include <ignition/gazebo/components/CanonicalLink.hh>
#include <ignition/gazebo/components/ChildLinkName.hh>
#include <ignition/gazebo/components/Collision.hh>
#include <ignition/gazebo/components/ContactSensorData.hh>
#include <ignition/gazebo/components/ExternalWorldWrenchCmd.hh>
#include <ignition/gazebo/components/Geometry.hh>
#include <ignition/gazebo/components/Gravity.hh>
#include <ignition/gazebo/components/Inertial.hh>
#include <ignition/gazebo/components/Joint.hh>
#include <ignition/gazebo/components/JointAxis.hh>
#include <ignition/gazebo/components/JointForce.hh>
#include <ignition/gazebo/components/JointForceCmd.hh>
#include <ignition/gazebo/components/JointPosition.hh>
#include <ignition/gazebo/components/JointType.hh>
#include <ignition/gazebo/components/JointVelocity.hh>
#include <ignition/gazebo/components/JointVelocityCmd.hh>
#include <ignition/gazebo/components/LinearAcceleration.hh>
#include <ignition/gazebo/components/LinearVelocity.hh>
#include <ignition/gazebo/components/Link.hh>
#include <ignition/gazebo/components/Model.hh>
#include <ignition/gazebo/components/Name.hh>
#include <ignition/gazebo/components/ParentEntity.hh>
#include <ignition/gazebo/components/ParentLinkName.hh>
#include <ignition/gazebo/components/Pose.hh>
#include <ignition/gazebo/components/PoseCmd.hh>
#include <ignition/gazebo/components/Static.hh>
#include <ignition/gazebo/components/ThreadPitch.hh>
#include <ignition/gazebo/components/World.hh>
#include <ignition/math/eigen3/Conversions.hh>
#include <ignition/msgs/Utility.hh>
#include <ignition/msgs/contact.pb.h>
#include <ignition/msgs/contacts.pb.h>
#include <ignition/msgs/entity.pb.h>
#include <ignition/physics/BoxShape.hh>
#include <ignition/physics/CylinderShape.hh>
#include <ignition/physics/FeatureList.hh>
#include <ignition/physics/FeaturePolicy.hh>
#include <ignition/physics/ForwardStep.hh>
#include <ignition/physics/FrameSemantics.hh>
#include <ignition/physics/FreeGroup.hh>
#include <ignition/physics/GetContacts.hh>
#include <ignition/physics/GetEntities.hh>
#include <ignition/physics/Joint.hh>
#include <ignition/physics/Link.hh>
#include <ignition/physics/RelativeQuantity.hh>
#include <ignition/physics/RemoveEntities.hh>
#include <ignition/physics/RequestEngine.hh>
#include <ignition/physics/Shape.hh>
#include <ignition/physics/SphereShape.hh>
#include <ignition/physics/mesh/MeshShape.hh>
#include <ignition/physics/sdf/ConstructCollision.hh>
#include <ignition/physics/sdf/ConstructJoint.hh>
#include <ignition/physics/sdf/ConstructLink.hh>
#include <ignition/physics/sdf/ConstructModel.hh>
#include <ignition/physics/sdf/ConstructWorld.hh>
#include <ignition/plugin/Loader.hh>
#include <ignition/plugin/PluginPtr.hh>
#include <ignition/plugin/Register.hh>
#include <sdf/Collision.hh>
#include <sdf/Joint.hh>
#include <sdf/Link.hh>
#include <sdf/Mesh.hh>
#include <sdf/Model.hh>
#include <sdf/World.hh>

#include <deque>
#include <iostream>
#include <unordered_map>

using namespace ignition;
using namespace scenario::plugins::gazebo;
using namespace ignition::gazebo;
using namespace ignition::gazebo::systems;
using namespace ignition::gazebo::components;

class Physics::Impl
{
public:
    struct MinimumFeatureList
        : ignition::physics::FeatureList< //
              ignition::physics::FindFreeGroupFeature,
              ignition::physics::SetFreeGroupWorldPose,
              ignition::physics::FreeGroupFrameSemantics,
              ignition::physics::LinkFrameSemantics,
              ignition::physics::SetFreeGroupWorldVelocity,
              ignition::physics::AddLinkExternalForceTorque,
              ignition::physics::ForwardStep,
              ignition::physics::GetEntities,
              ignition::physics::GetContactsFromLastStepFeature,
              ignition::physics::RemoveEntities,
              ignition::physics::mesh::AttachMeshShapeFeature,
              ignition::physics::GetBasicJointProperties,
              ignition::physics::GetBasicJointState,
              ignition::physics::SetBasicJointState,
              ignition::physics::SetJointVelocityCommandFeature,
              ignition::physics::sdf::ConstructSdfCollision,
              ignition::physics::sdf::ConstructSdfJoint,
              ignition::physics::sdf::ConstructSdfLink,
              ignition::physics::sdf::ConstructSdfModel,
              ignition::physics::sdf::ConstructSdfWorld>
    {};

    using EnginePtrType =
        ignition::physics::EnginePtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using WorldType =
        ignition::physics::World<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using WorldPtrType =
        ignition::physics::WorldPtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using ModelPtrType =
        ignition::physics::ModelPtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using LinkPtrType =
        ignition::physics::LinkPtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using ShapePtrType =
        ignition::physics::ShapePtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using JointPtrType =
        ignition::physics::JointPtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    using FreeGroupPtrType =
        ignition::physics::FreeGroupPtr<ignition::physics::FeaturePolicy3d, MinimumFeatureList>;

    /// \brief Create physics entities
    /// \param[in] _ecm Constant reference to ECM.
    void CreatePhysicsEntities(const EntityComponentManager& _ecm);

    /// \brief Remove physics entities if they are removed from the ECM
    /// \param[in] _ecm Constant reference to ECM.
    void RemovePhysicsEntities(const EntityComponentManager& _ecm);

    /// \brief Update physics from components
    /// \param[in] _ecm Constant reference to ECM.
    void UpdatePhysics(const ignition::gazebo::UpdateInfo& _info, EntityComponentManager& _ecm);

    /// \brief Step the simulationrfor each world
    /// \param[in] _dt Duration
    void Step(const std::chrono::steady_clock::duration& _dt);

    /// \brief Update components from physics simulation
    /// \param[in] _ecm Mutable reference to ECM.
    void UpdateSim(const ignition::gazebo::UpdateInfo& _info, EntityComponentManager& _ecm) const;

    /// \brief Update collision components from physics simulation
    /// \param[in] _ecm Mutable reference to ECM.
    void UpdateCollisions(EntityComponentManager& _ecm) const;

    /// \brief FrameData relative to world at a given offset pose
    /// \param[in] _link ign-physics link
    /// \param[in] _pose Offset pose in which to compute the frame data
    /// \returns FrameData at the given offset pose
    physics::FrameData3d LinkFrameDataAtOffset(const LinkPtrType& _link,
                                               const math::Pose3d& _pose) const;

    /// \brief A map between world entity ids in the ECM to World Entities in
    /// ign-physics.
    std::unordered_map<Entity, WorldPtrType> entityWorldMap;

    /// \brief A map between model entity ids in the ECM to Model Entities in
    /// ign-physics.
    std::unordered_map<Entity, ModelPtrType> entityModelMap;

    /// \brief A map between link entity ids in the ECM to Link Entities in
    /// ign-physics.
    std::unordered_map<Entity, LinkPtrType> entityLinkMap;

    /// \brief Reverse of entityLinkMap. This is used for finding the Entity
    /// associated with a physics Link
    std::unordered_map<LinkPtrType, Entity> linkEntityMap;

    /// \brief A map between collision entity ids in the ECM to Shape Entities
    /// in ign-physics.
    std::unordered_map<Entity, ShapePtrType> entityCollisionMap;

    /// \brief A map between shape entities in ign-physics to collision entities
    /// in the ECM. This is the reverse map of entityCollisionMap.
    std::unordered_map<ShapePtrType, Entity> collisionEntityMap;

    /// \brief A map between joint entity ids in the ECM to Joint Entities in
    /// ign-physics
    std::unordered_map<Entity, JointPtrType> entityJointMap;

    /// \brief A map between model entity ids in the ECM to whether its battery
    /// has drained.
    std::unordered_map<Entity, bool> entityOffMap;

    /// \brief used to store whether physics objects have been created.
    bool initialized = false;

    /// \brief Pointer to the underlying ign-physics Engine entity.
    EnginePtrType engine = nullptr;

    /// \brief Vector3d equality comparison function.
    std::function<bool(const math::Vector3d&, const math::Vector3d&)> vec3Eql{
        [](const math::Vector3d& _a, const math::Vector3d& _b) { return _a.Equal(_b, 1e-6); }};

    /// \brief Pose3d equality comparison function.
    std::function<bool(const math::Pose3d&, const math::Pose3d&)> pose3Eql{
        [](const math::Pose3d& _a, const math::Pose3d& _b) {
            return _a.Pos().Equal(_b.Pos(), 1e-6) && math::equal(_a.Rot().X(), _b.Rot().X(), 1e-6)
                   && math::equal(_a.Rot().Y(), _b.Rot().Y(), 1e-6)
                   && math::equal(_a.Rot().Z(), _b.Rot().Z(), 1e-6)
                   && math::equal(_a.Rot().W(), _b.Rot().W(), 1e-6);
        }};

    /// \brief Boolean value that is true only the first call of Configure and
    /// PreUpdate.
    bool firstRun = true;
};

Physics::Physics()
    : System()
    , pImpl(std::make_unique<Impl>())
{
    ignition::plugin::Loader pl;
    // dartsim_plugin_LIB is defined by cmake
    std::unordered_set<std::string> plugins = pl.LoadLib(dartsim_plugin_LIB);
    if (!plugins.empty()) {
        const std::string className = "ignition::physics::dartsim::Plugin";
        ignition::plugin::PluginPtr plugin = pl.Instantiate(className);

        if (plugin) {
            this->pImpl->engine =
                ignition::physics::RequestEngine<ignition::physics::FeaturePolicy3d,
                                                 Impl::MinimumFeatureList>::From(plugin);
        }
        else {
            ignerr << "Unable to instantiate " << className << ".\n";
        }
    }
    else {
        ignerr << "Unable to load the " << dartsim_plugin_LIB << " library.\n";
        return;
    }
}

Physics::~Physics() = default;

void Physics::Update(const UpdateInfo& _info, EntityComponentManager& _ecm)
{
    // \TODO(anyone) Support rewind
    if (_info.dt < std::chrono::steady_clock::duration::zero()) {
        ignwarn << "Detected jump back in time ["
                << std::chrono::duration_cast<std::chrono::seconds>(_info.dt).count()
                << "s]. System may not work properly." << std::endl;
    }

    // Update the component with the time in seconds that  the simulation will
    // have after the step
    _ecm.Each<components::World, components::SimulatedTime>(
        [&](const Entity& worldEntity, const components::World*, components::SimulatedTime*) {
            scenario::gazebo::utils::setExistingComponentData<
                ignition::gazebo::components::SimulatedTime>(&_ecm, worldEntity, _info.simTime);
            return true;
        });

    if (this->pImpl->engine) {
        this->pImpl->CreatePhysicsEntities(_ecm);
        this->pImpl->UpdatePhysics(_info, _ecm);

        // Only step if not paused.
        if (!_info.paused) {
            this->pImpl->Step(_info.dt);
        }

        this->pImpl->UpdateSim(_info, _ecm);

        // Entities scheduled to be removed should be removed from physics after
        // the simulation step. Otherwise, since the to-be-removed entity still
        // shows up in the ECM::Each the UpdatePhysics and UpdateSim calls will
        // have an error
        this->pImpl->RemovePhysicsEntities(_ecm);
    }
}

void Physics::Impl::CreatePhysicsEntities(const EntityComponentManager& _ecm)
{
    auto processWorld = [&](const Entity& _entity,
                            const components::World* /* _world */,
                            const components::Name* _name,
                            const components::Gravity* _gravity) -> bool {
        if (this->entityWorldMap.find(_entity) != this->entityWorldMap.end()) {
            ignwarn << "World entity [" << _entity
                    << "] marked as new, but it's already on the map." << std::endl;
            return true;
        }

        sdf::World world;
        world.SetName(_name->Data());
        world.SetGravity(_gravity->Data());
        auto worldPtrPhys = this->engine->ConstructWorld(world);
        this->entityWorldMap.insert(std::make_pair(_entity, worldPtrPhys));

        return true;
    };

    auto processModel = [&](const Entity& _entity,
                            const components::Model*,
                            const components::Name* _name,
                            const components::Pose* _pose,
                            const components::ParentEntity* _parent) -> bool {
        //        ignerr << "model " << _name->Data() << std::endl;
        // Check if model already exists
        if (this->entityModelMap.find(_entity) != this->entityModelMap.end()) {
            ignwarn << "Model entity [" << _entity
                    << "] marked as new, but it's already on the map." << std::endl;
            return true;
        }

        // TODO(anyone) Don't load models unless they have collisions

        // Check if parent world exists
        // TODO(louise): Support nested models, see
        // https://bitbucket.org/ignitionrobotics/ign-physics/issues/10
        if (this->entityWorldMap.find(_parent->Data()) == this->entityWorldMap.end()) {
            ignwarn << "Model's parent entity [" << _parent->Data() << "] not found on world map."
                    << std::endl;
            return true;
        }
        auto worldPtrPhys = this->entityWorldMap.at(_parent->Data());

        sdf::Model model;
        model.SetName(_name->Data());
        model.SetRawPose(_pose->Data());

        auto staticComp = _ecm.Component<components::Static>(_entity);
        if (staticComp && staticComp->Data()) {
            model.SetStatic(staticComp->Data());
        }

        auto modelPtrPhys = worldPtrPhys->ConstructModel(model);
        this->entityModelMap.insert(std::make_pair(_entity, modelPtrPhys));

        return true;
    };

    auto processLink = [&](const Entity& _entity,
                           const components::Link* /* _link */,
                           const components::Name* _name,
                           const components::Pose* _pose,
                           const components::ParentEntity* _parent) -> bool {
        // Check if link already exists
        if (this->entityLinkMap.find(_entity) != this->entityLinkMap.end()) {
            ignwarn << "Link entity [" << _entity << "] marked as new, but it's already on the map."
                    << std::endl;
            return true;
        }

        // TODO(anyone) Don't load links unless they have collisions

        // Check if parent model exists
        if (this->entityModelMap.find(_parent->Data()) == this->entityModelMap.end()) {
            ignwarn << "Link's parent entity [" << _parent->Data() << "] not found on model map."
                    << std::endl;
            return true;
        }
        auto modelPtrPhys = this->entityModelMap.at(_parent->Data());

        sdf::Link link;
        link.SetName(_name->Data());
        link.SetRawPose(_pose->Data());

        // get link inertial
        auto inertial = _ecm.Component<components::Inertial>(_entity);
        if (inertial) {
            link.SetInertial(inertial->Data());
        }

        auto linkPtrPhys = modelPtrPhys->ConstructLink(link);
        this->entityLinkMap.insert(std::make_pair(_entity, linkPtrPhys));
        this->linkEntityMap.insert(std::make_pair(linkPtrPhys, _entity));

        return true;
    };

    auto processCollision = [&](const Entity& _entity,
                                const components::Collision*,
                                const components::Name* _name,
                                const components::Pose* _pose,
                                const components::Geometry* _geom,
                                const components::CollisionElement* _collElement,
                                const components::ParentEntity* _parent) -> bool {
        if (this->entityCollisionMap.find(_entity) != this->entityCollisionMap.end()) {
            ignwarn << "Collision entity [" << _entity
                    << "] marked as new, but it's already on the map." << std::endl;
            return true;
        }

        // Check if parent link exists
        if (this->entityLinkMap.find(_parent->Data()) == this->entityLinkMap.end()) {
            ignwarn << "Collision's parent entity [" << _parent->Data()
                    << "] not found on link map." << std::endl;
            return true;
        }
        auto linkPtrPhys = this->entityLinkMap.at(_parent->Data());

        // Make a copy of the collision DOM so we can set its pose which has
        // been resolved and is now expressed w.r.t the parent link of the
        // collision.
        sdf::Collision collision = _collElement->Data();
        collision.SetRawPose(_pose->Data());
        collision.SetPoseRelativeTo("");

        ShapePtrType collisionPtrPhys;
        if (_geom->Data().Type() == sdf::GeometryType::MESH) {
            const sdf::Mesh* meshSdf = _geom->Data().MeshShape();
            if (nullptr == meshSdf) {
                ignwarn << "Mesh geometry for collision [" << _name->Data()
                        << "] missing mesh shape." << std::endl;
                return true;
            }

            auto& meshManager = *ignition::common::MeshManager::Instance();
            auto fullPath = asFullPath(meshSdf->Uri(), meshSdf->FilePath());
            auto* mesh = meshManager.Load(fullPath);
            if (nullptr == mesh) {
                ignwarn << "Failed to load mesh from [" << fullPath << "]." << std::endl;
                return true;
            }

            collisionPtrPhys =
                linkPtrPhys->AttachMeshShape(_name->Data(),
                                             *mesh,
                                             ignition::math::eigen3::convert(_pose->Data()),
                                             ignition::math::eigen3::convert(meshSdf->Scale()));
        }
        else {
            collisionPtrPhys = linkPtrPhys->ConstructCollision(collision);
        }

        this->entityCollisionMap.insert(std::make_pair(_entity, collisionPtrPhys));
        this->collisionEntityMap.insert(std::make_pair(collisionPtrPhys, _entity));
        return true;
    };

    auto processJoint = [&](const Entity& _entity,
                            const components::Joint* /* _joint */,
                            const components::Name* _name,
                            const components::JointType* _jointType,
                            const components::Pose* _pose,
                            const components::ThreadPitch* _threadPitch,
                            const components::ParentEntity* _parentModel,
                            const components::ParentLinkName* _parentLinkName,
                            const components::ChildLinkName* _childLinkName) -> bool {
        // Check if joint already exists
        if (this->entityJointMap.find(_entity) != this->entityJointMap.end()) {
            ignwarn << "Joint entity [" << _entity
                    << "] marked as new, but it's already on the map." << std::endl;
            return true;
        }

        // Check if parent model exists
        if (this->entityModelMap.find(_parentModel->Data()) == this->entityModelMap.end()) {
            ignwarn << "Joint's parent entity [" << _parentModel->Data()
                    << "] not found on model map." << std::endl;
            return true;
        }
        auto modelPtrPhys = this->entityModelMap.at(_parentModel->Data());

        sdf::Joint joint;
        joint.SetName(_name->Data());
        joint.SetType(_jointType->Data());
        joint.SetRawPose(_pose->Data());
        joint.SetThreadPitch(_threadPitch->Data());

        joint.SetParentLinkName(_parentLinkName->Data());
        joint.SetChildLinkName(_childLinkName->Data());

        auto jointAxis = _ecm.Component<components::JointAxis>(_entity);
        auto jointAxis2 = _ecm.Component<components::JointAxis2>(_entity);

        // Since we're making copies of the joint axes that were created using
        // `Model::Load`, frame semantics should work for resolving their xyz
        // axis
        if (jointAxis)
            joint.SetAxis(0, jointAxis->Data());
        if (jointAxis2)
            joint.SetAxis(1, jointAxis2->Data());

        // Use the parent link's parent model as the model of this joint
        auto jointPtrPhys = modelPtrPhys->ConstructJoint(joint);

        if (jointPtrPhys.Valid()) {
            // Some joints may not be supported, so only add them to the map if
            // the physics entity is valid
            this->entityJointMap.insert(std::make_pair(_entity, jointPtrPhys));
        }
        return true;
    };

    if (this->firstRun) {
        this->firstRun = false;

        _ecm.Each<components::World, components::Name, components::Gravity>(processWorld);

        _ecm.Each<components::Model, components::Name, components::Pose, components::ParentEntity>(
            processModel);

        _ecm.Each<components::Link, components::Name, components::Pose, components::ParentEntity>(
            processLink);

        // We don't need to add visuals to the physics engine.

        _ecm.Each<components::Collision,
                  components::Name,
                  components::Pose,
                  components::Geometry,
                  components::CollisionElement,
                  components::ParentEntity>(processCollision);

        _ecm.Each<components::Joint,
                  components::Name,
                  components::JointType,
                  components::Pose,
                  components::ThreadPitch,
                  components::ParentEntity,
                  components::ParentLinkName,
                  components::ChildLinkName>(processJoint);

        _ecm.Each<components::BatterySoC>(
            [&](const Entity& _entity, const components::BatterySoC*) -> bool {
                // Parent entity of battery is model entity
                this->entityOffMap.insert(std::make_pair(_ecm.ParentEntity(_entity), false));
                return true;
            });
    }
    else {
        _ecm.EachNew<components::World, components::Name, components::Gravity>(processWorld);

        _ecm.EachNew<components::Model,
                     components::Name,
                     components::Pose,
                     components::ParentEntity>(processModel);

        _ecm.EachNew<components::Link,
                     components::Name,
                     components::Pose,
                     components::ParentEntity>(processLink);

        // We don't need to add visuals to the physics engine.

        _ecm.EachNew<components::Collision,
                     components::Name,
                     components::Pose,
                     components::Geometry,
                     components::CollisionElement,
                     components::ParentEntity>(processCollision);

        _ecm.EachNew<components::Joint,
                     components::Name,
                     components::JointType,
                     components::Pose,
                     components::ThreadPitch,
                     components::ParentEntity,
                     components::ParentLinkName,
                     components::ChildLinkName>(processJoint);

        _ecm.EachNew<components::BatterySoC>(
            [&](const Entity& _entity, const components::BatterySoC*) -> bool {
                // Parent entity of battery is model entity
                this->entityOffMap.insert(std::make_pair(_ecm.ParentEntity(_entity), false));
                return true;
            });
    }
}

void Physics::Impl::RemovePhysicsEntities(const EntityComponentManager& _ecm)
{
    // Assume the world will not be erased
    // Only removing models is supported by ign-physics right now so we only
    // remove links, joints and collisions if they are children of the removed
    // model.
    // We assume the links, joints and collisions will be removed from the
    // physics engine when the containing model gets removed so, here, we only
    // remove the entities from the gazebo entity->physics entity map.
    _ecm.EachRemoved<components::Model>([&](const Entity& _entity, const components::Model *
                                            /* _model */) -> bool {
        // Remove model if found
        auto modelIt = this->entityModelMap.find(_entity);
        if (modelIt != this->entityModelMap.end()) {
            // Remove child links, collisions and joints first
            for (const auto& childLink : _ecm.ChildrenByComponents(_entity, components::Link())) {
                for (const auto& childCollision :
                     _ecm.ChildrenByComponents(childLink, components::Collision())) {
                    auto collIt = this->entityCollisionMap.find(childCollision);
                    if (collIt != this->entityCollisionMap.end()) {
                        this->collisionEntityMap.erase(collIt->second);
                        this->entityCollisionMap.erase(collIt);
                    }
                }
                // First erase the entry associated with this link from the
                // linkEntityMap which is the reverse of entityLinkMap
                auto linkPhysIt = this->entityLinkMap.find(childLink);
                if (linkPhysIt != this->entityLinkMap.end()) {
                    this->linkEntityMap.erase(linkPhysIt->second);
                }
                this->entityLinkMap.erase(childLink);
            }

            for (const auto& childJoint : _ecm.ChildrenByComponents(_entity, components::Joint())) {
                this->entityJointMap.erase(childJoint);
            }

            // Remove the model from the physics engine
            modelIt->second->Remove();
            this->entityModelMap.erase(_entity);
        }
        return true;
    });
}

void Physics::Impl::UpdatePhysics(const ignition::gazebo::UpdateInfo& _info,
                                  EntityComponentManager& _ecm)
{
    // Battery state
    _ecm.Each<components::BatterySoC>(
        [&](const Entity& _entity, const components::BatterySoC* _bat) {
            if (_bat->Data() <= 0)
                entityOffMap[_ecm.ParentEntity(_entity)] = true;
            else
                entityOffMap[_ecm.ParentEntity(_entity)] = false;
            return true;
        });

    // Handle joint state
    _ecm.Each<components::Joint, components::Name>(
        [&](const Entity& _entity, const components::Joint*, const components::Name* _name) {
            auto jointIt = this->entityJointMap.find(_entity);
            if (jointIt == this->entityJointMap.end())
                return true;

            // Model is out of battery
            if (this->entityOffMap[_ecm.ParentEntity(_entity)]) {
                std::size_t nDofs = jointIt->second->GetDegreesOfFreedom();
                for (std::size_t i = 0; i < nDofs; ++i) {
                    jointIt->second->SetForce(i, 0);
                    // TODO(anyone): Only for diff drive, which does not use
                    //   JointForceCmd. Remove when it does.
                    jointIt->second->SetVelocityCommand(i, 0);
                }
                return true;
            }

            auto posReset = _ecm.Component<components::JointPositionReset>(_entity);
            auto velReset = _ecm.Component<components::JointVelocityReset>(_entity);

            // Reset the velocity
            if (velReset) {
                auto& jointVelocity = velReset->Data();

                if (jointVelocity.size() != jointIt->second->GetDegreesOfFreedom()) {
                    ignwarn << "There is a mismatch in the degrees of freedom between "
                            << "Joint [" << _name->Data() << "(Entity=" << _entity
                            << ")] and its JointForceCmd component. The joint has "
                            << jointIt->second->GetDegreesOfFreedom() << " while the "
                            << " component has " << jointVelocity.size() << ".\n";
                }

                std::size_t nDofs =
                    std::min(jointVelocity.size(), jointIt->second->GetDegreesOfFreedom());

                for (std::size_t i = 0; i < nDofs; ++i) {
                    jointIt->second->SetVelocity(i, jointVelocity[i]);
                }
            }

            // Reset the position
            if (posReset) {
                auto& jointPosition = posReset->Data();

                if (jointPosition.size() != jointIt->second->GetDegreesOfFreedom()) {
                    ignwarn << "There is a mismatch in the degrees of freedom between "
                            << "Joint [" << _name->Data() << "(Entity=" << _entity
                            << ")] and its JointForceCmd component. The joint has "
                            << jointIt->second->GetDegreesOfFreedom() << " while the "
                            << " component has " << jointPosition.size() << ".\n";
                }
                std::size_t nDofs =
                    std::min(jointPosition.size(), jointIt->second->GetDegreesOfFreedom());
                for (std::size_t i = 0; i < nDofs; ++i) {
                    jointIt->second->SetPosition(i, jointPosition[i]);
                }
            }

            auto force = _ecm.Component<components::JointForceCmd>(_entity);
            auto velCmd = _ecm.Component<components::JointVelocityCmd>(_entity);

            if (force) {
                if (force->Data().size() != jointIt->second->GetDegreesOfFreedom()) {
                    ignwarn << "There is a mismatch in the degrees of freedom between "
                            << "Joint [" << _name->Data() << "(Entity=" << _entity
                            << ")] and its JointForceCmd component. The joint has "
                            << jointIt->second->GetDegreesOfFreedom() << " while the "
                            << " component has " << force->Data().size() << ".\n";
                }
                std::size_t nDofs =
                    std::min(force->Data().size(), jointIt->second->GetDegreesOfFreedom());
                for (std::size_t i = 0; i < nDofs; ++i) {
                    jointIt->second->SetForce(i, force->Data()[i]);
                }
            }
            else {
                // Only set joint velocity if joint force is not set.
                // If both the cmd and reset components are found, cmd is ignored.
                if (velCmd) {
                    auto velocityCmd = velCmd->Data();

                    if (velReset) {
                        ignwarn << "Found both JointVelocityReset and "
                                << "JointVelocityCmd components "
                                << "for Joint [" << _name->Data() << "(Entity=" << _entity
                                << "]). Ignoring JointVelocityReset component." << std::endl;
                    }
                    else {
                        if (velocityCmd.size() != jointIt->second->GetDegreesOfFreedom()) {
                            ignwarn << "There is a mismatch in the degrees of freedom between"
                                    << " Joint [" << _name->Data() << "(Entity=" << _entity
                                    << ")] and its JointVelocityCmd component. The joint has "
                                    << jointIt->second->GetDegreesOfFreedom() << " while the "
                                    << " component has " << velCmd->Data().size() << ".\n";
                        }

                        std::size_t nDofs =
                            std::min(velocityCmd.size(), jointIt->second->GetDegreesOfFreedom());

                        for (std::size_t i = 0; i < nDofs; ++i) {
                            jointIt->second->SetVelocityCommand(i, velocityCmd[i]);
                        }
                    }
                }
            }

            return true;
        });

    // Link wrenches
    _ecm.Each<components::ExternalWorldWrenchCmd>(
        [&](const Entity& _entity, const components::ExternalWorldWrenchCmd* _wrenchComp) {
            auto linkIt = this->entityLinkMap.find(_entity);
            if (linkIt == this->entityLinkMap.end())
                return true;

            math::Vector3 force = msgs::Convert(_wrenchComp->Data().force());
            math::Vector3 torque = msgs::Convert(_wrenchComp->Data().torque());
            linkIt->second->AddExternalForce(math::eigen3::convert(force));
            linkIt->second->AddExternalTorque(math::eigen3::convert(torque));

            return true;
        });

    // Link wrenches with duration
    if (!_info.paused) {
        _ecm.Each<components::ExternalWorldWrenchCmdWithDuration>(
            [&](const Entity& _entity,
                components::ExternalWorldWrenchCmdWithDuration* _wrenchWithDurComp) {
                auto linkIt = this->entityLinkMap.find(_entity);
                if (linkIt == this->entityLinkMap.end())
                    return true;

                auto totalWrench = _wrenchWithDurComp->Data().totalWrench();
                math::Vector3 force = msgs::Convert(totalWrench.force());
                math::Vector3 torque = msgs::Convert(totalWrench.torque());

                linkIt->second->AddExternalForce(math::eigen3::convert(force));
                linkIt->second->AddExternalTorque(math::eigen3::convert(torque));

                // NOTE: Cleaning could be moved to UpdateSim, but let's
                //       keep things all together for now
                auto simTimeAfterStep = _info.simTime;
                _wrenchWithDurComp->Data().cleanExpired(simTimeAfterStep);

                return true;
            });
    }

    _ecm.Each<components::Model, components::WorldPoseCmd>(
        [&](const Entity& _entity,
            const components::Model*,
            const components::WorldPoseCmd* _poseCmd) {
            auto modelIt = this->entityModelMap.find(_entity);
            if (modelIt == this->entityModelMap.end())
                return true;

            // The canonical link as specified by sdformat is different from the
            // canonical link of the FreeGroup object

            // TODO(addisu) Store the free group instead of searching for it at
            // every iteration
            auto freeGroup = modelIt->second->FindFreeGroup();
            if (!freeGroup)
                return true;

            // Get canonical link offset
            auto linkEntityIt = this->linkEntityMap.find(freeGroup->CanonicalLink());
            if (linkEntityIt == this->linkEntityMap.end())
                return true;

            auto canonicalPoseComp = _ecm.Component<components::Pose>(linkEntityIt->second);

            freeGroup->SetWorldPose(
                math::eigen3::convert(_poseCmd->Data() * canonicalPoseComp->Data()));

            // Process pose commands for static models here, as one-time changes
            const components::Static* staticComp = _ecm.Component<components::Static>(_entity);
            if (staticComp && staticComp->Data()) {
                auto worldPoseComp = _ecm.Component<components::Pose>(_entity);
                if (worldPoseComp) {
                    auto state = worldPoseComp->SetData(
                                     _poseCmd->Data() * canonicalPoseComp->Data(), this->pose3Eql)
                                     ? ComponentState::OneTimeChange
                                     : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::Pose::typeId, state);
                }
            }

            return true;
        });

    // Process WorldVelocityCmd
    _ecm.Each<components::Model, components::WorldVelocityCmd>(
        [&](const Entity& _entity,
            const components::Model*,
            components::WorldVelocityCmd* _modelWorldVelocityCmd) {
            auto modelIt = this->entityModelMap.find(_entity);
            if (modelIt == this->entityModelMap.end())
                return true;

            // The canonical link as specified by sdformat is different from the
            // canonical link of the FreeGroup object

            // TODO(addisu) Store the free group instead of searching for it at
            // every iteration

            // The FreeGroup is created only for floating-base object that do
            // not have any defined joint between the world and their base
            auto freeGroup = modelIt->second->FindFreeGroup();
            if (!freeGroup) {
                ignwarn << "Failed to find FreeGroup. Linear and angular "
                           "velocities commands ignored."
                        << std::endl;
                return true;
            }

            ignition::math::Vector3d& modelWorldLinearVelocity =
                _modelWorldVelocityCmd->Data().linear;
            ignition::math::Vector3d& modelWorldAngularVelocity =
                _modelWorldVelocityCmd->Data().angular;

            freeGroup->SetWorldLinearVelocity(math::eigen3::convert(modelWorldLinearVelocity));
            freeGroup->SetWorldAngularVelocity(math::eigen3::convert(modelWorldAngularVelocity));

            // TODO(diego): static models from above
            return true;
        });

    // Clear pending commands
    // Note: Removing components from inside an Each call can be dangerous.
    // Instead, we collect all the entities that have the desired components and
    // remove the component from them afterward.
    std::vector<Entity> entitiesWorldCmd;
    _ecm.Each<components::WorldPoseCmd>(
        [&](const Entity& _entity, components::WorldPoseCmd*) -> bool {
            entitiesWorldCmd.push_back(_entity);
            return true;
        });

    for (const Entity& entity : entitiesWorldCmd) {
        _ecm.RemoveComponent<components::WorldPoseCmd>(entity);
    }

    // Clear WorldVelocityCmd
    entitiesWorldCmd.clear();
    _ecm.Each<components::WorldVelocityCmd>(
        [&](const Entity& _entity, components::WorldVelocityCmd*) -> bool {
            entitiesWorldCmd.push_back(_entity);
            return true;
        });

    for (const Entity& entity : entitiesWorldCmd) {
        _ecm.RemoveComponent<components::WorldVelocityCmd>(entity);
    }
}

void Physics::Impl::Step(const std::chrono::steady_clock::duration& _dt)
{
    ignition::physics::ForwardStep::Input input;
    ignition::physics::ForwardStep::State state;
    ignition::physics::ForwardStep::Output output;

    input.Get<std::chrono::steady_clock::duration>() = _dt;

    for (auto& world : this->entityWorldMap) {
        world.second->Step(output, state, input);
    }
}

void Physics::Impl::UpdateSim(const ignition::gazebo::UpdateInfo& _info,
                              EntityComponentManager& _ecm) const
{
    // local pose
    _ecm.Each<components::Link, components::Pose, components::ParentEntity>(
        [&](const Entity& _entity,
            components::Link* /*_link*/,
            components::Pose* _pose,
            const components::ParentEntity* _parent) -> bool {
            // If parent is static, don't process pose changes as periodic
            const auto* staticComp = _ecm.Component<components::Static>(_parent->Data());

            if (staticComp && staticComp->Data())
                return true;

            auto linkIt = this->entityLinkMap.find(_entity);
            if (linkIt != this->entityLinkMap.end()) {
                auto canonicalLink = _ecm.Component<components::CanonicalLink>(_entity);

                // get the pose component of the parent model
                const components::Pose* parentPose =
                    _ecm.Component<components::Pose>(_parent->Data());

                auto frameData = linkIt->second->FrameDataRelativeToWorld();
                const auto& worldPose = frameData.pose;

                // if the parentPose is a nullptr, something is wrong with ECS
                // creation
                if (!parentPose) {
                    ignerr << "The pose component of " << _parent->Data()
                           << " could not be found. This should never happen!\n";
                    return true;
                }
                if (canonicalLink) {
                    // This is the canonical link, update the model
                    // The Pose component, _pose, of this link is the initial
                    // transform of the link w.r.t its model. This component never
                    // changes because it's "fixed" to the model. Instead, we change
                    // the model's pose here. The physics engine gives us the pose of
                    // this link relative to world so to set the model's pose, we have
                    // to post-multiply it by the inverse of the initial transform of
                    // the link w.r.t to its model.
                    auto mutableParentPose = _ecm.Component<components::Pose>(_parent->Data());
                    *(mutableParentPose) = components::Pose(_pose->Data().Inverse()
                                                            + math::eigen3::convert(worldPose));
                    _ecm.SetChanged(
                        _parent->Data(), components::Pose::typeId, ComponentState::PeriodicChange);
                }
                else {
                    // Compute the relative pose of this link from the model
                    *_pose = components::Pose(math::eigen3::convert(worldPose)
                                              + parentPose->Data().Inverse());
                    _ecm.SetChanged(
                        _entity, components::Pose::typeId, ComponentState::PeriodicChange);
                }

                // Populate world poses, velocities and accelerations of the link. For
                // now these components are updated only if another system has created
                // the corresponding component on the entity.
                auto worldPoseComp = _ecm.Component<components::WorldPose>(_entity);
                if (worldPoseComp) {
                    auto state = worldPoseComp->SetData(math::eigen3::convert(frameData.pose),
                                                        this->pose3Eql)
                                     ? ComponentState::PeriodicChange
                                     : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::WorldPose::typeId, state);
                }

                // Velocity in world coordinates
                auto worldLinVelComp = _ecm.Component<components::WorldLinearVelocity>(_entity);
                if (worldLinVelComp) {
                    auto state = worldLinVelComp->SetData(
                                     math::eigen3::convert(frameData.linearVelocity), this->vec3Eql)
                                     ? ComponentState::PeriodicChange
                                     : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::WorldLinearVelocity::typeId, state);
                }

                // Angular velocity in world frame coordinates
                auto worldAngVelComp = _ecm.Component<components::WorldAngularVelocity>(_entity);
                if (worldAngVelComp) {
                    auto state =
                        worldAngVelComp->SetData(math::eigen3::convert(frameData.angularVelocity),
                                                 this->vec3Eql)
                            ? ComponentState::PeriodicChange
                            : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::WorldAngularVelocity::typeId, state);
                }

                // Acceleration in world frame coordinates
                auto worldLinAccelComp =
                    _ecm.Component<components::WorldLinearAcceleration>(_entity);
                if (worldLinAccelComp) {
                    auto state =
                        worldLinAccelComp->SetData(
                            math::eigen3::convert(frameData.linearAcceleration), this->vec3Eql)
                            ? ComponentState::PeriodicChange
                            : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::WorldLinearAcceleration::typeId, state);
                }

                // Angular acceleration in world frame coordinates
                auto worldAngAccelComp =
                    _ecm.Component<components::WorldAngularAcceleration>(_entity);

                if (worldAngAccelComp) {
                    auto state =
                        worldAngAccelComp->SetData(
                            math::eigen3::convert(frameData.angularAcceleration), this->vec3Eql)
                            ? ComponentState::PeriodicChange
                            : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::WorldAngularAcceleration::typeId, state);
                }

                const Eigen::Matrix3d R_bs = worldPose.linear().transpose(); // NOLINT

                // Velocity in body-fixed frame coordinates
                auto bodyLinVelComp = _ecm.Component<components::LinearVelocity>(_entity);
                if (bodyLinVelComp) {
                    Eigen::Vector3d bodyLinVel = R_bs * frameData.linearVelocity;
                    auto state =
                        bodyLinVelComp->SetData(math::eigen3::convert(bodyLinVel), this->vec3Eql)
                            ? ComponentState::PeriodicChange
                            : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::LinearVelocity::typeId, state);
                }

                // Angular velocity in body-fixed frame coordinates
                auto bodyAngVelComp = _ecm.Component<components::AngularVelocity>(_entity);
                if (bodyAngVelComp) {
                    Eigen::Vector3d bodyAngVel = R_bs * frameData.angularVelocity;
                    auto state =
                        bodyAngVelComp->SetData(math::eigen3::convert(bodyAngVel), this->vec3Eql)
                            ? ComponentState::PeriodicChange
                            : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::AngularVelocity::typeId, state);
                }

                // Acceleration in body-fixed frame coordinates
                auto bodyLinAccelComp = _ecm.Component<components::LinearAcceleration>(_entity);
                if (bodyLinAccelComp) {
                    Eigen::Vector3d bodyLinAccel = R_bs * frameData.linearAcceleration;
                    auto state = bodyLinAccelComp->SetData(math::eigen3::convert(bodyLinAccel),
                                                           this->vec3Eql)
                                     ? ComponentState::PeriodicChange
                                     : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::LinearAcceleration::typeId, state);
                }

                // Angular acceleration in world frame coordinates
                auto bodyAngAccelComp = _ecm.Component<components::AngularAcceleration>(_entity);
                if (bodyAngAccelComp) {
                    Eigen::Vector3d bodyAngAccel = R_bs * frameData.angularAcceleration;
                    auto state = bodyAngAccelComp->SetData(math::eigen3::convert(bodyAngAccel),
                                                           this->vec3Eql)
                                     ? ComponentState::PeriodicChange
                                     : ComponentState::NoChange;
                    _ecm.SetChanged(_entity, components::AngularAcceleration::typeId, state);
                }
            }
            else {
                ignwarn << "Unknown link with id " << _entity << " found\n";
            }
            return true;
        });

    // joint force
    _ecm.Each<components::Joint,
              components::Name,
              components::JointForce,
              components::JointForceCmd>([&](const Entity& _entity,
                                             components::Joint* /*_joint*/,
                                             components::Name* _name,
                                             components::JointForce* _force,
                                             components::JointForceCmd* _forceCmd) -> bool {
        // Get the data from the components
        auto& jointForceData = _force->Data();
        auto jointForceCmdData = _forceCmd->Data();

        if (jointForceData.size() != jointForceCmdData.size()) {
            ignwarn << "There is a mismatch in the degrees of freedom in"
                    << " Joint [" << _name->Data() << "(Entity=" << _entity
                    << ")] between its JointForce and JointForceCmd components." << std::endl;
        }

        // Copy the force cmd
        jointForceData = jointForceCmdData;

        // If the history is enabled, append the force command also there
        auto historyComponent =
            _ecm.Component<ignition::gazebo::components::HistoryOfAppliedJointForces>(_entity);

        // Since the operation is an append, we have to perform it only when
        // the physics step is actually performed
        if (!_info.paused && historyComponent) {
            auto& history = scenario::gazebo::utils::getExistingComponentData<
                ignition::gazebo::components::HistoryOfAppliedJointForces>(&_ecm, _entity);

            for (const auto& jointForce : jointForceData) {
                history.push(jointForce);
            }
        }

        return true;
    });

    // pose/velocity/acceleration of non-link entities such as sensors /
    // collisions. These get updated only if another system has created a
    // components::WorldPose component for the entity.
    // Populated components:
    // * WorldPose
    // * WorldLinearVelocity
    // * AngularVelocity
    // * LinearAcceleration

    // world pose
    _ecm.Each<components::Pose, components::WorldPose, components::ParentEntity>(
        [&](const Entity&,
            const components::Pose* _pose,
            components::WorldPose* _worldPose,
            const components::ParentEntity* _parent) -> bool {
            // check if parent entity is a link, e.g. entity is sensor / collision
            auto linkIt = this->entityLinkMap.find(_parent->Data());
            if (linkIt != this->entityLinkMap.end()) {
                const auto entityFrameData =
                    this->LinkFrameDataAtOffset(linkIt->second, _pose->Data());

                *_worldPose = components::WorldPose(math::eigen3::convert(entityFrameData.pose));
            }

            return true;
        });

    // world linear velocity
    _ecm.Each<components::Pose, components::WorldLinearVelocity, components::ParentEntity>(
        [&](const Entity&,
            const components::Pose* _pose,
            components::WorldLinearVelocity* _worldLinearVel,
            const components::ParentEntity* _parent) -> bool {
            // check if parent entity is a link, e.g. entity is sensor / collision
            auto linkIt = this->entityLinkMap.find(_parent->Data());
            if (linkIt != this->entityLinkMap.end()) {
                const auto entityFrameData =
                    this->LinkFrameDataAtOffset(linkIt->second, _pose->Data());

                // set entity world linear velocity
                *_worldLinearVel = components::WorldLinearVelocity(
                    math::eigen3::convert(entityFrameData.linearVelocity));
            }

            return true;
        });

    // body angular velocity
    _ecm.Each<components::Pose, components::AngularVelocity, components::ParentEntity>(
        [&](const Entity&,
            const components::Pose* _pose,
            components::AngularVelocity* _angularVel,
            const components::ParentEntity* _parent) -> bool {
            // check if parent entity is a link, e.g. entity is sensor / collision
            auto linkIt = this->entityLinkMap.find(_parent->Data());
            if (linkIt != this->entityLinkMap.end()) {
                const auto entityFrameData =
                    this->LinkFrameDataAtOffset(linkIt->second, _pose->Data());

                auto entityWorldPose = math::eigen3::convert(entityFrameData.pose);
                ignition::math::Vector3d entityWorldAngularVel =
                    math::eigen3::convert(entityFrameData.angularVelocity);

                auto entityBodyAngularVel =
                    entityWorldPose.Rot().RotateVectorReverse(entityWorldAngularVel);
                *_angularVel = components::AngularVelocity(entityBodyAngularVel);
            }

            return true;
        });

    // body linear acceleration
    _ecm.Each<components::Pose, components::LinearAcceleration, components::ParentEntity>(
        [&](const Entity&,
            const components::Pose* _pose,
            components::LinearAcceleration* _linearAcc,
            const components::ParentEntity* _parent) -> bool {
            auto linkIt = this->entityLinkMap.find(_parent->Data());
            if (linkIt != this->entityLinkMap.end()) {
                const auto entityFrameData =
                    this->LinkFrameDataAtOffset(linkIt->second, _pose->Data());

                auto entityWorldPose = math::eigen3::convert(entityFrameData.pose);
                ignition::math::Vector3d entityWorldLinearAcc =
                    math::eigen3::convert(entityFrameData.linearAcceleration);

                auto entityBodyLinearAcc =
                    entityWorldPose.Rot().RotateVectorReverse(entityWorldLinearAcc);
                *_linearAcc = components::LinearAcceleration(entityBodyLinearAcc);
            }

            return true;
        });

    // Clear reset components
    _ecm.Each<components::JointPositionReset>(
        [&](const Entity& _entity, components::JointPositionReset*) -> bool {
            _ecm.RemoveComponent<components::JointPositionReset>(_entity);
            return true;
        });

    _ecm.Each<components::JointVelocityReset>(
        [&](const Entity& _entity, components::JointVelocityReset*) -> bool {
            _ecm.RemoveComponent<components::JointVelocityReset>(_entity);
            return true;
        });

    // Clear pending commands
    _ecm.Each<components::JointForceCmd>(
        [&](const Entity&, components::JointForceCmd* _force) -> bool {
            std::fill(_force->Data().begin(), _force->Data().end(), 0.0);
            return true;
        });

    _ecm.Each<components::ExternalWorldWrenchCmd>(
        [&](const Entity&, components::ExternalWorldWrenchCmd* _wrench) -> bool {
            _wrench->Data().Clear();
            return true;
        });

    _ecm.Each<components::JointVelocityCmd>(
        [&](const Entity&, components::JointVelocityCmd* _vel) -> bool {
            std::fill(_vel->Data().begin(), _vel->Data().end(), 0.0);
            return true;
        });

    // Update joint positions
    _ecm.Each<components::Joint, components::JointPosition>(
        [&](const Entity& _entity,
            components::Joint*,
            components::JointPosition* _jointPos) -> bool {
            auto jointIt = this->entityJointMap.find(_entity);
            if (jointIt != this->entityJointMap.end()) {
                _jointPos->Data().resize(jointIt->second->GetDegreesOfFreedom());
                for (std::size_t i = 0; i < jointIt->second->GetDegreesOfFreedom(); ++i) {
                    _jointPos->Data()[i] = jointIt->second->GetPosition(i);
                }
            }
            return true;
        });

    // Update joint Velocities
    _ecm.Each<components::Joint, components::JointVelocity>(
        [&](const Entity& _entity,
            components::Joint*,
            components::JointVelocity* _jointVel) -> bool {
            auto jointIt = this->entityJointMap.find(_entity);
            if (jointIt != this->entityJointMap.end()) {
                _jointVel->Data().resize(jointIt->second->GetDegreesOfFreedom());
                for (std::size_t i = 0; i < jointIt->second->GetDegreesOfFreedom(); ++i) {
                    _jointVel->Data()[i] = jointIt->second->GetVelocity(i);
                }
            }
            return true;
        });
    this->UpdateCollisions(_ecm);
}

void Physics::Impl::UpdateCollisions(EntityComponentManager& _ecm) const
{
    // Quit early if the ContactData component hasn't been created. This means
    // there are no systems that need contact information
    if (!_ecm.HasComponentType(components::ContactSensorData::typeId))
        return;

    // TODO(addisu) If systems are assumed to only have one world, we should
    // capture the world Entity in a Configure call
    Entity worldEntity = _ecm.EntityByComponents(components::World());

    if (worldEntity == kNullEntity) {
        ignerr << "Missing world entity.\n";
        return;
    }

    // Safe to assume this won't throw because the world entity should always be
    // available
    auto worldPhys = this->entityWorldMap.at(worldEntity);

    // Struct containing pointer of contact data
    struct AllContactData
    {
        const WorldType::ContactPoint* point;
        const WorldType::ExtraContactData* extra;
    };

    // Each contact object we get from ign-physics contains the EntityPtrs of
    // the two colliding entities and other data about the contact such as the
    // position. This map groups contacts so that it is easy to query all the
    // contacts of one entity.
    using EntityContactMap = std::unordered_map<Entity, std::deque<AllContactData>>;

    // This data structure is essentially a mapping between a pair of entities
    // and a list of pointers to their contact object. We use a map inside a map
    // to create msgs::Contact objects conveniently later on.
    std::unordered_map<Entity, EntityContactMap> entityContactMap;

    // Note that we are temporarily storing pointers to elements in this
    // ("allContacts") container. Thus, we must make sure it doesn't get
    // destroyed until the end of this function.
    auto allContacts = worldPhys->GetContactsFromLastStep();
    for (const auto& contactComposite : allContacts) {
        // Get the RequireData
        const auto& contact = contactComposite.Get<WorldType::ContactPoint>();
        auto coll1It = this->collisionEntityMap.find(contact.collision1);
        auto coll2It = this->collisionEntityMap.find(contact.collision2);

        // Check the ExpectData
        const auto* extraContactData = contactComposite.Query<WorldType::ExtraContactData>();

        if ((coll1It != this->collisionEntityMap.end())
            && (coll2It != this->collisionEntityMap.end())) {

            AllContactData allContactData;
            allContactData.point = &contact;

            if (extraContactData) {
                allContactData.extra = extraContactData;
            }

            // Note that the ExtraContactData is valid only when the first
            // collision is the first body. Quantities like the force and the
            // normal must be flipped in the second case.
            entityContactMap[coll1It->second][coll2It->second].push_back(allContactData);
            entityContactMap[coll2It->second][coll1It->second].push_back(allContactData);
        }
    }

    // Go through each collision entity that has a ContactData component and
    // set the component value to the list of contacts that correspond to
    // the collision entity
    _ecm.Each<components::Collision, components::ContactSensorData>(
        [&](const Entity& _collEntity1,
            components::Collision*,
            components::ContactSensorData* _contacts) -> bool {
            if (entityContactMap.find(_collEntity1) == entityContactMap.end()) {
                // Clear the last contact data
                *_contacts = components::ContactSensorData();
                return true;
            }

            const auto& contactMap = entityContactMap[_collEntity1];

            msgs::Contacts contactsComp;

            for (const auto& [collEntity2, contactData] : contactMap) {

                msgs::Contact* contactMsg = contactsComp.add_contact();
                contactMsg->mutable_collision1()->set_id(_collEntity1);
                contactMsg->mutable_collision2()->set_id(collEntity2);

                for (const auto& contact : contactData) {
                    auto* position = contactMsg->add_position();
                    position->set_x(contact.point->point.x());
                    position->set_y(contact.point->point.y());
                    position->set_z(contact.point->point.z());

                    if (contact.extra) {
                        // Add the penetration depth
                        contactMsg->add_depth(contact.extra->depth);

                        // Get the name of the collisions
                        auto collisionName1 =
                            _ecm.Component<components::Name>(_collEntity1)->Data();
                        auto collisionName2 = _ecm.Component<components::Name>(collEntity2)->Data();

                        // Add the wrench (only the force component)
                        auto* wrench = contactMsg->add_wrench();
                        wrench->set_body_1_name(collisionName1);
                        wrench->set_body_2_name(collisionName2);
                        auto* wrench1 = wrench->mutable_body_1_wrench();
                        auto* wrench2 = wrench->mutable_body_2_wrench();

                        auto* force1 = wrench1->mutable_force();
                        auto* force2 = wrench2->mutable_force();
                        auto* torque1 = wrench1->mutable_torque();
                        auto* torque2 = wrench2->mutable_torque();

                        // The same ContactPoint and ExtraContactData are used
                        // for the contact between collision1 and collision2. In
                        // those structures there is some data, like the force
                        // and normal, that cannot commute.
                        if (_collEntity1
                            == this->collisionEntityMap.at(contact.point->collision1)) {
                            assert(collEntity2
                                   == this->collisionEntityMap.at(contact.point->collision2));
                            // Use the data as it is
                            *force1 = msgs::Convert(math::eigen3::convert(contact.extra->force));
                            *force2 = msgs::Convert(-math::eigen3::convert(contact.extra->force));
                            // Add the wrench normal
                            auto* normal = contactMsg->add_normal();
                            normal->set_x(contact.extra->normal.x());
                            normal->set_y(contact.extra->normal.y());
                            normal->set_z(contact.extra->normal.z());
                        }
                        else {
                            assert(collEntity2
                                   == this->collisionEntityMap.at(contact.point->collision1));
                            // Flip the force
                            *force1 = msgs::Convert(-math::eigen3::convert(contact.extra->force));
                            *force2 = msgs::Convert(math::eigen3::convert(contact.extra->force));
                            // Flip the normal
                            auto* normal = contactMsg->add_normal();
                            normal->set_x(-contact.extra->normal.x());
                            normal->set_y(-contact.extra->normal.y());
                            normal->set_z(-contact.extra->normal.z());
                        }

                        *torque1 = msgs::Convert(math::Vector3d::Zero);
                        *torque2 = msgs::Convert(math::Vector3d::Zero);
                    }
                }
            }
            *_contacts = components::ContactSensorData(contactsComp);

            return true;
        });
}

physics::FrameData3d Physics::Impl::LinkFrameDataAtOffset(const LinkPtrType& _link,
                                                          const math::Pose3d& _pose) const
{
    physics::FrameData3d parent;
    parent.pose = math::eigen3::convert(_pose);
    physics::RelativeFrameData3d relFrameData(_link->GetFrameID(), parent);
    return this->engine->Resolve(relFrameData, physics::FrameID::World());
}

IGNITION_ADD_PLUGIN(Physics, ignition::gazebo::System, Physics::ISystemUpdate)
IGNITION_ADD_PLUGIN_ALIAS(Physics, "ignition::gazebo::systems::Physics")
