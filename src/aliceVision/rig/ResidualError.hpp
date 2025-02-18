// This file is part of the AliceVision project.
// Copyright (c) 2016 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <aliceVision/geometry/Pose3.hpp>
#include <aliceVision/camera/camera.hpp>  //todo: not generic
                                          // only radial3 is currently supported
                                          // todo: allows internal parameters refinement
#include <ceres/ceres.h>
#include <ceres/rotation.h>

namespace aliceVision {
namespace rig {

class ResidualErrorMainCameraFunctor
{
  public:
    ResidualErrorMainCameraFunctor(const camera::Pinhole& intrinsics, const Vec2& pt2d, const Vec3& pt3d)
    {
        // Set the intrinsics
        _K = intrinsics.K();

        _params.reserve(4);
        _params.push_back(intrinsics.getParams()[4]);
        _params.push_back(intrinsics.getParams()[5]);
        _params.push_back(intrinsics.getParams()[6]);

        // Set the observation
        _observation[0] = pt2d[0];
        _observation[1] = pt2d[1];

        // Set the 3D point
        _point(0) = pt3d(0);
        _point(1) = pt3d(1);
        _point(2) = pt3d(2);
    }

    // Enum to map intrinsics parameters between aliceVision & ceres camera data parameter block.
    //  enum {
    //    OFFSET_FOCAL_LENGTH = 0,
    //    OFFSET_PRINCIPAL_POINT_X = 1,
    //    OFFSET_PRINCIPAL_POINT_Y = 2,
    //    OFFSET_DISTO_K1 = 3,
    //    OFFSET_DISTO_K2 = 4,
    //    OFFSET_DISTO_K3 = 5,
    //  };

    /**
     * @param[in] cam_K: Camera intrinsics( focal, principal point [x,y] )
     * @param[in] cam_Rt: Camera parameterized using one block of 6 parameters [R;t]:
     *   - 3 for rotation(angle axis), 3 for translation
     * @param[in] pos_3dpoint
     * @param[out] out_residuals
     */
    template<typename T>
    bool operator()(const T* const cam_Rt, T* out_residuals) const
    {
        //--
        // Apply external parameters (Pose)
        //--

        const T* cam_R = cam_Rt;
        const T* cam_t = &cam_Rt[3];

        T pos_3dpoint[3];
        pos_3dpoint[0] = T(_point(0));
        pos_3dpoint[1] = T(_point(1));
        pos_3dpoint[2] = T(_point(2));

        T pos_proj[3];
        // Rotate the point according the camera rotation
        ceres::AngleAxisRotatePoint(cam_R, pos_3dpoint, pos_proj);

        // Apply the camera translation
        pos_proj[0] += cam_t[0];
        pos_proj[1] += cam_t[1];
        pos_proj[2] += cam_t[2];

        // Transform the point from homogeneous to euclidean (undistorted point)
        const T x_u = pos_proj[0] / pos_proj[2];
        const T y_u = pos_proj[1] / pos_proj[2];

        //--
        // Apply intrinsic parameters
        //--

        const T focal = T(_K(0, 0));
        const T principal_point_x = T(_K(0, 2));
        const T principal_point_y = T(_K(1, 2));
        const T k1 = T(_params[0]);
        const T k2 = T(_params[1]);
        const T k3 = T(_params[2]);

        // Apply distortion (xd,yd) = disto(x_u,y_u)
        const T r2 = x_u * x_u + y_u * y_u;
        const T r4 = r2 * r2;
        const T r6 = r4 * r2;
        const T r_coeff = (T(1) + k1 * r2 + k2 * r4 + k3 * r6);
        const T x_d = x_u * r_coeff;
        const T y_d = y_u * r_coeff;

        // Apply focal length and principal point to get the final image coordinates
        const T projected_x = principal_point_x + focal * x_d;
        const T projected_y = principal_point_y + focal * y_d;

        // Compute and return the error is the difference between the predicted
        //  and observed position
        out_residuals[0] = projected_x - _observation[0];
        out_residuals[1] = projected_y - _observation[1];

        return true;
    }

  private:
    aliceVision::Mat3 _K;            // Calibration matrix
    std::vector<double> _params;     // {K1, K2, K3}
    aliceVision::Vec3 _point;        // 3D point
    aliceVision::Vec2 _observation;  // its image location
};

class ResidualErrorSecondaryCameraFunctor
{
  public:
    ResidualErrorSecondaryCameraFunctor(const camera::Pinhole& intrinsics,
                                        const aliceVision::Vec2& pt2d,
                                        const aliceVision::Vec3& pt3d)  // const double* const pos_2dpoint
    {
        // Set the intrinsics
        _K = intrinsics.K();

        _params.reserve(3);
        _params.push_back(intrinsics.getParams()[3]);
        _params.push_back(intrinsics.getParams()[4]);
        _params.push_back(intrinsics.getParams()[5]);

        // Set the observation
        _observation[0] = pt2d[0];
        _observation[1] = pt2d[1];

        // Set the 3D point
        _point(0) = pt3d(0);
        _point(1) = pt3d(1);
        _point(2) = pt3d(2);
    }

    // Enum to map intrinsics parameters between aliceVision & ceres camera data parameter block.
    //  enum {
    //    OFFSET_FOCAL_LENGTH = 0,
    //    OFFSET_PRINCIPAL_POINT_X = 1,
    //    OFFSET_PRINCIPAL_POINT_Y = 2,
    //    OFFSET_DISTO_K1 = 3,
    //    OFFSET_DISTO_K2 = 4,
    //    OFFSET_DISTO_K3 = 5,
    //  };

    /**
     * @param[in] cam_Rt_main: Main camera parameterized using one block of 6 parameters [R;t]:
     *   - 3 for rotation(angle axis), 3 for translation
     * @param[in] cam_Rt_relative: Relative pose of the witness camera relatively to the main one
     *   parameterized using one block of 6 parameters [R;t]:
     *   - 3 for rotation(angle axis), 3 for translation
     * @param[out] out_residuals
     */
    template<typename T>
    bool operator()(const T* const cam_Rt_main, const T* const cam_Rt_relative, T* out_residuals) const
    {
        //--
        // Apply external parameters (Pose)
        //--

        const T* RMain = cam_Rt_main;
        const T* tMain = &cam_Rt_main[3];

        const T* RRelative = cam_Rt_relative;
        const T* tRelative = &cam_Rt_relative[3];

        T pos_3dpoint[3];
        pos_3dpoint[0] = T(_point(0));
        pos_3dpoint[1] = T(_point(1));
        pos_3dpoint[2] = T(_point(2));

        T pos_tmp[3];
        // Rotate the point according the relative rotation first
        ceres::AngleAxisRotatePoint(RMain, pos_3dpoint, pos_tmp);

        // Apply the relative translation first
        pos_tmp[0] += tMain[0];
        pos_tmp[1] += tMain[1];
        pos_tmp[2] += tMain[2];

        T pos_proj[3];
        // Rotate the point according the main camera rotation
        ceres::AngleAxisRotatePoint(RRelative, pos_tmp, pos_proj);

        // Apply the main camera translation
        pos_proj[0] += tRelative[0];
        pos_proj[1] += tRelative[1];
        pos_proj[2] += tRelative[2];

        // Transform the point from homogeneous to euclidean (undistorted point)
        const T x_u = pos_proj[0] / pos_proj[2];
        const T y_u = pos_proj[1] / pos_proj[2];

        //--
        // Apply intrinsic parameters
        //--

        const T focal = T(_K(0, 0));
        const T principal_point_x = T(_K(0, 2));
        const T principal_point_y = T(_K(1, 2));
        const T k1 = T(_params[0]);
        const T k2 = T(_params[1]);
        const T k3 = T(_params[2]);

        // Apply distortion (xd,yd) = disto(x_u,y_u)
        const T r2 = x_u * x_u + y_u * y_u;
        const T r4 = r2 * r2;
        const T r6 = r4 * r2;
        const T r_coeff = (T(1) + k1 * r2 + k2 * r4 + k3 * r6);
        const T x_d = x_u * r_coeff;
        const T y_d = y_u * r_coeff;

        // Apply focal length and principal point to get the final image coordinates
        const T projected_x = principal_point_x + focal * x_d;
        const T projected_y = principal_point_y + focal * y_d;

        // Compute and return the error is the difference between the predicted
        //  and observed position
        out_residuals[0] = projected_x - _observation[0];
        out_residuals[1] = projected_y - _observation[1];

        return true;
    }

  private:
    aliceVision::Mat3 _K;            // Calibration matrix
    std::vector<double> _params;     // {K1, K2, K3}
    aliceVision::Vec3 _point;        // 3D point
    aliceVision::Vec2 _observation;  // its image location
};

/**
 * @brief class to be used when the the poses of the witness cameras are known and
 * must be kept fixed (no refining) and only the pose of the whole rig must be
 * refined
 */
class ResidualErrorSecondaryCameraFixedRelativeFunctor
{
  public:
    ResidualErrorSecondaryCameraFixedRelativeFunctor(const camera::Pinhole& intrinsics,
                                                     const Vec2& pt2d,
                                                     const Vec3& pt3d,
                                                     const geometry::Pose3& relativePose)  // const double* const pos_2dpoint
    {
        // Set the intrinsics
        _K = intrinsics.K();

        _params.reserve(3);
        _params.push_back(intrinsics.getParams()[3]);
        _params.push_back(intrinsics.getParams()[4]);
        _params.push_back(intrinsics.getParams()[5]);

        // Set the observation
        _observation[0] = pt2d[0];
        _observation[1] = pt2d[1];

        // Set the 3D point
        _point(0) = pt3d(0);
        _point(1) = pt3d(1);
        _point(2) = pt3d(2);

        const aliceVision::Mat3& R = relativePose.rotation();
        const aliceVision::Vec3& t = relativePose.translation();

        // convert the relative pose into angle axis representation
        ceres::RotationMatrixToAngleAxis((const double*)R.data(), _relativePose);
        // and set the translation
        _relativePose[3] = t(0);
        _relativePose[4] = t(1);
        _relativePose[5] = t(2);
    }

    // Enum to map intrinsics parameters between aliceVision & ceres camera data parameter block.
    //  enum {
    //    OFFSET_FOCAL_LENGTH = 0,
    //    OFFSET_PRINCIPAL_POINT_X = 1,
    //    OFFSET_PRINCIPAL_POINT_Y = 2,
    //    OFFSET_DISTO_K1 = 3,
    //    OFFSET_DISTO_K2 = 4,
    //    OFFSET_DISTO_K3 = 5,
    //  };

    /**
     * @param[in] cam_Rt_main: Main camera parameterized using one block of 6 parameters [R;t]:
     *   - 3 for rotation(angle axis), 3 for translation
     * @param[out] out_residuals
     */
    template<typename T>
    bool operator()(const T* const cam_Rt_main, T* out_residuals) const
    {
        //--
        // Apply external parameters (Pose)
        //--

        const T* RMain = cam_Rt_main;
        const T* tMain = &cam_Rt_main[3];

        T RRelative[3];
        RRelative[0] = T(_relativePose[0]);
        RRelative[1] = T(_relativePose[1]);
        RRelative[2] = T(_relativePose[2]);
        T tRelative[3];
        tRelative[0] = T(_relativePose[3]);
        tRelative[1] = T(_relativePose[4]);
        tRelative[2] = T(_relativePose[5]);

        T pos_3dpoint[3];
        pos_3dpoint[0] = T(_point(0));
        pos_3dpoint[1] = T(_point(1));
        pos_3dpoint[2] = T(_point(2));

        T pos_tmp[3];
        // Rotate the point according the main rotation first
        ceres::AngleAxisRotatePoint(RMain, pos_3dpoint, pos_tmp);

        // Apply the main translation first
        pos_tmp[0] += tMain[0];
        pos_tmp[1] += tMain[1];
        pos_tmp[2] += tMain[2];

        T pos_proj[3];
        // Rotate the point according the relative camera rotation
        ceres::AngleAxisRotatePoint(RRelative, pos_tmp, pos_proj);

        // Apply the relative camera translation
        pos_proj[0] += T(tRelative[0]);
        pos_proj[1] += T(tRelative[1]);
        pos_proj[2] += T(tRelative[2]);

        // Transform the point from homogeneous to euclidean (undistorted point)
        const T x_u = pos_proj[0] / pos_proj[2];
        const T y_u = pos_proj[1] / pos_proj[2];

        //--
        // Apply intrinsic parameters
        //--

        const T focal = T(_K(0, 0));
        const T principal_point_x = T(_K(0, 2));
        const T principal_point_y = T(_K(1, 2));
        const T k1 = T(_params[0]);
        const T k2 = T(_params[1]);
        const T k3 = T(_params[2]);

        // Apply distortion (xd,yd) = disto(x_u,y_u)
        const T r2 = x_u * x_u + y_u * y_u;
        const T r4 = r2 * r2;
        const T r6 = r4 * r2;
        const T r_coeff = (T(1) + k1 * r2 + k2 * r4 + k3 * r6);
        const T x_d = x_u * r_coeff;
        const T y_d = y_u * r_coeff;

        // Apply focal length and principal point to get the final image coordinates
        const T projected_x = principal_point_x + focal * x_d;
        const T projected_y = principal_point_y + focal * y_d;

        // Compute and return the error is the difference between the predicted
        //  and observed position
        out_residuals[0] = projected_x - _observation[0];
        out_residuals[1] = projected_y - _observation[1];

        return true;
    }

  private:
    aliceVision::Mat3 _K;            // Calibration matrix
    std::vector<double> _params;     // {K1, K2, K3}
    aliceVision::Vec3 _point;        // 3D point
    aliceVision::Vec2 _observation;  // its image location
    double _relativePose[6];         // the relative pose of the witness camera wrt
                                     // in angle axis format the main camera
};

}  // namespace rig
}  // namespace aliceVision