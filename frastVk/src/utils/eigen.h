#pragma once

#include <Eigen/Core>

using Eigen::Vector2f;
using Eigen::Vector2d;
using Eigen::Vector3f;
using Eigen::Vector3d;
using RowMatrix3f = Eigen::Matrix<float,3,3,Eigen::RowMajor>;
using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using RowMatrix4f = Eigen::Matrix<float,4,4,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;

template <class S, int R, int C>
using RowMatrix = Eigen::Matrix<S,R,C,Eigen::RowMajor>;
