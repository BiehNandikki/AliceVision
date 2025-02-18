// This file is part of the AliceVision project.
// Copyright (c) 2022 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#define BOOST_TEST_MODULE colmap

#include <aliceVision/camera/cameraCommon.hpp>

#include <aliceVision/sfmDataIO/colmap.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/floating_point_comparison.hpp>
#include <aliceVision/unitTest.hpp>

using namespace aliceVision;

BOOST_AUTO_TEST_CASE(colmap_isCompatible)
{
    const std::map<std::pair<camera::EINTRINSIC, camera::EDISTORTION>, bool> compatibility{
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_NONE}, true},
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_RADIALK1}, true},
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_RADIALK3}, true},
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_BROWN}, true},
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_FISHEYE}, true},
      {{camera::EINTRINSIC::PINHOLE_CAMERA, camera::EDISTORTION::DISTORTION_FISHEYE1}, true},
      {{camera::EINTRINSIC::EQUIDISTANT_CAMERA, camera::EDISTORTION::DISTORTION_NONE}, false},
      {{camera::EINTRINSIC::EQUIDISTANT_CAMERA, camera::EDISTORTION::DISTORTION_RADIALK3PT}, false},
    };

    for (const auto& c : compatibility)
    {
        BOOST_CHECK(sfmDataIO::isColmapCompatible(c.first.first, c.first.second) == c.second);
    }
}

BOOST_AUTO_TEST_CASE(colmap_convertIntrinsicsToColmapString)
{
    sfmData::SfMData sfmTest{};
    auto& intrTest = sfmTest.getIntrinsics();
    BOOST_CHECK(intrTest.empty());
    // add some compatible intrinsics
    intrTest.emplace(10, camera::createPinhole(camera::EDISTORTION::DISTORTION_NONE,camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54));
    intrTest.emplace(11, camera::createPinhole(camera::EDISTORTION::DISTORTION_RADIALK1, camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54, {-0.02078}));
    intrTest.emplace(
      12,
      camera::createPinhole(camera::EDISTORTION::DISTORTION_RADIALK3, camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54, {-0.02078, 0.1705, -0.00714}));
    intrTest.emplace(
      13,
      camera::createPinhole(camera::EDISTORTION::DISTORTION_BROWN, camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54, {-0.02078, 0.1705, -0.00714, 0.00134, -0.000542}));
    intrTest.emplace(
      14,
      camera::createPinhole(camera::EDISTORTION::DISTORTION_FISHEYE, camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54, {-0.02078, 0.1705, -0.00714, 0.00134}));
    intrTest.emplace(15,
                     camera::createPinhole(camera::EDISTORTION::DISTORTION_FISHEYE1, camera::EUNDISTORTION::UNDISTORTION_NONE, 1920, 1080, 1548.76, 1547.32, 992.36, 549.54, {-0.000542}));
    // add some incompatible intrinsics
    intrTest.emplace(23, camera::createEquidistant(camera::EDISTORTION::DISTORTION_NONE,
                                                1920, 1080, 1548.76, 549.54, -0.02078));
    intrTest.emplace(
      24,
      camera::createEquidistant(
        camera::EDISTORTION::DISTORTION_RADIALK3PT, 
        1920, 1080, 1548.76, 549.54, -0.02078, {0.1705, -0.00714, 0.00134}
    )
    );

    // reference for each intrinsic ID the relevant expected string
    const std::map<IndexT, std::string> stringRef{
      {10, "10 PINHOLE 1920 1080 1548.76 1547.32 1952.36 1089.54\n"},
      {11, "11 FULL_OPENCV 1920 1080 1548.76 1547.32 1952.36 1089.54 -0.02078 0.0 0.0 0.0 0.0 0.0 0.0 0.0\n"},
      {12, "12 FULL_OPENCV 1920 1080 1548.76 1547.32 1952.36 1089.54 -0.02078 0.1705 0 0 -0.00714 0 0 0\n"},
      {13, "13 FULL_OPENCV 1920 1080 1548.76 1547.32 1952.36 1089.54 -0.02078 0.1705 0.00134 -0.000542 -0.00714 0 0 0\n"},
      {14, "14 OPENCV_FISHEYE 1920 1080 1548.76 1547.32 1952.36 1089.54 -0.02078 0.1705 -0.00714 0.00134\n"},
      {15, "15 FOV 1920 1080 1548.76 1547.32 1952.36 1089.54 -0.000542\n"}};

    // test the string conversion
    for (const auto& toTest : stringRef)
    {
        const auto id = toTest.first;
        const auto& ref = toTest.second;
        const auto out = sfmDataIO::convertIntrinsicsToColmapString(id, intrTest.at(id));
        BOOST_CHECK(ref == out);
    }

    // test throw if not compatible
    for (const auto& item : intrTest)
    {
        const auto id = item.first;
        const auto& intr = item.second;
        if (id >= 20)
        {
            BOOST_CHECK_THROW(sfmDataIO::convertIntrinsicsToColmapString(id, intr), std::invalid_argument);
        }
        else
        {
            camera::EDISTORTION distoType = camera::getDistortionType(*intr);
            BOOST_CHECK(sfmDataIO::isColmapCompatible(intr->getType(), distoType));
        }
    }

    // test get compatible intrinsics from sfmdata
    {
        const auto compIntr = sfmDataIO::getColmapCompatibleIntrinsics(sfmTest);
        BOOST_CHECK(compIntr.size() == 6);
        for (const auto& id : compIntr)
        {
            BOOST_CHECK(id < 20);
        }
    }

    // test compatible views
    {
        // add some fake view for each intrinsics and test get compatible views
        for (const auto& item : intrTest)
        {
            const auto intrID = item.first;
            for (IndexT cam = 0; cam < 5; ++cam)
            {
                const auto camID = intrID * 10 + cam;
                sfmTest.getViews().emplace(camID, std::make_shared<sfmData::View>("", cam, intrID, camID));
                sfmTest.getPoses().emplace(camID, sfmData::CameraPose());
            }
        }

        const auto compatibleViews = sfmDataIO::getColmapCompatibleViews(sfmTest);
        BOOST_CHECK(compatibleViews.size() == 30);
        // check the retrieved views are compatible
        for (const auto& id : compatibleViews)
        {
            const auto intrID = sfmTest.getViews().at(id)->getIntrinsicId();
            const auto intrinsics = sfmTest.getIntrinsics().at(intrID);

            camera::EDISTORTION distoType = camera::getDistortionType(*intrinsics);

            BOOST_CHECK(sfmDataIO::isColmapCompatible(intrinsics->getType(), distoType));
        }
    }
}