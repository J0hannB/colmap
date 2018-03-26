// COLMAP - Structure-from-Motion and Multi-View Stereo.
// Copyright (C) 2017  Johannes L. Schoenberger <jsch at inf.ethz.ch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef COLMAP_SRC_BASE_COST_FUNCTIONS_H_
#define COLMAP_SRC_BASE_COST_FUNCTIONS_H_

#include <Eigen/Core>

#include <ceres/ceres.h>
#include <ceres/rotation.h>

namespace colmap {

// class CameraPoseCostFunction {
//  public:
//   CameraPoseCostFunction(const Eigen::Vector3d& tvec,
//                          const Eigen::Matrix<double, 6, 6>& cov)
//       : t_(tvec),
//         cov_(cov) 
//     {}
// 
//   static ceres::CostFunction* Create(const Eigen::Vector3d& tvec,
//                                      const Eigen::Matrix<double, 6, 6>& cov) {
//     return (new ceres::AutoDiffCostFunction<
//             CameraPoseCostFunction, 3, 3>(
//         new CameraPoseCostFunction(tvec, cov)));
//   }
// 
//   template <typename T>
//   bool operator()(const T* const tvec, 
//                   T* residuals) const {
// 
//     typedef Eigen::Matrix<T, 3, 1> TVEC;
//     // typedef Eigen::Matrix<T, 6, 6> COV;
//     typedef Eigen::Matrix<T, 3, 3> COV;
// 
//     // Measurements
//     const TVEC tvec_meas = t_.cast<T>();
// 
//     // Square root of information matrix
//     const Eigen::Matrix3d info = cov_.topLeftCorner(3,3).inverse();
//     const Eigen::LLT<Eigen::Matrix3d> chol(info); 
//     const Eigen::Matrix3d Upper = chol.matrixU();
//     const COV sqrt_info = Upper.cast<T>();
// 
//     // Estimates
//     const TVEC tvec_est(tvec[0], tvec[1], tvec[2]);
// 
//     // Residuals scaled by square root information
//     const TVEC r = sqrt_info * (tvec_est - tvec_meas);
//      
//     residuals[0] = r(0);
//     residuals[1] = r(1);
//     residuals[2] = r(2);
// 
//     return true;
//   }
// 
//  private:
//   const Eigen::Vector3d t_;
//   const Eigen::Matrix<double, 6, 6> cov_;
// };

// Cost function to estimate camera pose given a measurement of its pose
class CameraPoseCostFunction {
 public:
  CameraPoseCostFunction(const Eigen::Vector4d& qvec,
                         const Eigen::Vector3d& tvec,
                         const Eigen::Matrix<double, 6, 6>& cov)
      : q_(qvec),
        t_(tvec),
        cov_(cov) 
    {}

  static ceres::CostFunction* Create(const Eigen::Vector4d& qvec,
                                     const Eigen::Vector3d& tvec,
                                     const Eigen::Matrix<double, 6, 6>& cov) {
    return (new ceres::AutoDiffCostFunction<
            CameraPoseCostFunction, 6, 4, 3>(
        new CameraPoseCostFunction(qvec, tvec, cov)));
  }

  template <typename T>
  bool operator()(const T* const qvec, const T* const tvec, 
                  T* residuals) const {

    typedef Eigen::Matrix<T, 4, 1> QVEC;
    typedef Eigen::Matrix<T, 3, 1> TVEC;
    typedef Eigen::Matrix<T, 6, 6> COV;

    // Measurements
    const QVEC qvec_meas = q_.cast<T>();
    const TVEC tvec_meas = t_.cast<T>();

    // Square root of information matrix
    const Eigen::Matrix<double, 6, 6> info = cov_.inverse();
    const Eigen::LLT<Eigen::Matrix<double, 6, 6> > chol(info); 
    const Eigen::Matrix<double, 6, 6> Upper = chol.matrixU();
    const COV sqrt_info = Upper.cast<T>();

    // Estimates
    const QVEC qvec_est(qvec[0], qvec[1], qvec[2], qvec[3]);
    const TVEC tvec_est(tvec[0], tvec[1], tvec[2]);

    // Conjugate/invert estimated quaternion
    // Conjugate is inverse when quaternion is unit
    const QVEC qvec_est_inv(qvec[0], -qvec[1], -qvec[2], -qvec[3]); 

    // Calculate quaternion error
    T dq[4];
    ceres::QuaternionProduct(qvec_est_inv.data(), qvec_meas.data(), dq);

    // Normalize quaternion error
    const T norm = sqrt(dq[0]*dq[0] + dq[1]*dq[1] + dq[2]*dq[2] + dq[3]*dq[3]);
    dq[0] /= norm;
    dq[1] /= norm;
    dq[2] /= norm;
    dq[3] /= norm;

    // Convert quaternion error to axis-angle representation
    T re[3];
    ceres::QuaternionToAngleAxis(dq, re);

    // tvec residual
    const TVEC rt = tvec_est - tvec_meas;

    // Combined residual
    const Eigen::Matrix<T, 6, 1> r = (Eigen::Matrix<T, 6, 1>() << rt(0), rt(1), rt(2), re[0], re[1], re[2]).finished();

    // Scale by square root info
    const Eigen::Matrix<T, 6, 1> rr = sqrt_info * r; 

    // Output
    residuals[0] = rr(0);
    residuals[1] = rr(1);
    residuals[2] = rr(2);
    residuals[3] = rr(3);
    residuals[4] = rr(4);
    residuals[5] = rr(5);

    return true;
  }

 private:
  const Eigen::Vector4d q_;
  const Eigen::Vector3d t_;
  const Eigen::Matrix<double, 6, 6> cov_;
};

// Standard bundle adjustment cost function for variable
// camera pose and calibration and point parameters.
template <typename CameraModel>
class BundleAdjustmentCostFunction {
 public:
  explicit BundleAdjustmentCostFunction(const Eigen::Vector2d& point2D)
      : x_(point2D(0)), y_(point2D(1)) {}

  static ceres::CostFunction* Create(const Eigen::Vector2d& point2D) {
    return (new ceres::AutoDiffCostFunction<
            BundleAdjustmentCostFunction<CameraModel>, 2, 4, 3, 3,
            CameraModel::kNumParams>(
        new BundleAdjustmentCostFunction(point2D)));
  }

  template <typename T>
  bool operator()(const T* const qvec, const T* const tvec,
                  const T* const point3D, const T* const camera_params,
                  T* residuals) const {
    // Rotate and translate.
    T projection[3];
    ceres::UnitQuaternionRotatePoint(qvec, point3D, projection);
    projection[0] += tvec[0];
    projection[1] += tvec[1];
    projection[2] += tvec[2];

    // Project to image plane.
    projection[0] /= projection[2];
    projection[1] /= projection[2];

    // Distort and transform to pixel space.
    CameraModel::WorldToImage(camera_params, projection[0], projection[1],
                              &residuals[0], &residuals[1]);

    // Re-projection error.
    residuals[0] -= T(x_);
    residuals[1] -= T(y_);

    // Covariance of pixels. Assumed to be independent. Divide by standard deviation.
    const T sig = T(5.0);
    residuals[0] /= sig;
    residuals[1] /= sig;

    return true;
  }

 private:
  const double x_;
  const double y_;
};

// Bundle adjustment cost function for variable
// camera calibration and point parameters, and fixed camera pose.
template <typename CameraModel>
class BundleAdjustmentConstantPoseCostFunction {
 public:
  BundleAdjustmentConstantPoseCostFunction(const Eigen::Vector4d& qvec,
                                           const Eigen::Vector3d& tvec,
                                           const Eigen::Vector2d& point2D)
      : qw_(qvec(0)),
        qx_(qvec(1)),
        qy_(qvec(2)),
        qz_(qvec(3)),
        tx_(tvec(0)),
        ty_(tvec(1)),
        tz_(tvec(2)),
        x_(point2D(0)),
        y_(point2D(1)) {}

  static ceres::CostFunction* Create(const Eigen::Vector4d& qvec,
                                     const Eigen::Vector3d& tvec,
                                     const Eigen::Vector2d& point2D) {
    return (new ceres::AutoDiffCostFunction<
            BundleAdjustmentConstantPoseCostFunction<CameraModel>, 2, 3,
            CameraModel::kNumParams>(
        new BundleAdjustmentConstantPoseCostFunction(qvec, tvec, point2D)));
  }

  template <typename T>
  bool operator()(const T* const point3D, const T* const camera_params,
                  T* residuals) const {
    const T qvec[4] = {T(qw_), T(qx_), T(qy_), T(qz_)};

    // Rotate and translate.
    T projection[3];
    ceres::UnitQuaternionRotatePoint(qvec, point3D, projection);
    projection[0] += T(tx_);
    projection[1] += T(ty_);
    projection[2] += T(tz_);

    // Project to image plane.
    projection[0] /= projection[2];
    projection[1] /= projection[2];

    // Distort and transform to pixel space.
    CameraModel::WorldToImage(camera_params, projection[0], projection[1],
                              &residuals[0], &residuals[1]);

    // Re-projection error.
    residuals[0] -= T(x_);
    residuals[1] -= T(y_);

    return true;
  }

 private:
  double qw_;
  double qx_;
  double qy_;
  double qz_;
  double tx_;
  double ty_;
  double tz_;
  double x_;
  double y_;
};

// Rig bundle adjustment cost function for variable camera pose and calibration
// and point parameters. Different from the standard bundle adjustment function,
// this cost function is suitable for camera rigs with consistent relative poses
// of the cameras within the rig. The cost function first projects points into
// the local system of the camera rig and then into the local system of the
// camera within the rig.
template <typename CameraModel>
class RigBundleAdjustmentCostFunction {
 public:
  explicit RigBundleAdjustmentCostFunction(const Eigen::Vector2d& point2D)
      : x_(point2D(0)), y_(point2D(1)) {}

  static ceres::CostFunction* Create(const Eigen::Vector2d& point2D) {
    return (new ceres::AutoDiffCostFunction<
            RigBundleAdjustmentCostFunction<CameraModel>, 2, 4, 3, 4, 3, 3,
            CameraModel::kNumParams>(
        new RigBundleAdjustmentCostFunction(point2D)));
  }

  template <typename T>
  bool operator()(const T* const rig_qvec, const T* const rig_tvec,
                  const T* const rel_qvec, const T* const rel_tvec,
                  const T* const point3D, const T* const camera_params,
                  T* residuals) const {
    // Concatenate rotations.
    T qvec[4];
    ceres::QuaternionProduct(rel_qvec, rig_qvec, qvec);

    // Concatenate translations.
    T tvec[3];
    ceres::UnitQuaternionRotatePoint(rel_qvec, rig_tvec, tvec);
    tvec[0] += rel_tvec[0];
    tvec[1] += rel_tvec[1];
    tvec[2] += rel_tvec[2];

    // Rotate and translate.
    T projection[3];
    ceres::UnitQuaternionRotatePoint(qvec, point3D, projection);
    projection[0] += tvec[0];
    projection[1] += tvec[1];
    projection[2] += tvec[2];

    // Project to image plane.
    projection[0] /= projection[2];
    projection[1] /= projection[2];

    // Distort and transform to pixel space.
    CameraModel::WorldToImage(camera_params, projection[0], projection[1],
                              &residuals[0], &residuals[1]);

    // Re-projection error.
    residuals[0] -= T(x_);
    residuals[1] -= T(y_);

    return true;
  }

 private:
  const double x_;
  const double y_;
};

// Cost function for refining two-view geometry based on the Sampson-Error.
//
// First pose is assumed to be located at the origin with 0 rotation. Second
// pose is assumed to be on the unit sphere around the first pose, i.e. the
// pose of the second camera is parameterized by a 3D rotation and a
// 3D translation with unit norm. `tvec` is therefore over-parameterized as is
// and should be down-projected using `HomogeneousVectorParameterization`.
class RelativePoseCostFunction {
 public:
  RelativePoseCostFunction(const Eigen::Vector2d& x1, const Eigen::Vector2d& x2)
      : x1_(x1(0)), y1_(x1(1)), x2_(x2(0)), y2_(x2(1)) {}

  static ceres::CostFunction* Create(const Eigen::Vector2d& x1,
                                     const Eigen::Vector2d& x2) {
    return (new ceres::AutoDiffCostFunction<RelativePoseCostFunction, 1, 4, 3>(
        new RelativePoseCostFunction(x1, x2)));
  }

  template <typename T>
  bool operator()(const T* const qvec, const T* const tvec,
                  T* residuals) const {
    Eigen::Matrix<T, 3, 3, Eigen::RowMajor> R;
    ceres::QuaternionToRotation(qvec, R.data());

    // Matrix representation of the cross product t x R.
    Eigen::Matrix<T, 3, 3> t_x;
    t_x << T(0), -tvec[2], tvec[1], tvec[2], T(0), -tvec[0], -tvec[1], tvec[0],
        T(0);

    // Essential matrix.
    const Eigen::Matrix<T, 3, 3> E = t_x * R;

    // Homogeneous image coordinates.
    const Eigen::Matrix<T, 3, 1> x1_h(T(x1_), T(y1_), T(1));
    const Eigen::Matrix<T, 3, 1> x2_h(T(x2_), T(y2_), T(1));

    // Squared sampson error.
    const Eigen::Matrix<T, 3, 1> Ex1 = E * x1_h;
    const Eigen::Matrix<T, 3, 1> Etx2 = E.transpose() * x2_h;
    const T x2tEx1 = x2_h.transpose() * Ex1;
    residuals[0] = x2tEx1 * x2tEx1 /
                   (Ex1(0) * Ex1(0) + Ex1(1) * Ex1(1) + Etx2(0) * Etx2(0) +
                    Etx2(1) * Etx2(1));

    return true;
  }

 private:
  const double x1_;
  const double y1_;
  const double x2_;
  const double y2_;
};

}  // namespace colmap

#endif  // COLMAP_SRC_BASE_COST_FUNCTIONS_H_
