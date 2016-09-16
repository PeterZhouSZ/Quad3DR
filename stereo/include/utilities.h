//==================================================
// utilities.h
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Sep 3, 2016
//==================================================

#pragma once

#include <vector>
#include <opencv2/opencv.hpp>
#include <stereo_calibration.h>

namespace stereo
{

class Timer
{
  double timing_;
  bool running;

public:
  Timer(bool startTimer=true)
  : timing_(-1.0)
  {
    if (startTimer)
    {
      start();
    }
  }

  void start()
  {
    timing_ = static_cast<double>(cv::getTickCount());
  }

  double getElapsedTime()
  {
    if (timing_ < 0)
    {
      throw std::runtime_error("Timer has not been started");
    }
    double elapsed_time = (static_cast<double>(cv::getTickCount()) - timing_) / cv::getTickFrequency();
    return elapsed_time;
  }

  double stop()
  {
    double elapsed_time = getElapsedTime();
    timing_ = -1.0;
    return elapsed_time;
  }

  double printTiming(const std::string &name)
  {
    double elapsed_time = getElapsedTime();
    std::cout << "Timing for " << name << ": " << elapsed_time << " s" << std::endl;
    return elapsed_time;
  }
  double stopAndPrintTiming(const std::string &name)
  {
    double elapsed_time = printTiming(name);
    stop();
    return elapsed_time;
  }

};

#if WITH_PROFILING
  using ProfilingTimer = Timer;
#else
  class ProfilingTimer
  {
  public:
    double stop()
    {
      return 0.0;
    }

    double stopAndPrintTiming(const std::string &name)
    {
      return 0.0;
    }
  };
#endif

class Utilities
{
public:
  Utilities() = delete;

  static StereoCameraCalibration readStereoCalibration(const std::string &filename);

  static cv::Mat convertToGrayscale(cv::InputArray img);

  static cv::Mat drawKeypoints(cv::InputArray img, const std::vector<cv::KeyPoint> &keypoints);

  static cv::Mat drawFeatureMatches(cv::InputArray left_img, const std::vector<cv::KeyPoint> &left_keypoints, cv::InputArray right_img, const std::vector<cv::KeyPoint> &right_keypoints, const std::vector<cv::DMatch> &matches);

  template <typename T>
  static cv::Mat drawPoints(cv::InputArray img, const std::vector<cv::Point_<T>> &points, int radius=4)
  {
    cv::Mat img_with_points = img.getMat().clone();
    for (int i = 0; i < points.size(); ++i)
    {
      double f = 255.0 * (double)(i) / (points.size() - 1);
      cv::Scalar color(255.0 - f, 0.5, 0.0 + f, 255.0);
      cv::circle(img_with_points, points[i], radius, color);
    }
    return img_with_points;
  }

  template <typename T, typename U>
  static cv::Mat drawPointMatches(
      cv::InputArray left_img,
      const std::vector<cv::Point_<T>> &left_points,
      cv::InputArray right_img,
      const std::vector<cv::Point_<U>> &right_points,
      int radius=4)
  {
    CV_Assert(left_img.size() == right_img.size());
    CV_Assert(left_points.size() == right_points.size());
    cv::Size image_size = left_img.size();
    cv::Mat img_with_matches;
    cv::hconcat(left_img, right_img, img_with_matches);
    for (int i = 0; i < left_points.size(); ++i)
    {
      double f = 255.0 * (double)(i) / (left_points.size() - 1);
      cv::Scalar color(255.0 - f, 0.5, 0.0 + f, 255.0);
      cv::Point_<T> left_point = left_points[i];
      cv::Point_<U> right_point = cv::Point_<U>(right_points[i].x + image_size.width, right_points[i].y);
      cv::circle(img_with_matches, left_point, radius, color);
      cv::circle(img_with_matches, right_point, radius, color);
      cv::line(img_with_matches, left_point, right_point, color);
    }
    return img_with_matches;
  }

  static void convertPointsToHomogeneous(const cv::Mat &points, cv::OutputArray hom_points)
  {
    CV_Assert(points.channels() == 1);
    if (points.type() == CV_32F || points.type() == CV_64F)
    {
      hom_points.create(points.rows, points.cols + 1, points.type());
      cv::Mat hom_points_mat = hom_points.getMat();
      points.copyTo(hom_points_mat(cv::Rect(0, 0, points.cols, points.rows)));
      for (int i = 0; i < hom_points_mat.rows; ++i)
      {
        if (points.type() == CV_64F)
        {
          hom_points_mat.at<double>(i, points.cols) = 1;
        }
        else // CV_32F
        {
          hom_points_mat.at<float>(i, points.cols) = 1;
        }
      }
      hom_points_mat(cv::Rect(points.cols, 0, 1, points.rows));
    }
    else
    {
      throw std::runtime_error("Unable to convert non-floating point array to homogeneous coordinates");
    }
  }
};

} /* namespace stereo */
