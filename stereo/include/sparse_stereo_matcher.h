//==================================================
// sparse_stereo_matcher.h
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Sep 3, 2016
//==================================================

#pragma once

#include <vector>
#include <opencv2/opencv.hpp>
#include "stereo_calibration.h"

namespace stereo
{

struct SparseMatchResult
{
  std::vector<cv::Point3d> points_3d;
  std::vector<cv::KeyPoint> left_keypoints;
  std::vector<cv::KeyPoint> right_keypoints;
  std::vector<cv::Point2d> left_points;
  std::vector<cv::Point2d> right_points;
  std::vector<cv::Mat> left_descriptors;
  std::vector<cv::Mat> right_descriptors;
  std::vector<cv::DMatch> matches;
  std::vector<double> epipolar_constraints;
};

template <typename T, typename U>
class FeatureDetectorOpenCV
{
  cv::Ptr<T> detector_;
  cv::Ptr<T> detector_2_;
  cv::Ptr<U> descriptor_computer_;
  cv::Ptr<U> descriptor_computer_2_;

  int max_num_of_keypoints_;


  void detectFeatureKeypoints(
      cv::Ptr<T> detector,
      const cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      std::vector<cv::Point2d> *points_ptr) const;

  void computeFeatureDescriptors(
      cv::Ptr<U> descriptor_computer,
      cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      cv::OutputArray descriptors) const;

public:
  FeatureDetectorOpenCV(const cv::Ptr<T> &detector, const cv::Ptr<U> &descriptor_computer);
  FeatureDetectorOpenCV(
      const cv::Ptr<T> &detector, const cv::Ptr<T> &detector_2,
      const cv::Ptr<U> &descriptor_computer, const cv::Ptr<U> &descriptor_computer_2);
  virtual ~FeatureDetectorOpenCV();

  int getMaxNumOfKeypoints() const
  {
    return max_num_of_keypoints_;
  }

  void setMaxNumOfKeypoints(int max_num_of_keypoints)
  {
    max_num_of_keypoints_ = max_num_of_keypoints;
  }

  void detectFeatureKeypoints(
      const cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      std::vector<cv::Point2d> *points_ptr) const;
  void computeFeatureDescriptors(
      cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      cv::OutputArray descriptors) const;

  void detectAndComputeFeatures(
      cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      cv::OutputArray descriptors,
      std::vector<cv::Point2d> *points_ptr) const;

  void detectAndComputeFeatures(
      cv::InputArray img_left,
      cv::InputArray img_right,
      std::vector<cv::KeyPoint> *keypoints_left_ptr,
      std::vector<cv::KeyPoint> *keypoints_right_ptr,
      cv::OutputArray descriptors_left,
      cv::OutputArray descriptors_right,
      std::vector<cv::Point2d> *points_left_ptr,
      std::vector<cv::Point2d> *points_right_ptr) const;
};

template <typename T>
class FeatureDetectorOpenCVSurfCuda
{
  cv::Ptr<T> feature_computer_;

public:
  FeatureDetectorOpenCVSurfCuda(const cv::Ptr<T> &feature_computer);
  virtual ~FeatureDetectorOpenCVSurfCuda();

  void detectAndComputeFeatures(
      cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      cv::OutputArray descriptors,
      std::vector<cv::Point2d> *points_ptr) const;

  void detectAndComputeFeatures(
      cv::InputArray img_left,
      cv::InputArray img_right,
      std::vector<cv::KeyPoint> *keypoints_left_ptr,
      std::vector<cv::KeyPoint> *keypoints_right_ptr,
      cv::OutputArray descriptors_left,
      cv::OutputArray descriptors_right,
      std::vector<cv::Point2d> *points_left_ptr,
      std::vector<cv::Point2d> *points_right_ptr) const;
};

template <typename T>
class FeatureDetectorOpenCVCuda
{
  cv::Ptr<T> feature_computer_;

public:
  FeatureDetectorOpenCVCuda(const cv::Ptr<T> &feature_computer);
  virtual ~FeatureDetectorOpenCVCuda();

  void detectAndComputeFeatures(
      cv::InputArray img,
      std::vector<cv::KeyPoint> *keypoints_ptr,
      cv::OutputArray descriptors,
      std::vector<cv::Point2d> *points_ptr) const;

  void detectAndComputeFeatures(
      cv::InputArray img_left,
      cv::InputArray img_right,
      std::vector<cv::KeyPoint> *keypoints_left_ptr,
      std::vector<cv::KeyPoint> *keypoints_right_ptr,
      cv::OutputArray descriptors_left,
      cv::OutputArray descriptors_right,
      std::vector<cv::Point2d> *points_left_ptr,
      std::vector<cv::Point2d> *points_right_ptr) const;
};

template <typename T>
class SparseStereoMatcher
{
  cv::Ptr<T> feature_detector_;

  StereoCameraCalibration calib_;
  double ratio_test_threshold_;
  double epipolar_constraint_threshold_;
  int match_norm_;
  cv::Ptr<cv::flann::IndexParams> flann_index_params_;
  cv::Ptr<cv::flann::SearchParams> flann_search_params_;

public:
  class Error : public std::runtime_error
  {
  public:
    Error(const std::string &msg);
    ~Error() override;
  };

  SparseStereoMatcher(const cv::Ptr<T> &feature_detector, const StereoCameraCalibration &calib);
  virtual ~SparseStereoMatcher();

  double getRatioTestThreshold() const
  {
    return ratio_test_threshold_;
  }
  void setRatioTestThreshold(double ratio_test_threshold)
  {
    ratio_test_threshold_ = ratio_test_threshold;
  }

  double getEpipolarConstraintThreshold() const
  {
    return epipolar_constraint_threshold_;
  }
  void setEpipolarConstraintThreshold(double epipolar_constraint_threshold)
  {
    epipolar_constraint_threshold_ = epipolar_constraint_threshold;
  }

  int getMatchNorm()
  {
    return match_norm_;
  }
  void setMatchNorm(int match_norm)
  {
    match_norm_ = match_norm;
  }

  const cv::Ptr<cv::flann::IndexParams> getFlannIndexParams() const
  {
    return flann_index_params_;
  }
  void setFlannIndexParams(const cv::Ptr<cv::flann::IndexParams> &index_params)
  {
    flann_index_params_ = index_params;
  }

  const cv::Ptr<cv::flann::SearchParams> getFlannSearchParams() const
  {
    return flann_search_params_;
  }
  void setFlannSearchParams(const cv::Ptr<cv::flann::SearchParams> &search_params)
  {
    flann_search_params_ = search_params;
  }

  // Convenience method for matching
  std::vector<cv::Point3d> match(
      const cv::InputArray left_input_img, cv::InputArray right_input_img,
      std::vector<cv::Point2d> *image_points) const;

  SparseMatchResult matchFull(const cv::InputArray left_color_img, cv::InputArray right_color_img) const;

  cv::Mat undistortPoints(cv::InputArray points, cv::InputArray camera_matrix, cv::InputArray dist_coefficients) const;
  void undistortPoints(cv::InputOutputArray left_points, cv::InputOutputArray right_points) const;

  std::vector<cv::DMatch> matchFeaturesBf(cv::InputArray left_descriptors, cv::InputArray right_descriptors) const;
  std::vector<cv::DMatch> matchFeaturesBfKnn2(cv::InputArray left_descriptors, cv::InputArray right_descriptors) const;
  std::vector<cv::DMatch> matchFeaturesFlann(cv::InputArray left_descriptors, cv::InputArray right_descriptors) const;
  std::vector<cv::DMatch> matchFeaturesFlannKnn2(cv::InputArray left_descriptors, cv::InputArray right_descriptors) const;

  std::vector<cv::DMatch> filterMatchesWithDistance(const std::vector<cv::DMatch> &all_matches, double good_threshold_multiplier=5, double min_good_threshold=0.02) const;
  std::vector<cv::DMatch> filterMatchesWithLoweRatioTest(const std::vector<std::vector<cv::DMatch>> &matches, double ratio_test_threshold=-1.0) const;

  std::vector<cv::DMatch> filterMatchesWithEpipolarConstraint(
      const std::vector<cv::DMatch> &matches,
      const cv::Mat &left_undist_points, const cv::Mat &right_undist_points,
      std::vector<double> *best_epipolar_constraints=nullptr) const;

  std::vector<cv::DMatch> filterMatchesWithEpipolarConstraint(
      const std::vector<cv::DMatch> &matches,
      const std::vector<cv::Point2d> &left_undist_points, const std::vector<cv::Point2d> &right_undist_points,
      std::vector<double> *best_epipolar_constraints=nullptr) const;

  void retrieveMatchedPointsAndUpdateMatches(
      const std::vector<cv::Point2d> &left_points, const std::vector<cv::Point2d> &right_points,
      std::vector<cv::DMatch> *matches,
      std::vector<cv::Point2d> *left_match_points, std::vector<cv::Point2d> *right_match_points) const;

  void retrieveMatchedPointsAndUpdateMatches(
      const std::vector<cv::KeyPoint> &left_keypoints, const std::vector<cv::KeyPoint> &right_keypoints,
      std::vector<cv::DMatch> *matches,
      std::vector<cv::Point2d> *left_match_points, std::vector<cv::Point2d> *right_match_points,
      std::vector<cv::KeyPoint> *left_match_keypoints, std::vector<cv::KeyPoint> *right_match_keypoints) const;

  void correctMatches(std::vector<cv::Point2d> *left_points, std::vector<cv::Point2d> *right_points) const;

  void correctMatchesAndUpdateKeypoints(
      std::vector<cv::Point2d> *left_points, std::vector<cv::Point2d> *right_points,
      std::vector<cv::KeyPoint> *left_keypoints, std::vector<cv::KeyPoint> *right_keypoints) const;

  std::vector<double> computeEpipolarConstraints(
      const std::vector<cv::Point2d> &left_points, const std::vector<cv::Point2d> &right_points) const;

  template <typename V>
  std::vector<cv::Point3_<V>> triangulatePoints(const std::vector<cv::Point_<V>> &left_points, const std::vector<cv::Point_<V>> &right_points) const;
};

} /* namespace stereo */

#include "sparse_stereo_matcher.hpp"

