# Copyright (C) 2020 Istituto Italiano di Tecnologia (IIT). All rights reserved.
# This software may be modified and distributed under the terms of the
# GNU Lesser General Public License v2.1 or any later version.

import pytest
pytestmark = pytest.mark.scenario

import numpy as np
from ..common import utils
import gym_ignition_models
from typing import Callable
from gym_ignition.utils import misc
from ..common.utils import gazebo_fixture as gazebo
from gym_ignition import scenario_bindings as bindings


def get_cube_urdf_string_double_collision() -> str:

    mass = 5.0
    edge = 0.2
    i = 1 / 12 * mass * (edge ** 2 + edge ** 2)
    cube_urdf = f"""
    <robot name="cube_robot" xmlns:xacro="http://www.ros.org/wiki/xacro">
        <link name="cube">
            <inertial>
              <origin rpy="0 0 0" xyz="0 0 0"/>
              <mass value="{mass}"/>
              <inertia ixx="{i}" ixy="0" ixz="0" iyy="{i}" iyz="0" izz="{i}"/>
            </inertial>
            <visual>
              <geometry>
                <box size="{edge} {edge} {edge}"/>
              </geometry>
              <origin rpy="0 0 0" xyz="0 0 0"/>
            </visual>
            <collision>
              <geometry>
                <box size="{edge} {edge / 2} {edge}"/>
              </geometry>
              <origin rpy="0 0 0" xyz="0 -{edge / 4} 0"/>
            </collision>
            <collision>
              <geometry>
                <box size="{edge} {edge / 2} {edge}"/>
              </geometry>
              <origin rpy="0 0 0" xyz="0 {edge / 4} 0"/>
            </collision>
        </link>
    </robot>"""

    return cube_urdf


@pytest.mark.parametrize("gazebo, get_model_str",
                         [((0.001, 1.0, 1), utils.get_cube_urdf_string),
                          ((0.001, 1.0, 1), get_cube_urdf_string_double_collision)],
                         indirect=["gazebo"],
                         ids=utils.id_gazebo_fn)
def test_cube_contact(gazebo: bindings.GazeboSimulator,
                      get_model_str: Callable):

    assert gazebo.initialize()
    world = gazebo.getWorld()

    # Insert the Physics system
    assert world.insertWorldPlugin("libPhysicsSystem.so",
                                   "scenario::plugins::gazebo::Physics")

    # Insert the ground plane
    assert world.insertModel(gym_ignition_models.get_model_file("ground_plane"))
    assert len(world.modelNames()) == 1

    # Insert the cube
    cube_urdf = misc.string_to_file(get_model_str())
    assert world.insertModel(cube_urdf,
                             bindings.Pose([0, 0, 0.15], [1., 0, 0, 0]),
                             "cube")
    assert len(world.modelNames()) == 2

    # Get the cube
    cube = world.getModel("cube")

    # Enable contact detection
    assert not cube.contactsEnabled()
    assert cube.enableContacts()
    assert cube.contactsEnabled()

    # The cube was inserted floating in the air with a 5cm gap wrt to ground.
    # There should be no contacts.
    gazebo.run(paused=True)
    assert not cube.getLink("cube").inContact()
    assert len(cube.contacts()) == 0

    # Make the cube fall for 150ms
    for _ in range(150):
        gazebo.run()

    # There should be only one contact, between ground and the cube
    assert cube.getLink("cube").inContact()
    assert len(cube.contacts()) == 1

    # Get the contact
    contact_with_ground = cube.contacts()[0]

    assert contact_with_ground.bodyA == "cube::cube"
    assert contact_with_ground.bodyB == "ground_plane::link"

    # Check contact points
    for point in contact_with_ground.points:

        assert point.normal == pytest.approx([0, 0, 1])

    # Check that the contact force matches the weight of the cube
    z_forces = [point.force[2] for point in contact_with_ground.points]
    assert np.sum(z_forces) == pytest.approx(50, abs=1.0)

    # Forces of all contact points are combined by the following method
    assert cube.getLink("cube").contactWrench() == pytest.approx([0, 0, np.sum(z_forces),
                                                                  0, 0, 0])


@pytest.mark.parametrize("gazebo, get_model_str",
                         [((0.001, 1.0, 1), utils.get_cube_urdf_string),
                          ((0.001, 1.0, 1), get_cube_urdf_string_double_collision)],
                         indirect=["gazebo"],
                         ids=utils.id_gazebo_fn)
def test_cube_multiple_contacts(gazebo: bindings.GazeboSimulator,
                                get_model_str: Callable):

    assert gazebo.initialize()
    world = gazebo.getWorld()

    # Insert the Physics system
    assert world.insertWorldPlugin("libPhysicsSystem.so",
                                   "scenario::plugins::gazebo::Physics")

    # Insert the ground plane
    assert world.insertModel(gym_ignition_models.get_model_file("ground_plane"))
    assert len(world.modelNames()) == 1

    # Insert two cubes side to side with a 10cm gap
    cube_urdf = misc.string_to_file(get_model_str())
    assert world.insertModel(cube_urdf,
                             bindings.Pose([0, -0.15, 0.101], [1., 0, 0, 0]),
                             "cube1")
    assert world.insertModel(cube_urdf,
                             bindings.Pose([0, 0.15, 0.101], [1., 0, 0, 0]),
                             "cube2")
    assert len(world.modelNames()) == 3

    # Get the cubes
    cube1 = world.getModel("cube1")
    cube2 = world.getModel("cube2")

    # Enable contact detection
    assert cube1.enableContacts()
    assert cube2.enableContacts()

    # The cubes were inserted floating in the air with a 1mm gap wrt to ground.
    # There should be no contacts.
    gazebo.run(paused=True)
    assert not cube1.getLink("cube").inContact()
    assert not cube2.getLink("cube").inContact()
    assert len(cube1.contacts()) == 0
    assert len(cube2.contacts()) == 0

    # Make the cubes fall for 50ms
    for _ in range(50):
        gazebo.run()

    assert cube1.getLink("cube").inContact()
    assert cube2.getLink("cube").inContact()
    assert len(cube1.contacts()) == 1
    assert len(cube2.contacts()) == 1

    # Now we make another cube fall above the gap. It will touch both cubes.
    assert world.insertModel(cube_urdf,
                             bindings.Pose([0, 0, 0.301], [1., 0, 0, 0]),
                             "cube3")
    assert len(world.modelNames()) == 4

    cube3 = world.getModel("cube3")
    assert cube3.enableContacts()

    gazebo.run(paused=True)
    assert not cube3.getLink("cube").inContact()
    assert len(cube3.contacts()) == 0

    # Make the cube fall for 50ms
    for _ in range(50):
        gazebo.run()

    # There will be two contacts, respectively with cube1 and cube2.
    # Contacts are related to the link, not the collision elements. In the case of two
    # collisions elements associated to the same link, contacts are merged together.
    assert cube3.getLink("cube").inContact()
    assert len(cube3.contacts()) == 2

    contact1 = cube3.contacts()[0]
    contact2 = cube3.contacts()[1]

    assert contact1.bodyA == "cube3::cube"
    assert contact2.bodyA == "cube3::cube"

    assert contact1.bodyB == "cube1::cube"
    assert contact2.bodyB == "cube2::cube"

    # Calculate the total contact wrench of cube3
    assert cube3.getLink("cube").contactWrench() == pytest.approx([0, 0, 50, 0, 0, 0],
                                                                  abs=1.1)

    # Calculate the total contact force of the cubes below.
    # They will have 1.5 their weight from below and -0.5 from above.
    assert cube1.getLink("cube").contactWrench()[2] == pytest.approx(50, abs=1.1)
    assert cube1.getLink("cube").contactWrench()[2] == pytest.approx(50, abs=1.1)

    # Check the normals and the sign of the forces
    for contact in cube2.contacts():
        if contact.bodyB == "cube3::cube":
            for point in contact.points:
                assert point.force[2] < 0
                assert point.normal == pytest.approx([0, 0, -1], abs=0.001)
        if contact.bodyB == "ground_plane::link":
            for point in contact.points:
                assert point.force[2] > 0
                assert point.normal == pytest.approx([0, 0, 1], abs=0.001)
