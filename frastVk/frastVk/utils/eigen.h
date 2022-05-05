#pragma once

#include <Eigen/Core>

using Eigen::Vector2f;
using Eigen::Vector2d;
using Eigen::Vector3f;
using Eigen::Vector3d;
using Eigen::Vector4f;
using Eigen::Vector4d;
using RowMatrix3f = Eigen::Matrix<float,3,3,Eigen::RowMajor>;
using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using RowMatrix4f = Eigen::Matrix<float,4,4,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;
using Eigen::Matrix4d;
using Eigen::Matrix4f;
using Eigen::Matrix3d;
using Eigen::Matrix3f;
using Eigen::Matrix;

template <class S, int R, int C>
using RowMatrix = Eigen::Matrix<S,R,C,Eigen::RowMajor>;
